// Harvest the retail weapon crate's pixels out of the player's own game data,
// and hand them to the renderer as a sideloaded texture. No Model pointer from
// the harvested LEV escapes this file.
//
// WHY THIS EXISTS
// ---------------
// The relic variant of EVERY track's level file simply does not ship the weapon
// crate model -- verified against retail data on all 18 tracks (1p/2p/4p carry
// `crate_question`, relic never does; it carries the three time crates instead).
// So AP boxes could not be drawn in any relic race, which strands ~30 locations
// on the two trial tracks that have no non-relic race at all.
//
// The crate's TEXTURE is likewise absent in relic mode: every mode uploads the
// same two VRAM rectangles, but with different content, so the crate's texels
// are simply not there. And there is no reliable free VRAM to inject them into
// (the relic uploads leave as few as two spare 16x16 blocks on some tracks).
//
// HOW IT WORKS
// ------------
// 1. Read a 1p level file straight out of BIGFILE with the engine's own reader.
// 2. Run the engine's own pointer fixup on it, so the file becomes walkable
//    with the ordinary struct definitions. No format is reimplemented here.
// 3. Find `crate_question`, then follow each header's command list to the
//    texture layouts it genuinely uses (the layout array has no stored count --
//    RenderBucket indexes it by `command & 0x1ff`, 1-based).
// 4. Pull those texels and palettes out of the matching 1p VRM, convert 4bpp +
//    CLUT to RGBA on the CPU, and pack them into one atlas.
// 5. Copy the largest face layout by value, remapped to the atlas and carrying
//    AP_TPAGE_SIDELOAD_BIT, for the AP-owned crate model to consume.
//
// HARVEST ONCE, NOT PER TRACK. The crate's texture bytes are byte-identical on
// every track (verified: same texel+palette hash for all of 0/5/10/17); only
// its VRAM location differs. So one harvest serves every relic race.
//
// The level buffer is scratch storage only. Its fixed-up pointer graph is walked
// during the harvest, but never registered with the live level or retained.

#ifdef CTR_AP

#include <common.h>
#include <stdio.h>

#include "ap_retail_crate.h"
#include "ap_retail_asset.h" // AP_RetailAsset_ReadSubfile (shared with #219)
#include "ap_hooks.h" // AP_LogLine

#include <platform/native_gpu.h>        // AP_TPAGE_SIDELOAD_BIT, NativeGpu_SetSideloadTexture
#include <platform/native_renderer.h> // NativeRenderer_CreateRGBATexture

// BIGFILE layout: BI_ARCADETRACKS + levelID*8, then (vrm, lev) per mode in the
// order 1p, 2p, 4p, relic. So the 1p pair is +0 (vrm) and +1 (lev).
#define AP_CRATE_SRC_TRACK 0
#define AP_CRATE_SRC_VRM   (BI_ARCADETRACKS + AP_CRATE_SRC_TRACK * 8 + 0)
#define AP_CRATE_SRC_LEV   (BI_ARCADETRACKS + AP_CRATE_SRC_TRACK * 8 + 1)

// Sector-aligned maxima measured across all 18 tracks: 1p LEV tops out at
// 780008 bytes, every 1p VRM is exactly 458808.
#define AP_CRATE_LEV_BUF 780288
#define AP_CRATE_VRM_BUF 460800

// The crate needs exactly three distinct rects on every track: 64x64, 32x32 and
// 16x16 texels. 128x64 holds all three with room to spare.
#define AP_CRATE_ATLAS_W 128
#define AP_CRATE_ATLAS_H 64

#define AP_CRATE_MAX_RECTS 8
#define MODELHEADER_STRIDE 0x40 // CTR_STATIC_ASSERT'd in RenderBucket_QueueExecute.c

static u8  s_levBuf[AP_CRATE_LEV_BUF];
static u8  s_vrmBuf[AP_CRATE_VRM_BUF];
static u8  s_atlas[AP_CRATE_ATLAS_W * AP_CRATE_ATLAS_H * 4];
// Unity-build name must be module-specific. ap_retail_crystal.c is included in
// the same C translation unit and also owns a harvest state; a generic static
// tentative definition silently coalesces in C, making a successful crystal
// harvest falsely report the crate pixels ready before this module ever runs.
static int s_crateHarvestState; // 0 = untried, 1 = ready, 2 = failed (do not retry)

