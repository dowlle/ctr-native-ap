#ifndef AP_CORTEX_TRACK_H
#define AP_CORTEX_TRACK_H

// Cortex Vortex as a full pad track (schema 15), freestanding decisions.
//
// The track has no AdvProgress bits and no retail identity of its own. It loads
// host LevelID 13, and every LevelID-13 reward bit, podium rung, box, letter row
// and relic time belongs to Oxide Station. So nothing on this path is derived
// from the level: each check is a direct wire code (ctr_cfg.cortex_track), and
// the bit-keyed display/lifecycle helpers see it through the process-local
// pseudo-bits below, exactly as the trial-track (#343) and custom-track checks
// do. Pure so tools/test-cortex-track.c can pin every answer out of engine.

#include "ap_seedcfg.h"

// ── pseudo-bits ──────────────────────────────────────────────────────────────
// Above the podium rung range [0x100, 0x1fa), the custom-track bits 0x200..0x202
// and the trial bits 0x210..0x213. Process-local keys, never wire values.
#define AP_CV_PSEUDO_BASE 0x220
enum
{
	AP_CV_SLOT_TROPHY = 0,
	AP_CV_SLOT_RELIC0 = 1, // Sapphire; +1 Gold, +2 Platinum
	AP_CV_SLOT_TOKEN = 4,
	AP_CV_SLOT_LETTER0 = 5, // C; +1 T, +2 R
	AP_CV_SLOT_WUMPA = 8,
	AP_CV_SLOT_RUNG0 = 9,  // held_1st; +1 held_3rd, +2 held_5th, +3 finish_podium, +4 finish_any
	AP_CV_SLOT_COUNT = 14
};

// Logical podium track for the race listener and podium fan-out: after the 16
// retail tracks, 32 custom slots and 2 trial tracks.
#define AP_CV_PODIUM_LOGICAL_TRACK 50

static inline int AP_CortexPseudoBit(int slot)
{
	return AP_CV_PSEUDO_BASE + slot;
}

// 1 when `bit` is a Cortex Vortex pseudo-bit, slot written to *outSlot.
static inline int AP_CortexPseudoDecode(int bit, int *outSlot)
{
	int off = bit - AP_CV_PSEUDO_BASE;
	if (off < 0 || off >= AP_CV_SLOT_COUNT)
		return 0;
	if (outSlot)
		*outSlot = off;
	return 1;
}

// The wire code a slot carries this seed, or -1. A refused block answers -1 for
// every slot: there is then no Cortex Vortex check to send, and certainly no
// Oxide Station one.
static inline long AP_CortexSlotCode(const ctr_cortex_track *cv, int slot)
{
	long code = -1;
	if (cv == 0 || !cv->valid || slot < 0 || slot >= AP_CV_SLOT_COUNT)
		return -1;
	switch (slot)
	{
	case AP_CV_SLOT_TROPHY: code = cv->trophy; break;
	case AP_CV_SLOT_RELIC0: case AP_CV_SLOT_RELIC0 + 1: case AP_CV_SLOT_RELIC0 + 2:
		code = cv->relic[slot - AP_CV_SLOT_RELIC0]; break;
	case AP_CV_SLOT_TOKEN: code = cv->ctr_token; break;
	case AP_CV_SLOT_LETTER0: case AP_CV_SLOT_LETTER0 + 1: case AP_CV_SLOT_LETTER0 + 2:
		code = cv->letters[slot - AP_CV_SLOT_LETTER0]; break;
	case AP_CV_SLOT_WUMPA: code = cv->wumpa; break;
	case AP_CV_SLOT_RUNG0: code = cv->podium.held_1st; break;
	case AP_CV_SLOT_RUNG0 + 1: code = cv->podium.held_3rd; break;
	case AP_CV_SLOT_RUNG0 + 2: code = cv->podium.held_5th; break;
	case AP_CV_SLOT_RUNG0 + 3: code = cv->podium.finish_podium; break;
	default: code = cv->podium.finish_any; break;
	}
	return code > 0 ? code : -1;
}

