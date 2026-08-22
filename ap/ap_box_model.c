// AP-owned fallback model for tracks without a resident weapon crate.

#ifdef CTR_AP

#include <common.h>

#include "ap_box_model.h"
#include "ap_box_model_data.h"
#include "ap_hooks.h"
#include "ap_box_texture.h"

struct ApBoxModelFrame
{
	struct ModelFrame frame;
	u8 verts[AP_BOX_MODEL_NUM_VERTS * 3];
};

CTR_STATIC_ASSERT(sizeof(struct ModelFrame) == 0x1c);
CTR_STATIC_ASSERT(offsetof(struct ApBoxModelFrame, verts) == 0x1c);

static struct ApBoxModelFrame s_apBoxFrame;
static struct ModelHeader s_apBoxHeader;
static struct Model s_apBoxModel;
static struct TextureLayout s_apBoxLayoutSet[6];
static struct TextureLayout *s_apBoxLayouts[6] = {
	&s_apBoxLayoutSet[0], &s_apBoxLayoutSet[1], &s_apBoxLayoutSet[2],
	&s_apBoxLayoutSet[3], &s_apBoxLayoutSet[4], &s_apBoxLayoutSet[5],
};
static int s_apBoxBuilt;
static int s_apBoxTextured;

// RULED (2026-08-21 18:06): the AP box is the EXACT size of the retail
// "?" item crate, everywhere. Rendered size = headerScale x meshExtent, the
// AP cube's mesh spans AP_BOX_MODEL_EXTENT units, and the live size probe
// measured both retail crates at scale 0x6d3 with a full 255-unit extent
// (identical to each other), so the exact header scale for this cube is
// retailScale x retailExtent / AP_BOX_MODEL_EXTENT -- derived live from
// whichever retail crate the level carries. The fallback is that same number
// as measured (0x6d3 x 255 / 192 = 0x910), used only when a LEV carries
// neither crate model; the probe log lines make a drifted retail value
// visible if that ever happens.
#define AP_BOX_MODEL_EXTENT   192
#define AP_BOX_FALLBACK_SCALE 0x910

// ── mesh measurement ────────────────────────────────────────────────────────
// Walks a retail model exactly the way the renderer consumes it
// (RenderBucket_DrawFunc_Normal): skip the first command word, then every
// word with a nonzero high half and bit 26 clear ((cmd >> 24) & 4) consumes
// the next vertex from the frame data, until the 0xffffffff terminator.
// Returns the model's max bounding extent (model units) via *extentOut and its
// header scale via *scaleOut; 0 on anything unexpected.
static int AP_BoxModel_Measure(struct Model *m, int *scaleOut, int *extentOut)
{
	struct ModelHeader *h;
	struct ModelFrame *frame;
	const u32 *cmd;
	const u8 *verts;
	int n = 0, guard = 0, i, ext = 0;
	int mn[3], mx[3];

	if (m == 0 || m->headers == 0 || m->numHeaders <= 0)
		return 0;
	h = &m->headers[0];
	frame = h->ptrFrameData;
	cmd = (const u32 *)(uintptr_t)h->ptrCommandList;
	if (frame == 0 || cmd == 0 || h->scale.x <= 0)
		return 0;

	for (cmd++; *cmd != 0xffffffffu && guard < 8192; cmd++, guard++)
	{
		if ((*cmd >> 16) == 0)
			continue; // color-only command, no vertex
		if (((*cmd >> 24) & 4) == 0)
			n++;
	}
	if (n <= 0 || n > 2048)
		return 0;

	verts = (const u8 *)frame + frame->vertexOffset;
	for (i = 0; i < 3; i++) { mn[i] = 255; mx[i] = 0; }
	for (i = 0; i < n * 3; i++)
	{
		int a = i % 3;
		if (verts[i] < mn[a]) mn[a] = verts[i];
		if (verts[i] > mx[a]) mx[a] = verts[i];
	}
	for (i = 0; i < 3; i++)
		if (mx[i] - mn[i] > ext)
			ext = mx[i] - mn[i];
	if (ext <= 0)
		return 0;

	*scaleOut = h->scale.x;
	*extentOut = ext;
	return 1;
}

