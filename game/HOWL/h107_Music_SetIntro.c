#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002dd24-0x8002dd74
void DECOMP_Music_SetIntro(void)
{
	struct Bank thisBank;

	sdata->audioDefaults[7] = 0;

	DECOMP_Bank_Load(33, &thisBank);

	while (DECOMP_Bank_AssignSpuAddrs() == 0)
	{
	}

	DECOMP_howl_SetSong(28);

	while (DECOMP_howl_LoadSong() == 0)
	{
	}
}
