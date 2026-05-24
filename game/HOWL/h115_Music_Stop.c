#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002e4ec-0x8002e524
void DECOMP_Music_Stop(void)
{
	// quit if no music is playing
	if (sdata->cseqBoolPlay == 0)
		return;

	DECOMP_CseqMusic_Stop(sdata->cseqHighestIndex & 0xffff);

	sdata->cseqBoolPlay = 0;
	sdata->cseqHighestIndex = -1;
}
