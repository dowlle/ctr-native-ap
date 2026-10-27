#ifdef CTR_AP

#include <common.h> // structs Driver/GameTracker + sdata + gameMode/enum flags + functions.h (RB_Player_ModifyWumpa)

#include "ap_wumpa.h"
#include "ap_hooks.h" // AP_LogLine (non-static log shim)
#include "ap_net.h"   // ap_net_wumpa_credit: the #249 bank
#include "ap_deathlink.h" // AP_RaceAttemptIsForcedLoss
#include "ap_wumpa_shop.h"

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
static int g_wumpa_starting = 0;
static int g_starting_applied = 1;

// Countdown latch: set once the lights sequence is OBSERVED running on a live
// track (trafficLightsTimer >= 1 while LOAD_IsOpen_RacingOrBattle()), cleared
// off-track, at END_OF_RACE, and on any non-idle load stage. Guards the
// race-load gap: for a few frames
// after a track load the race init has not yet stamped gameMode1 /
// trafficLightsTimer, the stale free-roam values (no flags, timer 0) pass the
// flag/timer test, and a grant there is wiped by VehBirth -- observed in the
// 2026-07-17 batch smoke as "granted +10" on the same tick as the level
// transition with 0 fruit at spawn. A countdown can never be observed inside
// that gap, so the latch closes it; it also re-arms per cup leg (END_OF_RACE
// reset) so a mid-cup receipt cannot leak into the next leg's load gap.
static int g_countdown_seen = 0;

// #249: the held-at-finish payout, armed when a new attempt's countdown is seen.
static AP_WumpaFinishLatch g_finish_latch = {0};

// #249: does this seed bank fruit? Banking runs on the local human player in
// Adventure mode only.
static int AP_WumpaBanking(void)
{
	return ctr_cfg_active() && !ctr_cfg.schema_newer &&
	       AP_WumpaBankingActive(&ctr_wumpa_shop);
}

static void AP_WumpaBank(int n, const char *why)
{
	char msg[96];
	if (n <= 0)
		return;
	ap_net_wumpa_credit(n);
	snprintf(msg, sizeof msg, "[AP WUMPA BANK] +%d %s (lifetime %lld)\n", n, why,
	         ap_net_wumpa_lifetime());
	AP_LogLine(msg);
}

void AP_WumpaBankKart(struct Driver *driver, int held, int delta)
{
	struct GameTracker *gGT = sdata->gGT;
	if (!AP_WumpaBanking() || gGT == 0 || driver == 0 || driver != gGT->drivers[0])
		return;
	if ((driver->actionsFlagSet & ACTION_BOT) != 0 || (gGT->gameMode1 & ADVENTURE_MODE) == 0)
		return;
	AP_WumpaBank(AP_WumpaBankOverflow(held, delta), "past 10");
}

int AP_WumpaHudBanked(struct Driver *driver, char *out, int cap)
{
	struct GameTracker *gGT = sdata->gGT;
	if (!AP_WumpaBanking() || gGT == 0 || out == 0 || cap <= 0 || driver != gGT->drivers[0] ||
	    gGT->numPlyrCurrGame != 1 || (gGT->gameMode1 & ADVENTURE_MODE) == 0)
		return 0;
	snprintf(out, (size_t)cap, "%lld", ap_net_wumpa_lifetime());
	return 1;
}

void AP_WumpaOnRaceFinish(struct Driver *driver)
{
	struct GameTracker *gGT = sdata->gGT;
	int held;
	if (gGT == 0 || driver == 0 || driver != gGT->drivers[0])
		return;
	held = AP_WumpaFinishTake(&g_finish_latch, driver->numWumpas, AP_RaceAttemptIsForcedLoss());
	if (!AP_WumpaBanking() || (gGT->gameMode1 & ADVENTURE_MODE) == 0)
		return;
	AP_WumpaBank(held, "held at the finish");
}

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
	int banked, discarded;

	if (count <= 0)
		return;

	// #144: report what actually happened. A receive that would push the bank
	// past AP_WUMPA_MAX has the overflow discarded by the clamp below, not
	// banked -- "banked +N" for a discarded N was a lie the log told fifteen
	// times in a row in one player's submitted log.
	banked = count;
	g_wumpa_pending += count;
	if (g_wumpa_pending > AP_WUMPA_MAX)
	{
		banked = count - (g_wumpa_pending - AP_WUMPA_MAX);
		g_wumpa_pending = AP_WUMPA_MAX; // a kart can never hold more; no cross-race hoard
	}
	discarded = count - banked;
	// #249 (ruled 2026-10-16): with banking on, AP fruit past 10 goes into the
	// Wumpa bank at once, in or out of a race, instead of being discarded.
	if (discarded > 0 && AP_WumpaBanking())
	{
		AP_WumpaBank(discarded, "AP fruit past 10");
		discarded = 0;
	}

	if (discarded == 0)
		snprintf(msg, sizeof msg, "[AP WUMPA] banked +%d (pending %d)\n",
		         banked, g_wumpa_pending);
	else if (banked == 0)
		snprintf(msg, sizeof msg, "[AP WUMPA] discarded +%d (bank full at %d)\n",
		         discarded, AP_WUMPA_MAX);
	else
		snprintf(msg, sizeof msg,
		         "[AP WUMPA] banked +%d, discarded +%d (bank full at %d)\n",
		         banked, discarded, AP_WUMPA_MAX);
	AP_LogLine(msg);
}

