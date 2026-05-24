#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002e994-0x8002e9c0
void DECOMP_PlayWarppadSound(u32 distance)
{
	DECOMP_CalculateVolumeFromDistance((u32 *)&sdata->SoundFadeInput[0].soundID_soundCount, 0x98, distance);
}

void PlayWarppadSound(u32 distance)
{
	DECOMP_PlayWarppadSound(distance);
}
