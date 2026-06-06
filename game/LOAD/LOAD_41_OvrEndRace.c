#include <common.h>

// DLL loaded = param_1 + 221
// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800334f4-0x80033570.
void LOAD_OvrEndRace(u32 param_1)
{
	struct GameTracker *gGT = sdata->gGT;

	// if new EndOfRace overlay needs to load
	if ((u32)gGT->overlayIndex_EndOfRace != param_1)
	{
#ifndef CTR_NATIVE
		// EndOfRace overlay 221-225
		LOAD_AppendQueue(0, LT_SETADDR, BI_OVERLAYSECT1 + param_1, &OVR_Region1, LOAD_Callback_Overlay_Generic);
#endif

		gGT->overlayIndex_EndOfRace = param_1;
		gGT->overlayIndex_LOD = 0xff;
	}
	return;
}
