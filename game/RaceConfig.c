#include <common.h>


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80043c10-0x80043d24.
void RaceConfig_LoadGameOptions(void)
{
	int i;
	s16 *volumes;

	if (sdata->boolHasLoadedOptions != 0)
	{
		return;
	}

	sdata->boolHasLoadedOptions = 1;
	volumes = &sdata->gameOptions.volFx;

	for (i = 0; i < 3; i++)
	{
		howl_VolumeSet(i, (u8)volumes[i]);
		memcpy(&data.rwd[0], &sdata->gameOptions.rwd[0], sizeof(data.rwd));
	}

	sdata->gGT->gameMode1 |= sdata->gameOptions.gameMode1_vibrationFlags & GAME_MODE_VIBRATION_MASK;
	howl_ModeSet((u8)sdata->gameOptions.audioMode & 1);

	// config.ini audio (see platform/native_config.c) is authoritative over the
	// adventure-profile card: reapply the saved values so the memcard reconcile
	// just done above cannot stomp the user's config.ini choice. volFx < 0 means
	// no [Audio] was captured yet -- leave the card's values in place.
	if (g_config.volFx >= 0)
	{
		howl_VolumeSet(0, (u8)g_config.volFx);
		howl_VolumeSet(1, (u8)g_config.volMusic);
		howl_VolumeSet(2, (u8)g_config.volVoice);
		howl_ModeSet(g_config.stereo != 0);
	}
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80043d24-0x80043e34.
void RaceConfig_SaveGameOptions(void)
{
	int i;
	s16 *volumes;

	volumes = &sdata->gameOptions.volFx;

	for (i = 0; i < 3; i++)
	{
		volumes[i] = howl_VolumeGet(i) & 0xff;
	}

	memcpy(&sdata->gameOptions.rwd[0], &data.rwd[0], sizeof(data.rwd));
	sdata->gameOptions.gameMode1_vibrationFlags = sdata->gGT->gameMode1 & GAME_MODE_VIBRATION_MASK;
	CTR_WriteU16LE(&sdata->gameOptions.audioMode, (u16)(howl_ModeGet() != 0));
}
