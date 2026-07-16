#ifdef CTR_AP

#include <common.h> // structs Driver/GameTracker + sdata + gameMode/enum flags + functions.h (RB_Player_ModifyWumpa)

#include "ap_wumpa.h"
#include "ap_hooks.h" // AP_LogLine (non-static log shim)

// ============================================================================
// AP WUMPA FRUIT filler grant -- implementation. See ap_wumpa.h for the design
// contract (bank-then-grant, timing semantics, zero-logic-impact). Single unity
// translation-unit member (game/game_unity.h), same convention as ap_traps.c.
// ============================================================================

// The vanilla max fruit a kart can hold. RB_Player_ModifyWumpa clamps to this too;
// we also clamp the bank so an over-long hub session can't accumulate a meaningless
// hoard (a race resets fruit to 0, so > 10 banked could never be felt).
#define AP_WUMPA_MAX 10

// Pending fruit to hand the local player at the next grant opportunity.
static int g_wumpa_pending = 0;

// The local human player. Single-player Adventure: drivers[0]. Same convention as
// the trap module's AP_TrapLocalDriver.
static struct Driver *AP_WumpaLocalDriver(struct GameTracker *gGT)
{
	if (gGT == 0)
		return 0;
	return gGT->drivers[0];
}

// ── AP item pipeline seam ──
void AP_WumpaReceive(int count)
{
	char msg[96];

	if (count <= 0)
		return;

	g_wumpa_pending += count;
	if (g_wumpa_pending > AP_WUMPA_MAX)
		g_wumpa_pending = AP_WUMPA_MAX; // a kart can never hold more; no cross-race hoard

	snprintf(msg, sizeof msg, "[AP WUMPA] banked +%d (pending %d)\n",
	         count, g_wumpa_pending);
	AP_LogLine(msg);
}

// ── Per-frame lifecycle ──
void AP_WumpaTick(struct GameTracker *gGT)
{
	int raceActive;
	struct Driver *local;
	char msg[96];

	if (gGT == 0 || g_wumpa_pending <= 0)
		return;

	// Race window: mid-race, countdown finished, not paused/menu/cutscene/EOR.
	// Same test AP_TrapTick uses (anchors: gameMode flags namespace_Main.h;
	// trafficLightsTimer < 1 = lights out, PlayLevel.c:338). At this point VehBirth
	// has already zeroed the kart's fruit, so a grant here sticks and shows on the
	// HUD -- both "start of race" (banked in the hub) and "live mid-race" receipts
	// land through this one gate.
	raceActive = (gGT->gameMode1 &
	              (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) == 0 &&
	             gGT->trafficLightsTimer < 1;
	if (!raceActive)
		return;

	local = AP_WumpaLocalDriver(gGT);
	if (local == 0)
		return;

	// Hand the whole bank over in one call. RB_Player_ModifyWumpa caps at 10, plays
	// the "juiced up" jingle if this crossing hits 10, tallies numTimesWumpa for the
	// end-of-race comments, and no-ops under the unlimited-wumpa cheat -- exactly the
	// canonical fruit-grant path every pickup uses (game/231/RB_Player.c).
	RB_Player_ModifyWumpa(local, g_wumpa_pending);
	snprintf(msg, sizeof msg, "[AP WUMPA] granted +%d in-race (now %d)\n",
	         g_wumpa_pending, (int)local->numWumpas);
	AP_LogLine(msg);
	g_wumpa_pending = 0;
}

#endif // CTR_AP
