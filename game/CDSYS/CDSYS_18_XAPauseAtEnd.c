#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8001d06c-0x8001d094.
void DECOMP_CDSYS_XAPauseAtEnd()
{
	// wait until IRQ says XA is finished
	if (sdata->XA_boolFinished == 0)
		return;

	DECOMP_CDSYS_XAPauseForce();
}
