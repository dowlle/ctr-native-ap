#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8006924c-0x8006925c.
int DECOMP_VehTalkMask_boolNoXA()
{
	return sdata->XA_State == 0;
}
