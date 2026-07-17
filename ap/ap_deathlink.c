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

	// Apply a queued received death: force the full mask reset. Out-of-race deaths
	// wait here until the next race window; a mid-race death fires at once. Gate on
	// not-already-grabbed so the forced reset always produces a real, single edge
	// (which the guard below swallows).
	if (g_dl_pending_recv && raceActive && local != 0 &&
	    local->kartState != KS_MASK_GRABBED)
	{
		char msg[192];
		local->collisionFlags |= DRIVER_COLL_FLAG_MASK_GRAB_REQUEST; // COLL.c:1484 precedent
		g_dl_pending_recv = 0;
		g_dl_swallow_edge = 1; // no-loop guard: swallow the resulting mask-grab edge
		snprintf(msg, sizeof msg, "[AP DEATH] received -> forced mask reset (%s)\n",
		         g_dl_pending_cause[0] ? g_dl_pending_cause : "a death");
		AP_LogLine(msg);
	}

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

#endif // CTR_AP
