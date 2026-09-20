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
// contract (send tiers, receive = forced mask reset, out-of-race depth-1 queue,
// and the hard no-loop guard). The tagged-Bounce transport is in ap_net.cpp.
// ============================================================================

// ── Send-edge state ──
static int g_dl_prev_maskgrab = 0; // rising-edge latch on the local KS_MASK_GRABBED
static int g_dl_amnesty_count  = 0; // eligible deaths counted toward the amnesty N

// ── Receive state ──
static int  g_dl_pending_recv = 0;         // depth-1 inbound queue (extras dropped)
static char g_dl_pending_cause[128] = {0}; // last inbound cause (log/flavour only)

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

// Effective mode: the config preference wins over the seed option. The forced
// values ARE the tier enum (1 = mask_reset, 2 = any_hit, 3 = race_loss), so all
// in-game DeathLink layers stay selectable when forcing on.
static int AP_DeathLinkEffMode(void)
{
	if (g_config.deathLink == 0)
		return CTR_DL_OFF;
	if (g_config.deathLink == CTR_DL_MASK_RESET ||
	    g_config.deathLink == CTR_DL_ANY_HIT ||
	    g_config.deathLink == CTR_DL_RACE_LOSS)
		return g_config.deathLink;
	return ctr_cfg_active() ? ctr_cfg.death_link : CTR_DL_OFF; // -1: follow the seed
}

int AP_DeathLinkActive(void)
{
	return AP_DeathLinkEffMode() != CTR_DL_OFF;
}

