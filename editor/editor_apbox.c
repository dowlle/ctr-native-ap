// The AP item box, drawn in the editor the way the AP client draws it.
//
// Included by editor/editor.c (editor unity build only). The editor is a
// vanilla-engine build (CTR_EDITOR and CTR_AP cannot be combined), so the AP
// box modules themselves (ap/ap_box_model.c, ap/ap_box_texture.c) are not
// compiled here. This file rebuilds the same model from the same generated
// data headers the AP client compiles:
//
//   ap/ap_box_model_data.h    the 36-vertex cube, its textured command list
//                             and the neutral 0x80 colour table
//   ap/ap_box_texture_data.h  the 128x64 face atlas compiled into the client
//
// and repeats the three pieces of AP-side logic that decide how it looks:
// the header scale (the retail item crate's size, 0x910 on every retail LEV),
// the face rect corners (stopping at W-2 / H-2, as ap_box_texture.c does), and
// the six corner-role layouts (the table in ap_box_model.c). If any of those
// change in ap/, change them here too; the editor must show what the client
// shows.
//
// Editor-only addition: --editor-apbox-atlas FILE replaces the compiled atlas
// with a raw 128x64 RGBA file (32768 bytes, rows from the top), so candidate
// box art can be compared on a track without building a client.
//
// Framed box wood border (--editor-apbox-framed): as in ap/ap_box_texture.c,
// the atlas's 16x16 wood rect is filled from the disc at first use: the retail
// crate's wood tile out of track 0's 1p LEV/VRM in BIGFILE, decoded and
// recoloured by ap/ap_box_edge_logic.h. This does not depend on the level
// being shown. --editor-apbox-edge builtin skips the read and shows the
// compiled border tile, which is what the client falls back to.
// --editor-apbox-face wumpa|plain also puts a recoloured crate face from the
// disc behind the Archipelago logo, as the client's AP_BOX_FACE_BASE does.

#include "../ap/ap_box_model_data.h"
#include "../ap/ap_box_texture_data.h"
#include "../ap/ap_box_model_framed_data.h"
#include "../ap/ap_box_edge_logic.h"
#include "../ap/ap_box_logo_mask_data.h"
#include <platform/native_assets.h>
#include <platform/native_disc_image.h>

#include <platform/native_gpu.h>
#include <platform/native_renderer.h>

// Same numbers as ap/ap_box_model.c: the fallback scale is the measured
// retail crate size (0x6d3 x 255 / 192), which is what DeriveScale returns on
// every retail track.
#define EDITOR_APBOX_FALLBACK_SCALE 0x910
#define EDITOR_APBOX_EXTENT         192
// Retail crate_question near-LOD tpage/clut, carried as ap_box_texture.c does.
#define EDITOR_APBOX_FACE_TPAGE 0x0069
#define EDITOR_APBOX_FACE_CLUT  0x3F6A
#define EDITOR_APBOX_ATLAS_BYTES (AP_BOX_TEXTURE_ATLAS_W * AP_BOX_TEXTURE_ATLAS_H * 4)

struct EditorApBoxFrame
{
	struct ModelFrame frame;
	u8 verts[AP_BOX_MODEL_NUM_VERTS * 3];
};

static struct EditorApBoxFrame s_edApBoxFrame;
static struct ModelHeader s_edApBoxHeader;
static struct Model s_edApBox;
static struct TextureLayout s_edApBoxLayoutSet[6];
static struct TextureLayout *s_edApBoxLayouts[6] = {
	&s_edApBoxLayoutSet[0], &s_edApBoxLayoutSet[1], &s_edApBoxLayoutSet[2],
	&s_edApBoxLayoutSet[3], &s_edApBoxLayoutSet[4], &s_edApBoxLayoutSet[5],
};
static int s_edApBoxBuilt;
static int s_edApBoxTextured; // 0 untried, 1 textured, 2 upload failed (plain cube)
static char s_edApBoxAtlasPath[1024];
// --editor-apbox-framed: draw the retail-crate-style box (face square plus a
// border ring carrying the 16x16 wood rect) from ap_box_model_framed_data.h.
static int s_edApBoxFramed;
struct EditorApBoxFramedFrame
{
	struct ModelFrame frame;
	u8 verts[AP_BOX_FRAMED_NUM_VERTS * 3];
};
static struct EditorApBoxFramedFrame s_edApBoxFramedFrame;
static struct TextureLayout *s_edApBoxFramedLayoutPtrs[AP_BOX_FRAMED_NUM_TRIS];
static unsigned char s_edApBoxAtlasOverride[EDITOR_APBOX_ATLAS_BYTES];
static int s_edApBoxEdgeBuiltin; // --editor-apbox-edge builtin

