#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80047f20-0x80047fb8.
void SelectProfile_PrintInteger(int value, int posX, int posY, int usePaddedFormat, int color)
{
	char text[64];
	char *format;

	if (usePaddedFormat == 1)
		format = &sdata->stringFormat1[0];
	else
		format = &sdata->stringFormat2[0];

	sprintf(text, format, value);
	DecalFont_DrawLine(text, posX, posY, FONT_BIG, color);
}
