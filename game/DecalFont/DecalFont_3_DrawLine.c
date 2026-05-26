#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80022878-0x800228c4.
void DecalFont_DrawLine(char *str, int posX, int posY, s16 fontType, int flags)
{
	DecalFont_DrawLineStrlen(str, -1, (s16)posX, (s16)posY, fontType, (s16)flags);
}
