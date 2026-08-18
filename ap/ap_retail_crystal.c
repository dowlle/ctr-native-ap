// Harvest the retail crystal (STATIC_CRYSTAL, 0x60) out of the player's own game
// data so #219 can draw it on surfaces where the engine never loads it.
//
// WHY THIS EXISTS
// ---------------
// The adventure hub loads BI_ADVENTUREPACK, and that pack carries gem1 (0x5f),
// relic (0x61), trophylow (0x62), key (0x63) and token (0x7d) -- but no crystal.
// `crystal` (0x60) ships in BI_1PARCADEPACK instead. That single fact is why
// every OTHER pad reward renders in the hub today and the crystal cannot, and it
// is settled from retail data rather than argued from the loader: probed on
// 2026-08-17 against an NTSC-U BIGFILE, and confirmed again here.
//
// Two earlier attempts at this feature died on exactly this hole. Lessons
// Learned §24: a display resolver handed back a model id nobody checked was
// drawable, and the pad silently kept the model it was already showing while the
// tint resolved for the model it was not. §29: the follow-up tried to settle
// residency by ARGUING which pack the hub runs, got it wrong, and the rule that
// came out of it is the one this file implements -- when a design depends on a
// fact the build cannot verify, stop depending on it. So the client owns the
// model: it parks its own harvested crystal wherever the level left the slot
// empty, and steps aside where a retail one is loaded.
//
// NO TEXTURE WORK IS NEEDED, AND THAT IS MEASURED, NOT ASSUMED
// ------------------------------------------------------------
// The #256 crate harvest had to convert texels and sideload them, because the
// crate's texture genuinely is not in VRAM during a relic race. The crystal's
// is. All eleven of its texture layouts name ONE page (960, 0) and ONE palette
// at (272, 243), 4bpp, over a single 16x16 texel rect -- and both of those live
// in shared.vrm (BIGFILE entry 258, BI_SHAREDMPKVRM), which MainMain.c:706
// uploads once at startup as the shared UI texture set and which no hub or track
// upload overwrites: level VRMs land on (512,0,384,256) and (512,256,...), never
// on x >= 896, and the palette sits in the y216-263 band between the two display
// buffers.
//
// The decisive check is not that argument, though. It is that `gem1` samples the
// SAME page and the SAME palette, with its texels inside the crystal's own 16x16
// rect -- and gem1 is in the adventure pack and demonstrably renders correctly in
// the hub today. So the crystal's texture is resident there as observed fact.
// The layouts are therefore left completely untouched: no atlas, no sideload
// bit, no VRAM injection, and no claim on the single global sideload slot that
// the crate already owns.
//
// The pack buffer is retained for the lifetime of the process ON PURPOSE, the
// same reasoning as the crate's level buffer: after the pointer fixup the
// harvested model's internals point into it, so keeping it avoids deep-copying a
// pointer graph whose element sizes are recorded nowhere. It is a static buffer,
// never the engine's MEMPACK, which is a bump allocator with no free.

#ifdef CTR_AP

#include <common.h>
#include <stdio.h>

#include "ap_retail_crystal.h"
#include "ap_retail_asset.h" // AP_RetailAsset_ReadSubfile (shared with #256)
#include "ap_hooks.h"        // AP_LogLine

// Character 0's 1P arcade pack. The pack is per-racer (BI_1PARCADEPACK +
// characterID, LOAD_Assets.c:164) but the crystal is a shared object: all
// sixteen packs carry model id 0x60. A fixed entry keeps the harvest
// deterministic and independent of who the player is driving.
#define AP_CRYSTAL_SRC_PACK BI_1PARCADEPACK

// Sector-aligned maximum measured across all sixteen 1P arcade packs (largest
// is 314856 bytes). Sizing for the whole family rather than for entry 260 alone
// means changing the source character above cannot silently start failing the
// size check.
#define AP_CRYSTAL_PACK_BUF 315392

#define MODELHEADER_STRIDE 0x40 // CTR_STATIC_ASSERT'd in RenderBucket_QueueExecute.c

static u8  s_packBuf[AP_CRYSTAL_PACK_BUF]; // retained: the model points into it
static int s_harvestState;                 // 0 = untried, 1 = ready, 2 = failed (do not retry)

static struct Model *s_crystalModel;

