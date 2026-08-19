#ifdef CTR_AP

#include <common.h>

#include "ap_blue_fire.h"
#include "ap_blue_fire_logic.h"
#include "ap_capability.h"

// Palette data and NTSC VRAM positions adapted from Retro-Fueled's Blue Fire
// implementation. This is the visual half of the tier-3 mechanical capstone;
// grant and handling behavior live at the relevant physics decisions. See
// THIRD_PARTY_NOTICES.md.
static u32 g_apRedFlamesClut[8] = {
	0x80DA809A, 0x805A80BA, 0x811A803A, 0x819A81DA,
	0x813A817A, 0x829A821A, 0x835A839A, 0x82DA831A};
static u32 g_apBlueFlamesClut[8] = {
	0xFF25FF65, 0xFFA5FF45, 0xFEE5FFC5, 0xFE65FE25,
	0xFEC5FE85, 0xFD65FDE5, 0xFCA5FC65, 0xFD25FCE5};
static u32 g_apRedPlumesClut[8] = {
	0x000083FF, 0x821B833E, 0x829D81B8, 0x80F6800D,
	0x819A8054, 0x80D98011, 0x80078019, 0x84008015};
static u32 g_apBluePlumesClut[8] = {
	0x7FFFF260, 0xFC20F980, 0xF400FC20, 0xF980AC00,
	0xF260F980, 0xB400FCA0, 0x8800FC20, 0x8800FC20};

static RECT g_apFlamesPos = {176, 256, 16, 1};
static RECT g_apPlumesPos = {464, 257, 16, 1};
static int g_apBlueFirePaletteState = AP_BLUE_FIRE_PALETTE_UNKNOWN;

void AP_BlueFireTick(struct GameTracker *gGT)
{
	struct Driver *driver = 0;
	int resident;
	int wantBlue = 0;
	int action;

	if (gGT == 0)
		return;

	// AP_OnFrame runs in hubs and menus too, unlike the original mod's race-only
	// hook. Require the racing/battle overlay positively so these fixed VRAM cells
	// are never written after an Adventure hub has loaded unrelated content there.
	resident = sdata != 0 && sdata->Loading.stage == LOAD_IDLE &&
	           LOAD_IsOpen_RacingOrBattle() != 0 &&
	           (gGT->gameMode1 & (MAIN_MENU | GAME_CUTSCENE | LOADING)) == 0;
	if (resident && gGT->numPlyrCurrGame == 1)
	{
		driver = gGT->drivers[0];
		wantBlue = driver != 0 && driver->reserves > 0 &&
		           AP_CapabilityBoostTier() == AP_CAP_BOOST_BLUEFIRE &&
		           (int)driver->fireSpeedCap > (int)driver->const_SacredFireSpeed;
	}

	action = AP_BlueFirePaletteActionFor(g_apBlueFirePaletteState, resident, wantBlue);
	switch (action)
	{
	case AP_BLUE_FIRE_ACTION_FORGET:
		g_apBlueFirePaletteState = AP_BLUE_FIRE_PALETTE_UNKNOWN;
		break;
	case AP_BLUE_FIRE_ACTION_RED:
		LoadImage(&g_apFlamesPos, g_apRedFlamesClut);
		LoadImage(&g_apPlumesPos, g_apRedPlumesClut);
		g_apBlueFirePaletteState = AP_BLUE_FIRE_PALETTE_RED;
		break;
	case AP_BLUE_FIRE_ACTION_BLUE:
		LoadImage(&g_apFlamesPos, g_apBlueFlamesClut);
		LoadImage(&g_apPlumesPos, g_apBluePlumesClut);
		g_apBlueFirePaletteState = AP_BLUE_FIRE_PALETTE_BLUE;
		break;
	default:
		break;
	}
}

#endif
