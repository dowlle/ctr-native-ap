#include <common.h>

// param_1 = numPlyrCurrGame {1,2,3,4}
// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80033474-0x800334f4.
void LOAD_OvrLOD(u32 param_1)
{
	// change {1-4} -> {0-3}
	param_1 -= 1;

	struct GameTracker *gGT = sdata->gGT;

	// if new LOD overlay needs to load
	if ((u32)gGT->overlayIndex_LOD != param_1)
	{
#ifndef CTR_NATIVE
		// LOD overlay 226-229
		LOAD_AppendQueue(0, LT_SETADDR, BI_OVERLAYSECT2 + param_1, &OVR_Region2, LOAD_Callback_Overlay_Generic);
#endif

		// save ID, and reload next overlay (sector read invalidation)
		gGT->overlayIndex_LOD = param_1;
		gGT->overlayIndex_Threads = 0xff;
	}
	return;
}
