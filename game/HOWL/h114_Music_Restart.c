#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002e4c0-0x8002e4ec
void DECOMP_Music_Restart(void)
{
	// if cseq music is playing
	if (sdata->cseqBoolPlay != 0)
	{
		DECOMP_CseqMusic_Restart(sdata->cseqHighestIndex, 8);
	}
}
