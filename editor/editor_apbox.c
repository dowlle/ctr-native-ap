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

#include "../ap/ap_box_model_data.h"
#include "../ap/ap_box_texture_data.h"
#include "../ap/ap_box_model_framed_data.h"
#include "../ap/ap_box_edge_logic.h"
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
static unsigned char s_edApBoxAtlasLive[EDITOR_APBOX_ATLAS_BYTES];
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

static int Editor_ApBoxFindWood(const u8 *body, int size, AP_BoxEdgeRect *out)
{
	const struct Level *lev = (const struct Level *)body;
	u32 i;

	if (size < (int)sizeof(struct Level) || lev->numModels == 0 || lev->numModels > 512 ||
	    (const u8 *)lev->ptrModelsPtrArray < body || (const u8 *)(lev->ptrModelsPtrArray + lev->numModels) > body + size)
		return 0;
	for (i = 0; i < lev->numModels; i++)
	{
		const struct Model *m = lev->ptrModelsPtrArray[i];
		int h;

		if (m == NULL)
			break;
		if ((const u8 *)m < body || (const u8 *)m + sizeof(struct Model) > body + size)
			return 0;
		if (m->id != PU_RANDOM_CRATE)
			continue;
		if (m->numHeaders <= 0 || (const u8 *)m->headers < body ||
		    (const u8 *)m->headers + (u32)m->numHeaders * 0x40 > body + size)
			return 0;
		for (h = 0; h < m->numHeaders; h++)
		{
			const struct ModelHeader *mh = (const struct ModelHeader *)((const u8 *)m->headers + h * 0x40);
			const u32 *cmd = (const u32 *)(uintptr_t)mh->ptrCommandList;
			int n, maxCmd;

			if ((const u8 *)cmd < body || (const u8 *)cmd + 8 > body + size ||
			    (const u8 *)mh->ptrTexLayout < body || (const u8 *)mh->ptrTexLayout >= body + size)
				continue;
			maxCmd = (int)(((const u32 *)(body + size)) - cmd);
			if (maxCmd > 4096)
				maxCmd = 4096;
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

// Returns the atlas to upload: `base` with, on success, the recoloured disc
// tile in its wood rect.
static const unsigned char *Editor_ApBoxWithDiscEdge(const unsigned char *base)
{
	unsigned char *buf, *body;
	unsigned char tile[16 * 16 * 4];
	AP_BoxEdgeRect rect;
	int size = 0, ok = 0, y;
	const char *why = "level read";

	if (s_edApBoxEdgeBuiltin)
	{
		Editor_Log("APBOX edge=builtin (forced)");
		return base;
	}
	buf = (unsigned char *)malloc(780288);
	if (buf != NULL)
	{
		body = Editor_ApBoxReadSubfile(BI_ARCADETRACKS + 1, 1, buf, 780288, &size);
		why = body == NULL ? "level read" : "crate wood layout";
		ok = body != NULL && Editor_ApBoxFindWood(body, size, &rect);
		free(buf);
	}
	if (ok)
	{
		ok = 0;
		why = "texture read";
		buf = (unsigned char *)malloc(460800);
		if (buf != NULL)
		{
			body = Editor_ApBoxReadSubfile(BI_ARCADETRACKS + 0, 0, buf, 460800, &size);
			if (body != NULL)
			{
				why = "texel decode";
				ok = AP_BoxEdge_Decode4bpp(body, size, &rect, tile, 16);
			}
			free(buf);
		}
	}
	if (!ok)
	{
		Editor_Log("APBOX edge=builtin reason=%s", why);
		return base;
	}
	Editor_Log("APBOX edge=disc page=%d,%d texel=%d,%d clut=%d,%d", rect.pageX, rect.pageY, rect.minU, rect.minV,
	           rect.clutX, rect.clutY);
	AP_BoxEdge_Recolour(tile, 16, 16, 16);
	memcpy(s_edApBoxAtlasLive, base, sizeof(s_edApBoxAtlasLive));
	for (y = 0; y < AP_BOX_TEXTURE_WOOD_H; y++)
		memcpy(&s_edApBoxAtlasLive[((AP_BOX_TEXTURE_WOOD_Y + y) * AP_BOX_TEXTURE_ATLAS_W + AP_BOX_TEXTURE_WOOD_X) * 4],
		       &tile[y * 16 * 4], AP_BOX_TEXTURE_WOOD_W * 4);
	{
		// Research aid: the recoloured tile as raw RGBA next to the log.
		const char *dump = getenv("CTR_EDITOR_APBOX_EDGE_DUMP");
		FILE *f = dump != NULL ? fopen(dump, "wb") : NULL;
		if (f != NULL)
		{
			fwrite(tile, 1, sizeof(tile), f);
			fclose(f);
		}
	}
	return s_edApBoxAtlasLive;
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
