#ifdef CTR_AP

#include <common.h> // Driver / GameTracker + sdata + kartState + gameMode flags
#include <stdio.h>
#include <string.h>

#include "ap_deathlink.h"
#include "ap_race_attempt_logic.h" // #286 freestanding attempt latch / permutation
#include "ap_hooks.h"   // AP_LogLine
#include "ap_net.h"     // ap_net_deathlink_enable / _send / _take, ap_net_is_connected
#include "ap_seedcfg.h" // ctr_cfg, ctr_cfg_active

#include "platform/native_config.h" // g_config.deathLink (the OPTIONS menu row)

// ============================================================================
// AP DEATHLINK -- game-side semantics. See ap_deathlink.h for the design
// contract (send triggers, receive = forced mask reset or race loss, no queue, and
// the hard no-loop guard). The tagged-Bounce transport is in ap_net.cpp.
// ============================================================================

// ── Send-edge state ──
static int g_dl_prev_maskgrab = 0; // rising-edge latch on the local KS_MASK_GRABBED
static int g_dl_amnesty_count  = 0; // eligible deaths counted toward the amnesty N

// ── Receive state ──
// DeathLink is never queued. A received death is either applied inside a live race
// or dropped. g_dl_pending_recv is only the short grace window (AP_DL_GRACE_FRAMES)
// for a death that arrived in a live race but whose landing preconditions are not
// met yet; outside a live race it is dropped at once.
static int  g_dl_pending_recv = 0;         // in-window death still trying to land
static int  g_dl_grace_left = 0;           // frames of grace left for it
static char g_dl_pending_cause[128] = {0}; // its cause (log and popup)
static char g_dl_pending_source[64] = {0}; // its source slot (popup)
static int  g_dl_defer_logged = 0;         // the deferral of THIS death is logged
static int  g_dl_hold_logged = 0;          // the pause hold of THIS death is logged

// #286 attempt-owned forced-loss latch. Deliberately separate from the network
// receive state above: it belongs to the race attempt, so AP_DeathLinkConnectReset
// and the feature-off early return below never touch it. It is armed by
// AP_DeathLinkApplyRaceLoss and cleared only by AP_RaceAttempt_OnLevelStart when
// the next eligible racing level loads.
static APRaceAttemptState g_dl_race_attempt;

// No-loop guard: armed the frame a RECEIVED death forces the mask grab; consumed
// by the next mask-grab rising edge so that forced reset never sends. No timeout:
// a never-consumed latch (forced grab that never materialised) at worst swallows
// one later genuine mask-grab -- a MISSED send, which is safe. Expiring it early
// would risk exactly the outgoing-send-on-received-death loop this must prevent.
static int g_dl_swallow_edge = 0;

// Suppression for damage the AP layer inflicts on the local player deliberately.
// The Flatten trap (#280) drives the engine's own damage dispatch, which runs the
// any_hit send hook exactly as a hazard would, and Flatten's ruling is that the
// effect is self-inflicted and must not send DeathLink.
//
// Deliberately NOT g_dl_swallow_edge: that latch guards the mask-grab edge while a
// RECEIVED death is being applied, and it is consumed by whichever edge arrives
// next. Folding a second, unrelated no-send reason into it would let a trap
// consume a guard the received-death path is still relying on.
static int g_dl_trap_self_inflicted = 0;

void AP_DeathLinkSuppressSelfInflicted(int on)
{
	g_dl_trap_self_inflicted = on ? 1 : 0;
}

// Send cooldown (frames). Observed live (2026-07-19 Deck 3-player session): a
// RECEIVED death's forced mask grab bounces the kart state machine through
// KS_MASK_GRABBED several times, producing 4-5 rising edges while the one-shot
// swallow guard covers only the first -- the extra edges all sent, and with a
// 91%-damage StS partner that closed a death ping-pong loop. Damping: no two
// sends within 2s, and nothing sends for 5s after a received death lands.
#define AP_DL_COOLDOWN_AFTER_SEND 60
#define AP_DL_COOLDOWN_AFTER_RECV 150
static int g_dl_send_cooldown = 0;

// ── Runtime preference (OPTIONS -> Archipelago -> DeathLink menu row) ──
// The preference lives in g_config.deathLink (-1 follow seed / 0 force off /
// 1 force on), persisted to config.ini, so the menu row and a
// hand-edited ini are all the same switch. The tag follows the preference via
// AP_DeathLinkSyncTag below -- re-checked every frame, so menu edits apply
// live without a menu-exit hook.
static int g_dl_tag_on = 0; // tag state last declared to the server

