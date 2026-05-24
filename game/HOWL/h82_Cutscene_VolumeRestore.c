#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002c1d0-0x8002c208
void DECOMP_Cutscene_VolumeRestore(void)
{
	// enter critical section
	DECOMP_Smart_EnterCriticalSection();

	// copy does not exist
	sdata->boolStoringVolume = 0;

	// Set volume of FX
	DECOMP_howl_VolumeSet(0, sdata->storedVolume);

	// exit critical section
	DECOMP_Smart_ExitCriticalSection();

	return;
}
