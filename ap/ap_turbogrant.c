#ifdef CTR_AP

#include <stdio.h>
#include <string.h>

#include <common.h> // structs Driver/GameTracker + sdata + gameMode/action flags

#include "ap_turbogrant.h"
#include "ap_turbogrant_logic.h" // the pure accounting + the ruled gate
#include "ap_hooks.h"            // AP_LogLine (non-static log shim)
#include "ap_net.h"              // location membership probe + seed/slot identity
#include "ap_seedcfg.h"          // ctr_cfg_active

// ============================================================================
// Turbo Grant (#224) -- implementation. ap_turbogrant.h holds the design
// contract and ap_turbogrant_logic.h the pure rules; this file is engine glue
// plus the delivery gate; the fired count itself is kept by ap_hooks.c (#299).
// ============================================================================

// The itemsanity location block's first code. Membership in the slot's location
// set is the authoritative "itemsanity is on for this seed" signal -- the same
// probe #223 and the #145 native half use.
#define AP_TURBOGRANT_ITEMSANITY_PROBE_CODE 35016000L

// The fired count lives in the room's DataStorage since #299 (ap_hooks.c,
// AP_FxTurbo*), so a fresh room of the same seed starts at zero and a reconnect
// to the same room reads back what was fired. The 0.2.1 local file
// (ctr-ap-turbogrant.txt, seed<TAB>slot<TAB>fired) is only read once, to migrate
// a room that has no key yet. See ap_fxmarker_logic.h.

// Receipts, rebuilt from zero by every fresh connect's authoritative replay.
static int g_tg_received = 0;
// The itemsanity `Turbo` weapon receipt. Same lifecycle.
static int g_tg_turbo_weapon = 0;
// A delivered Turbo sitting in the weapon slot, not yet fired. Session only.
static int g_tg_inflight = 0;

// Countdown latch, identical in purpose and maintenance to the Wumpa filler's:
// set once the lights sequence is OBSERVED on a live track, cleared off-track and
// at END_OF_RACE. It is what closes the race-load gap, where the stale free-roam
// flags pass the plain race test for a few frames and anything written to the
// kart is then wiped by VehBirth. See ap_wumpa.c for the full account.
static int g_tg_countdown_seen = 0;

// ── AP item pipeline seam ──
void AP_TurboGrantReset(void)
{
	g_tg_received = 0;
	g_tg_turbo_weapon = 0;
	g_tg_inflight = 0;
	g_tg_countdown_seen = 0;
}

void AP_TurboGrantReceive(void)
{
	char msg[128];

	g_tg_received++;
	snprintf(msg, sizeof msg,
	         "[AP TURBO] grant received (received %d, fired %d, pending %d)\n",
	         g_tg_received, AP_FxTurboFired(),
	         AP_TurboGrantPending(g_tg_received, AP_FxTurboFired(), g_tg_inflight));
	AP_LogLine(msg);
}

void AP_TurboGrantReceiveTurboWeapon(void)
{
	g_tg_turbo_weapon = 1;
}

// Is itemsanity on for this seed? Absent slot_data or a pre-0.2.0 seed answers
// "off", which is correct: neither can carry the weapon items either.
static int AP_TurboGrantItemsanityOn(void)
{
	return ctr_cfg_active() &&
	       ap_net_location_exists(AP_TURBOGRANT_ITEMSANITY_PROBE_CODE);
}

// The local human player. Single-player Adventure: drivers[0]. Same convention
// as the trap and Wumpa modules.
static struct Driver *AP_TurboGrantLocalDriver(struct GameTracker *gGT)
{
	if (gGT == 0)
		return 0;
	return gGT->drivers[0];
}

// Is the weapon slot genuinely free to receive an item this frame?
//
// The first two tests are the ruled precondition ("the weapon slot is empty").
// The rest are RB_Crate_Collide's own refusals, reused rather than re-derived:
// they are the conditions under which the engine itself declines to let a weapon
// box start a roll, and a grant that ignored them would be writing into a slot
// the engine considers busy. Failing any of them costs nothing -- the grant stays
// pending and the next frame tries again.
static int AP_TurboGrantSlotFree(struct Driver *d)
{
	if (d->heldItemID != AP_TURBOGRANT_HELD_ITEM_NONE)
		return 0; // holding something, or mid-roll (0x10)
	if (d->numHeldItems != 0)
		return 0;
	if (d->noItemTimer != 0)
		return 0; // an item is still expiring out of the slot
	if ((d->actionsFlagSet & ACTION_WEAPON_FIRE_REQUEST) != 0)
		return 0; // a fire is committed but not yet dispatched this frame
	if (d->thCloud != 0 &&
	    ((struct RainCloud *)d->thCloud->object)->effect == RAIN_CLOUD_EFFECT_ITEM_ROLL)
		return 0;
	if (d->clockReceive != 0)
		return 0;
	return 1;
}