// The resolved state (AP_DeathLinkResolve): the OPTIONS rows over the seed, with the
// rule that anything that sends also receives. Nothing without slot_data.
static APDeathLinkState AP_DeathLinkStateNow(void)
{
	APDeathLinkState off = {0, 0};
	int row = g_config.deathLink;

	if (!ctr_cfg_active())
		return off;
	if (row < 0 || row > CTR_DL_RACE_LOSS)
		row = -1; // a hand-edited out-of-range value follows the seed
	return AP_DeathLinkResolve(ctr_cfg.death_link, ctr_cfg.death_link_send,
	                           row, g_config.dlSendFall,
	                           g_config.dlSendHit, g_config.dlSendLoss);
}

// Effective receive mode. The forced values ARE the tier enum (1 = mask_reset,
// 2 = any_hit, 3 = race_loss), so all in-game DeathLink layers stay selectable.
static int AP_DeathLinkEffMode(void)
{
	return AP_DeathLinkStateNow().recv;
}

// The effective send mask (AP_DL_TRIG_*): which events send, separate from the
// receive effect. 0 when DeathLink is off.
static int AP_DeathLinkSendMaskNow(void)
{
	return AP_DeathLinkStateNow().send;
}

// 1 when DeathLink does anything: it receives, or any send trigger is on.
int AP_DeathLinkActive(void)
{
	APDeathLinkState st = AP_DeathLinkStateNow();
	return AP_DeathLinkTagWanted(st.recv, st.send);
}

// #286 authoritative attempt predicate. Every result-derived producer reads this,
// so the failed attempt's finish checks and the genuine later attempt's checks
// can never disagree about which attempt owns them.
int AP_RaceAttemptIsForcedLoss(void)
{
	return AP_RaceAttempt_IsForcedLoss(&g_dl_race_attempt);
}

// #286: the one production decision every result-derived guard calls. Naming the
// producer class here (rather than testing the predicate ad hoc) keeps the
// guards and tools/test-race-attempt-lifecycle.c on the same code path: the
// harness drives AP_RaceAttempt_SuppressResultProducer through the same classes
// production passes. The cup-aggregate class is the deliberate exception that
// lets the overall Gem Cup reward through while the latch is set.
int AP_RaceAttempt_ProducerBlocked(int producerClass)
{
	return AP_RaceAttempt_SuppressResultProducer(
	    producerClass, AP_RaceAttempt_IsForcedLoss(&g_dl_race_attempt));
}

// #286 attempt boundary, called from MainInit_FinalizeInit right after
// MainGameStart_Initialize. LOAD_IsOpen_RacingOrBattle() is the race/battle
// thread overlay (1); the hub is 2, the main menu 0 and the podium 3, so those
// loads cannot clear the latch. A battle arena is not an eligible attempt either.
void AP_RaceAttempt_OnLevelStart(struct GameTracker *gGT)
{
	int cleared;

	if (gGT == 0)
		return;

	cleared = AP_RaceAttempt_LevelStartStep(
	    &g_dl_race_attempt,
	    LOAD_IsOpen_RacingOrBattle() != 0,
	    (sdata != 0 && sdata->Loading.stage == LOAD_IDLE),
	    (gGT->gameMode1 & (ADVENTURE_ARENA | BATTLE_MODE)) != 0);

	if (cleared)
		AP_LogLine("[AP DEATH] new race attempt -> forced-loss latch cleared\n");
}

// Declare/withdraw the DeathLink tag whenever the effective state and the
// server-declared tag disagree. Catches every edit path alike: the menu
// row, a hand-edited config.ini, and reconnects.
static void AP_DeathLinkSyncTag(void)
{
	int want = AP_DeathLinkActive() ? 1 : 0;
	if (!ap_net_is_connected() || want == g_dl_tag_on)
		return;
	if (want)
		ap_net_deathlink_enable();
	else
		ap_net_deathlink_disable();
	g_dl_tag_on = want;
	AP_LogLine(want ? "[AP DEATH] DeathLink ON (tag declared)\n"
	                : "[AP DEATH] DeathLink OFF (tag withdrawn)\n");
}


static int AP_DeathLinkAmnesty(void)
{
	int a = ctr_cfg_active() ? ctr_cfg.deathlink_amnesty : 1;
	return a < 1 ? 1 : a;
}

// Short, name-free cause phrase for a landed hit (the slot name is prepended by
// ap_net_deathlink_send for the standard "<player> <cause>" line). Built from
// damageType + the VehPickState reason code (VehPickState.c:141-198).
static const char *AP_DeathLinkHitCause(int damageType, int reason)
{
	switch (damageType)
	{
	case 1: // spin-out
		return "got spun out";
	case 2: // blasted
		if (reason == 1)
			return "was blown up by a bomb";
		if (reason == 3)
			return "was hit by a missile";
		return "was blown up";
	case 3: // squished
		return "got squished";
	case 4: // burned
		return "got torched";
	default:
		return "wiped out";
	}
}

