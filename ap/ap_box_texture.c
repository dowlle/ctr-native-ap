// The AP box's texture atlas, uploaded as the AP sideload texture.
//
// WHAT THE BOX LOOKS LIKE
// -----------------------
// The box is framed like the retail "?" crate (ap/ap_box_model_framed_data.h):
// a 64x64 face in the middle of each side and a border ring that samples a
// 16x16 wood tile. Both are built at runtime from the player's own disc, so
// the build ships no retail pixel:
//
//   * the wood is the retail crate's wood tile, recoloured;
//   * the face is the retail Wumpa crate's face (slats, no picture),
//     recoloured, with JurnthReinal's six Archipelago circles on top
//     (ap_box_logo_mask_data.h marks which face pixels are the logo).
//
// Both come from the 1p level of track 0 and its texture file. They are the
// same on every track, so one read serves every race, relic races included
// (their level files carry no crate). The read waits for a frame with no load
// in flight (AP_BoxTexture_Prepare, called every frame from the frame hook),
// so it normally completes at the title screen, long before a box spawns.
// Retail crates in the game are untouched: only the AP box's own atlas changes.
//
// THE FALLBACK
// ------------
// If the read fails (no readable disc image, unexpected level layout, bad
// texture file) or has not run by the time a box needs the atlas, the box
// keeps JurnthReinal's own compiled border and face
// (ap/ap_box_texture_data.h, tools/apbox-texture). It is still framed and
// textured. If the atlas cannot be uploaded at all, the caller keeps the plain
// untextured cube.
//
// COLOURS
// -------
// The atlas holds the box in five colours: the default pink and the four
// Archipelago item colours, one slot each (ap/ap_box_colour_logic.h). Which
// slot a box samples is decided per box in ap/ap_boxes.c.
//
// The layouts' tpage and clut words are the retail crate_question words plus
// AP_TPAGE_SIDELOAD_BIT (tools/apbox-texture/gen_framed_model.py). The tpage's
// semi-transparency bits must stay as they are: the primitive writer emits an
// opaque code word only when they are non-zero.

#ifdef CTR_AP

#include <common.h>
#include <stdio.h>

#include "ap_box_texture.h"
#include "ap_box_texture_data.h"
#include "ap_hooks.h" // AP_LogLine
#include "ap_retail_asset.h" // AP_RetailAsset_ReadSubfile
#include "ap_box_colour_logic.h" // decode, recolour, logo compose and the colour atlas (harness-pinned)
#include "ap_box_logo_mask_data.h" // which face pixels are the Archipelago logo (JurnthReinal art)

#include <stdlib.h>
#include <string.h>

#include <platform/native_gpu.h>      // NativeGpu_SetSideloadTexture
#include <platform/native_renderer.h> // NativeRenderer_CreateRGBATexture

// Unity-build names must be module-specific: several AP modules land in the same
// C translation unit, and a generic static tentative definition silently
// coalesces in C.
static int s_apBoxTextureState; // 0 = untried, 1 = uploaded, 2 = failed

// ── the wood tile and crate face from the player's disc ────────────────────

// Source: the 1p level of track 0 and its texture file. BIGFILE groups each
// track as (vrm, lev) pairs per mode, 1p first. The crate's wood tile and the
// Wumpa crate's face are the same on every track; only their VRAM position
// differs, and the level's own models name that position, so the pair must be
// read together.
#define AP_BOX_DISC_SRC_VRM (BI_ARCADETRACKS + 0 * 8 + 0)
#define AP_BOX_DISC_SRC_LEV (BI_ARCADETRACKS + 0 * 8 + 1)
// Sector-aligned maxima: 1p LEV at most 780008 bytes across all 18 tracks,
// every 1p VRM 458808. Temporary: freed right away.
#define AP_BOX_DISC_LEV_BUF 780288
#define AP_BOX_DISC_VRM_BUF 460800
#define AP_BOX_DISC_HEADER_STRIDE 0x40 // sizeof(struct ModelHeader), asserted in RenderBucket
#define AP_BOX_DISC_MAX_MODELS 512
#define AP_BOX_DISC_MAX_CMDS 4096

static int s_apBoxDiscState;  // 0 = untried, 1 = tried (sticky either way)
static int s_apBoxHaveWood;   // the disc wood tile is in s_apBoxDiscWood
static int s_apBoxHaveCrate;  // the disc Wumpa crate face is in s_apBoxDiscCrate
static u8  s_apBoxDiscWood[16 * 16 * 4];
static u8  s_apBoxDiscCrate[64 * 64 * 4];
static u8  s_apBoxAtlasLive[AP_BOX_ATLAS_W * AP_BOX_ATLAS_H * 4];

