#ifndef AP_HUD_LAYOUT_H
#define AP_HUD_LAYOUT_H

// Placement of the AP numeric speed readout (UI_DrawSpeedNum) next to the boost
// and reserves bars. Freestanding like ap_reserves_meter_logic.h so
// tools/test-hud-layout.c can pin it without the engine.
//
// The race draw area is 512 x 216 (the bottom row of the UI is y 215). The
// readout used to start 9 rows under the boost bar, at y 215, and the font is 17
// rows tall, so it was clipped away apart from one row of pixels. It now sits
// on the left of the bars, right-justified, with its bottom edge inside the
// draw area.

#define AP_HUD_DRAW_HEIGHT 216
#define AP_HUD_FONT_BIG_HEIGHT 17
#define AP_HUD_BAR_WIDTH 49
#define AP_HUD_READOUT_GAP 3
// Rows above the boost bar's anchor row. 206 - 9 = 197: rows 197 to 213.
#define AP_HUD_READOUT_RAISE 9

// Top row of the readout for a boost bar anchored at `boostY`.
static inline int AP_HudSpeedNumY(int boostY)
{
	return boostY - AP_HUD_READOUT_RAISE;
}

// Right edge of the readout (draw with JUSTIFY_RIGHT) for a boost bar anchored
// at `boostX` (the bars grow left from this column).
static inline int AP_HudSpeedNumRightX(int boostX)
{
	return boostX - AP_HUD_BAR_WIDTH - AP_HUD_READOUT_GAP;
}

#endif
