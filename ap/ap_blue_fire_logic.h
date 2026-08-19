#ifndef AP_BLUE_FIRE_LOGIC_H
#define AP_BLUE_FIRE_LOGIC_H

enum AP_BlueFirePaletteState
{
	AP_BLUE_FIRE_PALETTE_UNKNOWN = 0,
	AP_BLUE_FIRE_PALETTE_RED,
	AP_BLUE_FIRE_PALETTE_BLUE
};

enum AP_BlueFirePaletteAction
{
	AP_BLUE_FIRE_ACTION_NONE = 0,
	AP_BLUE_FIRE_ACTION_FORGET,
	AP_BLUE_FIRE_ACTION_RED,
	AP_BLUE_FIRE_ACTION_BLUE
};

// Pure transition policy. A loading or menu scene may replace the same VRAM
// cells, so forget our latch without writing into that scene. Once racing
// textures are resident again, the unknown state forces the correct palette.
static inline int AP_BlueFirePaletteActionFor(int currentState,
	int racingTexturesResident, int wantBlue)
{
	if (!racingTexturesResident)
		return currentState == AP_BLUE_FIRE_PALETTE_UNKNOWN
		           ? AP_BLUE_FIRE_ACTION_NONE
		           : AP_BLUE_FIRE_ACTION_FORGET;

	if (wantBlue)
		return currentState == AP_BLUE_FIRE_PALETTE_BLUE
		           ? AP_BLUE_FIRE_ACTION_NONE
		           : AP_BLUE_FIRE_ACTION_BLUE;

	return currentState == AP_BLUE_FIRE_PALETTE_RED
	           ? AP_BLUE_FIRE_ACTION_NONE
	           : AP_BLUE_FIRE_ACTION_RED;
}

#endif
