#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8001cfec-0x8001d06c.
void DECOMP_CDSYS_XAPauseForce()
{
	if (sdata->boolUseDisc == 0)
		return;
	if (sdata->bool_XnfLoaded == 0)
		return;
	if (sdata->XA_State == 0)
		return;

	sdata->XA_boolFinished = 0;
	sdata->XA_State = 0;

	SpuSetIRQ(0);
	DECOMP_CDSYS_SpuDisableIRQ();

	CdControl(CdlPause, 0, 0);

	sdata->XA_PauseFrame = sdata->gGT->frameTimer_MainFrame_ResetDB;
}
