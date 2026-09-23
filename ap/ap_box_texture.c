// Upload the AP box's own face art as the AP sideload texture.
//
// WHAT REPLACED WHAT
// ------------------
// This module replaces the retail-crate harvest that previously produced the
// same atlas. That harvest read a 1p LEV+VRM pair out of BIGFILE, ran the
// engine's pointer fixup on it, walked `crate_question`'s command lists to the
// texture layouts it genuinely used, converted its 4bpp+CLUT texels to RGBA on
// the CPU and packed them into a 128x64 atlas. It worked, but it made the AP
// box's appearance a function of the player's game data, and it could fail --
// on an unreadable BIGFILE, an unexpected level layout, or a stray pointer --
// leaving the plain fallback cube behind. The face art is now shipped, so none
// of that can happen to the face. Only the 16x16 wood border is read from the
// disc again (see THE WOOD BORDER below), and a failed read there swaps the
// border for a built-in one; it never costs the box its texture.
//
// WHY THE ATLAS LOOKS THE WAY IT DOES
// -----------------------------------
// The atlas keeps the shape the harvest produced, so the layout below is the
// same twelve bytes the model consumed before, and only the pixels changed:
//
//   ( 0, 0) 16x16  the wood frame
//   (16, 0) 64x64  the near-LOD face -- the ONLY rect the box model samples
//   (80, 0) 32x32  the far-LOD face
//
// tpage and clut are copied from the retail layout this replaces, measured off
// NTSC-U BIGFILE entry 1 (1p level 0), model `crate_question`, near-LOD header.
// They are not decorative:
//
//   tpage bits 5-6 (the semi-transparency field) are 3 here. The engine's own
//   primitive writer branches on exactly that field: with it non-zero it emits
//   an OPAQUE code word, and with it zero it emits a semi-transparent one, so a
//   layout that lost those bits draws the box see-through. That is not
//   hypothetical -- overwriting the whole tpage word instead of OR-ing the
//   sideload bit into it drew the crate 50% transparent (observed live
//   2026-08-17). Bits 0-4 (the VRAM page) are meaningless once the primitive
//   samples the sideload texture, and bits 7-8 (colour depth) are overridden to
//   RGBA by the sideload path; they are kept only so the word stays the retail
//   word plus one flag.
//
//   clut is likewise inert for a sideloaded primitive -- the sideload path
//   forces TF_32_BIT_RGBA, so nothing resolves a palette -- and is carried for
//   the same reason.
//
// AP_TPAGE_SIDELOAD_BIT is what actually routes the primitive at the sideload
// texture. It is OR-ed in, never assigned over the word.
//
// THE WOOD BORDER
// ---------------
// The box is framed like the retail "?" crate (ap/ap_box_model_framed_data.h):
// the face sits in the middle of each side and a border ring samples the 16x16
// wood rect at (0,0). That rect is filled at runtime, once, from the player's
// own disc: the retail crate's wood tile, recoloured to the box's pink by
// AP_BoxEdge_Recolour (ap/ap_box_edge_logic.h). No retail pixel is compiled
// into this build. The read waits for a frame with no load in flight
// (AP_BoxTexture_PrepareEdge, called every frame from the frame hook), so it
// normally completes at the title screen, long before any box spawns. If it
// fails, or has not run by the time the atlas is uploaded, the compiled atlas
// keeps JurnthReinal's own 16x16 border tile, and the box is still framed.

#ifdef CTR_AP

#include <common.h>
#include <stdio.h>

#include "ap_box_texture.h"
#include "ap_box_texture_data.h"
#include "ap_hooks.h" // AP_LogLine
#include "ap_retail_asset.h" // AP_RetailAsset_ReadSubfile
#include "ap_box_edge_logic.h" // decode + recolour of the wood tile (harness-pinned)

#include <stdlib.h>
#include <string.h>