void AP_WumpaReceiveStarting(void)
{
	if (g_wumpa_starting < AP_WUMPA_MAX)
		g_wumpa_starting++;
}

void AP_WumpaConnectReset(void)
{
	g_wumpa_pending = 0;
	g_wumpa_starting = 0;
	// If this is a mid-race reconnect, do not duplicate a grant that may already
	// be in the kart. AP_WumpaTick clears this as soon as it observes off-track,
	// end-of-race, OR a non-idle load stage, which arms the next real race start.
	// The load stage joined that set on 2026-08-20 so a pause-menu restart counts
	// as a boundary; a reconnect followed by a retry therefore reapplies the
	// ladder in the restarted race rather than treating it as the same race.
	g_starting_applied = 1;
}

// ── Per-frame lifecycle ──
void AP_WumpaTick(struct GameTracker *gGT)
{
	int raceActive;
	int onTrack;
	int loading;
	struct Driver *local;
	char msg[96];

	if (gGT == 0)
		return;

	// Maintain the countdown latch every frame (even with an empty bank, so a
	// mid-race receipt can grant immediately once the countdown has been seen).
	//
	// The load stage is part of the boundary test because of the pause-menu
	// RESTART, reported against the Alpha as starting fruit that never comes back
	// after a retry. That path (MainFreeze.c:984 "RESTART"/"RETRY", and the
	// end-of-race RETRY at UI_RaceFlow.c:369) only sets Loading.stage =
	// LOAD_RESTART; MainMain.c:208 then reinitializes the world in place without
	// ever unloading overlay 1 and without passing through END_OF_RACE. So
	// off-track and END_OF_RACE both stay false across a whole restart, the two
	// latches survive it, and the restarted race starts with g_starting_applied
	// still set: the ladder is applied once and then never again for as long as
	// the player keeps retrying. Every other race boundary (hub, menu, next cup
	// leg) does clear one of the other two, which is why a plain "next race"
	// reapplies correctly and only the retry does not.
	//
	// It also closes the doomed-race window on the way out. MainMain keeps
	// running the live scene, AP_OnFrame included, until the checkered flag
	// covers the screen, and on those frames PAUSE_1 is already cleared and the
	// mid-race timer is still negative -- so without this the whole pending bank
	// drains into a race that is about to be torn down, and the fruit is gone
	// with it. Clearing g_countdown_seen holds the bank until the restarted
	// race's own countdown is observed. Every non-idle stage is a genuine race
	// boundary (namespace_Main.h: VLC, RESTART, REQUESTED, FINISHED, TenStages),
	// so testing the whole set rather than LOAD_RESTART alone costs nothing and
	// arms the two latches identically for a restart and for a real load.
	//
	// No null test on sdata: LOAD_IsOpen_RacingOrBattle dereferences it
	// unconditionally on the line above (LOAD_IsOpen.c:6), so a guard here would
	// imply a case the preceding line has not already survived.
	onTrack = LOAD_IsOpen_RacingOrBattle();
	loading = sdata->Loading.stage != LOAD_IDLE;
	if (!onTrack || loading || (gGT->gameMode1 & END_OF_RACE) != 0)
	{
		g_countdown_seen = 0;
		g_starting_applied = 0;
	}
	else if (gGT->trafficLightsTimer >= 1)
	{
		// #249: a newly seen countdown starts a race attempt (a pause-menu
		// restart too), which arms the held-at-finish payout once.
		if (!g_countdown_seen)
			AP_WumpaFinishArm(&g_finish_latch);
		g_countdown_seen = 1;
	}

	if (g_wumpa_pending <= 0 && (g_wumpa_starting <= 0 || g_starting_applied))
		return;

	// Race window: mid-race, countdown finished, not paused/menu/cutscene/EOR.
	// The flag/timer test alone is NOT enough. (1) The adventure hub passes it a
	// few seconds after spawn (MainGameStart sets START_OF_RACE +
	// trafficLightsTimer on hub entry, both clear while free-roaming) -- closed by
	// LOAD_IsOpen_RacingOrBattle() (overlay-1 test, false in the hub; the stock
	// mask-grab subsystem's own on-a-track predicate, VehStuckProc.c:451), same as
	// the DeathLink receive window. (2) The race-load gap passes it too (stale
	// flags/timer for a few frames after the track load, before race init) --
	// closed by the countdown latch above. Deliberately NOT the trap lapWindow
	// (lapIndex >= 1) -- that would delay a hub-banked grant to lap 2. At
	// lights-out VehBirth has long since zeroed the kart's fruit, so a grant here
	// sticks and shows on the HUD -- both "start of race" (banked in the hub) and
	// "live mid-race" receipts land through this one gate.
	raceActive = (gGT->gameMode1 &
	              (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) == 0 &&
	             gGT->trafficLightsTimer < 1 && onTrack && g_countdown_seen;
	if (!raceActive)
		return;

	local = AP_WumpaLocalDriver(gGT);
	if (local == 0)
		return;

	if (!g_starting_applied)
	{
		if (g_wumpa_starting > 0)
		{
			RB_Player_ModifyWumpa(local, g_wumpa_starting);
			snprintf(msg, sizeof msg, "[AP WUMPA] applied %d starting fruit (now %d)\n",
			         g_wumpa_starting, (int)local->numWumpas);
			AP_LogLine(msg);
		}
		g_starting_applied = 1;
	}

	if (g_wumpa_pending <= 0)
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