static inline long AP_CortexPseudoCode(const ctr_cortex_track *cv, int bit)
{
	int slot;
	if (!AP_CortexPseudoDecode(bit, &slot))
		return -1;
	return AP_CortexSlotCode(cv, slot);
}

// Prize slot under by_reward_type: Trophy and rungs ride the race slot, relic
// tiers the relic slot, CTR token / letters / Wumpa the token slot. -1 when not
// a Cortex Vortex bit.
static inline int AP_CortexPseudoRewardGroup(int bit)
{
	int slot;
	if (!AP_CortexPseudoDecode(bit, &slot))
		return -1;
	if (slot == AP_CV_SLOT_TROPHY || slot >= AP_CV_SLOT_RUNG0)
		return 0;
	if (slot >= AP_CV_SLOT_RELIC0 && slot < AP_CV_SLOT_RELIC0 + 3)
		return 1;
	return 2;
}

// Relic tier 0..2 of a Cortex Vortex relic pseudo-bit, else -1.
static inline int AP_CortexPseudoRelicTier(int bit)
{
	int slot;
	if (!AP_CortexPseudoDecode(bit, &slot) || slot < AP_CV_SLOT_RELIC0 ||
	    slot >= AP_CV_SLOT_RELIC0 + 3)
		return -1;
	return slot - AP_CV_SLOT_RELIC0;
}

typedef int (*ap_cortex_code_query)(long code, void *ctx);

// Append the still-open Cortex Vortex identities a pad advertises: the tiers
// (Trophy, relic tiers, CTR token) and/or the podium rungs. Letters and Wumpa
// are counted separately, like their retail families. A Gem Cup that legs 110
// asks for the rungs only: cup access exposes the leg's podium and Wumpa, never
// its Trophy/relic/token family. `exists` is server location membership,
// `checked` the server checked set.
#define AP_CV_APPEND_TIERS 1
#define AP_CV_APPEND_RUNGS 2
static inline int AP_CortexPadAppendOpen(const ctr_cortex_track *cv, int flags,
                                         int *outBits, int cap, int count,
                                         ap_cortex_code_query exists,
                                         ap_cortex_code_query checked, void *ctx)
{
	static const int slots[] = {
		AP_CV_SLOT_TROPHY, AP_CV_SLOT_RELIC0, AP_CV_SLOT_RELIC0 + 1,
		AP_CV_SLOT_RELIC0 + 2, AP_CV_SLOT_TOKEN,
		AP_CV_SLOT_RUNG0, AP_CV_SLOT_RUNG0 + 1, AP_CV_SLOT_RUNG0 + 2,
		AP_CV_SLOT_RUNG0 + 3, AP_CV_SLOT_RUNG0 + 4};
	unsigned i;
	if (cv == 0 || outBits == 0 || exists == 0 || checked == 0 || count < 0)
		return count;
	for (i = 0; i < sizeof slots / sizeof slots[0] && count < cap; i++)
	{
		long code;
		int bit, j, dup = 0;
		if (!(flags & (slots[i] >= AP_CV_SLOT_RUNG0 ? AP_CV_APPEND_RUNGS : AP_CV_APPEND_TIERS)))
			continue;
		code = AP_CortexSlotCode(cv, slots[i]);
		if (code < 0 || !exists(code, ctx) || checked(code, ctx))
			continue;
		bit = AP_CortexPseudoBit(slots[i]);
		for (j = 0; j < count; j++)
			if (outBits[j] == bit)
				dup = 1;
		if (!dup)
			outBits[count++] = bit;
	}
	return count;
}

// Open count of a family of slots (letters: first=LETTER0, n=3; Wumpa: n=1).
static inline int AP_CortexOpenCount(const ctr_cortex_track *cv, int firstSlot, int n,
                                     ap_cortex_code_query exists,
                                     ap_cortex_code_query checked, void *ctx)
{
	int i, open = 0;
	for (i = 0; i < n; i++)
	{
		long code = AP_CortexSlotCode(cv, firstSlot + i);
		if (code > 0 && exists(code, ctx) && !checked(code, ctx))
			open++;
	}
	return open;
}

