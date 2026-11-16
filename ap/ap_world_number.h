#ifndef AP_WORLD_NUMBER_H
#define AP_WORLD_NUMBER_H

#include "ap_box_number_logic.h"

// Call after setting the current viewport's GTE matrices. Both AP boxes and
// ordinary sanity pickups use the same raster, clipping and drop shadow.
static inline void AP_WorldNumber_Draw(const SVECTOR *world, const char *number)
{
	s16 screen[2];
	s32 depth;
	u32 flag;
	int width, oldWidth;
	CTR_GteLoadSV0(world);
	gte_rtps();
	CTR_GteStoreSXY(screen);
	gte_stsz(&depth);
	gte_stflg(&flag);
	width = AP_BoxNumber_LabelWidthForDepth((int)depth);
	if (width == 0 || (flag & 0x40000u) != 0 ||
	    screen[0] < 20 || screen[0] > 492 ||
	    screen[1] < 16 || screen[1] > 200)
		return;
	oldWidth = data.font_charPixWidth[FONT_SMALL];
	data.font_charPixWidth[FONT_SMALL] = (s16)width;
	DecalFont_DrawLine((char *)number, screen[0], screen[1],
	                   FONT_SMALL, JUSTIFY_CENTER | WHITE);
	DecalFont_DrawLine((char *)number, screen[0] + 1, screen[1] + 1,
	                   FONT_SMALL, JUSTIFY_CENTER | BLACK);
	data.font_charPixWidth[FONT_SMALL] = (s16)oldWidth;
}

#endif
