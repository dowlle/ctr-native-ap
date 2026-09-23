// AP-owned fallback model for tracks without a resident weapon crate.

#ifdef CTR_AP

#include <common.h>

#include "ap_box_model.h"
#include "ap_box_model_data.h"
#include "ap_box_offset_logic.h" // the freestanding mesh measurement + the lift
#include "ap_hooks.h"
#include "ap_box_texture.h"
#include "ap_box_model_framed_data.h" // the retail-crate-style box (face square + wood border ring)
#include "ap_box_colour_logic.h"       // the colour slots of the box atlas

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
// The textured box is built like the retail "?" crate: every side a face
// square plus a border ring carrying the 16x16 wood rect, from generated
// AP-owned data (tools/apbox-texture/gen_framed_model.py). Same 32..224 extent
// as the plain cube, so size and spawn lift are unchanged. The wood rect is
// filled at runtime from the player's disc (ap/ap_box_texture.c).
struct ApBoxFramedFrame
{
	struct ModelFrame frame;
	u8 verts[AP_BOX_FRAMED_NUM_VERTS * 3];
};
static struct ApBoxFramedFrame s_apBoxFramedFrame;
static struct TextureLayout *s_apBoxFramedLayoutPtrs[AP_BOX_FRAMED_NUM_TRIS];

// The same box in the four Archipelago item colours (ap_box_colour_logic.h):
// its own header and layouts, shifted to the colour's atlas slot, sharing the
// pink box's frame, command list and shading. Index = colour - 1.
#define AP_BOX_TINTS (AP_BOX_COLOUR_COUNT - 1)
static struct Model s_apBoxTintModel[AP_BOX_TINTS];
static struct ModelHeader s_apBoxTintHeader[AP_BOX_TINTS];
static struct TextureLayout s_apBoxTintLayouts[AP_BOX_TINTS][AP_BOX_FRAMED_NUM_TRIS];
static struct TextureLayout *s_apBoxTintLayoutPtrs[AP_BOX_TINTS][AP_BOX_FRAMED_NUM_TRIS];

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
// Walks a model exactly the way the renderer consumes it: the walk itself, the
// per-axis byte bounds and every unit conversion live in ap_box_offset_logic.h
// so tools/test-box-offset.c exercises the shipped arithmetic rather than a
// copy of it (Lessons Learned §5).
//
// Fills *boundsOut with the mesh's per-axis byte range, *frameOut with the frame
// origin (whose .y offsets the VERTICAL vertex byte -- see the axis-pairing note
// in the logic header) and *headerOut with the live header. 0 on anything this
// walk cannot make sense of, which is the "I could not measure it" answer every
// caller below is written to survive.
static int AP_BoxModel_MeasureMesh(struct Model *m, AP_BoxMeshBounds *boundsOut,
                                   struct ModelFrame **frameOut, struct ModelHeader **headerOut)
{
	struct ModelHeader *h;
	struct ModelFrame *frame;
	const u32 *cmd;
	int n;

	if (m == 0 || m->headers == 0 || m->numHeaders <= 0)
		return 0;
	h = &m->headers[0];
	frame = h->ptrFrameData;
	cmd = (const u32 *)(uintptr_t)h->ptrCommandList;
	if (frame == 0 || cmd == 0 || h->scale.x <= 0)
		return 0;

	n = AP_BoxMesh_CountVerts((const unsigned int *)cmd);
	if (n == 0)
		return 0;
	if (!AP_BoxMesh_Bounds((const unsigned char *)frame + frame->vertexOffset, n, boundsOut))
		return 0;

	*frameOut = frame;
	*headerOut = h;
	return 1;
}

// The size-ruling measurement: max bounding extent (model units) via *extentOut
// and header scale via *scaleOut; 0 on anything unexpected.
static int AP_BoxModel_Measure(struct Model *m, int *scaleOut, int *extentOut)
{
	AP_BoxMeshBounds b;
	struct ModelFrame *frame;
	struct ModelHeader *h;
	int ext;

	if (!AP_BoxModel_MeasureMesh(m, &b, &frame, &h))
		return 0;

	ext = AP_BoxMesh_Extent(&b);
	if (ext <= 0)
		return 0;

	*scaleOut = h->scale.x;
	*extentOut = ext;
	return 1;
}

