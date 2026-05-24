#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002dd74-0x8002de48
void DECOMP_Music_LoadBanks(void)
{
	u32 bankID;
	struct Bank thisBank;
	struct GameTracker *gGT = sdata->gGT;
	int level = gGT->levelID;
	u8 *arr = (u8 *)&sdata->audioDefaults[7];

	DECOMP_Audio_SetReverbMode(
	    // Level ID
	    level,

	    // Check to see if this is a boss race
	    (gGT->gameMode1 < 0),

	    // Boss ID
	    gGT->bossID);

	if (level == INTRO_RACE_TODAY)
	{
		DECOMP_Bank_DestroyAll();
		bankID = 0x22;
	}

	else if (level == NAUGHTY_DOG_CRATE)
	{
		DECOMP_Bank_DestroyAll();
		bankID = 0x21;
	}

	else
	{
		if (arr[0] == 0)
		{
			DECOMP_Bank_DestroyAll();

			DECOMP_Bank_Load(0, &thisBank);

			arr[0] = 1;
		}

		else
		{
			DECOMP_Bank_DestroyUntilIndex(0);
		}

		// loading state of song (one byte)
		arr[1] = 0;
		return;
	}

	arr[0] = 0;

	DECOMP_Bank_Load(bankID, &thisBank);

	// loading state of song (one byte)
	arr[1] = 3;
}
