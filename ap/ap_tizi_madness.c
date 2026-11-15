#ifdef CTR_AP

#include <stdio.h>

#include <common.h> // structs Driver/GameTracker + sdata + gameMode/action flags

#include "ap_tizi_madness.h"
#include "ap_tizi_madness_logic.h"
#include "ap_turbogrant_logic.h" // the per-attempt countdown latch (AP_TurboGrantRace)
#include "ap_tizi.h"  // the Mask weapon receipt and the Itemsanity probe (#223)
#include "ap_hooks.h" // AP_LogLine
#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h>
#endif
#ifdef CTR_CUSTOM_PACKAGES
#include <platform/native_custom_offline.h>
#endif

// ============================================================================
// Tizi Madness Mask (A4-96) -- engine glue. ap_tizi_madness_logic.h holds the
// rules. The Mask receipt is the one the Tizi Helper already keeps (ap_tizi.c),
// rebuilt from zero on every fresh connect, so the two features read one flag.
// ============================================================================

// The attempt latch the Turbo Grant uses (ap_turbogrant_logic.h): the
// countdown of this attempt was seen on a live track. Until then the race flags
// can be stale from the previous level, and anything written to the kart would
// land before VehBirth (the race-load gap, see ap_wumpa.c). Only the
// countdownSeen half is used; a Mask is never "given once".
static AP_TurboGrantRace g_tm_race;

// One log line per race attempt, so a run shows which way the gate went without
// a line per refill. Cleared with each new countdown.
static int g_tm_logged_state = -1;

// Is the level being raced Tizi Madness? Decided by the LEV and VRM pins of
// the bytes this load serves (include/platform/native_tizi_madness.h), on the
// custom Arcade and Time Trial pages and on every seed route (pad, cup leg,
// boss venue), never by the title. Zero in a build without custom tracks.
static int AP_TiziMadnessLoad(struct GameTracker *gGT)
{
#ifdef CTR_CUSTOM_TRACKS
	struct CustomTrackLoadContext ctx;
#ifdef CTR_CUSTOM_PACKAGES
	if (MainRaceTrack_OfflineCustomLoad())
		return CustomOffline_RuntimeTiziMadness();
#endif
	ctx.levelID = gGT->levelID;
	ctx.adventureCupActive = (gGT->gameMode1 & ADVENTURE_CUP) != 0;
	ctx.cupID = gGT->cup.cupID;
	ctx.bossID = gGT->bossID;
	ctx.adventureBossActive = (gGT->gameMode1 & ADVENTURE_BOSS) != 0;
	return CustomTrack_TiziMadness(&ctx);
#else
	(void)gGT;
	return 0;
#endif
}

static void AP_TiziMadnessLog(struct GameTracker *gGT, int state)
{
	char msg[160];
	(void)gGT;
	if (g_tm_logged_state == state)
		return;
	g_tm_logged_state = state;
	snprintf(msg, sizeof msg, state ?
	         "[AP TIZI MADNESS] on Tizi Madness: the weapon slot keeps a Mask\n" :
	         "[AP TIZI MADNESS] on Tizi Madness: waiting for the Mask item (Itemsanity)\n");
	AP_LogLine(msg);
}

void AP_TiziMadnessTick(struct GameTracker *gGT)
{
	struct Driver *local;
	int raceActive;
	int refill;
	int onTrack;

	if (gGT == 0)
		return;

	// Keep the attempt latch every frame, whatever else this tick decides. A new
	// countdown is a new attempt (a start or a restart), so the log line may
	// repeat for it.
	// A load in progress (a restart from the pause menu keeps running frames
	// until the transition covers the screen) ends the attempt: the kart is
	// about to be rebuilt by VehBirth. Same rule as the Wumpa lifecycle.
	onTrack = LOAD_IsOpen_RacingOrBattle() && sdata->Loading.stage == LOAD_IDLE;
	if (onTrack && (gGT->gameMode1 & END_OF_RACE) == 0 && gGT->trafficLightsTimer >= 1)
		g_tm_logged_state = -1;
	AP_TurboGrantRaceStep(&g_tm_race, onTrack, (gGT->gameMode1 & END_OF_RACE) != 0,
	                      gGT->trafficLightsTimer >= 1);
	if (!g_tm_race.countdownSeen)
		return;

	// The same live-race window as the Turbo Grant and the Wumpa filler: after
	// the countdown, not paused, not in a menu, cutscene or end of race.
	raceActive = (gGT->gameMode1 &
	              (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) == 0 &&
	             gGT->trafficLightsTimer < 1 && onTrack;
	if (!raceActive)
		return;
	if (!AP_TiziMadnessLoad(gGT))
		return;

	refill = AP_TiziMadnessRefillOn(1, AP_TiziItemsanitySeed(), AP_TiziMaskReceived());
	AP_TiziMadnessLog(gGT, refill);
	if (!refill)
		return;

	local = gGT->drivers[0];
	if (local == 0 || (local->actionsFlagSet & ACTION_BOT) != 0)
		return;
	if (!AP_TiziMadnessSlotFree(local->heldItemID, local->numHeldItems, local->noItemTimer,
	                            (local->actionsFlagSet & ACTION_WEAPON_FIRE_REQUEST) != 0,
	                            local->thCloud != 0 &&
	                                ((struct RainCloud *)local->thCloud->object)->effect ==
	                                    RAIN_CLOUD_EFFECT_ITEM_ROLL,
	                            local->clockReceive))
		return;

	// Straight into the slot, no roll and no sound: the Mask is not a pickup
	// the player earned, and a ping after every use would be noise.
	local->heldItemID = AP_TIZI_MADNESS_HELD_MASK;
}

#endif // CTR_AP