#include <platform/native_gpu.h>      // AP_TPAGE_SIDELOAD_BIT, NativeGpu_SetSideloadTexture
#include <platform/native_renderer.h> // NativeRenderer_CreateRGBATexture

// Retail `crate_question` near-LOD values, carried verbatim. See the note above.
#define AP_BOX_TEXTURE_FACE_TPAGE 0x0069
#define AP_BOX_TEXTURE_FACE_CLUT  0x3F6A

// Unity-build names must be module-specific: several AP modules land in the same
// C translation unit, and a generic static tentative definition silently
// coalesces in C.
static int                  s_apBoxTextureState; // 0 = untried, 1 = ready, 2 = failed
static struct TextureLayout s_apBoxTextureFace;

// ── the wood border from the player's disc ──────────────────────────────────

// Source: the 1p level of track 0 and its texture file. BIGFILE groups each
// track as (vrm, lev) pairs per mode, 1p first. The crate's wood tile is the
// same on every track; only its VRAM position differs, and the level's own
// crate_question names that position, so the pair must be read together.
#define AP_BOX_EDGE_SRC_VRM (BI_ARCADETRACKS + 0 * 8 + 0)
#define AP_BOX_EDGE_SRC_LEV (BI_ARCADETRACKS + 0 * 8 + 1)
// Sector-aligned maxima across all 18 tracks: 1p LEV at most 780008 bytes,
// every 1p VRM 458808. Temporary: freed as soon as the tile is decoded.
#define AP_BOX_EDGE_LEV_BUF 780288
#define AP_BOX_EDGE_VRM_BUF 460800
#define AP_BOX_EDGE_HEADER_STRIDE 0x40 // sizeof(struct ModelHeader), asserted in RenderBucket
#define AP_BOX_EDGE_MAX_MODELS 512
#define AP_BOX_EDGE_MAX_CMDS 4096

static int s_apBoxEdgeState; // 0 = untried, 1 = disc tile in the atlas, 2 = built-in border
static u8  s_apBoxAtlasLive[AP_BOX_TEXTURE_ATLAS_W * AP_BOX_TEXTURE_ATLAS_H * 4];

// Find the wood rect crate_question samples in a fixed-up level body. Every
// pointer is checked against the buffer before it is followed.
static int AP_BoxEdge_FindWood(const u8 *body, int size, AP_BoxEdgeRect *out)
{
	const struct Level *lev = (const struct Level *)body;
	u32 i;

	if (size < (int)sizeof(struct Level) || lev->numModels == 0 || lev->numModels > AP_BOX_EDGE_MAX_MODELS ||
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
		if (m->id != PU_RANDOM_CRATE)
			continue;
		if (m->numHeaders <= 0 || (const u8 *)m->headers < body ||
		    (const u8 *)m->headers + (u32)m->numHeaders * AP_BOX_EDGE_HEADER_STRIDE > body + size)
			return 0;

		for (h = 0; h < m->numHeaders; h++)
		{
			const struct ModelHeader *mh =
			    (const struct ModelHeader *)((const u8 *)m->headers + h * AP_BOX_EDGE_HEADER_STRIDE);
			const u32 *cmd = (const u32 *)(uintptr_t)mh->ptrCommandList;
			int n, maxCmd;

			if ((const u8 *)cmd < body || (const u8 *)cmd + 8 > body + size ||
			    (const u8 *)mh->ptrTexLayout < body || (const u8 *)mh->ptrTexLayout >= body + size)
				continue;
			maxCmd = (int)(((const u32 *)(body + size)) - cmd);
			if (maxCmd > AP_BOX_EDGE_MAX_CMDS)
				maxCmd = AP_BOX_EDGE_MAX_CMDS;

			// cmd[0] is the colour count; texture index is the low 9 bits,
			// 1-based, zero for a colour-only command.
			for (n = 1; n < maxCmd && cmd[n] != 0xFFFFFFFFu; n++)
			{
				int ti = (int)(cmd[n] & 0x1FF);
				const struct TextureLayout *tl;
				unsigned char uv[8];

				if ((cmd[n] >> 16) == 0 || ti == 0)
					continue;
				if ((const u8 *)&mh->ptrTexLayout[ti - 1] + sizeof(void *) > body + size)
					continue;
				tl = mh->ptrTexLayout[ti - 1];
				if ((const u8 *)tl < body || (const u8 *)tl + sizeof(struct TextureLayout) > body + size)
					continue;
				uv[0] = tl->u0; uv[1] = tl->v0; uv[2] = tl->u1; uv[3] = tl->v1;
				uv[4] = tl->u2; uv[5] = tl->v2; uv[6] = tl->u3; uv[7] = tl->v3;
				if (AP_BoxEdge_RectFromLayout(uv, tl->tpage, tl->clut, out) && AP_BoxEdge_IsWoodRect(out))
					return 1;
			}
		}
		return 0;
	}
	return 0;
}