void Editor_Log(const char *format, ...);

// Measured the way ap/ap_box_model.c measures a crate model, reduced to what a
// retail item crate needs: header 0 scale, and the vertex byte extent of its
// command list. Falls back to the measured constant on anything unexpected.
static s16 Editor_ApBoxScale(struct GameTracker *gGT)
{
	struct Model *src = gGT != NULL ? gGT->modelPtr[PU_RANDOM_CRATE] : NULL;
	struct ModelHeader *h;
	const u32 *cmd;
	const u8 *v;
	int n = 0, guard = 0, i, lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0}, ext = 0;

	if (src == NULL || src == &s_edApBox || src->headers == NULL || src->numHeaders <= 0)
		return EDITOR_APBOX_FALLBACK_SCALE;
	h = &src->headers[0];
	cmd = (const u32 *)(uintptr_t)h->ptrCommandList;
	if (h->ptrFrameData == NULL || cmd == NULL || h->scale.x <= 0)
		return EDITOR_APBOX_FALLBACK_SCALE;
	for (cmd++; *cmd != 0xffffffffu && guard < 8192; cmd++, guard++)
		if ((*cmd >> 16) != 0 && ((*cmd >> 24) & 4) == 0)
			n++;
	if (n <= 0 || n > 2048)
		return EDITOR_APBOX_FALLBACK_SCALE;
	v = (const u8 *)h->ptrFrameData + h->ptrFrameData->vertexOffset;
	for (i = 0; i < n * 3; i++)
	{
		int axis = i % 3;
		if (v[i] < lo[axis]) lo[axis] = v[i];
		if (v[i] > hi[axis]) hi[axis] = v[i];
	}
	for (i = 0; i < 3; i++)
		if (hi[i] - lo[i] > ext)
			ext = hi[i] - lo[i];
	if (ext <= 0)
		return EDITOR_APBOX_FALLBACK_SCALE;
	return (s16)((h->scale.x * ext) / EDITOR_APBOX_EXTENT);
}

static const unsigned char *Editor_ApBoxAtlas(void)
{
	FILE *f;
	size_t got;

	if (s_edApBoxAtlasPath[0] == 0)
		return s_apBoxTextureAtlas;
	f = fopen(s_edApBoxAtlasPath, "rb");
	if (f == NULL)
	{
		Editor_Log("APBOX atlas_open_failed path=%s; using the compiled atlas", s_edApBoxAtlasPath);
		return s_apBoxTextureAtlas;
	}
	got = fread(s_edApBoxAtlasOverride, 1, sizeof(s_edApBoxAtlasOverride), f);
	fclose(f);
	if (got != sizeof(s_edApBoxAtlasOverride))
	{
		Editor_Log("APBOX atlas_size_mismatch path=%s bytes=%u expected=%d; using the compiled atlas", s_edApBoxAtlasPath,
		           (unsigned)got, EDITOR_APBOX_ATLAS_BYTES);
		return s_apBoxTextureAtlas;
	}
	Editor_Log("APBOX atlas_override path=%s", s_edApBoxAtlasPath);
	return s_edApBoxAtlasOverride;
}