// The vertical half of the same measurement, in world units at the model's live
// header scale: the lift that puts its lowest face on the spawn anchor, how far
// its highest face sits above its origin, and its rendered height.
static int AP_BoxModel_MeasureVertical(struct Model *m, int *baseOut, int *topOut, int *heightOut)
{
	AP_BoxMeshBounds b;
	struct ModelFrame *frame;
	struct ModelHeader *h;
	int lo, hi, scale;

	if (!AP_BoxModel_MeasureMesh(m, &b, &frame, &h))
		return 0;

	scale = h->scale.y; // .y is the component the GTE applies to the vertical
	lo = b.lo[AP_BOX_VERT_AXIS_UP];
	hi = b.hi[AP_BOX_VERT_AXIS_UP];

	*baseOut = AP_BoxOffset_BaseY(frame->pos.y, lo, scale);
	*topOut = AP_BoxOffset_ModelToWorld(frame->pos.y + hi, scale);
	*heightOut = AP_BoxOffset_Height(lo, hi, scale);
	return 1;
}

// The exact item-crate-sized header scale for the AP cube, derived from the
// retail crate the level carries (item crate preferred; the time crate
// measures identically). One probe log line per source model per process.
static s16 AP_BoxModel_DeriveScale(struct GameTracker *gGT)
{
	static int loggedItem, loggedTime;
	struct Model *src;
	int scale, extent, isItem = 0;

	if (gGT == 0)
		return AP_BOX_FALLBACK_SCALE;

	src = gGT->modelPtr[PU_RANDOM_CRATE];
	if (src != 0 && src != &s_apBoxModel && AP_BoxModel_Measure(src, &scale, &extent))
		isItem = 1;
	else
	{
		src = gGT->modelPtr[STATIC_TIME_CRATE_01];
		if (src == 0 || !AP_BoxModel_Measure(src, &scale, &extent))
			return AP_BOX_FALLBACK_SCALE;
	}

	// The probe line carries the SPAWN-HEIGHT evidence as well as the size
	// evidence, because the two are measured off the same walk and a support log
	// has to be able to answer "is the AP crate standing where a retail crate
	// would" without a second session (Lane C acceptance gate: a diagnostic proves
	// the derived AP and retail bounds and the selected offset).
	if ((isItem && !loggedItem) || (!isItem && !loggedTime))
	{
		char msg[240];
		int base = 0, top = 0, height = 0;

		if (isItem) loggedItem = 1; else loggedTime = 1;
		if (!AP_BoxModel_MeasureVertical(src, &base, &top, &height))
			base = top = height = 0;
		snprintf(msg, sizeof msg,
		         "[AP BOX] size source %s crate: scale 0x%x, extent %d -> box scale 0x%x; its own "
		         "local Y is [%d, %d] world units about its origin, height %d, base lift %d\n",
		         isItem ? "item" : "time", (unsigned)scale, extent,
		         (unsigned)AP_BoxOffset_DeriveScale(scale, extent, AP_BOX_MODEL_EXTENT),
		         -base, top, height, base);
		AP_LogLine(msg);
	}

	return (s16)AP_BoxOffset_DeriveScale(scale, extent, AP_BOX_MODEL_EXTENT);
}

// Switch the built model to the framed, textured box: its own frame data, one
// layout per triangle addressing the face or the wood rect of the atlas, its
// command list and its per-side shade table. Then build the four coloured
// copies. Until this runs (or if the atlas upload fails) the untextured
// fallback cube stays.
static void AP_BoxModel_ApplyTexture(void)
{
	int i, t;

	s_apBoxFramedFrame.frame = s_apBoxFrame.frame;
	for (i = 0; i < AP_BOX_FRAMED_NUM_VERTS * 3; i++)
		s_apBoxFramedFrame.verts[i] = s_apBoxFramedVerts[i];
	for (i = 0; i < AP_BOX_FRAMED_NUM_TRIS; i++)
		s_apBoxFramedLayoutPtrs[i] = &s_apBoxFramedLayouts[i];
	s_apBoxHeader.ptrFrameData = &s_apBoxFramedFrame.frame;
	s_apBoxHeader.ptrTexLayout = s_apBoxFramedLayoutPtrs;
	s_apBoxHeader.ptrCommandList = (u32)(uintptr_t)s_apBoxFramedCommands;
	s_apBoxHeader.ptrColors = (u32 *)(uintptr_t)s_apBoxFramedColors;

	for (t = 0; t < AP_BOX_TINTS; t++)
	{
		int ox, oy;

		AP_BoxColour_SlotOrigin(t + 1, &ox, &oy);
		for (i = 0; i < AP_BOX_FRAMED_NUM_TRIS; i++)
		{
			struct TextureLayout *l = &s_apBoxTintLayouts[t][i];

			*l = s_apBoxFramedLayouts[i];
			l->u0 = (u8)(l->u0 + ox); l->v0 = (u8)(l->v0 + oy);
			l->u1 = (u8)(l->u1 + ox); l->v1 = (u8)(l->v1 + oy);
			l->u2 = (u8)(l->u2 + ox); l->v2 = (u8)(l->v2 + oy);
			l->u3 = (u8)(l->u3 + ox); l->v3 = (u8)(l->v3 + oy);
			s_apBoxTintLayoutPtrs[t][i] = l;
		}
		s_apBoxTintHeader[t] = s_apBoxHeader;
		s_apBoxTintHeader[t].ptrTexLayout = s_apBoxTintLayoutPtrs[t];
		s_apBoxTintModel[t] = s_apBoxModel;
		s_apBoxTintModel[t].headers = &s_apBoxTintHeader[t];
	}
	s_apBoxTextured = 1;
}