// Walk an MPK's PLYROBJECTLIST for one model id.
//
// Pack layout, from the engine rather than from inspection: body[0] is
// mpkIcons and PLYROBJECTLIST is body + 4 (LOAD_TenStages.c:336, which builds it
// as ptrMPK + 4), a NULL-terminated array of struct Model *.
//
// Every pointer here was produced by the fixup a moment ago, so anything landing
// outside the buffer means the fixup or the layout assumption is wrong. Walking
// on from that point would dereference an offset as an address, which is an
// access violation rather than a detectable error -- so the walk stops instead.
// The list length is not recorded anywhere, so the walk is capped as well as
// bounded: a pack whose terminator went missing would otherwise have 300 KB of
// arbitrary bytes read as pointers. 512 is the same order of sanity limit the
// crate harvest puts on a level's model count.
#define AP_CRYSTAL_MAX_MODELS 512

static struct Model *AP_CrystalFindModel(const u8 *body, int size, int wantID)
{
	int i, n;

	for (i = 4, n = 0; i + 4 <= size && n < AP_CRYSTAL_MAX_MODELS; i += 4, n++)
	{
		struct Model *m = *(struct Model *const *)(body + i);

		if (m == 0)
			break; // NULL terminator: end of the list

		if ((const u8 *)m < body || (const u8 *)m + sizeof(struct Model) > body + size)
			return 0; // not a fixed-up pointer: stop, do not dereference

		if (m->id == (s16)wantID)
		{
			if (m->numHeaders <= 0 || m->headers == 0 || (const u8 *)m->headers < body ||
			    (const u8 *)m->headers + (u32)m->numHeaders * MODELHEADER_STRIDE > body + size)
				return 0;
			return m;
		}
	}

	return 0;
}

static void AP_CrystalHarvest(void)
{
	u8           *body;
	int           size = 0;
	struct Model *model;
	char          msg[160];

	AP_LogLine("[AP CRYSTAL] harvesting the retail crystal from the game data...\n");

	body = AP_RetailAsset_ReadSubfile(AP_CRYSTAL_SRC_PACK, 1, s_packBuf, AP_CRYSTAL_PACK_BUF, &size);
	if (body == 0)
	{
		AP_LogLine("[AP CRYSTAL] could not read the source model pack; progression keeps the marker\n");
		s_harvestState = 2;
		return;
	}

	model = AP_CrystalFindModel(body, size, STATIC_CRYSTAL);
	if (model == 0)
	{
		AP_LogLine("[AP CRYSTAL] crystal absent from the source pack; progression keeps the marker\n");
		s_harvestState = 2;
		return;
	}

	// Nothing is rewritten: the layouts already name a page and palette that are
	// resident wherever this model gets drawn (see the header comment), so the
	// retail draw path samples them exactly as it does in a crystal challenge.
	s_crystalModel = model;
	s_harvestState = 1;

	// The name is logged rather than matched on. Model id 0x60 is what the engine
	// itself keys on, so it stays the match criterion; but a support log that says
	// which model was actually picked up turns "the crystal looks wrong" into a
	// one-line answer instead of a repro session.
	snprintf(msg, sizeof msg, "[AP CRYSTAL] harvested retail crystal '%.16s': pack %d, %d bytes, %d header(s)\n",
	         model->name, AP_CRYSTAL_SRC_PACK, size, model->numHeaders);
	AP_LogLine(msg);
}

void AP_RetailCrystal_Register(struct GameTracker *gGT)
{
	if (gGT == 0 || s_harvestState == 2)
		return;

	if (s_harvestState == 0)
	{
		// The read borrows nothing from the engine's loader, but it does block on
		// disc IO, so it waits for a frame with no load in flight rather than
		// stealing time from one. Same gate the crate harvest uses.
		if (sdata == 0 || sdata->Loading.stage != LOAD_IDLE)
			return;

		AP_CrystalHarvest();
		if (s_harvestState != 1)
			return;
	}

	// STATIC_CRYSTAL is a normal per-level slot and LibraryOfModels_Clear wipes it
	// on every transition, which is why this reasserts every frame rather than
	// installing once. Reassert ONLY while the slot is absent, so a level that
	// carries the real crystal -- a crystal challenge -- keeps its own, whose
	// texture coordinates belong to the pack that level actually loaded.
	if (gGT->modelPtr[STATIC_CRYSTAL] == 0)
		gGT->modelPtr[STATIC_CRYSTAL] = s_crystalModel;
}

int AP_RetailCrystal_IsRegistered(void)
{
	return s_harvestState == 1;
}

#endif // CTR_AP