// Adventure + connection + amnesty gated outgoing send. cause is a short verb
// phrase. Only deaths that clear every gate count toward amnesty, so amnesty
// throttles ACTUAL sends rather than raw death events.
static void AP_DeathLinkFireLocal(struct GameTracker *gGT, const char *cause)
{
	char msg[160];

	if (!ctr_cfg_active() || AP_DeathLinkSendMaskNow() == 0)
		return;
	if (gGT == 0 || (gGT->gameMode1 & ADVENTURE_MODE) == 0)
		return; // sends only from adventure mode
	if (!ap_net_is_connected())
		return;

	if (g_dl_send_cooldown > 0)
	{
		snprintf(msg, sizeof msg, "[AP DEATH] held by cooldown (%df left): %s\n",
		         g_dl_send_cooldown, cause);
		AP_LogLine(msg);
		return;
	}

	// Amnesty: send one death per N eligible deaths (N == 1 => every death).
	if (++g_dl_amnesty_count < AP_DeathLinkAmnesty())
	{
		snprintf(msg, sizeof msg, "[AP DEATH] held by amnesty (%d/%d): %s\n",
		         g_dl_amnesty_count, AP_DeathLinkAmnesty(), cause);
		AP_LogLine(msg);
		return;
	}
	g_dl_amnesty_count = 0;

	ap_net_deathlink_send(cause);
	g_dl_send_cooldown = AP_DL_COOLDOWN_AFTER_SEND;
	snprintf(msg, sizeof msg, "[AP DEATH] sent: %s\n", cause);
	AP_LogLine(msg);
}

// weapon_hit send hook (VehPickState_NewState). Only the weapon_hit trigger sends on hits;
// mask-grab (damageType 5) is owned by the edge detector so it is never doubled.
void AP_DeathLinkOnHit(struct Driver *victim, int damageType, int reason)
{
	struct GameTracker *gGT;

	if (!ctr_cfg_active() ||
	    !AP_DeathLinkTriggerSends(AP_DeathLinkSendMaskNow(), AP_DL_TRIG_HIT,
	                              AP_RaceAttemptIsForcedLoss()))
		return;
	if (damageType < 1 || damageType > 4)
		return;
	if (sdata == 0 || sdata->gGT == 0)
		return;
	gGT = sdata->gGT;
	if (victim == 0 || victim != gGT->drivers[0])
		return; // local player only
	if (g_dl_swallow_edge)
		return; // mid received-death application: never send
	if (g_dl_trap_self_inflicted)
		return; // a trap is driving this damage: ruled self-inflicted, never send

	AP_DeathLinkFireLocal(gGT, AP_DeathLinkHitCause(damageType, reason));
}

void AP_DeathLinkConnectReset(void)
{
	g_dl_prev_maskgrab = 0;
	g_dl_amnesty_count = 0;
	g_dl_pending_recv = 0;
	g_dl_grace_left = 0;
	g_dl_pending_cause[0] = '\0';
	g_dl_pending_source[0] = '\0';
	g_dl_defer_logged = 0;
	g_dl_hold_logged = 0;
	g_dl_swallow_edge = 0;
	// Defence in depth. The bracket around the trap's dispatch is set and cleared
	// across one synchronous call, so this should already be 0; clearing it with
	// the other send-edge state means a latch that somehow survived cannot silence
	// real deaths for the rest of a session.
	g_dl_trap_self_inflicted = 0;

	// Opt in to the DeathLink tag for this seed. Safe to call unconditionally on a
	// fresh connect: ctr_cfg was parsed in the slot-connected handler just before
	// AP_NetTick's reset block runs.
	g_dl_tag_on = 0; // fresh connection: no tag declared yet
	AP_DeathLinkSyncTag();

	// g_dl_race_attempt is deliberately NOT reset here. The #286 forced-loss latch
	// belongs to the race attempt, so a disconnect or reconnect during the result
	// sequence must not re-enable the failed attempt's finish checks. It clears
	// only in AP_RaceAttempt_OnLevelStart.
	AP_RaceAttempt_OnConnectReset(&g_dl_race_attempt);
}

// Build the rank -> driver-slot ordering from the live race order. Returns 0 if
// any participating rank is unpopulated, which defers the forced loss.
static int AP_DeathLinkBuildOrder(struct GameTracker *gGT, int racers, int *order)
{
	int r;

	for (r = 0; r < racers; r++)
	{
		struct Driver *d = gGT->driversInRaceOrder[r];
		if (d == 0)
			return 0;
		order[r] = (int)d->driverID;
	}
	return 1;
}

