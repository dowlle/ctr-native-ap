#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003ea6c-0x8003eaac
int MixRNG_Particles(int param_1)
{
	u32 uVar1;

	uVar1 = RngDeadCoed((u32 *)&sdata->gGT->deadcoed_struct);
	return (int)((uVar1 & 0xffff) * param_1) >> 0x10;
}