static struct TextureLayout s_faceLayout;

// Bounds for validating fixed-up pointers while walking the harvested file.
static const u8 *s_levBase;
static int       s_levSize;

// ── VRM access ──────────────────────────────────────────────────────────────
//
// Format from LOAD_VramFileCallback: word0 == 0x20 means several TIMs packed,
// then repeating { int size; VramHeader{char data[0xC]; RECT rect}; pixels },
// where size is the byte step from the header to the next size word.
static const u16 *AP_CrateVramAt(const u8 *vrm, int vrmSize, int x, int y, int *strideOut)
{
	int pos;

	if (vrmSize < 8 || *(const u32 *)vrm != 0x20)
		return 0;

	pos = 4;
	while (pos + 4 <= vrmSize)
	{
		int         size = *(const int *)(vrm + pos);
		const u8   *vh;
		const s16  *rect;
		int         rx, ry, rw, rh;

		if (size == 0)
			break;

		vh = vrm + pos + 4;
		if ((int)((vh + 0x14) - vrm) > vrmSize)
			break;

		rect = (const s16 *)(vh + 0xC);
		rx = rect[0];
		ry = rect[1];
		rw = rect[2];
		rh = rect[3];

		if (rw > 0 && rh > 0 && x >= rx && y >= ry && x < rx + rw && y < ry + rh)
		{
			*strideOut = rw;
			return (const u16 *)(vh + 0x14) + (y - ry) * rw + (x - rx);
		}

		pos = (int)((vh - vrm)) + size;
	}

	return 0;
}

// PSX 15-bit BGR555 -> RGBA8. Colour 0x0000 is the transparent entry.
static void AP_CratePutPixel(u8 *dst, u16 c)
{
	if (c == 0)
	{
		dst[0] = 0;
		dst[1] = 0;
		dst[2] = 0;
		dst[3] = 0;
		return;
	}

	dst[0] = (u8)((c & 0x1F) << 3);
	dst[1] = (u8)(((c >> 5) & 0x1F) << 3);
	dst[2] = (u8)(((c >> 10) & 0x1F) << 3);
	dst[3] = 0xFF;
}

// Copy one 4bpp rect out of the VRM into the atlas, resolving through its CLUT.
static int AP_CrateBlit(const u8 *vrm, int vrmSize, int pageX, int pageY, int minU, int minV, int w, int h, int clutX,
                        int clutY, int atlasX, int atlasY)
{
	const u16 *pal;
	int        palStride;
	int        yy;

	pal = AP_CrateVramAt(vrm, vrmSize, clutX, clutY, &palStride);
	if (pal == 0)
		return 0;

	for (yy = 0; yy < h; yy++)
	{
		int        stride;
		const u16 *row = AP_CrateVramAt(vrm, vrmSize, pageX + (minU >> 2), pageY + minV + yy, &stride);
		int        xx;

		if (row == 0)
			return 0;

		for (xx = 0; xx < w; xx++)
		{
			// 4 texels per 16-bit word, low nibble first.
			int texel = minU + xx;
			int word = (texel >> 2) - (minU >> 2);
			int nib = texel & 3;
			u16 packed = row[word];
			int index = (packed >> (nib * 4)) & 0xF;

			AP_CratePutPixel(&s_atlas[((atlasY + yy) * AP_CRATE_ATLAS_W + (atlasX + xx)) * 4], pal[index]);
		}
	}

	return 1;
}

// ── the harvest ─────────────────────────────────────────────────────────────

struct ApCrateRect
{
	int pageX, pageY;
	int minU, minV;
	int w, h; // texels
	int clutX, clutY;
	int atlasX, atlasY;
};

// Bounds-checked against the buffer the file was read into: after fixup every
// pointer in the file must land inside it, so anything outside means the fixup
// or the layout assumption is wrong and must be reported rather than followed.
static struct Model *AP_CrateFindModel(struct Level *lev, const u8 *base, int size)
{
	u32 i;

	if (lev == 0 || lev->ptrModelsPtrArray == 0)
		return 0;