// A forced loss that cannot land yet is retried every frame for the grace window,
// so its log line is written once per death instead of once per frame.
static void AP_DeathLinkDeferOnce(const char *msg)
{
	if (g_dl_defer_logged)
		return;
	g_dl_defer_logged = 1;
	AP_LogLine(msg);
}

// Popup for the death being handled: two short feed lines (source, then cause; or
// "ignored" and the reason). Each line is cut at 28 characters by
// AP_FeedDeathLinkLine so it stays clear of the minimap.
static void AP_DeathLinkPopup(int applied, const char *why)
{
	char src[17]; // up to 16 characters of the source slot name
	char line[96];
	const char *cause = g_dl_pending_cause;
	size_t n = strlen(g_dl_pending_source);

	snprintf(src, sizeof src, "%s", g_dl_pending_source[0] ? g_dl_pending_source : "A PLAYER");
	if (!applied)
	{
		// "DEATHLINK IGNORED" / "<SOURCE>: <WHY>" (the source cut to 10 characters).
		snprintf(line, sizeof line, "DEATHLINK IGNORED");
		AP_FeedDeathLinkLine(line, 1);
		snprintf(line, sizeof line, "%.10s: %s", src, why);
		AP_FeedDeathLinkLine(line, 1);
		return;
	}
	// "DEATHLINK: <SOURCE>" then the cause on its own line. The standard cause
	// already begins with the source slot ("<slot> <cause>"), so that prefix is
	// dropped from the second line.
	snprintf(line, sizeof line, "DEATHLINK: %s", src);
	AP_FeedDeathLinkLine(line, 0);
	if (n > 0 && strncmp(cause, g_dl_pending_source, n) == 0)
	{
		cause += n;
		while (*cause == ' ')
			cause++;
	}
	snprintf(line, sizeof line, "%s", cause[0] ? cause : "DIED");
	AP_FeedDeathLinkLine(line, 0);
}

// The pending death ends here without landing: log it, show the ignored popup.
static void AP_DeathLinkDrop(const char *why)
{
	char msg[224];

	snprintf(msg, sizeof msg, "[AP DEATH] dropped: %s (%s)\n", why,
	         g_dl_pending_cause[0] ? g_dl_pending_cause : "a death");
	AP_LogLine(msg);
	AP_DeathLinkPopup(0, why);
	g_dl_pending_recv = 0;
	g_dl_grace_left = 0;
	g_dl_pending_cause[0] = '\0';
	g_dl_pending_source[0] = '\0';
	g_dl_defer_logged = 0;
	g_dl_hold_logged = 0;
}

// Common bookkeeping once a forced loss is going to be applied: consume the
// death, show its popup, and arm the attempt latch BEFORE the retail end sequence,
// so every result callback in that sequence observes the forced loss.
static void AP_DeathLinkConsumeForLoss(char *cause, size_t causeSize)
{
	snprintf(cause, causeSize, "%s",
	         g_dl_pending_cause[0] ? g_dl_pending_cause : "a death");
	AP_DeathLinkPopup(1, 0);
	g_dl_pending_recv = 0;
	g_dl_grace_left = 0;
	g_dl_pending_cause[0] = '\0';
	g_dl_pending_source[0] = '\0';
	g_dl_defer_logged = 0;
	g_dl_hold_logged = 0;
	g_dl_send_cooldown = AP_DL_COOLDOWN_AFTER_RECV;
	AP_RaceAttempt_ArmForcedLoss(&g_dl_race_attempt);
}

// Crystal Challenge: end it the way the retail clock does when time runs out
// (UI_DrawLimitClock): finished flag on the human players, then the common
// end-of-event initializer. CC_EndEvent_DrawMenu reads the missing crystals as a
// loss (TRY AGAIN), so no token reward or check is produced; the armed latch also
// blocks the reward producer.
static void AP_DeathLinkApplyCrystalLoss(struct GameTracker *gGT)
{
	char cause[128];
	char msg[192];
	int i;

	AP_DeathLinkConsumeForLoss(cause, sizeof cause);
	for (i = 0; i < (int)(u8)gGT->numPlyrCurrGame; i++)
	{
		if (gGT->drivers[i] != 0)
			gGT->drivers[i]->actionsFlagSet |= ACTION_RACE_FINISHED;
	}
	MainGameEnd_Initialize();

	snprintf(msg, sizeof msg, "[AP DEATH] received -> crystal challenge failed (%s)\n", cause);
	AP_LogLine(msg);
}

