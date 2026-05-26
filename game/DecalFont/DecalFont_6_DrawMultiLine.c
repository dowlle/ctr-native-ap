#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80022b34-0x80022b94.
int DecalFont_DrawMultiLine(char *str, int posX, int posY, int maxPixLen, s16 fontType, int flags)
{
	return (s16)DecalFont_DrawMultiLineStrlen(str, -1, (s16)posX, (s16)posY, (s16)maxPixLen, fontType, (s16)flags);
}