// ── the wood border from the disc (mirrors ap/ap_box_texture.c) ─────────────
// ap/ap_retail_asset.c is AP-only, so its raw BIGFILE read is repeated here in
// short: loose BIGFILE.BIG first, else the disc image; DRAM files get a bounded
// pointer-map check and the engine fixup, VRAM files are raw.
static unsigned char *Editor_ApBoxReadSubfile(int index, int isDram, unsigned char *dst, int dstSize, int *outSize)
{
	struct BigHeader *big = sdata != NULL ? sdata->ptrBigfile1 : NULL;
	struct BigEntry *entries;
	struct NativeDiscImageFile file;
	FILE *fp;
	int size, sectors, ok = 0;

	if (big == NULL || index < 0 || index >= big->numEntry)
		return NULL;
	entries = BIG_GETENTRY(big);
	size = entries[index].size;
	sectors = (size + 0x7FF) >> 11;
	if (size <= 4 || sectors * 2048 > dstSize)
		return NULL;
	fp = NativeAssets_OpenHostBigfile("rb");
	if (fp != NULL)
	{
		ok = fseek(fp, (long)entries[index].offset * 2048, SEEK_SET) == 0 &&
		     fread(dst, 1, (size_t)sectors * 2048, fp) == (size_t)sectors * 2048;
		fclose(fp);
	}
	if (!ok && NativeDiscImage_FindFile("BIGFILE.BIG", &file))
		ok = NativeDiscImage_ReadDataSectors(&file, (u32)entries[index].offset, (u32)sectors, dst);
	if (!ok)
		return NULL;
	if (!isDram)
	{
		*outSize = size;
		return dst;
	}
	{
		int mapOff = *(const int *)dst, numBytes, i;
		unsigned char *body = dst + 4;
		int bodySize = size - 4;
		int *offs;

		if (mapOff < 0 || mapOff > bodySize - 4)
			return NULL;
		numBytes = *(const int *)(body + mapOff);
		if (numBytes < 0 || (numBytes & 3) != 0 || numBytes > bodySize - mapOff - 4)
			return NULL;
		offs = (int *)(body + mapOff + 4);
		for (i = 0; i < numBytes / 4; i++)
			if ((((unsigned)offs[i] >> 2) << 2) > (unsigned)(bodySize - 4))
				return NULL;
		LOAD_RunPtrMap((char *)body, offs, numBytes / 4);
		*outSize = bodySize;
		return body;
	}
}

// The client's harvest (ap/ap_box_texture.c), pasted verbatim between the
// markers below so the editor shows exactly what the client builds. Shims: the
// editor's own BIGFILE reader, its log, the atlas it starts from, and the face
// variant as a runtime option (--editor-apbox-face) instead of a build macro.
static int s_edApBoxFace; // AP_BOX_FACE_*; --editor-apbox-face jurnth|wumpa|plain
#define AP_RetailAsset_ReadSubfile Editor_ApBoxReadSubfile
#define AP_BOX_FACE_BASE s_edApBoxFace
#define s_apBoxTextureAtlas (s_edApBoxHarvestBase)
static const unsigned char *s_edApBoxHarvestBase;
static void Editor_ApBoxLogLine(const char *m)
{
	char line[200];
	size_t n;
	strncpy(line, m, sizeof(line) - 1);
	line[sizeof(line) - 1] = 0;
	n = strlen(line);
	if (n > 0 && line[n - 1] == '\n')
		line[n - 1] = 0;
	Editor_Log("APBOX %s", line);
}
#define AP_LogLine Editor_ApBoxLogLine