// #286 authoritative attempt predicate. Every result-derived producer reads this,
// so the failed attempt's finish checks and the genuine later attempt's checks
// can never disagree about which attempt owns them.
int AP_RaceAttemptIsForcedLoss(void)
{
	return AP_RaceAttempt_IsForcedLoss(&g_dl_race_attempt);
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

	if (!ctr_cfg_active() || AP_DeathLinkEffMode() == CTR_DL_OFF)
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

// any_hit send hook (VehPickState_NewState). Only the any_hit tier sends on hits;
// mask-grab (damageType 5) is owned by the edge detector so it is never doubled.
void AP_DeathLinkOnHit(struct Driver *victim, int damageType, int reason)
{
	struct GameTracker *gGT;

	if (!ctr_cfg_active() || AP_DeathLinkEffMode() != CTR_DL_ANY_HIT)
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
	g_dl_pending_cause[0] = '\0';
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

// #286: end the current adventure race attempt as a retail last-place loss.
// The rank permutation is validated BEFORE anything mutates, so an invalid or
// duplicate ordering logs and defers without consuming the queued death. On
// success the latch is armed before MainGameEnd_Initialize, so every result
// callback in the retail end sequence observes the forced loss.
static void AP_DeathLinkApplyRaceLoss(struct GameTracker *gGT, struct Driver *local)
{
	int order[8];
	int racers = (int)(u8)gGT->numPlyrCurrGame + (int)(u8)gGT->numBotsNextGame;
	int oldRank = local->driverRank;
	char cause[128];
	char msg[192];
	int r;

	if (racers < 1)
		racers = 1;
	if (racers > 8)
		racers = 8;

	if (oldRank < 0 || oldRank >= racers ||
	    !AP_DeathLinkBuildOrder(gGT, racers, order) ||
	    order[oldRank] != (int)local->driverID ||
	    !AP_RaceAttempt_ApplyLastPlaceSwap(racers, oldRank, order))
	{
		AP_LogLine("[AP DEATH] race loss deferred: invalid rank ordering\n");
		return; // do not consume the death, do not arm the latch
	}

	// Write the bijective permutation back: every participating driver occurs
	// exactly once and ranks are exactly 0..N-1.
	for (r = 0; r < racers; r++)
	{
		struct Driver *d = gGT->drivers[order[r]];
		d->driverRank = r;
		gGT->driversInRaceOrder[r] = d;
	}

	snprintf(cause, sizeof cause, "%s",
	         g_dl_pending_cause[0] ? g_dl_pending_cause : "a death");

	// Consume the queued death and arm the attempt latch BEFORE the retail end
	// sequence.
	g_dl_pending_recv = 0;
	g_dl_pending_cause[0] = '\0';
	g_dl_send_cooldown = AP_DL_COOLDOWN_AFTER_RECV;
	AP_RaceAttempt_ArmForcedLoss(&g_dl_race_attempt);

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

void AP_DeathLinkTick(struct GameTracker *gGT)
{
	struct Driver *local;
	int raceActive, maskGrabNow, action;
	char cause[128];

	if (gGT == 0)
		return;

	// Live preference sync: menu-row and ini edits all land here. Must run
	// even when the effective mode is OFF so a toggle-off withdraws the tag.
	AP_DeathLinkSyncTag();

	if (g_dl_send_cooldown > 0)
		g_dl_send_cooldown--;

	if (!ctr_cfg_active() || AP_DeathLinkEffMode() == CTR_DL_OFF)
	{
		// Feature off: keep edge/queue state clean so a later opt-in starts fresh.
		// g_dl_race_attempt is NOT cleared: DeathLink config becoming inactive must
		// not re-enable the failed attempt's finish checks.
		g_dl_prev_maskgrab = 0;
		g_dl_pending_recv = 0;
		g_dl_swallow_edge = 0;
		return;
	}

	local = gGT->drivers[0];

	// Drain the network depth-1 inbound latch into the game-side queue. Depth 1 on
	// both sides => multiple deaths arriving before we can apply one collapse to a
	// single reset (extras dropped, per the ruling).
	if (ap_net_deathlink_take(cause, sizeof cause))
	{
		g_dl_pending_recv = 1;
		snprintf(g_dl_pending_cause, sizeof g_dl_pending_cause, "%s", cause);
	}

	// Race window (the trap-window idiom, ap_traps.c:313-318): mid-race, lights
	// out, not paused / menu / cutscene / end-of-race.
	raceActive = (gGT->gameMode1 &
	              (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) == 0 &&
	             gGT->trafficLightsTimer < 1;

	// #286 receive decision. Mode 3 (race_loss) ends the attempt here; modes 1/2
	// keep the existing mask-reset path (applied later from the physics pipeline).
	// Anything else, or any out-of-window state, leaves the death queued.
	action = AP_DeathLinkReceiveDecision(
	    AP_DeathLinkEffMode(), g_dl_pending_recv,
	    (gGT->gameMode1 & ADVENTURE_MODE) != 0,
	    raceActive && LOAD_IsOpen_RacingOrBattle());
	if (action == AP_DL_RECV_RACE_LOSS && local != 0)
	{
		AP_DeathLinkApplyRaceLoss(gGT, local);
		return;
	}

	// A queued received death is APPLIED by AP_DeathLinkForceReset from inside the
	// physics pipeline (COLL_FIXED_PlayerSearch), NOT here: the request bit set from
	// AP_OnFrame is zeroed by VehPhysForce_OnApplyForces before the mask-grab gate
	// reads it. That path clears g_dl_pending_recv and arms g_dl_swallow_edge; the
	// send edge below then swallows the resulting mask-grab.

	// Send trigger: rising edge into KS_MASK_GRABBED (fell off / eaten). This fires
	// in BOTH tiers (mask_reset and any_hit). A forced-reset edge is swallowed once.
	// A genuine mask-grab only happens mid-race, so the send is gated on raceActive
	// too -- belt-and-suspenders against a spurious edge outside a live race.
	maskGrabNow = (local != 0 && local->kartState == KS_MASK_GRABBED) ? 1 : 0;
	if (maskGrabNow && !g_dl_prev_maskgrab)
	{
		if (g_dl_swallow_edge)
			g_dl_swallow_edge = 0; // received death caused this edge: never send
		else if (raceActive)
			AP_DeathLinkFireLocal(gGT, "wiped out");
	}
	g_dl_prev_maskgrab = maskGrabNow;
}

// Apply a queued received death. Called from INSIDE COLL_FIXED_PlayerSearch, right
// before the stock mask-grab gate (game/COLL.c), so the request bit we OR survives
// to that gate: VehPhysForce_OnApplyForces zeroes collisionFlags every frame before
// this function runs, so setting the bit from AP_OnFrame (as the first cut did) is
// wiped and the reset never fires. This mirrors the AP_ShortcutCheck / kill-plane
// precedents, which also set the bit from within this pipeline stage.
//
// Returns 1 (caller then OR's DRIVER_COLL_FLAG_MASK_GRAB_REQUEST) only when EVERY
// stock-gate precondition (COLL.c:1706) is already satisfied, so the grab is
// guaranteed to reach VehStuckProc_MaskGrab_Init this frame. That is what lets us
// clear the depth-1 queue and arm the no-loop guard here: we never arm the guard
// (which would swallow a later genuine send) for a grab that fails to land.
int AP_DeathLinkForceReset(struct Driver *d)
{
	struct GameTracker *gGT;
	int raceActive;
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

	// Race window, plus a real-track guard the send window does not need. The
	// gameMode1 / trafficLightsTimer test alone is not enough on the receive side. The
	// adventure hub itself passes it a few seconds after spawn: MainGameStart sets
	// START_OF_RACE and trafficLightsTimer=0xf00 on hub entry, CAM.c:1675 clears
	// START_OF_RACE when the fly-in ends, and MainMain.c counts the timer below 1 in
	// ~4s, while ADVENTURE_MODE stays set all session. A queued death would otherwise
	// fire while free-roaming the hub. LOAD_IsOpen_RacingOrBattle() (overlayIndex_Threads
	// == 1) is false in the hub (overlay 2); it is the same predicate the stock mask-grab
	// subsystem uses to decide it is on a track (VehStuckProc.c:451), the subsystem this
	// forced reset drives. It fires the death at the next race start (lap 1, lights out)
	// per the ruling. The trap lapWindow (ap_traps.c:311, lapIndex >= 1) also excludes the
	// hub but would wrongly delay the death to lap 2. Out-of-race deaths stay queued.
	raceActive = (gGT->gameMode1 &
	              (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) == 0 &&
	             gGT->trafficLightsTimer < 1 && LOAD_IsOpen_RacingOrBattle();
	if (!raceActive)
		return 0;

	// Mirror the stock gate's OWN preconditions so OR'ing the bit is guaranteed to
	// fire VehStuckProc_MaskGrab_Init this frame (checked here, one line before the
	// gate, so the values match what the gate sees). Only then is it safe to consume
	// the queue and arm the guard.
	if (d->kartState == KS_MASK_GRABBED || d->lastValid == 0 ||
	    (sdata->HudAndDebugFlags & 0x1000) != 0 ||
	    (d->stepFlagSet & COLL_STEP_TRIGGER_SUPPRESS_MASK_GRAB) != 0)
		return 0;

	g_dl_pending_recv = 0;
	g_dl_swallow_edge = 1; // no-loop guard: the resulting mask-grab edge must not send
	g_dl_send_cooldown = AP_DL_COOLDOWN_AFTER_RECV; // forced grab multi-edges: mute them all
	snprintf(msg, sizeof msg, "[AP DEATH] received -> forced mask reset (%s)\n",
	         g_dl_pending_cause[0] ? g_dl_pending_cause : "a death");
	AP_LogLine(msg);
	return 1;
}

#endif // CTR_AP