// One header scale for every colour: the size ruling below applies to all.
static void AP_BoxModel_SetScale(s16 scale)
{
	int t;

	s_apBoxHeader.scale.x = s_apBoxHeader.scale.y = s_apBoxHeader.scale.z = scale;
	for (t = 0; t < AP_BOX_TINTS; t++)
		s_apBoxTintHeader[t].scale = s_apBoxHeader.scale;
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
	AP_BoxModel_SetScale(scale);
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

	// One line so a size report from the field names the number it argues about,
	// and the crate's own measured bounds so a height report names them too.
	{
		char msg[192];
		int base = 0, top = 0, height = 0;

		if (!AP_BoxModel_MeasureVertical(&s_apBoxModel, &base, &top, &height))
			base = top = height = 0;
		snprintf(msg, sizeof msg,
		         "[AP BOX] model built, scale 0x%x, local Y [%d, %d] world units about its origin, "
		         "height %d, spawn lift +%d\n",
		         (unsigned)scale, -base, top, height, base);
		AP_LogLine(msg);
	}
}

int AP_BoxModel_Ensure(struct GameTracker *gGT)
{
	if (gGT == 0)
		return -1;
	if (gGT->modelPtr[PU_RANDOM_CRATE] != 0)
		return PU_RANDOM_CRATE;

	if (!s_apBoxBuilt)
		AP_BoxModel_Build(gGT);
	if (!s_apBoxTextured && AP_BoxTexture_Ensure())
		AP_BoxModel_ApplyTexture();

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
	if (gGT == 0)
		return 0;
	if (!s_apBoxBuilt)
		AP_BoxModel_Build(gGT);
	// Re-derive per call: the one-time build may have run on a LEV without a
	// crate model (observed live: a session stuck on the fallback anchor for
	// its whole run), and the size ruling is exact EVERYWHERE. The walk is a
	// few dozen words; spawn attempts are per-rebuild, not per-frame.
	AP_BoxModel_SetScale(AP_BoxModel_DeriveScale(gGT));
	if (!s_apBoxTextured && AP_BoxTexture_Ensure())
		AP_BoxModel_ApplyTexture();
	return &s_apBoxModel;
}

struct Model *AP_BoxModel_ForColour(struct Model *owned, int colour)
{
	if (owned != &s_apBoxModel || !s_apBoxTextured || colour <= AP_BOX_COLOUR_PINK ||
	    colour >= AP_BOX_COLOUR_COUNT)
		return owned;
	return &s_apBoxTintModel[colour - 1];
}

// ── the shared spawn transform ──────────────────────────────────────────────

int AP_BoxModel_BaseOffsetY(struct Model *model)
{
	static int warned;
	int base = 0, top = 0, height = 0;

	if (model == 0)
		return 0;

	if (AP_BoxModel_MeasureVertical(model, &base, &top, &height))
		return base;

	// FAIL CLOSED to the authored anchor: an unmeasurable model spawns exactly
	// where it did before this correction existed, which is a known state rather
	// than a guessed lift. Once per process, because this can only be reached
	// from a model whose command list or frame data this walk does not
	// understand, and that is worth exactly one line, not one per spawn.
	if (!warned)
	{
		warned = 1;
		AP_LogLine("[AP BOX] WARNING: a crate model could not be measured; its spawns fall back to "
		           "the authored anchor with no height correction\n");
	}
	return 0;
}

void AP_BoxModel_SpawnPos(struct Model *model, int x, int y, int z, Vec3 *out)
{
	if (out == 0)
		return;

	out->x = x;
	out->y = y + AP_BoxModel_BaseOffsetY(model);
	out->z = z;
}

int AP_BoxModel_EnsureRelic(struct GameTracker *gGT)
{
	if (gGT == 0)
		return -1;

	// Stand first, decorate second. The texture upload can remain pending or fail
	// forever without hiding a location or leaving invisible collision behind.
	AP_BoxModel_EnsureOwned(gGT);
	if (!s_apBoxTextured && AP_BoxTexture_Ensure())
	{
		AP_BoxModel_ApplyTexture();
		AP_LogLine("[AP BOX] relic race uses the AP-owned crate model (textured)\n");
	}
	return PU_RANDOM_CRATE;
}

#endif