	for (i = 0; i < lev->numModels; i++)
	{
		struct Model *m = lev->ptrModelsPtrArray[i];

		if (m == 0)
			continue;
		if ((const u8 *)m < base || (const u8 *)m + sizeof(struct Model) > base + size)
			return 0; // not a fixed-up pointer: stop, do not dereference

		if (m->id == PU_RANDOM_CRATE)
		{
			if (m->headers == 0 || (const u8 *)m->headers < base ||
			    (const u8 *)m->headers + (u32)m->numHeaders * MODELHEADER_STRIDE > base + size)
				return 0;
			return m;
		}
	}

	return 0;
}

// Collect the distinct source rects the crate actually samples, and rewrite each
// layout to the atlas as we go. Corner order is preserved by remapping every
// corner relative to the rect origin, so orientation cannot flip.
static int AP_CrateCollectAndRemap(struct Model *model, struct ApCrateRect *rects, int maxRects,
                                  struct TextureLayout *faceOut)
{
	int nRects = 0;
	int packX = 0, packY = 0, rowH = 0;
	int bestArea = 0;
	int h;

	for (h = 0; h < model->numHeaders; h++)
	{
		struct ModelHeader *mh = (struct ModelHeader *)((u8 *)model->headers + h * MODELHEADER_STRIDE);
		const u32          *cmd = (const u32 *)(uintptr_t)mh->ptrCommandList;
		int                 seenIdx[512];
		int                 i;
		int                 maxCmd;

		// The command list is walked until a 0xffffffff terminator. Nothing
		// records its length, so if ptrCommandList is not a fixed-up pointer, or
		// the terminator is missing, the walk runs off the end of the buffer.
		// Bound it to the file, and bound the layout array pointer too.
		if (cmd == 0 || mh->ptrTexLayout == 0)
			continue;
		if ((const u8 *)cmd < s_levBase || (const u8 *)cmd + 8 > s_levBase + s_levSize)
			continue;
		if ((const u8 *)mh->ptrTexLayout < s_levBase ||
		    (const u8 *)mh->ptrTexLayout + sizeof(void *) > s_levBase + s_levSize)
			continue;

		maxCmd = (int)(((const u32 *)(s_levBase + s_levSize)) - cmd);
		if (maxCmd > 4096)
			maxCmd = 4096;

		for (i = 0; i < 512; i++)
			seenIdx[i] = 0;

		// cmd[0] is the colour-cache count; commands run from cmd[1] to the
		// 0xffffffff terminator, one word each. A command whose high half is
		// zero is colour-only and carries no texture.
		for (i = 1; i < maxCmd && cmd[i] != 0xFFFFFFFFu; i++)
		{
			u32 c = cmd[i];
			int texIndex = (int)(c & 0x1FF);
			struct TextureLayout *tl;
			int us[4], vs[4], minU, minV, maxU, maxV, w, wTexels, k, found;

			if ((c >> 16) == 0 || texIndex == 0 || seenIdx[texIndex])
				continue;
			seenIdx[texIndex] = 1;

			// ptrTexLayout has NO stored length -- RenderBucket just indexes it
			// by (command & 0x1ff). A stray index would read past the array and
			// then dereference whatever it found, so both the slot and the
			// layout it yields are bounds-checked against the file buffer.
			if ((const u8 *)&mh->ptrTexLayout[texIndex - 1] + sizeof(void *) > s_levBase + s_levSize)
				continue;

			tl = mh->ptrTexLayout[texIndex - 1];
			if (tl == 0 || (const u8 *)tl < s_levBase ||
			    (const u8 *)tl + sizeof(struct TextureLayout) > s_levBase + s_levSize)
				continue;

			us[0] = tl->u0; us[1] = tl->u1; us[2] = tl->u2; us[3] = tl->u3;
			vs[0] = tl->v0; vs[1] = tl->v1; vs[2] = tl->v2; vs[3] = tl->v3;

			minU = maxU = us[0];
			minV = maxV = vs[0];
			for (k = 1; k < 4; k++)
			{
				if (us[k] < minU) minU = us[k];
				if (us[k] > maxU) maxU = us[k];
				if (vs[k] < minV) minV = vs[k];
				if (vs[k] > maxV) maxV = vs[k];
			}
			wTexels = maxU - minU + 1;

			// Reuse an identical rect rather than packing it twice: most of the
			// crate's 20 layouts share only two distinct rects.
			found = -1;
			for (k = 0; k < nRects; k++)
			{
				if (rects[k].pageX == ((tl->tpage & 0x0F) * 64) && rects[k].pageY == (((tl->tpage >> 4) & 1) * 256) &&
				    rects[k].minU == minU && rects[k].minV == minV && rects[k].w == wTexels)
				{
					found = k;
					break;
				}
			}

			if (found < 0)
			{
				if (nRects >= maxRects)
					return -1;

				w = wTexels;
				if (packX + w > AP_CRATE_ATLAS_W)
				{
					packX = 0;
					packY += rowH;
					rowH = 0;
				}
				if (packY + (maxV - minV + 1) > AP_CRATE_ATLAS_H)
					return -1;

				rects[nRects].pageX = (tl->tpage & 0x0F) * 64;
				rects[nRects].pageY = ((tl->tpage >> 4) & 1) * 256;
				rects[nRects].minU = minU;
				rects[nRects].minV = minV;
				rects[nRects].w = w;
				rects[nRects].h = maxV - minV + 1;
				rects[nRects].atlasX = packX;
				rects[nRects].atlasY = packY;
				rects[nRects].clutX = (tl->clut & 0x3F) * 16;
				rects[nRects].clutY = (tl->clut >> 6) & 0x1FF;

				packX += w;
				if (rects[nRects].h > rowH)
					rowH = rects[nRects].h;
				found = nRects;
				nRects++;
			}

			// Rewrite in place: same corners, atlas coordinates, sideload bit.
			tl->u0 = (u8)(rects[found].atlasX + (us[0] - minU));
			tl->u1 = (u8)(rects[found].atlasX + (us[1] - minU));
			tl->u2 = (u8)(rects[found].atlasX + (us[2] - minU));
			tl->u3 = (u8)(rects[found].atlasX + (us[3] - minU));
			tl->v0 = (u8)(rects[found].atlasY + (vs[0] - minV));
			tl->v1 = (u8)(rects[found].atlasY + (vs[1] - minV));
			tl->v2 = (u8)(rects[found].atlasY + (vs[2] - minV));
			tl->v3 = (u8)(rects[found].atlasY + (vs[3] - minV));
			// OR the flag in; do NOT replace the word. tpage also carries the
			// semi-transparency mode (bits 5-6), colour depth (7-8) and dither
			// (9), and AddSplit still reads those. Overwriting the whole word
			// zeroed the blend bits, which selects an averaging blend and drew
			// the crate 50% see-through (observed live 2026-08-17).
			//
			// The page bits (0-4) become meaningless once the texture is
			// sideloaded, which is harmless: nothing samples VRAM for these
			// prims, and the overlap check ignores non-16bpp pages.
			tl->tpage = (u16)(tl->tpage | AP_TPAGE_SIDELOAD_BIT);

			// The near-LOD question-mark face is the largest sampled square
			// (64x64 in retail). Copy the remapped value, never its pointer.
			if (rects[found].w == rects[found].h &&
			    rects[found].w * rects[found].h > bestArea)
			{
				*faceOut = *tl;
				bestArea = rects[found].w * rects[found].h;
			}
		}
	}