// The one-time read. Leaves s_apBoxAtlasLive holding the compiled atlas with,
// on success, the recoloured disc tile in its wood rect. Sticky either way.
static void AP_BoxEdge_Harvest(void)
{
	u8 *buf;
	u8 *body;
	int size = 0, ok = 0;
	AP_BoxEdgeRect rect;
	u8 tile[16 * 16 * 4];
	const char *why = "level read";

	memcpy(s_apBoxAtlasLive, s_apBoxTextureAtlas, sizeof s_apBoxAtlasLive);
	s_apBoxEdgeState = 2;

	buf = (u8 *)malloc(AP_BOX_EDGE_LEV_BUF);
	if (buf != 0)
	{
		body = AP_RetailAsset_ReadSubfile(AP_BOX_EDGE_SRC_LEV, 1, buf, AP_BOX_EDGE_LEV_BUF, &size);
		why = body == 0 ? "level read" : "crate wood layout";
		ok = body != 0 && AP_BoxEdge_FindWood(body, size, &rect);
		free(buf);
	}
	if (ok)
	{
		ok = 0;
		why = "texture read";
		buf = (u8 *)malloc(AP_BOX_EDGE_VRM_BUF);
		if (buf != 0)
		{
			body = AP_RetailAsset_ReadSubfile(AP_BOX_EDGE_SRC_VRM, 0, buf, AP_BOX_EDGE_VRM_BUF, &size);
			if (body != 0)
			{
				why = "texel decode";
				ok = AP_BoxEdge_Decode4bpp(body, size, &rect, tile, 16);
			}
			free(buf);
		}
	}
	if (!ok)
	{
		char msg[128];
		snprintf(msg, sizeof msg, "[AP BOX] wood border from the disc unavailable (%s); using the built-in border\n",
		         why);
		AP_LogLine(msg);
		return;
	}

	AP_BoxEdge_Recolour(tile, 16, 16, 16);
	{
		int y;
		for (y = 0; y < AP_BOX_TEXTURE_WOOD_H; y++)
			memcpy(&s_apBoxAtlasLive[((AP_BOX_TEXTURE_WOOD_Y + y) * AP_BOX_TEXTURE_ATLAS_W + AP_BOX_TEXTURE_WOOD_X) * 4],
			       &tile[y * 16 * 4], AP_BOX_TEXTURE_WOOD_W * 4);
	}
	s_apBoxEdgeState = 1;
	AP_LogLine("[AP BOX] wood border read from the disc and recoloured\n");
}

void AP_BoxTexture_PrepareEdge(void)
{
	if (s_apBoxEdgeState != 0)
		return;
	// Blocks on disc IO, so it waits for a frame with no load in flight, the
	// same gate the crystal and Wumpa harvests use.
	if (sdata == 0 || sdata->ptrBigfile1 == 0 || sdata->Loading.stage != LOAD_IDLE)
		return;
	AP_BoxEdge_Harvest();
}