// ---- BEGIN verbatim from ap/ap_box_texture.c ----
#define AP_BOX_EDGE_SRC_VRM (BI_ARCADETRACKS + 0 * 8 + 0)
#define AP_BOX_EDGE_SRC_LEV (BI_ARCADETRACKS + 0 * 8 + 1)
// The Naughty Dog intro crate (variant AP_BOX_FACE_PLAIN): its texture file
// and level file, and the model whose four 64x32 pieces make a plain 128x64
// side panel. Retail NTSC-U: entries 513/514, model ndi_box_box_03, id 187.
#define AP_BOX_PLAIN_SRC_VRM (BI_NDBOX + 0)
#define AP_BOX_PLAIN_SRC_LEV (BI_NDBOX + 1)
#define AP_BOX_PLAIN_MODEL_ID 187
// Sector-aligned maxima: 1p LEV at most 780008 bytes across all 18 tracks,
// every 1p VRM 458808, the intro level 671140. Temporary: freed right away.
#define AP_BOX_EDGE_LEV_BUF 780288
#define AP_BOX_EDGE_VRM_BUF 460800
#define AP_BOX_EDGE_HEADER_STRIDE 0x40 // sizeof(struct ModelHeader), asserted in RenderBucket
#define AP_BOX_EDGE_MAX_MODELS 512
#define AP_BOX_EDGE_MAX_CMDS 4096

#ifndef AP_BOX_FACE_BASE
#define AP_BOX_FACE_BASE AP_BOX_FACE_WUMPA
#endif

enum
{
	AP_BOX_RECT_WOOD,  // the only 16x16 rect
	AP_BOX_RECT_FACE,  // the only 64x64 rect
	AP_BOX_RECT_PANEL, // the union of the 64x32 pieces
};

static int s_apBoxEdgeState; // 0 = untried, 1 = disc tile in the atlas, 2 = built-in border
static int s_apBoxFaceState; // 1 = disc crate face behind the logo, 2 = JurnthReinal's face as drawn
static u8  s_apBoxAtlasLive[AP_BOX_TEXTURE_ATLAS_W * AP_BOX_TEXTURE_ATLAS_H * 4];

// Find a rect one model samples, in a fixed-up level body. Every pointer is
// checked against the buffer before it is followed.
static int AP_BoxEdge_FindRect(const u8 *body, int size, int modelId, int kind, AP_BoxEdgeRect *out)
{
	const struct Level *lev = (const struct Level *)body;
	int found = 0;
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
		if (m->id != modelId)
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
				if (kind == AP_BOX_RECT_WOOD && AP_BoxEdge_IsWoodRect(&r))
				{
					*out = r;
					return 1;
				}
				if (kind == AP_BOX_RECT_FACE && AP_BoxEdge_IsFaceRect(&r))
				{
					*out = r;
					return 1;
				}
				if (kind == AP_BOX_RECT_PANEL && r.w >= 64 && r.w <= 65 && r.h >= 32 && r.h <= 33 &&
				    AP_BoxEdge_RectUnion(out, &r, !found))
					found = 1;
			}
		}
		break;
	}
	if (kind == AP_BOX_RECT_PANEL && found && out->w >= 128 && out->h >= 64)
	{
		// The panel is 128x64; its middle 64x64 is planks and the cross plank.
		out->minU += (out->w - 64) / 2;
		out->minV += (out->h - 64) / 2;
		out->w = out->h = 64;
		return 1;
	}
	return 0;
}

// Read one level/texture pair and decode up to two rects from it. Returns the
// number decoded (all or nothing per rect, in order); *why names the step that
// stopped it.
static int AP_BoxEdge_ReadPair(int levEntry, int vrmEntry, int levBuf, int nRects, const int *modelIds,
                               const int *kinds, u8 *const *dst, const int *dstW, const char **why)
{
	AP_BoxEdgeRect rect[2];
	u8 *buf, *body;
	int size = 0, found = 0, done = 0, i;

	*why = "level read";
	buf = (u8 *)malloc((size_t)levBuf);
	if (buf == 0)
		return 0;
	body = AP_RetailAsset_ReadSubfile(levEntry, 1, buf, levBuf, &size);
	if (body != 0)
	{
		*why = "crate layout";
		for (found = 0; found < nRects; found++)
			if (!AP_BoxEdge_FindRect(body, size, modelIds[found], kinds[found], &rect[found]))
				break;
	}
	free(buf);
	if (found == 0)
		return 0;

	*why = "texture read";
	buf = (u8 *)malloc(AP_BOX_EDGE_VRM_BUF);
	if (buf == 0)
		return 0;
	body = AP_RetailAsset_ReadSubfile(vrmEntry, 0, buf, AP_BOX_EDGE_VRM_BUF, &size);
	if (body != 0)
	{
		*why = "texel decode";
		for (i = 0; i < found; i++, done++)
			if (!AP_BoxEdge_Decode4bpp(body, size, &rect[i], dst[i], dstW[i]))
				break;
	}
	free(buf);
	return done;
}

