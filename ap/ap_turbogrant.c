#ifdef CTR_AP

#include <stdio.h>
#include <string.h>

#include <common.h> // structs Driver/GameTracker + sdata + gameMode/action flags

#include "ap_turbogrant.h"
#include "ap_turbogrant_logic.h" // the per-race latch + the ruled gate
#include "ap_hooks.h"            // AP_LogLine (non-static log shim)
#include "ap_net.h"              // location membership probe + seed/slot identity
#include "ap_seedcfg.h"          // ctr_cfg_active

// ============================================================================
// Turbo Grant (#224, a Turbo every race since #487) -- implementation.
// ap_turbogrant.h holds the design contract and ap_turbogrant_logic.h the pure
// rules; this file is engine glue plus the delivery gate.
// ============================================================================

// The itemsanity location block's first code. Membership in the slot's location
// set is the authoritative "itemsanity is on for this seed" signal -- the same
// probe #223 and the #145 native half use.
#define AP_TURBOGRANT_ITEMSANITY_PROBE_CODE 35016000L

// Receipts, rebuilt from zero by every fresh connect's authoritative replay.
// Since #487 any non-zero count means "a Turbo every race"; the count is kept
// only for the log.
static int g_tg_received = 0;
// The itemsanity `Turbo` weapon receipt. Same lifecycle.
static int g_tg_turbo_weapon = 0;

// The per-attempt latch (ap_turbogrant_logic.h): the countdown of this attempt
// was observed on a live track, and whether its Turbo was already handed out.
// The countdown half closes the race-load gap, where the stale free-roam flags
// pass the plain race test for a few frames and anything written to the kart is
// then wiped by VehBirth. See ap_wumpa.c for the full account.
static AP_TurboGrantRace g_tg_race;

// ── AP item pipeline seam ──
void AP_TurboGrantReset(void)
{
	g_tg_received = 0;
	g_tg_turbo_weapon = 0;
	// The per-race latch is game state, not connection state: a reconnect in
	// the middle of a race must neither forget that the countdown was seen
	// (the race's Turbo would never come) nor that the Turbo was delivered
	// (it would come twice). AP_TurboGrantTick clears it off-track.
}

void AP_TurboGrantReceive(void)
{
	char msg[128];

	g_tg_received++;
	snprintf(msg, sizeof msg,
	         "[AP TURBO] Turbo Grant received (copies %d): a Turbo every race\n",
	         g_tg_received);
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
	struct Driver *local;
	char msg[160];

	if (gGT == 0)
		return;

	// Maintain the attempt latch every frame, whatever else this tick decides.
	onTrack = LOAD_IsOpen_RacingOrBattle();
	AP_TurboGrantRaceStep(&g_tg_race, onTrack, (gGT->gameMode1 & END_OF_RACE) != 0,
	                      gGT->trafficLightsTimer >= 1);

	if (!AP_TurboGrantOwedThisRace(&g_tg_race, g_tg_received))
		return;

	// The ruled itemsanity gate. Checked here rather than at receive time so the
	// race's Turbo simply waits for the `Turbo` weapon item.
	if (!AP_TurboGrantDeliverable(AP_TurboGrantItemsanityOn(), g_tg_turbo_weapon))
		return;

	// Race window: mid-race, countdown finished, not paused/menu/cutscene/EOR,
	// and on an actual track. Identical to the Wumpa filler's, for the reasons
	// documented there; a Turbo delivered anywhere else is either wiped by
	// VehBirth or handed to a kart that is not racing.
	raceActive = (gGT->gameMode1 &
	              (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) == 0 &&
	             gGT->trafficLightsTimer < 1 && onTrack;
	if (!raceActive)
		return;

	local = AP_TurboGrantLocalDriver(gGT);
	if (local == 0 || !AP_TurboGrantSlotFree(local))
		return;

	// Deliver this attempt's Turbo. The roll is skipped deliberately: the Turbo
	// lands in the slot immediately, and driving it through the 90-frame
	// roulette would mean the player watches a roll whose outcome is already
	// decided, with a window in which a weapon box could contend for the slot.
	local->heldItemID = AP_TURBOGRANT_HELD_ITEM_TURBO;
	g_tg_race.givenThisRace = 1;

	// The normal item-pickup ping, taken from the roll-completion site in
	// VehPhysProc rather than invented: the plain "ding" at 0x5e, or the juiced
	// "ka-ching" at 0x41 when the player is holding ten or more wumpa.
	if (local->numWumpas > 9)
		OtherFX_Play(0x41, 1);
	else
		OtherFX_Play(0x5e, 0);

	snprintf(msg, sizeof msg,
	         "[AP TURBO] race Turbo delivered to the weapon slot (copies %d)\n",
	         g_tg_received);
	AP_LogLine(msg);
}

#endif // CTR_AP