// ── Per-frame lifecycle ──
void AP_TurboGrantTick(struct GameTracker *gGT)
{
	int raceActive;
	int onTrack;
	int pending;
	struct Driver *local;
	char msg[160];

	if (gGT == 0)
		return;

	// Maintain the latch every frame, whatever else this tick decides.
	onTrack = LOAD_IsOpen_RacingOrBattle();
	if (!onTrack || (gGT->gameMode1 & END_OF_RACE) != 0)
		g_tg_countdown_seen = 0;
	else if (gGT->trafficLightsTimer >= 1)
		g_tg_countdown_seen = 1;

	local = AP_TurboGrantLocalDriver(gGT);

	// Requeue check, BEFORE delivery and independent of the race window. An
	// in-flight grant is only in flight while the Turbo this module put in the
	// slot is still sitting there unfired. A death, a race restart (VehBirth
	// rewrites the slot to "no item"), a level change, a driver that stopped
	// existing, or any future effect that rerolls the inventory all show up here
	// as "the slot no longer holds our Turbo". Dropping the bit is the whole
	// requeue: pending derives one back on its own, and `fired` is untouched
	// because nothing was fired.
	//
	// numHeldItems is part of the test on purpose: the Turbo cheat also births a
	// kart holding item 0, but with nine of them, so without this an in-flight
	// grant would appear to survive a restart it did not survive.
	if (g_tg_inflight &&
	    (local == 0 || local->heldItemID != AP_TURBOGRANT_HELD_ITEM_TURBO ||
	     local->numHeldItems != 0))
	{
		g_tg_inflight = 0;
		AP_LogLine("[AP TURBO] in-flight grant left the slot unfired -> requeued\n");
	}

	// #299: until the room has said how many grants this slot already fired,
	// nothing is owed yet. Delivering now could hand out a grant a previous
	// session already fired.
	if (!AP_FxTurboFiredKnown())
		return;

	pending = AP_TurboGrantPending(g_tg_received, AP_FxTurboFired(), g_tg_inflight);
	if (pending <= 0)
		return;

	// The ruled itemsanity gate. Checked here rather than at receive time so a
	// grant that arrives before the `Turbo` weapon item simply waits for it.
	if (!AP_TurboGrantDeliverable(AP_TurboGrantItemsanityOn(), g_tg_turbo_weapon))
		return;

	// Race window: mid-race, countdown finished, not paused/menu/cutscene/EOR,
	// and on an actual track. Identical to the Wumpa filler's, for the reasons
	// documented there; a grant delivered anywhere else is either wiped by
	// VehBirth or handed to a kart that is not racing.
	raceActive = (gGT->gameMode1 &
	              (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) == 0 &&
	             gGT->trafficLightsTimer < 1 && onTrack && g_tg_countdown_seen;
	if (!raceActive)
		return;

	if (local == 0 || !AP_TurboGrantSlotFree(local))
		return;

	// Deliver exactly one. The roll is skipped deliberately: the issue asks for
	// the Turbo to land in the slot immediately, and driving it through the
	// 90-frame roulette would mean the player watches a roll whose outcome is
	// already decided, with a window in which a weapon box could contend for the
	// same slot.
	local->heldItemID = AP_TURBOGRANT_HELD_ITEM_TURBO;
	g_tg_inflight = 1;

	// The normal item-pickup ping, taken from the roll-completion site in
	// VehPhysProc rather than invented: the plain "ding" at 0x5e, or the juiced
	// "ka-ching" at 0x41 when the player is holding ten or more wumpa.
	if (local->numWumpas > 9)
		OtherFX_Play(0x41, 1);
	else
		OtherFX_Play(0x5e, 0);

	snprintf(msg, sizeof msg,
	         "[AP TURBO] grant delivered to the weapon slot "
	         "(received %d, fired %d, pending %d)\n",
	         g_tg_received, AP_FxTurboFired(),
	         AP_TurboGrantPending(g_tg_received, AP_FxTurboFired(), g_tg_inflight));
	AP_LogLine(msg);
}

void AP_TurboGrantOnFire(struct Driver *driver, int heldItemID)
{
	struct GameTracker *gGT = sdata->gGT;
	char msg[128];

	if (!g_tg_inflight)
		return;
	// Local player only, same isolation every other AP hook uses.
	if (gGT == 0 || driver == 0 || driver != gGT->drivers[0])
		return;
	if (heldItemID != AP_TURBOGRANT_HELD_ITEM_TURBO)
		return;

	// This is the ONLY place the fired count moves, and it moves at the engine's
	// committed-use choke point: the request flag has already been raised by
	// VehPhysProc, which only does so after spending the item, and the caller
	// clears it immediately, so this runs exactly once per use. Persisting here
	// rather than at delivery is what makes a crash or a quit between delivery
	// and firing hand the grant back instead of eating it.
	AP_FxTurboFiredIncrement(); // written to the room with `max` (#299)
	g_tg_inflight = 0;

	snprintf(msg, sizeof msg,
	         "[AP TURBO] grant fired (received %d, fired %d, pending %d)\n",
	         g_tg_received, AP_FxTurboFired(),
	         AP_TurboGrantPending(g_tg_received, AP_FxTurboFired(), g_tg_inflight));
	AP_LogLine(msg);
}

#endif // CTR_AP
