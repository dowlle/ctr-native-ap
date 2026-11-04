#ifndef AP_BOX_NUMBER_LOGIC_H
#define AP_BOX_NUMBER_LOGIC_H

#include "ap_box_map.h"

static inline int AP_BoxNumber_Standing(int used, int spawned, int checked)
{
	return used && spawned && !checked;
}

// A box's authored slot is stable even when earlier locations are absent or
// checked. Keep the player-visible number tied to that slot, never to the
// compact live-set index.
static inline int AP_BoxNumber_Text(int slot, char out[3])
{
	int number = slot + 1;
	if (out == 0 || slot < 0 || slot >= AP_BOX_SLOTS_PER_TRACK)
		return 0;
	if (number >= 10)
	{
		out[0] = '1';
		out[1] = (char)('0' + number - 10);
		out[2] = '\0';
	}
	else
	{
		out[0] = (char)('0' + number);
		out[1] = '\0';
	}
	return 1;
}

// SZ3 uses world units here. Keep distant numbers legible, then stop drawing
// before they turn into screen clutter. Font width is restored after drawing.
static inline int AP_BoxNumber_LabelWidthForDepth(int depth)
{
	if (depth < 100 || depth >= 1500)
		return 0;
	if (depth < 450)
		return 13;
	if (depth < 900)
		return 11;
	return 9;
}

#endif
