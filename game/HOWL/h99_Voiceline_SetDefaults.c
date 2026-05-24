#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002d2b0-0x8002d2f4
void DECOMP_Voiceline_SetDefaults(void)
{
	sdata->unkAudioState = 0;
	sdata->desiredXA_RaceIntroIndex = 0;

	sdata->WrongWayDirection_bool = false;

	sdata->framesDrivingSameDirection = 0;
	sdata->nTropyVoiceCount = 0;
	sdata->boolNeedXASeek = 0;

	DECOMP_Music_SetDefaults();
}
