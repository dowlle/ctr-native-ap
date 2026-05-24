#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002acb8-0x8002ad04
void DECOMP_UpdateChannelVol_EngineFX(struct EngineFX *engineFX, struct ChannelAttr *attr, int vol, int LR)
{
	DECOMP_Channel_SetVolume(attr, (sdata->vol_FX * engineFX->volume * vol) >> 10, LR);
}
