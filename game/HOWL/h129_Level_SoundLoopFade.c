#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002ea44-0x8002eab8
void DECOMP_Level_SoundLoopFade(int *fade, u32 soundID, int desiredVolume, int fadeStep)
{
	int currentVolume = fade[2];
	bool clamped;

	if (currentVolume == desiredVolume)
		return;

	fade[1] = desiredVolume;

	if (currentVolume < desiredVolume)
	{
		fade[2] = currentVolume + fadeStep;
		clamped = desiredVolume < currentVolume + fadeStep;
	}
	else
	{
		if (currentVolume <= desiredVolume)
			goto updateSound;

		fade[2] = currentVolume - fadeStep;
		clamped = currentVolume - fadeStep < desiredVolume;
	}

	if (clamped)
		fade[2] = desiredVolume;

updateSound:
	DECOMP_Level_SoundLoopSet(&fade[3], soundID, fade[2]);
}

void Level_SoundLoopFade(int *fade, u32 soundID, int desiredVolume, int fadeStep)
{
	DECOMP_Level_SoundLoopFade(fade, soundID, desiredVolume, fadeStep);
}