	return bestArea > 0 ? nRects : 0;
}

int AP_RetailCrate_EnsureTexture(struct TextureLayout *outFace)
{
	struct ApCrateRect rects[AP_CRATE_MAX_RECTS];
	struct Level      *lev;
	struct Model      *model;
	u8                *levBody;
	u8                *vrmBody;
	int                levSize = 0, vrmSize = 0;
	int                nRects, i;
	unsigned           tex;
	char               msg[160];

	if (s_crateHarvestState == 2)
		return 0;

	if (s_crateHarvestState == 1)
	{
		if (outFace != 0)
			*outFace = s_faceLayout;
		return 1;
	}

	if (outFace == 0 || sdata == 0 || sdata->ptrBigfile1 == 0 ||
	    sdata->Loading.stage != LOAD_IDLE)
		return 0;

	AP_LogLine("[AP CRATE] harvesting the retail crate from the game data...\n");

	levBody = AP_RetailAsset_ReadSubfile(AP_CRATE_SRC_LEV, 1, s_levBuf, AP_CRATE_LEV_BUF, &levSize);
	if (levBody == 0)
	{
		AP_LogLine("[AP CRATE] could not read the source level file; keeping the fallback cube\n");
		s_crateHarvestState = 2;
		return 0;
	}

	lev = (struct Level *)levBody;

	// Validate before walking. Every pointer below has just been produced by the
	// fixup, and a wrong ptrMapOffset would leave them as raw offsets that look
	// like tiny addresses -- dereferencing those is an access violation, not a
	// detectable error, so the sanity check has to happen here.
	{
		char dbg[192];
		snprintf(dbg, sizeof dbg, "[AP CRATE] level read: %d bytes, numModels=%u, modelArray=%p\n", levSize,
		         (unsigned)lev->numModels, (void *)lev->ptrModelsPtrArray);
		AP_LogLine(dbg);
	}

	if (lev->numModels == 0 || lev->numModels > 512 || lev->ptrModelsPtrArray == 0 ||
	    (u8 *)lev->ptrModelsPtrArray < levBody || (u8 *)lev->ptrModelsPtrArray >= levBody + levSize)
	{
		AP_LogLine("[AP CRATE] level header failed validation; keeping the fallback cube\n");
		s_crateHarvestState = 2;
		return 0;
	}

	model = AP_CrateFindModel(lev, levBody, levSize);
	if (model == 0)
	{
		AP_LogLine("[AP CRATE] crate_question absent from the source level; keeping the fallback cube\n");
		s_crateHarvestState = 2;
		return 0;
	}

	{
		char dbg[128];
		snprintf(dbg, sizeof dbg, "[AP CRATE] found crate model, %d header(s)\n", model->numHeaders);
		AP_LogLine(dbg);
	}

	s_levBase = levBody;
	s_levSize = levSize;
	nRects = AP_CrateCollectAndRemap(model, rects, AP_CRATE_MAX_RECTS, &s_faceLayout);

	{
		char dbg[128];
		snprintf(dbg, sizeof dbg, "[AP CRATE] collected %d rect(s)\n", nRects);
		AP_LogLine(dbg);
	}
	if (nRects <= 0)
	{
		AP_LogLine("[AP CRATE] no usable texture layouts; keeping the fallback cube\n");
		s_crateHarvestState = 2;
		return 0;
	}

	vrmBody = AP_RetailAsset_ReadSubfile(AP_CRATE_SRC_VRM, 0, s_vrmBuf, AP_CRATE_VRM_BUF, &vrmSize);
	if (vrmBody == 0)
	{
		AP_LogLine("[AP CRATE] could not read the source texture file; keeping the fallback cube\n");
		s_crateHarvestState = 2;
		return 0;
	}

	{
		char dbg[128];
		snprintf(dbg, sizeof dbg, "[AP CRATE] texture file read: %d bytes\n", vrmSize);
		AP_LogLine(dbg);
	}

	for (i = 0; i < nRects; i++)
	{
		if (!AP_CrateBlit(vrmBody, vrmSize, rects[i].pageX, rects[i].pageY, rects[i].minU, rects[i].minV, rects[i].w,
		                  rects[i].h, rects[i].clutX, rects[i].clutY, rects[i].atlasX, rects[i].atlasY))
		{
			AP_LogLine("[AP CRATE] texel copy failed; keeping the fallback cube\n");
			s_crateHarvestState = 2;
			return 0;
		}
	}

	tex = (unsigned)NativeRenderer_CreateRGBATexture(AP_CRATE_ATLAS_W, AP_CRATE_ATLAS_H, s_atlas);
	if (tex == 0)
	{
		AP_LogLine("[AP CRATE] atlas upload failed; keeping the fallback cube\n");
		s_crateHarvestState = 2;
		return 0;
	}

	NativeGpu_SetSideloadTexture(tex, AP_CRATE_ATLAS_W, AP_CRATE_ATLAS_H);

	s_crateHarvestState = 1;
	*outFace = s_faceLayout;

	snprintf(msg, sizeof msg, "[AP CRATE] harvested retail crate pixels: %d headers, %d rect(s), atlas %dx%d\n",
	         model->numHeaders, nRects, AP_CRATE_ATLAS_W, AP_CRATE_ATLAS_H);
	AP_LogLine(msg);

	return 1;
}

#endif // CTR_AP