// The one-time read. Leaves s_apBoxAtlasLive holding the compiled atlas with,
// on success, the recoloured disc wood in its wood rect and (variants 1, 2) the
// recoloured crate face behind the logo in its face rect. Sticky either way.
static void AP_BoxEdge_Harvest(void)
{
	static u8 wood[16 * 16 * 4];
	static u8 crate[64 * 64 * 4];
	const char *why = "";
	char msg[160];
	int got, y;

	memcpy(s_apBoxAtlasLive, s_apBoxTextureAtlas, sizeof s_apBoxAtlasLive);
	s_apBoxEdgeState = 2;
	s_apBoxFaceState = 2;

	{
		int ids[2] = {PU_RANDOM_CRATE, PU_FRUIT_CRATE};
		int kinds[2] = {AP_BOX_RECT_WOOD, AP_BOX_RECT_FACE};
		u8 *dst[2] = {wood, crate};
		int w[2] = {16, 64};
		int want = AP_BOX_FACE_BASE == AP_BOX_FACE_WUMPA ? 2 : 1;

		got = AP_BoxEdge_ReadPair(AP_BOX_EDGE_SRC_LEV, AP_BOX_EDGE_SRC_VRM, AP_BOX_EDGE_LEV_BUF, want, ids, kinds,
		                          dst, w, &why);
		if (got >= 1)
		{
			AP_BoxEdge_Recolour(wood, 16, 16, 16);
			for (y = 0; y < AP_BOX_TEXTURE_WOOD_H; y++)
				memcpy(&s_apBoxAtlasLive[((AP_BOX_TEXTURE_WOOD_Y + y) * AP_BOX_TEXTURE_ATLAS_W +
				                          AP_BOX_TEXTURE_WOOD_X) * 4],
				       &wood[y * 16 * 4], AP_BOX_TEXTURE_WOOD_W * 4);
			s_apBoxEdgeState = 1;
			AP_LogLine("[AP BOX] wood border read from the disc and recoloured\n");
		}
		else
		{
			snprintf(msg, sizeof msg, "[AP BOX] wood border from the disc unavailable (%s); using the built-in border\n",
			         why);
			AP_LogLine(msg);
		}
		if (AP_BOX_FACE_BASE == AP_BOX_FACE_WUMPA && got == 2)
			s_apBoxFaceState = 1;
	}

	if (AP_BOX_FACE_BASE == AP_BOX_FACE_PLAIN)
	{
		int ids[1] = {AP_BOX_PLAIN_MODEL_ID};
		int kinds[1] = {AP_BOX_RECT_PANEL};
		u8 *dst[1] = {crate};
		int w[1] = {64};

		// The intro level is smaller than the 1p level buffer.
		if (AP_BoxEdge_ReadPair(AP_BOX_PLAIN_SRC_LEV, AP_BOX_PLAIN_SRC_VRM, AP_BOX_EDGE_LEV_BUF, 1, ids, kinds, dst,
		                        w, &why) == 1)
			s_apBoxFaceState = 1;
	}

	if (AP_BOX_FACE_BASE != AP_BOX_FACE_JURNTH)
	{
		if (s_apBoxFaceState == 1)
		{
			AP_BoxEdge_Recolour(crate, 64, 64, 64);
			AP_BoxEdge_ComposeLogo(&s_apBoxAtlasLive[(AP_BOX_TEXTURE_FACE_Y * AP_BOX_TEXTURE_ATLAS_W +
			                                          AP_BOX_TEXTURE_FACE_X) * 4],
			                       AP_BOX_TEXTURE_ATLAS_W, crate, 64, s_apBoxLogoMask);
			AP_LogLine(AP_BOX_FACE_BASE == AP_BOX_FACE_WUMPA
			               ? "[AP BOX] face: Wumpa crate from the disc, recoloured, Archipelago logo on top\n"
			               : "[AP BOX] face: plain crate panel from the disc, recoloured, Archipelago logo on top\n");
		}
		else
		{
			snprintf(msg, sizeof msg, "[AP BOX] crate face from the disc unavailable (%s); using the built-in face\n",
			         why);
			AP_LogLine(msg);
		}
	}
}

