#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800224d0-0x800224fc.
int DecalFont_GetLineWidth(char *str, s16 fontType)
{
	return (s16)DecalFont_GetLineWidthStrlen(str, -1, fontType);
}
