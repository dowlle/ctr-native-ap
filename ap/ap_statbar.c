#ifdef CTR_AP

#include <common.h>

#include "ap_statbar.h"

// The Garage's seven gradient stops, red through blue.
//
// A COPY, deliberately, and the only thing here that is one. The live array is
// gGarage.barColors, initialised from OVR233_GARAGE_INITIALIZER
// (game/233/D233.c:47-56) -- overlay-233 state, and no file under game/232/
// reads D233 or gGarage anywhere in the tree. The Garage keeps passing its own
// array, so its bars are unchanged whatever happens here; this is what the hub
// passes, because the hub has nothing else to pass. Seven u32s with a cited
// source is the small half of the duplication; the drawing below, which is the
// half that would actually drift, is shared.
const unsigned int AP_STATBAR_COLORS[7] = {
    0xc80000, 0xA8700, 0xb428, 0xb4b4, 0x64dc, 0x28dc, 0xeb,
};

// Lifted verbatim from the bar block in CS_Garage_MenuProc, with gGarage.barLen
// and the function's locals turned into arguments. Same primitives, same order
// (outline, shadow, cells), same colour pairing, same OT link.
void AP_StatBar_Draw(int x, int y, int barLen, const unsigned int *colors, void *ot, struct PrimMem *primMem)
{
	RECT r;
	Color white = MakeColor(0xFF, 0xFF, 0xFF);
	Color black = MakeColor(0, 0, 0);
	int segmentIndex;

	if ((colors == NULL) || (ot == NULL) || (primMem == NULL))
		return;

	// bar outline
	r.x = (s16)x;
	r.y = (s16)y;
	r.w = (s16)barLen;
	r.h = AP_STATBAR_HEIGHT;
	CTR_Box_DrawWireBox(&r, &white, ot, primMem);

	// bar shadows
	r.x = (s16)(x + 1);
	r.y = (s16)(y + 1);
	r.w = (s16)(barLen - 2);
	r.h = AP_STATBAR_HEIGHT - 2;
	CTR_Box_DrawWireBox(&r, &black, ot, primMem);

	for (segmentIndex = 0; segmentIndex < AP_STATBAR_SEGMENTS; segmentIndex++)
	{
		const unsigned int *barColor = &colors[segmentIndex];
		int currSegmentLen = AP_StatBar_SegmentLen(barLen, segmentIndex);
		int segmentX;
		POLY_G4 *p;

		if (currSegmentLen <= 0)
			continue;

		p = primMem->cursor;

		// quit if prim mem runs out
		if (primMem->end < (void *)p)
			return;

		primMem->cursor = p + 1;

		// color data
		CtrGpu_WriteColorCode(&p->r0, barColor[0] | 0x38000000);
		CtrGpu_WriteColorCode(&p->r1, barColor[1] | 0x38000000);
		CtrGpu_WriteColorCode(&p->r2, barColor[0] | 0x38000000);
		CtrGpu_WriteColorCode(&p->r3, barColor[1] | 0x38000000);

		segmentX = x + segmentIndex * AP_STATBAR_SEGMENT_LEN;

		// top left
		p->x0 = (s16)segmentX;
		p->y0 = (s16)y;

		// top right
		p->x1 = (s16)(segmentX + currSegmentLen);
		p->y1 = (s16)y;

		// bottom left
		p->x2 = (s16)segmentX;
		p->y2 = (s16)(y + AP_STATBAR_HEIGHT);

		// bottom right
		p->x3 = (s16)(segmentX + currSegmentLen);
		p->y3 = (s16)(y + AP_STATBAR_HEIGHT);

		*(int *)p = CtrGpu_PackOTTag(*(uint32_t *)ot, 0x8000000);
		*(int *)ot = (int)CtrGpu_PrimToOTLink24(p);
	}
}

#endif // CTR_AP