// Relic Race: end the run as failed but leave the player the retail end-of-race
// menu (RETRY / EXIT TO MAP). The local driver finishes the way a solo race finish
// does (finished flag, held item dropped, AI takes the kart), then the common
// end-of-event initializer runs. The attempt latch is armed first, so
// RR_EndEvent_UnlockAward grants no relic and AP_NotifyRelicPerfect sends nothing.
// A relic race has no ghost save. The retail result would open the high-score name
// entry for the time reached, so the pending high score and best lap are cleared
// (the same two lines the name-entry Cancel and the relic results skip use): no
// score is written and no name entry appears. RETRY then reloads the level and
// AP_RaceAttempt_OnLevelStart clears the latch for the fresh run.
static void AP_DeathLinkApplyRelicLoss(struct GameTracker *gGT, struct Driver *local)
{
	char cause[128];
	char msg[192];

	AP_DeathLinkConsumeForLoss(cause, sizeof cause);

	local->actionsFlagSet |= ACTION_RACE_FINISHED;
	local->heldItemID = 0xf;
	if (local->noItemTimer != 0)
		local->noItemTimer = 0;
	BOTS_Driver_Convert(local);
	MainGameEnd_Initialize();

	gGT->newHighScoreIndex = -1;
	gGT->gameModeEnd &= ~(NEW_BEST_LAP | NEW_HIGH_SCORE);

	snprintf(msg, sizeof msg, "[AP DEATH] received -> relic race failed, retry offered (%s)\n", cause);
	AP_LogLine(msg);
}

// #286: end the current adventure race attempt as a retail last-place loss.
// The rank permutation is validated BEFORE anything mutates, so an invalid or
// duplicate ordering logs and retries within the grace window without consuming
// the death. On
// success the latch is armed before MainGameEnd_Initialize, so every result
// callback in the retail end sequence observes the forced loss. Crystal
// Challenges and Relic Races have no ranking to force and end through their own
// routes above.
static void AP_DeathLinkApplyRaceLoss(struct GameTracker *gGT, struct Driver *local)
{
	int order[8];
	int racers = (int)(u8)gGT->numPlyrCurrGame + (int)(u8)gGT->numBotsNextGame;
	int oldRank = local->driverRank;
	char cause[128];
	char msg[192];
	int r;
	int kind = AP_RaceAttempt_LossKind(IS_BOSS_RACE(gGT->gameMode1) != 0,
	                                   (gGT->gameMode1 & RELIC_RACE) != 0,
	                                   (gGT->gameMode1 & CRYSTAL_CHALLENGE) != 0);

	if (kind == AP_LOSS_CRYSTAL)
	{
		AP_DeathLinkApplyCrystalLoss(gGT);
		return;
	}
	if (kind == AP_LOSS_RELIC)
	{
		AP_DeathLinkApplyRelicLoss(gGT, local);
		return;
	}

	if (racers < 1)
		racers = 1;
	if (racers > 8)
		racers = 8;

	if (oldRank < 0 || oldRank >= racers ||
	    !AP_DeathLinkBuildOrder(gGT, racers, order) ||
	    order[oldRank] != (int)local->driverID ||
	    !AP_RaceAttempt_ApplyLastPlaceSwap(racers, oldRank, order))
	{
		AP_DeathLinkDeferOnce("[AP DEATH] race loss deferred: invalid rank ordering\n");
		return; // do not consume the death, do not arm the latch (grace, then drop)
	}

	// Write the bijective permutation back: every participating driver occurs
	// exactly once and ranks are exactly 0..N-1. Drivers ranked ahead of the
	// local driver are also finished, and the finished count covers every racer:
	// the state a retail last-place finish leaves behind. Without it
	// PlayLevel_UpdateLapStats starts ranking unfinished drivers after the
	// local driver's rank, the bots never get a rank and driversInRaceOrder keeps
	// NULL slots that the results HUD dereferences.
	for (r = 0; r < racers; r++)
	{
		struct Driver *d = gGT->drivers[order[r]];
		d->driverRank = r;
		gGT->driversInRaceOrder[r] = d;
		if (AP_RaceAttempt_RankIsFinishedAfterLoss(racers, r))
			d->actionsFlagSet |= ACTION_RACE_FINISHED;
	}
	sdata->numPlayersFinishedRace = AP_RaceAttempt_FinishedCountAfterLoss(racers);

	AP_DeathLinkConsumeForLoss(cause, sizeof cause);

	// Retail circuit finish: mark finished, drop the held item, hand the kart to
	// the AI, then enter the common end-of-event initializer (PlayLevel.c:156-204,
	// MainGameEnd.c:522).
	local->actionsFlagSet |= ACTION_RACE_FINISHED;
	local->heldItemID = 0xf;
	if (local->noItemTimer != 0)
		local->noItemTimer = 0;
	BOTS_Driver_Convert(local);
	MainGameEnd_Initialize();

	snprintf(msg, sizeof msg, "[AP DEATH] received -> race loss (%s)\n", cause);
	AP_LogLine(msg);
}