// ---- END verbatim from ap/ap_box_texture.c ----
#undef s_apBoxTextureAtlas

// Returns the atlas to upload: `base` with the disc wood (and, for the Wumpa or
// plain face variants, the recoloured crate face behind the logo) written in.
static const unsigned char *Editor_ApBoxWithDiscEdge(const unsigned char *base)
{
	if (s_edApBoxEdgeBuiltin)
	{
		Editor_Log("APBOX edge=builtin (forced)");
		return base;
	}
	s_edApBoxHarvestBase = base;
	AP_BoxEdge_Harvest();
	{
		// Research aid: the finished atlas as raw RGBA.
		const char *dump = getenv("CTR_EDITOR_APBOX_ATLAS_DUMP");
		FILE *f = dump != NULL ? fopen(dump, "wb") : NULL;
		if (f != NULL)
		{
			fwrite(s_apBoxAtlasLive, 1, sizeof(s_apBoxAtlasLive), f);
			fclose(f);
		}
	}
	return s_apBoxAtlasLive;
}

// ap/ap_box_texture.c AP_BoxTexture_EnsureFace + ap/ap_box_model.c
// AP_BoxModel_SetTextureLayouts / AP_BoxModel_ApplyTexture, in one step.
static void Editor_ApBoxApplyTexture(void)
{
	static const int roles[6][3][2] = {
		{{1,1},{0,1},{0,0}},
		{{1,1},{0,0},{1,0}},
		{{0,0},{1,0},{1,1}},
		{{0,0},{1,1},{0,1}},
		{{1,1},{0,1},{0,0}},
		{{1,1},{0,0},{1,0}},
	};
	struct TextureLayout face;
	unsigned tex;
	u8 lu, tv, ru, bv;
	int i;

	tex = (unsigned)NativeRenderer_CreateRGBATexture(
	    AP_BOX_TEXTURE_ATLAS_W, AP_BOX_TEXTURE_ATLAS_H,
	    (u8 *)(s_edApBoxFramed ? Editor_ApBoxWithDiscEdge(Editor_ApBoxAtlas()) : Editor_ApBoxAtlas()));
	if (tex == 0)
	{
		Editor_Log("APBOX atlas_upload_failed; drawing the plain cube");
		s_edApBoxTextured = 2;
		return;
	}
	NativeGpu_SetSideloadTexture(tex, AP_BOX_TEXTURE_ATLAS_W, AP_BOX_TEXTURE_ATLAS_H);

	memset(&face, 0, sizeof(face));
	face.u0 = (u8)(AP_BOX_TEXTURE_FACE_X);
	face.v0 = (u8)(AP_BOX_TEXTURE_FACE_Y);
	face.u1 = (u8)(AP_BOX_TEXTURE_FACE_X);
	face.v1 = (u8)(AP_BOX_TEXTURE_FACE_Y + AP_BOX_TEXTURE_FACE_H - 2);
	face.u2 = (u8)(AP_BOX_TEXTURE_FACE_X + AP_BOX_TEXTURE_FACE_W - 2);
	face.v2 = (u8)(AP_BOX_TEXTURE_FACE_Y);
	face.u3 = face.u2;
	face.v3 = face.v2;
	face.clut = (u16)EDITOR_APBOX_FACE_CLUT;
	face.tpage = (u16)(EDITOR_APBOX_FACE_TPAGE | AP_TPAGE_SIDELOAD_BIT);

	lu = face.u0; tv = face.v0; ru = face.u2; bv = face.v1;
	for (i = 0; i < 6; i++)
	{
		struct TextureLayout *l = &s_edApBoxLayoutSet[i];
		*l = face;
		l->u0 = roles[i][0][0] ? ru : lu;
		l->v0 = roles[i][0][1] ? bv : tv;
		l->u1 = roles[i][1][0] ? ru : lu;
		l->v1 = roles[i][1][1] ? bv : tv;
		l->u2 = roles[i][2][0] ? ru : lu;
		l->v2 = roles[i][2][1] ? bv : tv;
		l->u3 = l->u2;
		l->v3 = l->v2;
	}
	s_edApBoxHeader.ptrTexLayout = s_edApBoxLayouts;
	s_edApBoxHeader.ptrCommandList = (u32)(uintptr_t)s_apBoxModelCommandsTex;
	s_edApBoxHeader.ptrColors = (u32 *)(uintptr_t)s_apBoxModelColorsTex;
	if (s_edApBoxFramed)
	{
		s_edApBoxFramedFrame.frame = s_edApBoxFrame.frame;
		for (i = 0; i < AP_BOX_FRAMED_NUM_VERTS * 3; i++)
			s_edApBoxFramedFrame.verts[i] = s_apBoxFramedVerts[i];
		for (i = 0; i < AP_BOX_FRAMED_NUM_TRIS; i++)
			s_edApBoxFramedLayoutPtrs[i] = &s_apBoxFramedLayouts[i];
		s_edApBoxHeader.ptrFrameData = &s_edApBoxFramedFrame.frame;
		s_edApBoxHeader.ptrTexLayout = s_edApBoxFramedLayoutPtrs;
		s_edApBoxHeader.ptrCommandList = (u32)(uintptr_t)s_apBoxFramedCommands;
		s_edApBoxHeader.ptrColors = (u32 *)(uintptr_t)s_apBoxFramedColors;
	}
	s_edApBoxTextured = 1;
	Editor_Log("APBOX textured atlas=%s model=%s", s_edApBoxAtlasPath[0] ? s_edApBoxAtlasPath : "compiled",
	           s_edApBoxFramed ? "framed" : "cube");
}

