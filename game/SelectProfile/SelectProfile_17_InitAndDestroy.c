#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80048edc-0x80048f0c.
void SelectProfile_InitAndDestroy(void)
{
	SelectProfile_Init(data.menuFourAdvProfiles.drawStyle);
	SelectProfile_Destroy();
}