// The live-race window a received death must land in (the trap-window idiom,
// ap_traps.c:313-318): mid-race, lights out, not menu / cutscene / end-of-race, not
// loading, on a real track. Returns AP_DL_WIN_PAUSED for the same race while
// paused (the one state a death is held in), AP_DL_WIN_LIVE while it runs, and
// AP_DL_WIN_OUTSIDE for everything else. The adventure hub passes the gameMode1 /
// trafficLightsTimer test a few seconds after spawn (START_OF_RACE is cleared when
// the fly-in ends while ADVENTURE_MODE stays set), so the track guard matters:
// LOAD_IsOpen_RacingOrBattle() (overlayIndex_Threads == 1) is false in the hub
// (overlay 2), and is the predicate the stock mask-grab subsystem uses to know it
// is on a track (VehStuckProc.c:451). A pause-menu quit or restart requests a load
// (Loading.stage leaves LOAD_IDLE), which makes the window OUTSIDE at once.
static int AP_DeathLinkWindow(struct GameTracker *gGT)
{
	if ((gGT->gameMode1 & (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE)) != 0 ||
	    gGT->trafficLightsTimer >= 1 || sdata == 0 ||
	    sdata->Loading.stage != LOAD_IDLE || !LOAD_IsOpen_RacingOrBattle())
		return AP_DL_WIN_OUTSIDE;
	return (gGT->gameMode1 & PAUSE_ALL) != 0 ? AP_DL_WIN_PAUSED : AP_DL_WIN_LIVE;
}

void AP_DeathLinkTick(struct GameTracker *gGT)
{
	struct Driver *local;
	int window, raceActive, maskGrabNow, action;
	char cause[128];
	char source[64];

	if (gGT == 0)
		return;

	// Live preference sync: menu-row and ini edits all land here. Must run
	// even when the effective mode is OFF so a toggle-off withdraws the tag.
	AP_DeathLinkSyncTag();

	if (g_dl_send_cooldown > 0)
		g_dl_send_cooldown--;

	if (!ctr_cfg_active() || AP_DeathLinkActive() == 0)
	{
		// Feature off: keep edge/receive state clean so a later opt-in starts fresh.
		// g_dl_race_attempt is NOT cleared: DeathLink config becoming inactive must
		// not re-enable the failed attempt's finish checks.
		g_dl_prev_maskgrab = 0;
		g_dl_pending_recv = 0;
		g_dl_grace_left = 0;
		g_dl_defer_logged = 0;
		g_dl_hold_logged = 0;
		g_dl_swallow_edge = 0;
		(void)ap_net_deathlink_take(cause, sizeof cause, source, sizeof source);
		return;
	}

	local = gGT->drivers[0];

	// Drain the network inbound latch. A death is handled the frame it arrives: it
	// applies inside a live race, is held while that race is paused, or is dropped. Two deaths in one grace window
	// collapse to the newer one (the older is dropped without its own popup).
	if (ap_net_deathlink_take(cause, sizeof cause, source, sizeof source) &&
	    AP_DeathLinkEffMode() != CTR_DL_OFF)
	{
		g_dl_pending_recv = 1;
		g_dl_grace_left = AP_DL_GRACE_FRAMES;
		g_dl_defer_logged = 0; // a new death gets its own deferral line
		g_dl_hold_logged = 0;
		snprintf(g_dl_pending_cause, sizeof g_dl_pending_cause, "%s", cause);
		snprintf(g_dl_pending_source, sizeof g_dl_pending_source, "%s", source);
	}

	window = AP_DeathLinkWindow(gGT);
	raceActive = window == AP_DL_WIN_LIVE;

	// #286 receive decision. Mode 3 (race_loss) ends the attempt here; modes 1/2
	// keep the mask-reset path (applied from the physics pipeline). Outside a live
	// adventure race the death is dropped.
	action = AP_DeathLinkReceiveDecision(
	    AP_DeathLinkEffMode(), g_dl_pending_recv,
	    (gGT->gameMode1 & ADVENTURE_MODE) != 0, window);
	if (action == AP_DL_RECV_DROP)
	{
		AP_DeathLinkDrop("not in a race");
	}
	else if (action == AP_DL_RECV_HOLD)
	{
		// Paused mid-race: hold this one death for the pause only. The grace
		// countdown does not run; the next live frame applies it, and leaving
		// the race (quit, restart, exit) drops it through the OUTSIDE branch.
		if (!g_dl_hold_logged)
		{
			g_dl_hold_logged = 1;
			AP_LogLine("[AP DEATH] received while paused -> held until the race resumes\n");
		}
	}
	else if (action == AP_DL_RECV_RACE_LOSS && local != 0)
	{
		AP_DeathLinkApplyRaceLoss(gGT, local);
		// Consumed (pending cleared) when it landed; otherwise grace, then drop.
		if (g_dl_pending_recv && AP_DeathLinkGraceStep(&g_dl_grace_left))
			AP_DeathLinkDrop("could not apply");
		return;
	}
	else if (action != AP_DL_RECV_WAIT && g_dl_pending_recv &&
	         AP_DeathLinkGraceStep(&g_dl_grace_left))
	{
		AP_DeathLinkDrop("could not apply");
	}

	// A received mask-reset death is APPLIED by AP_DeathLinkForceReset from inside the
	// physics pipeline (COLL_FIXED_PlayerSearch), NOT here: the request bit set from
	// AP_OnFrame is zeroed by VehPhysForce_OnApplyForces before the mask-grab gate
	// reads it. That path clears g_dl_pending_recv and arms g_dl_swallow_edge; the
	// send edge below then swallows the resulting mask-grab.

	// Send trigger: rising edge into KS_MASK_GRABBED (fell off / eaten), the
	// mask_grab trigger. A forced-reset edge is swallowed once.
	// A genuine mask-grab only happens mid-race, so the send is gated on raceActive
	// too -- belt-and-suspenders against a spurious edge outside a live race.
	maskGrabNow = (local != 0 && local->kartState == KS_MASK_GRABBED) ? 1 : 0;
	if (maskGrabNow && !g_dl_prev_maskgrab)
	{
		if (g_dl_swallow_edge)
			g_dl_swallow_edge = 0; // received death caused this edge: never send
		else if (raceActive &&
		         AP_DeathLinkTriggerSends(AP_DeathLinkSendMaskNow(), AP_DL_TRIG_FALL,
		                                  AP_RaceAttemptIsForcedLoss()))
			AP_DeathLinkFireLocal(gGT, "wiped out");
	}
	g_dl_prev_maskgrab = maskGrabNow;
}