// ap/ap_box_model.c AP_BoxModel_Build.
static struct Model *Editor_ApBoxModel(struct GameTracker *gGT)
{
	int i;
	s16 scale;

	if (!s_edApBoxBuilt)
	{
		s_edApBoxFrame.frame.pos.x = -128;
		s_edApBoxFrame.frame.pos.y = -128;
		s_edApBoxFrame.frame.pos.z = -128;
		s_edApBoxFrame.frame.vertexOffset = 0x1c;
		for (i = 0; i < AP_BOX_MODEL_NUM_VERTS * 3; i++)
			s_edApBoxFrame.verts[i] = s_apBoxModelVerts[i];

		strcpy(s_edApBoxHeader.name, "apbox");
		s_edApBoxHeader.maxDistanceLOD = 0x7fff;
		s_edApBoxHeader.ptrCommandList = (u32)(uintptr_t)s_apBoxModelCommands;
		s_edApBoxHeader.ptrTexLayout = 0;
		s_edApBoxHeader.ptrFrameData = &s_edApBoxFrame.frame;
		s_edApBoxHeader.ptrColors = (u32 *)(uintptr_t)s_apBoxModelColors;

		strcpy(s_edApBox.name, "apbox");
		s_edApBox.id = PU_RANDOM_CRATE;
		s_edApBox.numHeaders = 1;
		s_edApBox.headers = &s_edApBoxHeader;
		s_edApBoxBuilt = 1;
	}
	scale = Editor_ApBoxScale(gGT);
	s_edApBoxHeader.scale.x = scale;
	s_edApBoxHeader.scale.y = scale;
	s_edApBoxHeader.scale.z = scale;
	if (s_edApBoxTextured == 0)
		Editor_ApBoxApplyTexture();
	return &s_edApBox;
}
