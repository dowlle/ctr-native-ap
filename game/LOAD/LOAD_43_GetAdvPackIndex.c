#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800335dc-0x80033610.
int LOAD_GetAdvPackIndex(void)
{
	int levelID = sdata->gGT->levelID;

	if ((levelID != GEM_STONE_VALLEY) && (levelID != GLACIER_PARK))
		return 1;

	return 2;
}