// Find the rect one model samples, in a fixed-up level body: the only 16x16
// rect (wood) or the only 64x64 rect (face). Every pointer is checked against
// the buffer before it is followed.
static int AP_BoxDisc_FindRect(const u8 *body, int size, int modelId, int wantWood, AP_BoxEdgeRect *out)
{
	const struct Level *lev = (const struct Level *)body;
	u32 i;

	if (size < (int)sizeof(struct Level) || lev->numModels == 0 || lev->numModels > AP_BOX_DISC_MAX_MODELS ||
	    (const u8 *)lev->ptrModelsPtrArray < body ||
	    (const u8 *)(lev->ptrModelsPtrArray + lev->numModels) > body + size)
		return 0;

	for (i = 0; i < lev->numModels; i++)
	{
		const struct Model *m = lev->ptrModelsPtrArray[i];
		int h;

		if (m == 0)
			break;
		if ((const u8 *)m < body || (const u8 *)m + sizeof(struct Model) > body + size)
			return 0;
		if (m->id != modelId)
			continue;
		if (m->numHeaders <= 0 || (const u8 *)m->headers < body ||
		    (const u8 *)m->headers + (u32)m->numHeaders * AP_BOX_DISC_HEADER_STRIDE > body + size)
			return 0;

		for (h = 0; h < m->numHeaders; h++)
		{
			const struct ModelHeader *mh =
			    (const struct ModelHeader *)((const u8 *)m->headers + h * AP_BOX_DISC_HEADER_STRIDE);
			const u32 *cmd = (const u32 *)(uintptr_t)mh->ptrCommandList;
			int n, maxCmd;

			if ((const u8 *)cmd < body || (const u8 *)cmd + 8 > body + size ||
			    (const u8 *)mh->ptrTexLayout < body || (const u8 *)mh->ptrTexLayout >= body + size)
				continue;
			maxCmd = (int)(((const u32 *)(body + size)) - cmd);
			if (maxCmd > AP_BOX_DISC_MAX_CMDS)
				maxCmd = AP_BOX_DISC_MAX_CMDS;

			// cmd[0] is the colour count; texture index is the low 9 bits,
			// 1-based, zero for a colour-only command.
			for (n = 1; n < maxCmd && cmd[n] != 0xFFFFFFFFu; n++)
			{
				int ti = (int)(cmd[n] & 0x1FF);
				const struct TextureLayout *tl;
				unsigned char uv[8];
				AP_BoxEdgeRect r;

				if ((cmd[n] >> 16) == 0 || ti == 0)
					continue;
				if ((const u8 *)&mh->ptrTexLayout[ti - 1] + sizeof(void *) > body + size)
					continue;
				tl = mh->ptrTexLayout[ti - 1];
				if ((const u8 *)tl < body || (const u8 *)tl + sizeof(struct TextureLayout) > body + size)
					continue;
				uv[0] = tl->u0; uv[1] = tl->v0; uv[2] = tl->u1; uv[3] = tl->v1;
				uv[4] = tl->u2; uv[5] = tl->v2; uv[6] = tl->u3; uv[7] = tl->v3;
				if (!AP_BoxEdge_RectFromLayout(uv, tl->tpage, tl->clut, &r))
					continue;
				if (wantWood ? AP_BoxEdge_IsWoodRect(&r) : AP_BoxEdge_IsFaceRect(&r))
				{
					*out = r;
					return 1;
				}
			}
		}
		break;
	}
	return 0;
}

