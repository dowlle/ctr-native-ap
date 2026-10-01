// Crate mesh measurement. Shared by the AP crate model (ap_box_model.c includes
// this file in place) and, in the authoring client without Archipelago, by Box
// Author Mode's preview markers (game/game_unity.h), so a marker stands at the
// height the AP build gives it. The arithmetic lives in ap_box_offset_logic.h.

#if defined(CTR_AP) || defined(CTR_BOX_AUTHORING)

#include <common.h>

#include "ap_box_model.h"
#include "ap_box_offset_logic.h"

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

#ifdef CTR_AP // the size ruling is the AP crate model's alone
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
#endif

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

#endif