int AP_BoxTexture_EnsureFace(struct TextureLayout *outFace)
{
	unsigned tex;
	char     msg[160];

	if (outFace == 0 || s_apBoxTextureState == 2)
		return 0;

	if (s_apBoxTextureState == 1)
	{
		*outFace = s_apBoxTextureFace;
		return 1;
	}

	AP_BoxTexture_PrepareEdge();
	if (s_apBoxEdgeState == 0)
	{
		// Asked for the atlas before an idle frame came by: go with the
		// built-in border rather than hold the box back.
		memcpy(s_apBoxAtlasLive, s_apBoxTextureAtlas, sizeof s_apBoxAtlasLive);
		s_apBoxEdgeState = 2;
		AP_LogLine("[AP BOX] box needed before the disc read could run; using the built-in border\n");
	}

	tex = (unsigned)NativeRenderer_CreateRGBATexture(AP_BOX_TEXTURE_ATLAS_W, AP_BOX_TEXTURE_ATLAS_H,
	                                                s_apBoxAtlasLive);
	if (tex == 0)
	{
		AP_LogLine("[AP BOX] box atlas upload failed; keeping the fallback cube\n");
		s_apBoxTextureState = 2;
		return 0;
	}

	NativeGpu_SetSideloadTexture(tex, AP_BOX_TEXTURE_ATLAS_W, AP_BOX_TEXTURE_ATLAS_H);

	// Corner order matches the retail layout this replaces: top-left,
	// bottom-left, top-right, and a fourth corner that repeats the third
	// because the source layout is a triangle. The far corners stop at W-2 /
	// H-2: PSX rasterization never samples a rect's last texel column or row,
	// so the contributed art carries magenta filler there (confirmed with the
	// artist, 2026-08-21). Retail hardware skips it; our GL path samples
	// inclusively and was painting that filler as a magenta fringe on two edges
	// of every face.
	s_apBoxTextureFace.u0 = (u8)(AP_BOX_TEXTURE_FACE_X);
	s_apBoxTextureFace.v0 = (u8)(AP_BOX_TEXTURE_FACE_Y);
	s_apBoxTextureFace.u1 = (u8)(AP_BOX_TEXTURE_FACE_X);
	s_apBoxTextureFace.v1 = (u8)(AP_BOX_TEXTURE_FACE_Y + AP_BOX_TEXTURE_FACE_H - 2);
	s_apBoxTextureFace.u2 = (u8)(AP_BOX_TEXTURE_FACE_X + AP_BOX_TEXTURE_FACE_W - 2);
	s_apBoxTextureFace.v2 = (u8)(AP_BOX_TEXTURE_FACE_Y);
	s_apBoxTextureFace.u3 = s_apBoxTextureFace.u2;
	s_apBoxTextureFace.v3 = s_apBoxTextureFace.v2;
	s_apBoxTextureFace.clut = (u16)AP_BOX_TEXTURE_FACE_CLUT;
	s_apBoxTextureFace.tpage = (u16)(AP_BOX_TEXTURE_FACE_TPAGE | AP_TPAGE_SIDELOAD_BIT);

	s_apBoxTextureState = 1;
	*outFace = s_apBoxTextureFace;

	snprintf(msg, sizeof msg, "[AP BOX] box face atlas uploaded: %dx%d, face rect %dx%d at (%d,%d), %s border\n",
	         AP_BOX_TEXTURE_ATLAS_W, AP_BOX_TEXTURE_ATLAS_H, AP_BOX_TEXTURE_FACE_W, AP_BOX_TEXTURE_FACE_H,
	         AP_BOX_TEXTURE_FACE_X, AP_BOX_TEXTURE_FACE_Y, s_apBoxEdgeState == 1 ? "disc wood" : "built-in");
	AP_LogLine(msg);

	return 1;
}

#endif // CTR_AP
