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

// #249: the full-count finish payout, armed when a new attempt's countdown is seen.
static AP_WumpaFinishLatch g_finish_latch = {0};
// -1 until the attempt first needs the full count. Only the real kart's fruit
// is capped; this count stays exposed to hits until a legitimate finish.
static int g_race_wumpa = -1;
static int g_wumpa_race_pending = 0;

// #249: does this seed bank fruit? Never under the unlimited-fruit cheat.
static int AP_WumpaBanking(void)
{
	return ctr_cfg_active() && !ctr_cfg.schema_newer &&
	       AP_WumpaBankingActive(&ctr_wumpa_shop) &&
	       (sdata->gGT == 0 || (sdata->gGT->gameMode2 & CHEAT_WUMPA) == 0);
}

// #249: race fruit banks only in Adventure races, for the local human player
// (decision 22, 2026-10-28, replacing the 2026-09-18 every-race-mode ruling),
// so Arcade cannot farm fruit outside the seed's tracks. A relic race carries
// RELIC_RACE without ADVENTURE_MODE (ap_boxes.c), so both count. Battle mode
// is not a race.
CTR_STATIC_ASSERT(AP_WUMPA_MODE_ADVENTURE == ADVENTURE_MODE);
CTR_STATIC_ASSERT(AP_WUMPA_MODE_RELIC == RELIC_RACE);
CTR_STATIC_ASSERT(AP_WUMPA_MODE_BATTLE == BATTLE_MODE);
CTR_STATIC_ASSERT(AP_WUMPA_MODE_ARCADE == ARCADE_MODE);
CTR_STATIC_ASSERT(AP_WUMPA_MODE_TIME_TRIAL == TIME_TRIAL);

static int AP_WumpaAdventureRace(const struct GameTracker *gGT)
{
	return gGT != 0 && AP_WumpaBankRaceMode(gGT->gameMode1);
}

// AP fruit past 10 waits here until the received-item effect marker that
// covers it has been written, so a crash cannot bank a replayed receipt twice
// (the marker write is queued before the bank write).
static int g_wumpa_ap_credit = 0;