// Apply a received mask-reset death that is inside the grace window. Called from INSIDE COLL_FIXED_PlayerSearch, right
// before the stock mask-grab gate (game/COLL.c), so the request bit we OR survives
// to that gate: VehPhysForce_OnApplyForces zeroes collisionFlags every frame before
// this function runs, so setting the bit from AP_OnFrame (as the first cut did) is
// wiped and the reset never fires. This mirrors the AP_ShortcutCheck / kill-plane
// precedents, which also set the bit from within this pipeline stage.
//
// Returns 1 (caller then OR's DRIVER_COLL_FLAG_MASK_GRAB_REQUEST) only when EVERY
// stock-gate precondition (COLL.c:1706) is already satisfied, so the grab is
// guaranteed to reach VehStuckProc_MaskGrab_Init this frame. That is what lets us
// consume the death and arm the no-loop guard here: we never arm the guard
// (which would swallow a later genuine send) for a grab that fails to land.
int AP_DeathLinkForceReset(struct Driver *d)
{
	struct GameTracker *gGT;
	char msg[192];

	if (!ctr_cfg_active() || AP_DeathLinkEffMode() == CTR_DL_OFF)
		return 0;
	// #286: race_loss is applied in AP_DeathLinkTick (rank reorder + retail end),
	// never as a mask reset. Leave the pending death for that path.
	if (AP_DeathLinkEffMode() == CTR_DL_RACE_LOSS)
		return 0;
	if (!g_dl_pending_recv || d == 0)
		return 0;
	if (sdata == 0 || sdata->gGT == 0)
		return 0;
	gGT = sdata->gGT;
	if (d != gGT->drivers[0])
		return 0; // local player only
	if ((gGT->gameMode1 & ADVENTURE_MODE) == 0)
		return 0; // receive, like send, only in adventure mode

	// Same live-race window as the tick's decision (hub, menus, loading, countdown,
	// pause and results are all outside it). A death outside it was already dropped
	// by AP_DeathLinkTick; this is the same test on the physics frame.
	if (AP_DeathLinkWindow(gGT) != AP_DL_WIN_LIVE)
		return 0;

	// Mirror the stock gate's OWN preconditions so OR'ing the bit is guaranteed to
	// fire VehStuckProc_MaskGrab_Init this frame (checked here, one line before the
	// gate, so the values match what the gate sees). Only then is it safe to consume
	// the death (else the tick drops it after the grace window) and arm the guard.
	if (d->kartState == KS_MASK_GRABBED || d->lastValid == 0 ||
	    (sdata->HudAndDebugFlags & 0x1000) != 0 ||
	    (d->stepFlagSet & COLL_STEP_TRIGGER_SUPPRESS_MASK_GRAB) != 0)
		return 0;

	AP_DeathLinkPopup(1, 0);
	g_dl_pending_recv = 0;
	g_dl_grace_left = 0;
	g_dl_swallow_edge = 1; // no-loop guard: the resulting mask-grab edge must not send
	g_dl_send_cooldown = AP_DL_COOLDOWN_AFTER_RECV; // forced grab multi-edges: mute them all
	snprintf(msg, sizeof msg, "[AP DEATH] received -> forced mask reset (%s)\n",
	         g_dl_pending_cause[0] ? g_dl_pending_cause : "a death");
	AP_LogLine(msg);
	g_dl_pending_cause[0] = '\0';
	g_dl_pending_source[0] = '\0';
	return 1;
}