// The exact item-crate-sized header scale for the AP cube, derived from the
// retail crate the level carries (item crate preferred; the time crate
// measures identically). One probe log line per source model per process.
static s16 AP_BoxModel_DeriveScale(struct GameTracker *gGT)
{
	static int loggedItem, loggedTime;
	struct Model *m;
	int scale, extent, isItem = 0;

	if (gGT == 0)
		return AP_BOX_FALLBACK_SCALE;

	m = gGT->modelPtr[PU_RANDOM_CRATE];
	if (m != 0 && m != &s_apBoxModel && AP_BoxModel_Measure(m, &scale, &extent))
		isItem = 1;
	else if (!(gGT->modelPtr[STATIC_TIME_CRATE_01] != 0 &&
	           AP_BoxModel_Measure(gGT->modelPtr[STATIC_TIME_CRATE_01], &scale, &extent)))
		return AP_BOX_FALLBACK_SCALE;

	if ((isItem && !loggedItem) || (!isItem && !loggedTime))
	{
		char msg[144];
		if (isItem) loggedItem = 1; else loggedTime = 1;
		snprintf(msg, sizeof msg,
		         "[AP BOX] size source %s crate: scale 0x%x, extent %d -> box scale 0x%x\n",
		         isItem ? "item" : "time", (unsigned)scale, extent,
		         (unsigned)((scale * extent) / AP_BOX_MODEL_EXTENT));
		AP_LogLine(msg);
	}

	return (s16)((scale * extent) / AP_BOX_MODEL_EXTENT);
}

// The corner-role layouts the textured command list references (see the table
// in ap_box_model_data.h). Corner k of a layout is consumed by strip vertex k
// of its triangle, so each entry spells out which rect corner that vertex holds
// AS SEEN FROM OUTSIDE the cube: texture top toward world +Y (up) on the four
// side faces, and visual left/right per the engine's proper-rotation camera
// (screen right = up x outwardNormal). The incoming face layout carries the
// retail corner semantics u0/v0 = top-left, u1/v1 = bottom-left, u2/v2 =
// top-right; the rect's four corners are derived from those.
static void AP_BoxModel_SetTextureLayouts(const struct TextureLayout *face)
{
	u8 lu = face->u0, tv = face->v0; // left u, top v
	u8 ru = face->u2, bv = face->v1; // right u, bottom v
	// corner role per layout, per vertex: {u,v} triplets
	static const int roles[6][3][2] = {
		{{1,1},{0,1},{0,0}}, // 1: BR,BL,TL  A of front/back/left/right/top
		{{1,1},{0,0},{1,0}}, // 2: BR,TL,TR  B of front/back/left/right/top
		{{0,0},{1,0},{1,1}}, // 3: TL,TR,BR  A of bottom
		{{0,0},{1,1},{0,1}}, // 4: TL,BR,BL  B of bottom
		{{1,1},{0,1},{0,0}}, // 5: unused, kept = 1 so the table stays full
		{{1,1},{0,0},{1,0}}, // 6: unused, kept = 2
	};
	int i;

	for (i = 0; i < 6; i++)
	{
		struct TextureLayout *l = &s_apBoxLayoutSet[i];
		*l = *face; // tpage (with the sideload bit) and clut carry over
		l->u0 = roles[i][0][0] ? ru : lu;
		l->v0 = roles[i][0][1] ? bv : tv;
		l->u1 = roles[i][1][0] ? ru : lu;
		l->v1 = roles[i][1][1] ? bv : tv;
		l->u2 = roles[i][2][0] ? ru : lu;
		l->v2 = roles[i][2][1] ? bv : tv;
		l->u3 = l->u2;
		l->v3 = l->v2;
	}
}

// Switch the built model to the contributed face art: the six corner-role
// layouts, the textured command list, and the neutral colour table (the
// fallback's orange palette must not tint the art; see ap_box_model_data.h).
static void AP_BoxModel_ApplyTexture(const struct TextureLayout *face)
{
	AP_BoxModel_SetTextureLayouts(face);
	s_apBoxHeader.ptrTexLayout = s_apBoxLayouts;
	s_apBoxHeader.ptrCommandList = (u32)(uintptr_t)s_apBoxModelCommandsTex;
	s_apBoxHeader.ptrColors = (u32 *)(uintptr_t)s_apBoxModelColorsTex;
	s_apBoxTextured = 1;
}