// ── relic targets ────────────────────────────────────────────────────────────
// Package-owned relic targets, keyed by the pinned LEV hash. Cortex Vortex has
// no published target times (not in the LEV, the VRM or the Saphi API as of
// 2026-09-13), so until Lockheart supplies them these are PLACEHOLDERS: Oxide
// Station's retail Sapphire / Gold / Platinum targets, data.RelicTime[39..41]
// (3:17.00 / 2:56.00 / 2:34.00, in 1/960 s race-clock units).
// tools/test-cortex-track.c pins them against game/zGlobal_DATA.c. Replace the
// row, not the mechanism, when real times arrive.
struct AP_CortexRelicTargets
{
	const char *levSha256;
	int time[3];
	int placeholder;
};

static const struct AP_CortexRelicTargets AP_CORTEX_RELIC_TARGETS = {
	CTR_CFG_CORTEX_LEV_SHA256,
	{0x0002e2c0, 0x00029400, 0x00024180}, // PLACEHOLDER: Oxide Station retail times
	1,
};

static inline int AP_CortexRelicTime(int tier)
{
	if (tier < 0 || tier > 2)
		tier = 0;
	return AP_CORTEX_RELIC_TARGETS.time[tier];
}

// The relic tiers a finished run beat, as a 3-bit mask (bit t = tier t), from
// a race time already reduced by any all-crates bonus. Retail rule: every tier
// is compared independently, so beating Platinum also beats Gold and Sapphire.
static inline int AP_CortexRelicTiersBeaten(int raceTime)
{
	int t, mask = 0;
	for (t = 0; t < 3; t++)
		if (raceTime <= AP_CortexRelicTime(t))
			mask |= 1 << t;
	return mask;
}

// ── results ─────────────────────────────────────────────────────────────────
// What a finished Cortex Vortex race sends, as pseudo-bits (0..n). Podium rungs
// and Wumpa are live listeners elsewhere and are not part of the result.
//   raceKind 0 Trophy Race: a win sends the Trophy.
//   raceKind 1 CTR Token Challenge: a token-earning win sends the CTR token.
//   raceKind 2 Relic Race: each beaten tier sends its relic.
// An absent (-1) code is skipped; the caller adds membership + dedup.
#define AP_CV_RACE_TROPHY 0
#define AP_CV_RACE_TOKEN  1
#define AP_CV_RACE_RELIC  2

static inline int AP_CortexResultBits(const ctr_cortex_track *cv, int raceKind,
                                      int won, int relicMask, int *outBits, int cap)
{
	int n = 0, t;
	if (cv == 0 || outBits == 0 || cap <= 0)
		return 0;
	if (raceKind == AP_CV_RACE_TROPHY && won &&
	    AP_CortexSlotCode(cv, AP_CV_SLOT_TROPHY) > 0)
		outBits[n++] = AP_CortexPseudoBit(AP_CV_SLOT_TROPHY);
	else if (raceKind == AP_CV_RACE_TOKEN && won &&
	         AP_CortexSlotCode(cv, AP_CV_SLOT_TOKEN) > 0)
		outBits[n++] = AP_CortexPseudoBit(AP_CV_SLOT_TOKEN);
	else if (raceKind == AP_CV_RACE_RELIC)
		for (t = 0; t < 3 && n < cap; t++)
			if ((relicMask & (1 << t)) &&
			    AP_CortexSlotCode(cv, AP_CV_SLOT_RELIC0 + t) > 0)
				outBits[n++] = AP_CortexPseudoBit(AP_CV_SLOT_RELIC0 + t);
	return n;
}

// ── letters ─────────────────────────────────────────────────────────────────
// Letter items 35010200..202 = item index 200..202 (C, T, R). Not part of the
// retail/trial letter index space (139..186, 194..199), so they never alias an
// Oxide Station letter row.
#define AP_CV_LETTER_ITEM_FIRST_INDEX 200

static inline int AP_CortexLetterItemIndexPure(long long idx)
{
	return (idx >= AP_CV_LETTER_ITEM_FIRST_INDEX && idx < AP_CV_LETTER_ITEM_FIRST_INDEX + 3)
	           ? (int)(idx - AP_CV_LETTER_ITEM_FIRST_INDEX) : -1;
}

#endif // AP_CORTEX_TRACK_H