// ── race_loss sends ──
// The cause phrase for a loss send ("<slot> <phrase>" on the wire).
static const char *AP_DeathLinkLossCause(int cause)
{
	switch (cause)
	{
	case AP_DL_CAUSE_LOST_RACE:    return "lost the race";
	case AP_DL_CAUSE_LOST_CRYSTAL: return "failed the crystal challenge";
	case AP_DL_CAUSE_LOST_CUP:     return "lost the gem cup";
	case AP_DL_CAUSE_RESTART:      return "restarted the race";
	case AP_DL_CAUSE_QUIT:         return "quit the race";
	}
	return "wiped out";
}

static void AP_DeathLinkSendLoss(struct GameTracker *gGT, int event, int lost, int inLiveRace)
{
	int cause = AP_DeathLinkLossSendDecision(
	    AP_DeathLinkSendMaskNow(), gGT != 0 && (gGT->gameMode1 & ADVENTURE_MODE) != 0,
	    AP_RaceAttemptIsForcedLoss(), event, lost, inLiveRace);

	if (cause == AP_DL_CAUSE_NONE)
		return;
	AP_DeathLinkFireLocal(gGT, AP_DeathLinkLossCause(cause));
}

// The local race ended (called from MainGameEnd_Initialize once per race, before
// the forced-loss path's own latch could matter: a received death arms the latch
// first, so its result never sends). Uses the game's own loss result: rank for a
// trophy / boss / CTR Challenge race (MainGameEnd_UpdateAdventureLosses), crystals
// collected for a Crystal Challenge (CC_EndEvent_DrawMenu didLose). A Gem Cup leg
// and a Relic Race are not a loss here.
void AP_DeathLinkOnRaceEnd(struct GameTracker *gGT, struct Driver *player)
{
	int crystal, lost;

	if (gGT == 0 || player == 0)
		return;
	crystal = (gGT->gameMode1 & CRYSTAL_CHALLENGE) != 0;
	lost = AP_DeathLinkRaceEndLost((gGT->gameMode1 & ADVENTURE_MODE) != 0,
	                               (gGT->gameMode1 & ADVENTURE_CUP) != 0,
	                               (gGT->gameMode1 & RELIC_RACE) != 0, crystal,
	                               (int)player->driverRank, (int)player->numCrystals,
	                               (int)gGT->numCrystalsInLEV);
	AP_DeathLinkSendLoss(gGT, AP_DL_EV_RACE_END, lost, 0);
}

// The Gem Cup standings closed with the cup lost (UI_CupStandings). Never called
// for a single lost leg.
void AP_DeathLinkOnCupLost(void)
{
	if (sdata == 0 || sdata->gGT == 0)
		return;
	AP_DeathLinkSendLoss(sdata->gGT, AP_DL_EV_CUP_END, 1, 0);
}

// RESTART (quit == 0) or EXIT TO MAP (quit == 1) chosen in the pause menu. Counts
// only while a race is on: not on the results screen, and never right after a
// received death (the latch is set).
void AP_DeathLinkOnPauseLeave(int quit)
{
	struct GameTracker *gGT;
	int inRace;

	if (sdata == 0 || sdata->gGT == 0)
		return;
	gGT = sdata->gGT;
	inRace = (gGT->gameMode1 & (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE)) == 0 &&
	         LOAD_IsOpen_RacingOrBattle() != 0;
	AP_DeathLinkSendLoss(gGT, quit ? AP_DL_EV_PAUSE_QUIT : AP_DL_EV_PAUSE_RESTART, 1, inRace);
}

#endif // CTR_AP