static void AP_BoxModel_Build(struct GameTracker *gGT)
{
	s16 scale = AP_BoxModel_DeriveScale(gGT);
	int i;

	s_apBoxFrame.frame.pos.x = -128;
	s_apBoxFrame.frame.pos.y = -128;
	s_apBoxFrame.frame.pos.z = -128;
	s_apBoxFrame.frame.vertexOffset = 0x1c;
	for (i = 0; i < AP_BOX_MODEL_NUM_VERTS * 3; i++)
		s_apBoxFrame.verts[i] = s_apBoxModelVerts[i];

	s_apBoxHeader.name[0] = 'a';
	s_apBoxHeader.name[1] = 'p';
	s_apBoxHeader.name[2] = 'b';
	s_apBoxHeader.name[3] = 'o';
	s_apBoxHeader.name[4] = 'x';
	s_apBoxHeader.name[5] = '\0';
	s_apBoxHeader.maxDistanceLOD = 0x7fff;
	s_apBoxHeader.flags = 0;
	s_apBoxHeader.scale.x = scale;
	s_apBoxHeader.scale.y = scale;
	s_apBoxHeader.scale.z = scale;
	// Flat-shaded, untextured. This is the LAST RESORT, reached only when the
	// box atlas could not be uploaded, so it deliberately stays the plain
	// vertex-coloured cube rather than borrowing the sideload slot: a fallback
	// should look like a stand-in, not like a broken texture.
	s_apBoxHeader.ptrCommandList = (u32)(uintptr_t)s_apBoxModelCommands;
	s_apBoxHeader.ptrTexLayout = 0;
	s_apBoxHeader.ptrFrameData = &s_apBoxFrame.frame;
	s_apBoxHeader.ptrColors = (u32 *)(uintptr_t)s_apBoxModelColors;
	s_apBoxHeader.unk3 = 0;
	s_apBoxHeader.numAnimations = 0;
	s_apBoxHeader.ptrAnimations = 0;
	s_apBoxHeader.animtex = 0;

	s_apBoxModel.name[0] = 'a';
	s_apBoxModel.name[1] = 'p';
	s_apBoxModel.name[2] = 'b';
	s_apBoxModel.name[3] = 'o';
	s_apBoxModel.name[4] = 'x';
	s_apBoxModel.name[5] = '\0';
	s_apBoxModel.id = PU_RANDOM_CRATE;
	s_apBoxModel.numHeaders = 1;
	s_apBoxModel.headers = &s_apBoxHeader;
	s_apBoxBuilt = 1;

	// One line so a size report from the field names the number it argues about.
	{
		char msg[64];
		snprintf(msg, sizeof msg, "[AP BOX] model built, scale 0x%x\n", (unsigned)scale);
		AP_LogLine(msg);
	}
}

int AP_BoxModel_Ensure(struct GameTracker *gGT)
{
	struct TextureLayout face;

	if (gGT == 0)
		return -1;
	if (gGT->modelPtr[PU_RANDOM_CRATE] != 0)
		return PU_RANDOM_CRATE;

	if (!s_apBoxBuilt)
		AP_BoxModel_Build(gGT);
	if (!s_apBoxTextured && AP_BoxTexture_EnsureFace(&face))
		AP_BoxModel_ApplyTexture(&face);

	// PU_RANDOM_CRATE is a normal per-level slot and LibraryOfModels_Clear
	// clears it on every transition. Reassert only while it is absent, so a
	// retail model always wins on levels that carry one.
	gGT->modelPtr[PU_RANDOM_CRATE] = &s_apBoxModel;
	return PU_RANDOM_CRATE;
}

int AP_BoxModel_EnsureOwned(struct GameTracker *gGT)
{
	if (gGT == 0)
		return -1;

	if (!s_apBoxBuilt)
		AP_BoxModel_Build(gGT);

	// Relic LEVs never contain PU_RANDOM_CRATE. Do not treat a model harvested
	// from another LEV as resident here: its pointer graph and texture remap can
	// pass registration while still producing no visible instance. The AP cube
	// owns all of its geometry and commands and is valid for this level.
	gGT->modelPtr[PU_RANDOM_CRATE] = &s_apBoxModel;
	return PU_RANDOM_CRATE;
}

struct Model *AP_BoxModel_GetOwned(struct GameTracker *gGT)
{
	struct TextureLayout face;

	if (gGT == 0)
		return 0;
	if (!s_apBoxBuilt)
		AP_BoxModel_Build(gGT);
	// Re-derive per call: the one-time build may have run on a LEV without a
	// crate model (observed live: a session stuck on the fallback anchor for
	// its whole run), and the size ruling is exact EVERYWHERE. The walk is a
	// few dozen words; spawn attempts are per-rebuild, not per-frame.
	s_apBoxHeader.scale.x = AP_BoxModel_DeriveScale(gGT);
	s_apBoxHeader.scale.y = s_apBoxHeader.scale.x;
	s_apBoxHeader.scale.z = s_apBoxHeader.scale.x;
	if (!s_apBoxTextured && AP_BoxTexture_EnsureFace(&face))
		AP_BoxModel_ApplyTexture(&face);
	return &s_apBoxModel;
}

int AP_BoxModel_EnsureRelic(struct GameTracker *gGT)
{
	struct TextureLayout face;

	if (gGT == 0)
		return -1;

	// Stand first, decorate second. The texture upload can remain pending or fail
	// forever without hiding a location or leaving invisible collision behind.
	AP_BoxModel_EnsureOwned(gGT);
	if (!s_apBoxTextured && AP_BoxTexture_EnsureFace(&face))
	{
		AP_BoxModel_ApplyTexture(&face);
		AP_LogLine("[AP BOX] relic race uses the AP-owned crate model (textured)\n");
	}
	return PU_RANDOM_CRATE;
}

#endif
