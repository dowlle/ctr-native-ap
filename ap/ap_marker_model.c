// The Archipelago-logo marker model for foreign multiworld items (#124).
//
// Compiled in as static data rather than loaded from an asset file: the native
// target is 32-bit (CMakeLists.txt:16-20), so a plain C aggregate produces a
// byte-identical struct Model that the render path can consume directly. No
// file IO, no pointer-map fixup, no shipped asset.
//
// The geometry comes from MMRecompRando's Archipelago-logo mesh (GPL-3.0),
// converted by tools/aplogo/convert.py; the logo itself is (c) 2022 Krista
// Corkos and Christopher Wilson, CC BY-NC 4.0. See tools/aplogo/README.md for
// the attribution still owed in THIRD_PARTY_NOTICES.

#ifdef CTR_AP

#include <common.h>

#include "ap_marker_model.h"
#include "ap_marker_model_data.h"

// The vertex bytes must sit immediately after the frame header, because the
// engine finds them at (char *)frame + frame->vertexOffset
// (namespace_Instance.h:301, read at RenderBucket_QueueExecute.c:5283).
struct ApMarkerFrame
{
	struct ModelFrame frame;
	u8                verts[AP_MARKER_NUM_VERTS * 3];
};

CTR_STATIC_ASSERT(sizeof(struct ModelFrame) == 0x1c);
CTR_STATIC_ASSERT(offsetof(struct ApMarkerFrame, verts) == 0x1c);

// Not const: the engine's pointer fields are non-const and the frame origin is
// written once at registration.
static struct ApMarkerFrame s_apMarkerFrame;
static struct ModelHeader   s_apMarkerHeader;
static struct Model         s_apMarkerModel;
static int                  s_apMarkerBuilt;

// Fallback model scale, used only if STATIC_GEM is not loaded when we register.
// Vanilla model scales live in BIGFILE data, not in this repo, so this number is
// a guess; the gem-derived path below avoids relying on it.
#define AP_MARKER_FALLBACK_SCALE 0x1000

static void AP_MarkerModel_Build(struct GameTracker *gGT)
{
	int i;

	// Vertices are byte coordinates in a 0..255 cube, so the model would hang
	// off the +octant of the instance origin. The frame origin recentres it.
	// Pairing (verified at RenderBucket_QueueExecute.c:2237-2244, :2264):
	// pos.x offsets vertex byte0 (horizontal), pos.y offsets byte2 (vertical),
	// pos.z offsets byte1 (depth).
	s_apMarkerFrame.frame.pos.x = -128;
	s_apMarkerFrame.frame.pos.y = -128;
	s_apMarkerFrame.frame.pos.z = -128;
	s_apMarkerFrame.frame.vertexOffset = 0x1c;
	for (i = 0; i < AP_MARKER_NUM_VERTS * 3; i++)
		s_apMarkerFrame.verts[i] = s_apMarkerVerts[i];

	s_apMarkerHeader.name[0] = 'a';
	s_apMarkerHeader.name[1] = 'p';
	s_apMarkerHeader.name[2] = '\0';
	// Win the LOD walk at every distance: the walk picks this header while
	// projectedDistance < (u16)maxDistanceLOD (QueueExecute.c:1283-1287), and
	// running out of headers means the model is not drawn at all (:1298-1300).
	s_apMarkerHeader.maxDistanceLOD = 0x7fff;
	s_apMarkerHeader.flags = 0;
	s_apMarkerHeader.ptrCommandList = (u32)(uintptr_t)s_apMarkerCommands;
	s_apMarkerHeader.ptrFrameData = &s_apMarkerFrame.frame;
	// NULL is legal only because every command's texture index is 0; a non-zero
	// index with no layout table would invalidate the prim (:2784-2805).
	s_apMarkerHeader.ptrTexLayout = 0;
	s_apMarkerHeader.ptrColors = (u32 *)(uintptr_t)s_apMarkerColors;
	// Both must stay zero: unk3 doubles as ptrDeltaArray and would send the
	// decoder down the bit-stream delta path instead of reading plain bytes
	// (:2398-2417); a non-null ptrAnimations would deselect ptrFrameData (:1956).
	s_apMarkerHeader.unk3 = 0;
	s_apMarkerHeader.numAnimations = 0;
	s_apMarkerHeader.ptrAnimations = 0;
	s_apMarkerHeader.animtex = 0;

	// Size the marker off the gem it replaces, so it lands in the same visual
	// ballpark without guessing at vanilla scale units. Vanilla model scales are
	// not in this repo (they live in BIGFILE data), so deriving beats inventing.
	{
		struct Model *gem = gGT->modelPtr[STATIC_GEM];
		s16           s = AP_MARKER_FALLBACK_SCALE;

		if (gem != 0 && gem->headers != 0 && gem->numHeaders > 0)
			s = gem->headers[0].scale.x;

		s_apMarkerHeader.scale.x = s;
		s_apMarkerHeader.scale.y = s;
		s_apMarkerHeader.scale.z = s;
	}

	s_apMarkerModel.name[0] = 'a';
	s_apMarkerModel.name[1] = 'p';
	s_apMarkerModel.name[2] = '\0';
	s_apMarkerModel.id = STATIC_AP;
	s_apMarkerModel.numHeaders = 1;
	s_apMarkerModel.headers = &s_apMarkerHeader;

	s_apMarkerBuilt = 1;
}

void AP_MarkerModel_Register(struct GameTracker *gGT)
{
	if (gGT == 0)
		return;

	// Build once the gem is available, so the derived scale is real rather than
	// the fallback. Before that the slot simply stays empty and the glow keeps
	// its previous marker -- the render site NULL-checks modelPtr already.
	if (!s_apMarkerBuilt)
	{
		if (gGT->modelPtr[STATIC_GEM] == 0)
			return;
		AP_MarkerModel_Build(gGT);
	}

	gGT->modelPtr[STATIC_AP] = &s_apMarkerModel;
}

int AP_MarkerModel_IsRegistered(void)
{
	return s_apMarkerBuilt;
}

#endif // CTR_AP
