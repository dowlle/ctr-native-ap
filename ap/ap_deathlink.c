#ifdef CTR_AP

#include <common.h> // Driver / GameTracker + sdata + kartState + gameMode flags
#include <stdio.h>
#include <string.h>

#include "ap_deathlink.h"
#include "ap_hooks.h"   // AP_LogLine
#include "ap_net.h"     // ap_net_deathlink_enable / _send / _take, ap_net_is_connected
#include "ap_seedcfg.h" // ctr_cfg, ctr_cfg_active

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

// No-loop guard: armed the frame a RECEIVED death forces the mask grab; consumed
// by the next mask-grab rising edge so that forced reset never sends. No timeout:
// a never-consumed latch (forced grab that never materialised) at worst swallows
// one later genuine mask-grab -- a MISSED send, which is safe. Expiring it early
// would risk exactly the outgoing-send-on-received-death loop this must prevent.
static int g_dl_swallow_edge = 0;

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

	if (!ctr_cfg_active() || ctr_cfg.death_link == CTR_DL_OFF)
		return;
	if (gGT == 0 || (gGT->gameMode1 & ADVENTURE_MODE) == 0)
		return; // sends only from adventure mode
	if (!ap_net_is_connected())
		return;

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
	snprintf(msg, sizeof msg, "[AP DEATH] sent: %s\n", cause);
	AP_LogLine(msg);
}

// any_hit send hook (VehPickState_NewState). Only the any_hit tier sends on hits;
// mask-grab (damageType 5) is owned by the edge detector so it is never doubled.
void AP_DeathLinkOnHit(struct Driver *victim, int damageType, int reason)
{
	struct GameTracker *gGT;

	if (!ctr_cfg_active() || ctr_cfg.death_link != CTR_DL_ANY_HIT)
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

	AP_DeathLinkFireLocal(gGT, AP_DeathLinkHitCause(damageType, reason));
}

void AP_DeathLinkConnectReset(void)
{
	g_dl_prev_maskgrab = 0;
	g_dl_amnesty_count = 0;
	g_dl_pending_recv = 0;
	g_dl_pending_cause[0] = '\0';
	g_dl_swallow_edge = 0;

	// Opt in to the DeathLink tag for this seed. Safe to call unconditionally on a
	// fresh connect: ctr_cfg was parsed in the slot-connected handler just before
	// AP_NetTick's reset block runs.
	if (ctr_cfg_active() && ctr_cfg.death_link != CTR_DL_OFF)
	{
		ap_net_deathlink_enable();
		AP_LogLine("[AP DEATH] DeathLink enabled for this seed\n");
	}
}

void AP_DeathLinkTick(struct GameTracker *gGT)
{
	struct Driver *local;
	int raceActive, maskGrabNow;
	char cause[128];

	if (gGT == 0)
		return;

	if (!ctr_cfg_active() || ctr_cfg.death_link == CTR_DL_OFF)
	{
		// Feature off: keep edge/queue state clean so a later opt-in starts fresh.
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

	// Race window (the trap-window idiom, ap_traps.c:301-306): mid-race, lights
	// out, not paused / menu / cutscene / end-of-race.
	raceActive = (gGT->gameMode1 &
	              (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) == 0 &&
	             gGT->trafficLightsTimer < 1;

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
// stock-gate precondition (COLL.c:1694) is already satisfied, so the grab is
// guaranteed to reach VehStuckProc_MaskGrab_Init this frame. That is what lets us
// clear the depth-1 queue and arm the no-loop guard here: we never arm the guard
// (which would swallow a later genuine send) for a grab that fails to land.
int AP_DeathLinkForceReset(struct Driver *d)
{
	struct GameTracker *gGT;
	int raceActive;
	char msg[192];

	if (!ctr_cfg_active() || ctr_cfg.death_link == CTR_DL_OFF)
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
	snprintf(msg, sizeof msg, "[AP DEATH] received -> forced mask reset (%s)\n",
	         g_dl_pending_cause[0] ? g_dl_pending_cause : "a death");
	AP_LogLine(msg);
	return 1;
}

#endif // CTR_AP