// The one-time read: the "?" crate's wood tile and the Wumpa crate's face,
// decoded but not recoloured (each colour recolours its own copy).
static void AP_BoxDisc_Read(void)
{
	AP_BoxEdgeRect wood, crate;
	const char *why = "level read";
	int haveWoodRect = 0, haveCrateRect = 0, size = 0;
	char msg[160];
	u8 *buf, *body;

	s_apBoxDiscState = 1;

	buf = (u8 *)malloc(AP_BOX_DISC_LEV_BUF);
	if (buf != 0)
	{
		body = AP_RetailAsset_ReadSubfile(AP_BOX_DISC_SRC_LEV, 1, buf, AP_BOX_DISC_LEV_BUF, &size);
		if (body != 0)
		{
			why = "crate layout";
			haveWoodRect = AP_BoxDisc_FindRect(body, size, PU_RANDOM_CRATE, 1, &wood);
			haveCrateRect = AP_BoxDisc_FindRect(body, size, PU_FRUIT_CRATE, 0, &crate);
		}
		free(buf);
	}

	if (haveWoodRect || haveCrateRect)
	{
		why = "texture read";
		buf = (u8 *)malloc(AP_BOX_DISC_VRM_BUF);
		if (buf != 0)
		{
			body = AP_RetailAsset_ReadSubfile(AP_BOX_DISC_SRC_VRM, 0, buf, AP_BOX_DISC_VRM_BUF, &size);
			if (body != 0)
			{
				why = "texel decode";
				s_apBoxHaveWood = haveWoodRect && AP_BoxEdge_Decode4bpp(body, size, &wood, s_apBoxDiscWood, 16);
				s_apBoxHaveCrate =
				    haveCrateRect && AP_BoxEdge_Decode4bpp(body, size, &crate, s_apBoxDiscCrate, 64);
			}
			free(buf);
		}
	}

	if (s_apBoxHaveWood)
		AP_LogLine("[AP BOX] wood border read from the disc\n");
	else
	{
		snprintf(msg, sizeof msg, "[AP BOX] wood border from the disc unavailable (%s); using the built-in border\n",
		         why);
		AP_LogLine(msg);
	}
	if (s_apBoxHaveCrate)
		AP_LogLine("[AP BOX] face: Wumpa crate from the disc, Archipelago logo on top\n");
	else
	{
		snprintf(msg, sizeof msg, "[AP BOX] crate face from the disc unavailable (%s); using the built-in face\n",
		         why);
		AP_LogLine(msg);
	}
}

void AP_BoxTexture_Prepare(void)
{
	if (s_apBoxDiscState != 0)
		return;
	// Blocks on disc IO, so it waits for a frame with no load in flight, the
	// same gate the crystal and Wumpa harvests use.
	if (sdata == 0 || sdata->ptrBigfile1 == 0 || sdata->Loading.stage != LOAD_IDLE)
		return;
	AP_BoxDisc_Read();
}

int AP_BoxTexture_Ensure(void)
{
	unsigned tex;
	char     msg[160];
	int      c;

	if (s_apBoxTextureState != 0)
		return s_apBoxTextureState == 1;

	AP_BoxTexture_Prepare();
	if (s_apBoxDiscState == 0)
	{
		// Asked for the atlas before an idle frame came by: go with the
		// built-in art rather than hold the box back.
		s_apBoxDiscState = 1;
		AP_LogLine("[AP BOX] box needed before the disc read could run; using the built-in border and face\n");
	}

	memset(s_apBoxAtlasLive, 0, sizeof s_apBoxAtlasLive);
	for (c = 0; c < AP_BOX_COLOUR_COUNT; c++)
		AP_BoxColour_BuildSlot(
		    s_apBoxAtlasLive, c, s_apBoxHaveWood ? s_apBoxDiscWood : 0, s_apBoxHaveCrate ? s_apBoxDiscCrate : 0,
		    &s_apBoxTextureAtlas[(AP_BOX_TEXTURE_WOOD_Y * AP_BOX_TEXTURE_ATLAS_W + AP_BOX_TEXTURE_WOOD_X) * 4],
		    &s_apBoxTextureAtlas[(AP_BOX_TEXTURE_FACE_Y * AP_BOX_TEXTURE_ATLAS_W + AP_BOX_TEXTURE_FACE_X) * 4],
		    AP_BOX_TEXTURE_ATLAS_W, s_apBoxLogoMask);

	tex = (unsigned)NativeRenderer_CreateRGBATexture(AP_BOX_ATLAS_W, AP_BOX_ATLAS_H, s_apBoxAtlasLive);
	if (tex == 0)
	{
		AP_LogLine("[AP BOX] box atlas upload failed; keeping the fallback cube\n");
		s_apBoxTextureState = 2;
		return 0;
	}

	NativeGpu_SetSideloadTexture(tex, AP_BOX_ATLAS_W, AP_BOX_ATLAS_H);
	s_apBoxTextureState = 1;

	snprintf(msg, sizeof msg, "[AP BOX] box atlas uploaded: %dx%d, %d colours, %s border, %s face\n",
	         AP_BOX_ATLAS_W, AP_BOX_ATLAS_H, AP_BOX_COLOUR_COUNT, s_apBoxHaveWood ? "disc wood" : "built-in",
	         s_apBoxHaveCrate ? "disc Wumpa crate" : "built-in");
	AP_LogLine(msg);
	return 1;
}

#endif // CTR_AP