void AP_WumpaCommitReceived(void)
{
	int n = g_wumpa_ap_credit;
	char msg[96];
	g_wumpa_ap_credit = 0;
	if (n <= 0)
		return;
	ap_net_wumpa_credit(n);
	snprintf(msg, sizeof msg, "[AP WUMPA BANK] +%d AP fruit past 10 (lifetime %lld)\n", n,
	         ap_net_wumpa_lifetime());
	AP_LogLine(msg);
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

static int AP_WumpaRaceTracking(struct Driver *driver)
{
	struct GameTracker *gGT = sdata->gGT;
	if (!ctr_cfg_active() || ctr_cfg.schema_newer || gGT == 0 || driver == 0 || driver != gGT->drivers[0])
		return 0;
	if ((gGT->gameMode2 & CHEAT_WUMPA) != 0)
		return 0;
	if ((driver->actionsFlagSet & ACTION_BOT) != 0 || !AP_WumpaAdventureRace(gGT))
		return 0;
	return g_countdown_seen && LOAD_IsOpen_RacingOrBattle() && sdata->Loading.stage == LOAD_IDLE;
}

// Receipts run before the per-frame countdown observer. Classify a receipt
// from the live scene, without requiring that observer to have run already.
// A receipt during pause or on the way out belongs to this attempt as well.
static int AP_WumpaReceiptInRace(void)
{
	struct GameTracker *gt = sdata->gGT;
	struct Driver *d = gt ? gt->drivers[0] : 0;
	return ctr_cfg_active() && !ctr_cfg.schema_newer && AP_WumpaAdventureRace(gt) &&
	       !(gt->gameMode2 & CHEAT_WUMPA) && LOAD_IsOpen_RacingOrBattle() && d &&
	       !(d->actionsFlagSet & (ACTION_BOT | ACTION_RACE_FINISHED)) &&
	       !(gt->gameMode1 & (END_OF_RACE | MAIN_MENU | GAME_CUTSCENE));
}

int AP_WumpaRaceCount(struct Driver *driver)
{
	if (driver == 0) return 0;
	return AP_WumpaRaceTracking(driver) && g_race_wumpa >= 0 ? g_race_wumpa : driver->numWumpas;
}

int AP_WumpaRaceModify(struct Driver *driver, int delta)
{
	long long next;
	if (!AP_WumpaRaceTracking(driver)) return 0;
	next = (long long)(g_race_wumpa >= 0 ? g_race_wumpa : driver->numWumpas) + delta;
	if (next < 0) next = 0;
	if (next > 2147483647) next = 2147483647;
	g_race_wumpa = (int)next;
	driver->numWumpas = next > AP_WUMPA_MAX ? AP_WUMPA_MAX : (int)next;
	return 1;
}

// #249 hub counter (decision 20, 2026-10-28): the spendable balance and the
// lifetime banked total, centred below the relic, key and trophy row in a 1P
// hub. The race HUD uses the full count of this race's fruit.
void AP_WumpaDrawHub(void)
{
	struct GameTracker *gGT = sdata->gGT;
	char line[48];
	if (!AP_WumpaBanking() || gGT == 0 || gGT->numPlyrCurrGame != 1)
		return;
	if (!ap_net_wumpa_ready())
		return;
	AP_WumpaHubLine(line, (int)sizeof line, ap_net_wumpa_balance(), ap_net_wumpa_lifetime());
	DecalFont_DrawLine(line, 0x100, 0x24, FONT_SMALL, ORANGE | JUSTIFY_CENTER);
}

// `wasHuman` and `held` come from the frame before the finish edge: the engine
// hands a finished player's kart to the AI on the finish frame
// (PlayLevel_UpdateLapStats -> BOTS_Driver_Convert), so ACTION_BOT is already
// set when the edge is seen.
void AP_WumpaOnRaceFinish(struct Driver *driver, int wasHuman, int held)
{
	struct GameTracker *gGT = sdata->gGT;
	int pay;
	if (gGT == 0 || driver == 0 || driver != gGT->drivers[0])
		return;
	pay = AP_WumpaFinishTake(&g_finish_latch, g_race_wumpa >= 0 ? g_race_wumpa : held, AP_RaceAttemptIsForcedLoss());
	if (!AP_WumpaBanking() || !wasHuman || !AP_WumpaAdventureRace(gGT))
		return;
	AP_WumpaBank(pay, "collected at the finish");
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
	char msg[112];
	int held, past, banked = 0;

	if (count <= 0)
		return;
	// In-race receipts join the exposed race total, never the wallet. The
	// normal tick applies them through ModifyWumpa, including past ten.
	if (AP_WumpaReceiptInRace())
	{
		long long pending = (long long)g_wumpa_race_pending + count;
		g_wumpa_race_pending = pending > 2147483647 ? 2147483647 : (int)pending;
		snprintf(msg, sizeof msg, "[AP WUMPA] held for this race +%d (pending %d)\n", count, g_wumpa_race_pending);
		AP_LogLine(msg);
		return;
	}

	// 0.3.0-alpha2: outside an Adventure race, received fruit
	// of any origin banks at once (balance and collected total) instead of
	// waiting for the kart. The credit waits in g_wumpa_ap_credit only until the
	// effect marker covering this item is written (AP_WumpaCommitReceived), the
	// same crash guard as fruit past 10. In-race receipts (above) keep joining
	// the race count; without banking the hold-for-kart rule below still applies.
	// To go back to holding, change this one condition.
	if (AP_WumpaBanking())
	{
		g_wumpa_ap_credit += count;
		snprintf(msg, sizeof msg, "[AP WUMPA] received +%d banked at once\n", count);
		AP_LogLine(msg);
		return;
	}

	// #144: report what actually happened. The fruit is held for the kart
	// (handed over at the next race start, or on the next frame mid-race); a
	// receive that would push the held fruit past AP_WUMPA_MAX has the overflow
	// banked or discarded below. Since 2026-11-02 it no longer logs "banked"
	// for the held fruit, which read as the Wumpa bank in play.
	held = count;
	g_wumpa_pending += count;
	if (g_wumpa_pending > AP_WUMPA_MAX)
	{
		held = count - (g_wumpa_pending - AP_WUMPA_MAX);
		g_wumpa_pending = AP_WUMPA_MAX; // a kart can never hold more; no cross-race hoard
	}
	past = count - held;
	// Banking seeds returned above, so fruit past 10 is only discarded here.
	AP_WumpaReceiveLine(msg, (int)sizeof msg, held, g_wumpa_pending, banked, past, AP_WUMPA_MAX);
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
	// #249: the player's race-finished rising edge, once per attempt (the
	// latch below); AP_WumpaOnRaceFinish pays only in an Adventure race. A finish seen during a load is ignored.
	{
		static int finishedPrev = 0, humanPrev = 0, heldPrev = 0;
		struct Driver *p = gGT->drivers[0];
		int finished = p != 0 && onTrack && (p->actionsFlagSet & ACTION_RACE_FINISHED) != 0;
		if (finished && !finishedPrev && !loading)
			AP_WumpaOnRaceFinish(p, humanPrev, p->numWumpas > heldPrev ? p->numWumpas : heldPrev);
		finishedPrev = finished;
		// The player as of this frame, for the next frame's finish edge.
		humanPrev = p != 0 && onTrack && !finished && (p->actionsFlagSet & ACTION_BOT) == 0;
		heldPrev = humanPrev ? p->numWumpas : 0;
	}
	if (!onTrack || loading || (gGT->gameMode1 & END_OF_RACE) != 0)
	{
		g_countdown_seen = 0;
		g_starting_applied = 0;
		g_race_wumpa = -1;
		g_wumpa_race_pending = 0;
	}
	else if (gGT->trafficLightsTimer >= 1)
	{
		// #249: a newly seen countdown starts a race attempt (a pause-menu
		// restart too), which arms the held-at-finish payout once.
		if (!g_countdown_seen)
			AP_WumpaFinishArm(&g_finish_latch);
		g_countdown_seen = 1;
	}

	if (g_wumpa_pending <= 0 && g_wumpa_race_pending <= 0 && (g_wumpa_starting <= 0 || g_starting_applied))
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

	if (g_wumpa_race_pending > 0)
	{
		RB_Player_ModifyWumpa(local, g_wumpa_race_pending);
		g_wumpa_race_pending = 0;
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
