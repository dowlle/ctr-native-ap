#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800298e4-0x8002991c
void DECOMP_Bank_DestroyAll()
{
	struct Bank *ptrLastBank;

	while (sdata->numAudioBanks != 0)
	{
		DECOMP_Bank_DestroyLast();
	}
}
