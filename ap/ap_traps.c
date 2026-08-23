#ifdef CTR_AP

#include <common.h> // structs Driver/GameTracker/GamepadBuffer/CameraDC + sdata + enums
#include <stdio.h>

#include "ap_traps.h"
#include "ap_hooks.h" // AP_LogLine, AP_FeedTrapLine, AP_DevKeysEnabled
#include "ap_deathlink.h" // AP_DeathLinkSuppressSelfInflicted, for Flatten's ruled
                          // self-inflicted attribution

// ============================================================================
// AP TRAP FRAMEWORK, engine half. See ap_traps.h for the contract and
// ap_trap_sched_logic.h for the rules. This file observes, presents and applies;
// it decides nothing about when a trap runs.
// ============================================================================

// Raw keyboard probe for the debug keybinds (platform/native_input.c, CTR_AP
// only). Same declaration ap_hooks.c uses, which keeps this module SDL-header-free.
int Platform_InputRawKeyDown(int scancode);

// Debug test-fire keys (SDL scancodes, externals/SDL/include/SDL3/SDL_scancode.h:
// KP_1=89..KP_6=94). Numpad, not F-keys: the engine's CTR_INTERNAL dev handlers
// own F1-F12 and fire regardless of AP keys. Not in the gameplay input map, so
// they never disturb driving.
//
// Every key now goes through the ORDINARY receive path. The old instant-fire
// shortcut bypassed the schedule it was supposed to be testing, and with per-effect
// scheduling there is no longer one shared lifecycle for it to skip.
#define AP_TRAP_KEY_ICY   89 // Numpad 1
#define AP_TRAP_KEY_GRAV  90 // Numpad 2
#define AP_TRAP_KEY_USF   91 // Numpad 3
#define AP_TRAP_KEY_BOOST 92 // Numpad 4
#define AP_TRAP_KEY_FP    93 // Numpad 5
#define AP_TRAP_KEY_ALL   94 // Numpad 6 -> arm all five

// First-person camera mode selector. cameraMode 0x10 = "first person" with yaw
// taken from the kart heading (d->angle), the stable hood-cam forward view
// (game/zGlobal_DATA.c:1037; implemented at game/CAM.c:2194-2210). 0 = normal
// chase. Restoring to chase also raises CAMERA_FLAG_DIRECTION_CHANGED so the chase
// cam re-seats instead of lerping from the head position (game/CAM.c convention).
#define AP_CAM_FIRSTPERSON 0x10
#define AP_CAM_CHASE       0

// Physics tuning. Gravity/friction are scaled by /256 fixed point.
#define AP_TRAP_GRAV_SCALE     74   // 74/256 ~= 0.29. Deliberately floatier than
                                    //  the engine's per-quad low-gravity factor,
                                    //  which computes 41/100 (VehPhysForce.c:104)
#define AP_TRAP_FRICTION_SCALE 40   // 40/256 ~= 0.16 grip -> icy
// Reserves FLOOR while a boost-control trap is active: reserves are only RAISED to
// this value when below it, never overwritten downward, so the per-frame decrement
// in VehPhysProc (VehPhysProc.c:111/628/2159) cannot zero the boost while banked
// reserves above the floor survive the trap.
#define AP_TRAP_BOOST_RESERVES 1200
// fireLevel of a super turbo pad (VehPhysForce.c:551), the input that makes
// VehFire_Increment (VehFire.c:358) compute a genuinely USF-tier fireSpeedCap.
#define AP_TRAP_USF_FIRELEVEL  0x800
#define AP_STICK_NEUTRAL       0x80 // analog centre (native_input.c:125-128)

// Reverse-recovery guard, shared by Forced Boost and Forced USF. Suppressing brake
// and analog reverse can wedge a kart against geometry with no way out, so both
// traps hand reverse back once the kart has been nearly stationary for about a
// second. High-speed braking is NOT handed back: the trap is still running.
//
// PROVISIONAL for Forced USF. The notebook rules Forced Boost's recovery
// explicitly and leaves Forced USF's open; applying the same guard is the only
// reading under which forced throttle cannot strand the player. Forced USF
// releases its forced throttle at the same moment, because reverse the player
// cannot reach is not a recovery path.
#define AP_TRAP_RECOVER_MS       1000
#define AP_TRAP_STATIONARY_SHIFT 4 // "nearly stationary" is under 1/16 top speed

// ── Wave 2 batch 1 constants ──
// The held-item sentinels, the slot classifier and the completion predicates
// live in ap/ap_trap_observe_logic.h, included via ap_traps.h, so the harness
// drives the same rules this file does. See that header for why the split
// exists and for the three review defects it pins.
//
// Roulette length a weapon box starts, reused so a trap reroll spins for exactly
// as long as a natural pickup (RB_Crate.c:288).
#define AP_TRAP_ROLL_FRAMES 90
// The juiced threshold the engine itself tests (VehPhysProc.c:156/327).
#define AP_TRAP_JUICED_WUMPA 10
// Warpball Ambush counts fifteen uninterrupted seconds in first place.
#define AP_TRAP_LEAD_MS 15000

static AP_TrapSched g_sched;
static int g_sched_ready = 0;

// Per-effect "is active this frame" cache, refreshed once per tick. The physics
// call-sites read this instead of scanning the registry, which keeps them O(1) and
// keeps their behaviour identical between two calls in one frame.
static unsigned char g_active[AP_TRAP_EFFECT_COUNT];

// Camera latch: 1 while we own cameraDC[0], so chase is restored exactly once.
static int g_fp_applied = 0;

// Map-boundary tracking. levelID alone is not enough: a restart and a Cup leg onto
// the same track keep the id, so the load itself has to count as a boundary.
static int g_epoch = 0;
static int g_prev_level = -1;
static int g_prev_load = LOAD_IDLE;

// Reverse-recovery accumulator, in milliseconds of near-stationary time.
static int g_recover_ms = 0;

// Reverse Steering applied its mirror to this frame's steering axis already.
// Cleared once per frame in AP_TrapTick; see AP_TrapDriveInput for why a mirror
// cannot simply be reapplied the way the boost-control writes can.
static int g_trap_steer_mirrored = 0;

static void AP_TrapEnsureReady(void)
{
	if (g_sched_ready)
		return;
	AP_TrapSchedReset(&g_sched);
	g_sched_ready = 1;
}

// The local human player. Single-player Adventure: drivers[0]. Guarded so the
// physics hooks stay safe outside a race, where drivers[] is not populated.
static struct Driver *AP_TrapLocalDriver(void)
{
	if (sdata == 0 || sdata->gGT == 0)
		return 0;
	return sdata->gGT->drivers[0];
}

static int AP_TrapIsLocal(struct Driver *driver)
{
	return driver != 0 && driver == AP_TrapLocalDriver();
}

static const char *AP_TrapName(int effect)
{
	if (effect < 0 || effect >= AP_TRAP_EFFECT_COUNT)
		return "?";
	return AP_TRAP_DESC[effect].name;
}

// ── World observation ──
//
// Which column of the settled activation-context matrix this map is. Order
// matters: a Crystal Challenge and a Cup leg both set flags an ordinary race also
// sets, so the more specific test has to come first. A map that is none of these
// (menus, loading, credits) reports 0, which no descriptor matches, so every armed
// copy simply stays armed.
static int AP_TrapContextOf(struct GameTracker *gGT, int loadStage)
{
	int gm1, gm2;

	if (gGT == 0 || loadStage != LOAD_IDLE)
		return 0;

	gm1 = gGT->gameMode1;
	gm2 = gGT->gameMode2;

	if ((gm1 & (MAIN_MENU | LOADING)) != 0)
		return 0;
	if ((gm1 & ADVENTURE_ARENA) != 0)
		return AP_TRAP_CTX_HUB;
	if ((gm1 & CRYSTAL_CHALLENGE) != 0)
		return AP_TRAP_CTX_CRYSTAL;
	if ((gm1 & BATTLE_MODE) != 0)
		return AP_TRAP_CTX_BATTLE;
	if ((gm1 & (RELIC_RACE | TIME_TRIAL)) != 0)
		return AP_TRAP_CTX_TIME_TRIAL;
	// A CTR Challenge is an ordinary Adventure race carrying the token objective,
	// so it is only distinguishable by TOKEN_RACE.
	if ((gm2 & TOKEN_RACE) != 0)
		return AP_TRAP_CTX_CTR_CHALLENGE;
	if ((gm2 & CUP_ANY_KIND) != 0)
		return AP_TRAP_CTX_CUP_LEG;
	// Standard and boss races share one column in the matrix.
	return AP_TRAP_CTX_RACE;
}

// Does the player genuinely have the kart? The starting countdown may show a
// warning but must not start an effect, which is the same test PlayLevel.c:338
// uses for lights-out.
static int AP_TrapControlUnlocked(struct GameTracker *gGT, struct Driver *local)
{
	if (gGT == 0)
		return 0;
	if ((gGT->gameMode1 & (START_OF_RACE | END_OF_RACE | MAIN_MENU | LOADING)) != 0)
		return 0;
	if (gGT->trafficLightsTimer >= 1)
		return 0;
	if (local == 0)
		return 0;
	return (local->actionsFlagSet & ACTION_RACE_FINISHED) == 0;
}

// Scripted camera or scripted control: anything that has taken the kart away from
// the player without ending the map. Ordinary respawns are deliberately absent,
// because the lifecycle continues through them.
static int AP_TrapScripted(struct GameTracker *gGT, struct Driver *local)
{
	if (gGT == 0)
		return 0;
	if ((gGT->gameMode1 & (GAME_CUTSCENE | AKU_SONG | UKA_SONG)) != 0)
		return 1;
	if ((gGT->gameMode2 & (VEH_FREEZE_PODIUM | VEH_FREEZE_DOOR)) != 0)
		return 1;
	if (local == 0)
		return 0;
	return local->kartState == KS_MASK_GRABBED || local->kartState == KS_WARP_PAD ||
	       local->kartState == KS_FREEZE;
}

static int AP_TrapFinishOrPodium(struct GameTracker *gGT, struct Driver *local)
{
	if (gGT == 0)
		return 0;
	if ((gGT->gameMode1 & END_OF_RACE) != 0)
		return 1;
	if ((gGT->gameMode2 & VEH_FREEZE_PODIUM) != 0)
		return 1;
	return local != 0 && (local->actionsFlagSet & ACTION_RACE_FINISHED) != 0;
}

// ── Conditional predicates ──
//
// One observer per AP_TrapCondition bit. Each answers "does the prerequisite
// genuinely hold this frame", nothing more: the scheduler decides what to do
// about it. An observer that cannot tell reports 0, which keeps the trap armed,
// because the shared ruling is that an unconsumable copy waits rather than
// discharging harmlessly.

// Is a fully resolved weapon in the slot? 0xF is the empty sentinel and 0x10 is
// the roulette placeholder (VehPhysProc.c:316/536, RB_Crate.c:286), and the roll
// is not settled until itemRollTimer reaches zero (VehPhysProc.c:320).
static int AP_TrapHeldItemResolved(struct Driver *local)
{
	if (local == 0)
		return 0;
	return AP_TrapHeldItemIsResolved((int)local->heldItemID, (int)local->itemRollTimer,
	                                 (int)local->noItemTimer);
}

// Would a fruit reset actually take anything away? Wumpa Wipeout is ruled to
// respect the engine's own protection and stay armed rather than fire
// harmlessly, so this mirrors the two early returns inside RB_Player_ModifyWumpa
// (RB_Player.c:146 CHEAT_WUMPA, RB_Player.c:151 ACTION_MASK_WEAPON) exactly. A
// mask GRAB is scripted control, which the shared lifecycle already suspends,
// but it is named here too so the predicate is true independently of that.
static int AP_TrapFruitProtected(struct GameTracker *gGT, struct Driver *local)
{
	if (gGT == 0 || local == 0)
		return 1;
	if ((gGT->gameMode2 & CHEAT_WUMPA) != 0)
		return 1;
	if ((local->actionsFlagSet & ACTION_MASK_WEAPON) != 0)
		return 1;
	return local->kartState == KS_MASK_GRABBED;
}

// The LEV instance table, handed over once per load from INSTANCE.c after every
// entry's ptrInstance is filled. Same seam and same reason as the Tizi helper's:
// there is no crate thread list to walk, because a crate has no thread at all
// until a driver first touches it (RB_Crate.c:195-223).
static struct InstDef *g_trap_lev_defs = 0;
static int g_trap_lev_count = 0;
static int g_trap_crate_epoch = -1;
static int g_trap_crate_present = 0;

void AP_TrapLevelInstances(struct InstDef *defs, int count)
{
	g_trap_lev_defs = defs;
	// Same guard shape as AP_TiziLevelInstances: a null table means no entries,
	// however large the count claims to be.
	g_trap_lev_count = (defs != 0 && count > 0) ? count : 0;
	g_trap_crate_epoch = -1; // force a fresh census on the next observation
}

// Forget the cached LEV table. The pointer is into the mempack arena, which a
// dev savestate restore rewrites wholesale: native_checkpoint.c relocates the
// pointer slots it knows about, and this cache is not one of them, so after a
// cross-level restore it would address whatever now occupies those bytes. There
// is nothing to relocate it TO either, because the restored level's InstDefs are
// only re-published when INSTANCE.c next instantiates a level. Dropping the
// cache is therefore the whole fix: the census answers "no eligible crates" and
// Empty Crates stays armed until the next real load re-publishes the table,
// which is the safe direction for a trap that is ruled to wait.
void AP_TrapForgetLevelInstances(void)
{
	g_trap_lev_defs = 0;
	g_trap_lev_count = 0;
	g_trap_crate_epoch = -1;
}

// Does the loaded map carry at least one crate Empty Crates would suppress?
//
// The census counts weapon boxes and Wumpa crates, and tests DRAW_COLLISION_MASK
// rather than the model id alone. That flag is exactly where the engine records
// its own per-mode crate filtering (INSTANCE.c:385-432): Time Trial and Relic
// Race clear it on both arcade crate models, and Crystal Challenge clears it on
// the fruit crate. Reading the flag therefore gives the matrix's "conditional:
// Wumpa crates" column for free and cannot drift from the engine's own rule.
//
// Cached per map epoch. The table only changes at a load, and walking a few
// thousand InstDefs every frame to answer a question with a per-map answer would
// be pure waste.
static int AP_TrapEligibleCrates(int epoch)
{
	struct InstDef *def;
	int i;

	if (g_trap_crate_epoch == epoch)
		return g_trap_crate_present;

	g_trap_crate_epoch = epoch;
	g_trap_crate_present = 0;

	def = g_trap_lev_defs;
	for (i = 0; i < g_trap_lev_count; i++, def++)
	{
		int modelID;
		if (def->model == 0 || def->ptrInstance == 0)
			continue;
		// Same low-half mask the Tizi census uses on model->id.
		modelID = (int)(def->model->id & 0xffff);
		if (modelID != PU_RANDOM_CRATE && modelID != PU_FRUIT_CRATE)
			continue;
		if ((def->ptrInstance->flags & DRAW_COLLISION_MASK) == 0)
			continue;
		g_trap_crate_present = 1;
		break;
	}
	return g_trap_crate_present;
}

// Continuous time the local player has held first place under active control,
// in milliseconds. Warpball Ambush is ruled to reset this completely on losing
// first place, and to EXCLUDE countdowns, pause, cutscenes and finish
// ceremonies from elapsed lead time, which is a freeze rather than a reset. The
// distinction is AP_TrapLeadAccumulate's, and is pinned in the harness.
static int g_trap_lead_ms = 0;

// Is there an AI racer that could still be told to fire? Finished, eliminated
// and empty slots are excluded, per the ruling. driverRank is 0-based, so first
// place is 0 (UI_Rank.c:86-87), and -1 means "not ranked this frame"
// (PlayLevel.c:247).
static int AP_TrapValidAiPresent(struct GameTracker *gGT, struct Driver *local)
{
	int i;
	for (i = 0; i < 8; i++)
	{
		struct Driver *d = gGT->drivers[i];
		if (d == 0 || d == local)
			continue;
		if ((d->actionsFlagSet & ACTION_BOT) == 0)
			continue;
		if ((d->actionsFlagSet & ACTION_RACE_FINISHED) != 0)
			continue;
		if (d->driverRank < 0)
			continue;
		return 1;
	}
	return 0;
}

static int AP_TrapAiLead(struct GameTracker *gGT, struct Driver *local, int counting,
                         int elapsedMs)
{
	// No world and no driver is not "the player was overtaken", so it freezes
	// like any other state the ruling excludes from elapsed time. The map and
	// session boundaries are what genuinely clear this timer.
	if (gGT == 0 || local == 0)
		return g_trap_lead_ms >= AP_TRAP_LEAD_MS;

	g_trap_lead_ms = AP_TrapLeadAccumulate(g_trap_lead_ms, (int)local->driverRank,
	                                       AP_TrapValidAiPresent(gGT, local), counting,
	                                       elapsedMs);
	return g_trap_lead_ms >= AP_TRAP_LEAD_MS;
}

// Satisfied conditional predicates, assembled once per frame. `counting` is the
// same "the player genuinely has the kart" test the scheduler applies before it
// lets anything fire; only the lead timer needs it, because it is the one
// predicate that accumulates rather than sampling.
static unsigned AP_TrapConditions(struct GameTracker *gGT, struct Driver *local,
                                  int epoch, int counting, int elapsedMs)
{
	unsigned bits = 0;

	if (local != 0 && (local->actionsFlagSet & ACTION_NEW_BOOST) != 0)
		bits |= AP_TRAP_COND_EARNED_BOOST;

	if (local != 0 && local->numWumpas >= AP_TRAP_JUICED_WUMPA &&
	    !AP_TrapFruitProtected(gGT, local))
		bits |= AP_TRAP_COND_TEN_WUMPA;

	if (AP_TrapHeldItemResolved(local))
		bits |= AP_TRAP_COND_HELD_ITEM;

	if (AP_TrapEligibleCrates(epoch))
		bits |= AP_TRAP_COND_ELIGIBLE_CRATES;

	if (AP_TrapAiLead(gGT, local, counting, elapsedMs))
		bits |= AP_TRAP_COND_AI_LEAD;

	return bits;
}

// ── Instant effects ──
//
// Every instant effect here carries AP_TRAP_DURATION_ENGINE_NATURAL, which means
// the scheduler holds the slot ACTIVE until the application code says the outcome
// landed. That report is AP_TrapSchedEffectDone, and it is what lets a serialized
// duplicate take its turn. Wumpa Wipeout reports on the spot because a fruit reset
// has no aftermath; Item Reroll waits for the roulette to settle, Forced Use waits
// for the weapon to actually leave the slot, and Flatten waits out the engine's
// own squish and spin recovery. All three of those are ruled to wait rather than
// be consumed unsuccessfully.

// Item Reroll: the weapon held when the trap fired, so the new roll can avoid it.
// -1 when no reroll is in flight.
static int g_trap_reroll_excluded = -1;
// Forced Use: what the player was holding when the trap fired, so the runtime can
// recognise the moment the engine actually consumed it.
static int g_trap_use_pending = 0;
static int g_trap_use_item = -1;
static int g_trap_use_count = 0;

// Flatten walks two stages, because the squish it asks for can be refused and
// then has an aftermath. PENDING means the copy has fired but the engine has not
// accepted the damage yet; APPLIED means it did, and the copy is waiting out the
// engine's own recovery plus the ruled grace interval.
static int g_trap_flatten_pending = 0;
static int g_trap_flatten_applied = 0;
static int g_trap_flatten_grace_ms = 0;

// The kart-state values ap_trap_observe_logic.h repeats as plain integers really
// are the engine's. If the decomp ever renumbers them, this stops the build here
// rather than letting Flatten silently mis-read a damage animation as safe.
typedef char ap_trap_ks_normal_matches[AP_TRAP_KS_NORMAL == KS_NORMAL ? 1 : -1];
typedef char ap_trap_ks_crashing_matches[AP_TRAP_KS_CRASHING == KS_CRASHING ? 1 : -1];
typedef char ap_trap_ks_drifting_matches[AP_TRAP_KS_DRIFTING == KS_DRIFTING ? 1 : -1];
typedef char ap_trap_ks_spinning_matches[AP_TRAP_KS_SPINNING == KS_SPINNING ? 1 : -1];
typedef char ap_trap_ks_maskgrab_matches[AP_TRAP_KS_MASK_GRABBED == KS_MASK_GRABBED ? 1 : -1];
typedef char ap_trap_ks_blasted_matches[AP_TRAP_KS_BLASTED == KS_BLASTED ? 1 : -1];
// The scripted states matter as much as the damage ones now that the gate is an
// allow-list: these are the values it must keep refusing.
typedef char ap_trap_ks_revving_matches[AP_TRAP_KS_ENGINE_REVVING == KS_ENGINE_REVVING ? 1 : -1];
typedef char ap_trap_ks_antivshift_matches[AP_TRAP_KS_ANTIVSHIFT == KS_ANTIVSHIFT ? 1 : -1];
typedef char ap_trap_ks_warppad_matches[AP_TRAP_KS_WARP_PAD == KS_WARP_PAD ? 1 : -1];
typedef char ap_trap_ks_freeze_matches[AP_TRAP_KS_FREEZE == KS_FREEZE ? 1 : -1];

// Give back the engine-wide bookkeeping a discarded weapon was holding.
//
// Two vanilla invariants are maintained by counters rather than by inspecting
// inventories: at most one Warpball in play (WARPBALL_HELD, claimed at
// VehPhysGeneral.c:1128-1130 and released when the object dies, RB_Warpball.c:38)
// and at most two drivers holding Missile x3 (numPlayersWith3Missiles, claimed at
// VehPhysGeneral.c:1160 and released in the slot-emptying block at
// VehPhysProc.c:358-371, as the post-use lockout expires). Deleting a
// held weapon outside the normal use path would leak both, and a leaked
// WARPBALL_HELD silently denies Warpball to the whole field for the rest of the
// race. The release conditions below are copied from those two release sites.
static void AP_TrapReleaseHeldItemClaims(struct GameTracker *gGT, struct Driver *local)
{
	if (local->heldItemID == 0x9)
		gGT->gameMode1 &= ~WARPBALL_HELD;

	if (local->heldItemID == 0xB && (u8)gGT->numPlyrCurrGame > 2 &&
	    (gGT->gameMode1 & BATTLE_MODE) == 0 && gGT->numPlayersWith3Missiles > 0)
		gGT->numPlayersWith3Missiles--;
}

// Item Reroll: discard the held weapon and start CTR's ordinary roulette, exactly
// as a weapon box does. The result is therefore drawn by the engine's own roll and
// passes the Itemsanity draw filter unchanged, which is what the ruling asks for:
// the seed's available weapon pool decides, and normal game rules decide whether
// the result is juiced.
static void AP_TrapStartReroll(struct GameTracker *gGT, struct Driver *local)
{
	AP_TrapReleaseHeldItemClaims(gGT, local);
	g_trap_reroll_excluded = (int)local->heldItemID;

	local->heldItemID = AP_TRAP_ITEM_ROLLING;
	local->numHeldItems = 0;
	local->noItemTimer = 0;
	local->itemRollTimer = AP_TRAP_ROLL_FRAMES;
	// Deliberately NOT touching PickupTimeboxHUD. A weapon box also seeds that
	// HUD's start coordinates from the crate's screen position (RB_Crate.c:315-320)
	// so the icon can fly in from the box that was broken. There is no box here,
	// and setting only the cooldown would animate it from whatever coordinates the
	// last real pickup left behind. The trap has its own presentation.
	if ((gGT->gameMode1 & ROLLING_ITEM) == 0)
		gGT->gameMode1 |= ROLLING_ITEM;
}

// Has the reroll settled? The roll resolves in VehPhysGeneral_SetHeldItem roughly
// 90 frames later, and only then has the trap delivered what it promised.
static void AP_TrapPollReroll(struct Driver *local)
{
	if (g_trap_reroll_excluded < 0)
		return;
	if (!AP_TrapSchedActive(&g_sched, AP_TRAP_ITEM_REROLL))
	{
		// The slot went away underneath us (map change, connect reset). Drop the
		// exclusion so it cannot leak onto an unrelated later roll.
		g_trap_reroll_excluded = -1;
		return;
	}
	if (local == 0)
		return;
	// A roll still in flight is the only reason to keep waiting. Anything else,
	// including the finish line confiscating the slot mid-spin, is done. See
	// AP_TrapRerollOutcome for why waiting on a resolve alone was a defect.
	if (AP_TrapRerollOutcome((int)local->heldItemID, (int)local->itemRollTimer,
	                         (int)local->noItemTimer) == AP_TRAP_OUTCOME_WAIT)
		return;
	g_trap_reroll_excluded = -1;
	AP_TrapSchedEffectDone(&g_sched, AP_TRAP_ITEM_REROLL);
}

// The exclusion the roll filter consults. Read once, at the moment the roll
// resolves; -1 means no reroll is in flight and the filter is inert.
int AP_TrapRerollExcludedItem(void)
{
	return g_trap_reroll_excluded;
}

// Forced Use: remember what has to leave the slot. The button injection itself
// happens in AP_TrapDriveInput, because the fire path reads a TAPPED button and
// the pad is only in hand there.
static void AP_TrapArmForcedUse(struct Driver *local)
{
	g_trap_use_pending = 1;
	g_trap_use_item = (int)local->heldItemID;
	g_trap_use_count = (int)local->numHeldItems;
}

// Is a forced use still owed? True while the trap is armed, active and its press
// has not yet landed. The map boundary consults this as well as the poll, so the
// two can never disagree about whether the copy was spent.
static int AP_TrapForcedUseOwed(struct Driver *local)
{
	if (!g_trap_use_pending || !AP_TrapSchedActive(&g_sched, AP_TRAP_FORCED_USE))
		return 0;
	// No driver to read means no evidence the use landed, and the ruling is to
	// wait rather than be consumed unsuccessfully, so the trap is still owed.
	if (local == 0)
		return 1;
	return AP_TrapForcedUseOutcome((int)local->heldItemID, (int)local->numHeldItems,
	                               (int)local->noItemTimer, g_trap_use_item,
	                               g_trap_use_count) == AP_TRAP_OUTCOME_WAIT;
}

// Did the forced use actually happen? The engine refuses the fire request in an
// incompatible driver state, during a respawn and while a TNT is on the player's
// head (VehPhysProc.c:485-489), so a consumed trap has to be proven rather than
// assumed. See AP_TrapForcedUseOutcome for what counts as proof and why an empty
// slot on its own does not.
static void AP_TrapPollForcedUse(struct Driver *local)
{
	if (!g_trap_use_pending)
		return;
	if (!AP_TrapSchedActive(&g_sched, AP_TRAP_FORCED_USE))
	{
		g_trap_use_pending = 0;
		return;
	}
	if (AP_TrapForcedUseOwed(local))
		return;
	g_trap_use_pending = 0;
	AP_TrapSchedEffectDone(&g_sched, AP_TRAP_FORCED_USE);
}

// ── Flatten ──
//
// Grace interval after the engine finishes recovering, before a serialized
// duplicate is allowed to warn. The ruling says "full recovery plus a short grace
// interval" without naming a number; one second is this implementation's reading
// of "short", chosen to match the warning length so two queued copies read as two
// distinct events rather than one long mangling.
#define AP_TRAP_FLATTEN_GRACE_MS 1000

// Is the local driver in a state that can accept a fresh squish right now?
static int AP_TrapFlattenCanApply(struct Driver *local)
{
	if (local == 0)
		return 0;
	return AP_TrapFlattenReady((int)local->kartState, (int)local->squishTimer,
	                           (int)local->invincibleTimer,
	                           (local->actionsFlagSet & ACTION_MASK_WEAPON) != 0,
	                           local->instBubbleHold != 0,
	                           (int)local->pendingDamageType);
}

// Hand the engine the squish.
//
// damageType 3 through VehPickState_NewState, which is the same entry the
// Blizzard Bluff and N. Gin Labs hazards reach through RB_Hazard_HurtDriver and
// the same one a turbo-landing driver reaches through pendingDamageType. Every
// part of the effect therefore comes from the engine: the squish sound, the
// 0.25 s input lock, the ~3.8 s squishTimer, the flattened model, the follow-up
// spinout and the FLATMAN/STEAMROLLER skill-point fields.
//
// NO ATTACKER, and that is a deliberate reading of "attribute the effect as
// self-inflicted without treating it as another player's weapon hit". Passing the
// victim as its own attacker is an engine idiom (PlayLevel.c:446 does exactly
// that for the out-of-bounds blast), but it also enters the attacker block at the
// tail of VehPickState_NewState, which calls RB_Player_KillPlayer and, under
// POINT_LIMIT, DECREMENTS the player's battle score. Docking a battle point is a
// second, larger punishment the ruling never asked for, so the trap passes null:
// the squish lands, and nobody is credited or charged for it. numTimesSquishedSomeone
// is untouched either way, since it needs both a non-null attacker and reason 5.
//
// Returns 1 when the engine accepted the damage.
static int AP_TrapApplyFlatten(struct Driver *local)
{
	int landed;

	if (!AP_TrapFlattenCanApply(local))
		return 0;

	// The ruling attributes this as self-inflicted and explicitly forbids sending
	// DeathLink for it, but the dispatch runs the any_hit send hook exactly as a
	// hazard would. Bracket the call rather than special-casing the hook.
	AP_DeathLinkSuppressSelfInflicted(1);
	landed = VehPickState_NewState(local, 3, 0, 0);
	AP_DeathLinkSuppressSelfInflicted(0);

	return landed != 0;
}

// Drive Flatten's two stages. Runs once per tick alongside the other completion
// polls, before the map boundary, for the same reason they do.
static void AP_TrapPollFlatten(struct Driver *local, int elapsedMs)
{
	if (!g_trap_flatten_pending && !g_trap_flatten_applied)
		return;
	if (!AP_TrapSchedActive(&g_sched, AP_TRAP_FLATTEN))
	{
		// Map change or connect reset took the slot; drop the stage machine.
		g_trap_flatten_pending = 0;
		g_trap_flatten_applied = 0;
		g_trap_flatten_grace_ms = 0;
		return;
	}

	if (g_trap_flatten_pending)
	{
		// Retry until the driver state becomes valid. A mask, a shield, a running
		// invincibility window or another damage animation all mean "not yet",
		// and the ruling is to wait for a valid state rather than consume the
		// copy on a squish that never happened.
		if (!AP_TrapApplyFlatten(local))
			return;
		g_trap_flatten_pending = 0;
		g_trap_flatten_applied = 1;
		g_trap_flatten_grace_ms = AP_TRAP_FLATTEN_GRACE_MS;
		return;
	}

	// Applied: wait out the engine's own squish and spin, then the grace interval,
	// before releasing the slot to a serialized duplicate.
	if (local == 0)
		return;
	if (!AP_TrapFlattenRecovered((int)local->kartState, (int)local->squishTimer))
	{
		g_trap_flatten_grace_ms = AP_TRAP_FLATTEN_GRACE_MS;
		return;
	}
	if (g_trap_flatten_grace_ms > 0)
	{
		g_trap_flatten_grace_ms -= elapsedMs;
		if (g_trap_flatten_grace_ms > 0)
			return;
	}
	g_trap_flatten_applied = 0;
	g_trap_flatten_grace_ms = 0;
	AP_TrapSchedEffectDone(&g_sched, AP_TRAP_FLATTEN);
}

// Apply one instant effect at the moment the scheduler fires it. Called from the
// event drain, which is the only place that sees the FIRE edge exactly once.
static void AP_TrapApplyInstant(struct GameTracker *gGT, struct Driver *local, int effect)
{
	if (gGT == 0 || local == 0)
		return;

	switch (effect)
	{
	case AP_TRAP_WUMPA_WIPEOUT:
		// Through the engine's own helper, so the clamp, the juiced-state
		// bookkeeping and the mask veto all behave exactly as they do for a
		// normal hit. The predicate has already established that nothing is
		// protecting the fruit, so this genuinely takes it.
		RB_Player_ModifyWumpa(local, -AP_TRAP_JUICED_WUMPA);
		AP_TrapSchedEffectDone(&g_sched, AP_TRAP_WUMPA_WIPEOUT);
		break;
	case AP_TRAP_ITEM_REROLL:
		AP_TrapStartReroll(gGT, local);
		break;
	case AP_TRAP_FORCED_USE:
		AP_TrapArmForcedUse(local);
		break;
	case AP_TRAP_FLATTEN:
		// Try immediately, so an ordinary flatten lands on the frame the warning
		// ends. If the driver is mid-animation or protected, the poll retries.
		g_trap_flatten_pending = 1;
		g_trap_flatten_applied = 0;
		g_trap_flatten_grace_ms = 0;
		if (AP_TrapApplyFlatten(local))
		{
			g_trap_flatten_pending = 0;
			g_trap_flatten_applied = 1;
			g_trap_flatten_grace_ms = AP_TRAP_FLATTEN_GRACE_MS;
		}
		break;
	default:
		break;
	}
}

// ── Presentation ──
// One place decides what the player is told, so the log and the on-screen feed can
// never disagree about a trap's state.
static void AP_TrapDrainEvents(struct GameTracker *gGT, struct Driver *local)
{
	AP_TrapEvent ev;
	char msg[128];

	while (AP_TrapSchedPopEvent(&g_sched, &ev))
	{
		const char *name = AP_TrapName(ev.effect);
		// The FIRE edge is seen exactly once here, which is what an instant
		// effect needs: it has no ACTIVE frames of its own to act on.
		if (ev.kind == AP_TRAP_EV_FIRE)
			AP_TrapApplyInstant(gGT, local, ev.effect);
		switch (ev.kind)
		{
		case AP_TRAP_EV_ARMED:
			snprintf(msg, sizeof msg, "%s ARMED", name);
			AP_FeedTrapLine(msg);
			snprintf(msg, sizeof msg, "[AP TRAP] armed: %s (slot %d)\n", name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_INACTIVE:
			// A trap this build cannot perform yet. One line, retained armed,
			// never consumed, so a newer apworld cannot lose an item here.
			snprintf(msg, sizeof msg,
			         "[AP TRAP] %s is not implemented in this build, held armed (slot %d)\n",
			         name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_WARN:
			snprintf(msg, sizeof msg, "%s INCOMING", name);
			AP_FeedTrapLine(msg);
			snprintf(msg, sizeof msg, "[AP TRAP] warning: %s (slot %d)\n", name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_FIRE:
			snprintf(msg, sizeof msg, "%s", name);
			AP_FeedTrapLine(msg);
			snprintf(msg, sizeof msg, "[AP TRAP] active: %s (slot %d)\n", name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_REFRESH:
			snprintf(msg, sizeof msg, "[AP TRAP] refreshed: %s (slot %d)\n", name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_CLEAR:
			snprintf(msg, sizeof msg, "[AP TRAP] cleared: %s (slot %d)\n", name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_REARM:
			snprintf(msg, sizeof msg, "[AP TRAP] re-armed before it fired: %s (slot %d)\n",
			         name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_SUSPEND:
		case AP_TRAP_EV_RESUME:
			// Frequent and self-evident on screen. Log only, and only at the
			// transition, which is what the scheduler emits.
			snprintf(msg, sizeof msg, "[AP TRAP] %s: %s\n",
			         ev.kind == AP_TRAP_EV_SUSPEND ? "suspended" : "resumed", name);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_DROPPED:
			snprintf(msg, sizeof msg, "[AP TRAP] registry full, %s dropped\n", name);
			AP_LogLine(msg);
			break;
		default:
			break;
		}
	}
}

// ── AP item pipeline seam ──
void AP_TrapReceive(int effect)
{
	AP_TrapEnsureReady();
	if (effect < 0 || effect >= AP_TRAP_EFFECT_COUNT)
	{
		char msg[96];
		snprintf(msg, sizeof msg, "[AP TRAP] unknown trap effect %d ignored\n", effect);
		AP_LogLine(msg);
		return;
	}
	// Presentation for the receipt itself is left to the scheduler: whether this
	// copy announces itself as armed depends on the world, which the next tick
	// supplies. A copy that lands in an eligible window goes straight to its
	// warning instead of flashing an armed state the player never had time to read.
	AP_TrapSchedReceive(&g_sched, effect);
}

void AP_Trap_ConnectReset(void)
{
	int e;

	AP_TrapEnsureReady();
	AP_TrapSchedReset(&g_sched);

	// Clear the cache in the same breath. The physics call-sites read it directly
	// and run before the next AP_TrapTick refreshes it, so a stale 1 here would
	// keep an effect applied for a frame past the reset.
	for (e = 0; e < AP_TRAP_EFFECT_COUNT; e++)
		g_active[e] = 0;
	g_recover_ms = 0;
	g_trap_lead_ms = 0;
	g_trap_reroll_excluded = -1;
	g_trap_use_pending = 0;
	g_trap_steer_mirrored = 0;
	g_trap_flatten_pending = 0;
	g_trap_flatten_applied = 0;
	g_trap_flatten_grace_ms = 0;

	AP_LogLine("[AP TRAP] fresh connect: dropped every armed and active trap\n");
}

// ── Config test trigger ──
int AP_TrapConfigLine(const char *line)
{
	const char *v;
	if (strncmp(line, "debug_trap=", 11) != 0)
		return 0;
	v = line + 11;
	if      (!strcmp(v, "icy"))     AP_TrapReceive(AP_TRAP_ICY);
	else if (!strcmp(v, "lowgrav")) AP_TrapReceive(AP_TRAP_LOWGRAV);
	else if (!strcmp(v, "usf"))     AP_TrapReceive(AP_TRAP_USF_NOBRAKE);
	else if (!strcmp(v, "boost"))   AP_TrapReceive(AP_TRAP_BOOST);
	else if (!strcmp(v, "fp"))      AP_TrapReceive(AP_TRAP_FIRSTPERSON);
	else if (!strcmp(v, "wumpa"))   AP_TrapReceive(AP_TRAP_WUMPA_WIPEOUT);
	else if (!strcmp(v, "flatten")) AP_TrapReceive(AP_TRAP_FLATTEN);
	else if (!strcmp(v, "reroll"))  AP_TrapReceive(AP_TRAP_ITEM_REROLL);
	else if (!strcmp(v, "use"))     AP_TrapReceive(AP_TRAP_FORCED_USE);
	else if (!strcmp(v, "reverse")) AP_TrapReceive(AP_TRAP_REVERSE_STEERING);
	else if (!strcmp(v, "all"))
	{
		// Every effect this build can actually perform, which is no longer the
		// leading AP_TRAP_COUNT block now that wave 2 activates effects out of
		// order. The descriptor's own active flag is the authority.
		int e;
		for (e = 0; e < AP_TRAP_EFFECT_COUNT; e++)
			if (AP_TRAP_DESC[e].active)
				AP_TrapReceive(e);
	}
	return 1;
}

// ── Debug keybinds ──
void AP_TrapDebugKeys(void)
{
	static int prev[6] = {0};
	struct
	{
		int scancode;
		int effect; // -1 = arm every implemented effect
	} keys[6] = {
	    {AP_TRAP_KEY_ICY, AP_TRAP_ICY},
	    {AP_TRAP_KEY_GRAV, AP_TRAP_LOWGRAV},
	    {AP_TRAP_KEY_USF, AP_TRAP_USF_NOBRAKE},
	    {AP_TRAP_KEY_BOOST, AP_TRAP_BOOST},
	    {AP_TRAP_KEY_FP, AP_TRAP_FIRSTPERSON},
	    {AP_TRAP_KEY_ALL, -1},
	};
	int k;

	// DEAD unless ap-config.txt dev_keys=1 (shared gate with the Shortcutless dev
	// keys). Unguarded these fire traps from any numpad press in any mode, the same
	// leak class as issue #16.
	if (!AP_DevKeysEnabled())
		return;

	for (k = 0; k < 6; k++)
	{
		int down = Platform_InputRawKeyDown(keys[k].scancode);
		if (down && !prev[k])
		{
			if (keys[k].effect < 0)
			{
				int e;
				for (e = 0; e < AP_TRAP_EFFECT_COUNT; e++)
					if (AP_TRAP_DESC[e].active)
						AP_TrapReceive(e);
			}
			else
				AP_TrapReceive(keys[k].effect);
		}
		prev[k] = down;
	}
}

// ── Camera ownership (First Person) ──
// Forced every frame while the effect is active, because the user L2 zoom and the
// start-of-race paths also write cameraMode and would otherwise clobber it
// (game/CAM.c). Runs from AP_TrapTick, which is called before the camera PROC
// ticks, so the camera update sees the forced mode.
static void AP_TrapApplyCamera(struct GameTracker *gGT, int wantFP)
{
	if (gGT == 0)
		return;
	if (wantFP)
	{
		gGT->cameraDC[0].cameraMode = AP_CAM_FIRSTPERSON;
		g_fp_applied = 1;
	}
	else if (g_fp_applied)
	{
		gGT->cameraDC[0].cameraMode = AP_CAM_CHASE;
		gGT->cameraDC[0].flags |= CAMERA_FLAG_DIRECTION_CHANGED;
		g_fp_applied = 0;
	}
}

// Near-stationary accumulator for the reverse-recovery guard. Kept on the frame
// tick rather than inside the input hook, which the engine may call more than once
// per frame.
static void AP_TrapTrackRecovery(struct Driver *local, int elapsedMs)
{
	int speed, limit;

	if (local == 0 || !(g_active[AP_TRAP_USF_NOBRAKE] || g_active[AP_TRAP_BOOST]))
	{
		g_recover_ms = 0;
		return;
	}

	speed = local->speedApprox;
	if (speed < 0)
		speed = -speed;
	limit = local->const_Speed_ClassStat >> AP_TRAP_STATIONARY_SHIFT;
	if (limit < 1)
		limit = 1;

	if (speed <= limit)
		g_recover_ms += elapsedMs;
	else
		g_recover_ms = 0;
}

static int AP_TrapReverseRestored(void)
{
	return g_recover_ms >= AP_TRAP_RECOVER_MS;
}

// ── Per-frame lifecycle ──
void AP_TrapTick(struct GameTracker *gGT)
{
	AP_TrapWorld w;
	struct Driver *local;
	int loadStage;
	int levelNow;
	int boundary;
	int e;

	AP_TrapEnsureReady();

	// One frame, one steering mirror. Cleared here rather than at the end of the
	// tick so it is also clear on any frame that returns early below.
	g_trap_steer_mirrored = 0;

	// Debug keys work anywhere, title screen included, but only when enabled via
	// ap-config.txt dev_keys=1 (gate inside AP_TrapDebugKeys).
	AP_TrapDebugKeys();

	if (gGT == 0)
		return;

	loadStage = (sdata != 0) ? sdata->Loading.stage : LOAD_IDLE;
	levelNow = gGT->levelID;
	local = gGT->drivers[0];

	// Engine-natural completion for anything that fired on an EARLIER tick.
	//
	// This runs before the map boundary on purpose. A forced press lands during
	// physics, which happens after this tick returns, so the evidence always
	// arrives one tick late. With the polls after the boundary block, a use that
	// landed in frame N's physics was still "pending" when frame N+1's boundary
	// ran, and the copy was re-armed as if it had never fired: one trap, two
	// weapons. Crediting the outcome first means the boundary only ever sees
	// genuinely unresolved copies.
	AP_TrapPollReroll(local);
	AP_TrapPollForcedUse(local);
	AP_TrapPollFlatten(local, gGT->elapsedTimeMS > 0 ? gGT->elapsedTimeMS : 32);

	// Map boundary. Clearing here, on the frame the load BEGINS and while the
	// source map is still standing, is what fixes the reported First Person
	// regression: a forced camera restored on the destination's first frame has
	// already leaked into that map's intro camera state and swallowed the input
	// meant to skip it. The scheduler sees the same boundary through the epoch, so
	// the ordering is enforced here and the bookkeeping is enforced there.
	boundary = (g_prev_level != levelNow) ||
	           (loadStage != LOAD_IDLE && g_prev_load == LOAD_IDLE);
	g_prev_level = levelNow;
	g_prev_load = loadStage;
	if (boundary)
	{
		// Forced Use is ruled to wait through transitions rather than be consumed
		// unsuccessfully, but the shared lifecycle clears every ACTIVE copy at a
		// map boundary. A copy that fired and never got its weapon away is
		// therefore re-armed here, so the load costs the player a wait rather
		// than the trap. Nothing else in this batch needs it: Wumpa Wipeout
		// completes within its firing tick, and a reroll has already discarded
		// the weapon by the time the load starts, so it was genuinely delivered.
		//
		// AP_TrapForcedUseOwed, not a bare pending flag: the poll above has
		// already credited any use that landed, and this asks the same predicate
		// so the two cannot disagree about whether the copy was spent.
		int refireForcedUse = AP_TrapForcedUseOwed(local);
		// Same rule for Flatten: only a copy still owed its squish comes back.
		int refireFlatten =
		    g_trap_flatten_pending && AP_TrapSchedActive(&g_sched, AP_TRAP_FLATTEN);

		g_epoch++;
		AP_TrapSchedMapChange(&g_sched);
		for (e = 0; e < AP_TRAP_EFFECT_COUNT; e++)
			g_active[e] = 0;
		AP_TrapApplyCamera(gGT, 0);
		g_recover_ms = 0;
		g_trap_lead_ms = 0;
		g_trap_reroll_excluded = -1;
		g_trap_use_pending = 0;
		// Flatten's stage machine does not survive the load either. A copy that
		// fired but never got its squish away is re-armed below for the same
		// reason Forced Use is; one that already landed has delivered its harm,
		// and its leftover recovery wait belongs to a map that is going away.
		g_trap_flatten_pending = 0;
		g_trap_flatten_applied = 0;
		g_trap_flatten_grace_ms = 0;

		if (refireFlatten)
			AP_TrapSchedReceive(&g_sched, AP_TRAP_FLATTEN);
		if (refireForcedUse)
			AP_TrapSchedReceive(&g_sched, AP_TRAP_FORCED_USE);
	}

	w.context = AP_TrapContextOf(gGT, loadStage);
	w.mapEpoch = g_epoch;
	w.controlUnlocked = AP_TrapControlUnlocked(gGT, local);
	w.paused = (gGT->gameMode1 & PAUSE_ALL) != 0;
	w.scripted = AP_TrapScripted(gGT, local);
	w.finishOrPodium = AP_TrapFinishOrPodium(gGT, local);
	w.elapsedMs = gGT->elapsedTimeMS > 0 ? gGT->elapsedTimeMS : 32;
	w.conditions = AP_TrapConditions(gGT, local, g_epoch,
	                                 w.controlUnlocked && !w.paused && !w.scripted &&
	                                     !w.finishOrPodium,
	                                 w.elapsedMs);

	AP_TrapSchedStep(&g_sched, &w);
	// One drain: it carries the CLEAR events the polls above emitted, the events
	// this step produced, and it is where an instant effect that fired on THIS
	// tick gets applied. Its outcome is then read on the next tick's poll.
	AP_TrapDrainEvents(gGT, local);

	for (e = 0; e < AP_TRAP_EFFECT_COUNT; e++)
		g_active[e] = (unsigned char)AP_TrapSchedActive(&g_sched, e);

	AP_TrapTrackRecovery(local, w.elapsedMs);
	AP_TrapApplyCamera(gGT, g_active[AP_TRAP_FIRSTPERSON]);
}

// ── Engine physics/input call-sites ──

// The Demo Camera prototype must not engage while the First Person trap owns
// cameraMode (it forces the mode every frame while active), and must force-
// clear if the trap arrives mid-engagement. Ownership means applied OR active:
// the applied latch lags one frame behind the scheduler on the arming edge.
int AP_TrapOwnsCamera(void)
{
	return g_fp_applied || AP_TrapSchedActive(&g_sched, AP_TRAP_FIRSTPERSON);
}

void AP_TrapDiagCounts(int *armed, int *warning, int *active, int *suspended)
{
	int i, nArmed = 0, nWarning = 0, nActive = 0, nSuspended = 0;
	AP_TrapEnsureReady();
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		const AP_TrapSlot *slot = &g_sched.slots[i];
		if (slot->state == AP_TRAP_SLOT_ARMED)
			nArmed++;
		else if (slot->state == AP_TRAP_SLOT_WARNING)
			nWarning++;
		else if (slot->state == AP_TRAP_SLOT_ACTIVE)
		{
			nActive++;
			if (slot->suspended)
				nSuspended++;
		}
	}
	if (armed != NULL)
		*armed = nArmed;
	if (warning != NULL)
		*warning = nWarning;
	if (active != NULL)
		*active = nActive;
	if (suspended != NULL)
		*suspended = nSuspended;
}

int AP_TrapGravity(struct Driver *driver, int gravityY)
{
	if (g_active[AP_TRAP_LOWGRAV] && AP_TrapIsLocal(driver))
		return (gravityY * AP_TRAP_GRAV_SCALE) / 256;
	return gravityY;
}

void AP_TrapFriction(struct Driver *driver, int *perpendicularFriction, int *forwardFriction)
{
	if (g_active[AP_TRAP_ICY] && AP_TrapIsLocal(driver))
	{
		// KNOWN, ACCEPTED SIDE EFFECT: this scales the friction scalars BEFORE the
		// per-terrain groundFrictionScale multiplies in (VehPhysForce.c:306-307),
		// so off-road drag is cut to about 16 percent as well. Kept as deliberate
		// counterplay (issue #116; Ignore Off-Road is a separate item, #14). With
		// map-lifetime duration this now lasts the whole map rather than 20 seconds.
		*perpendicularFriction = (*perpendicularFriction * AP_TRAP_FRICTION_SCALE) / 256;
		*forwardFriction = (*forwardFriction * AP_TRAP_FRICTION_SCALE) / 256;
	}
}

// Raise reserves to the trap floor without ever lowering them (s16 field; the
// floor is far below the 32767 ceiling and values already above it pass through
// untouched, so no overflow path is introduced).
static void AP_TrapFloorReserves(struct Driver *driver)
{
	if (driver->reserves < AP_TRAP_BOOST_RESERVES)
		driver->reserves = AP_TRAP_BOOST_RESERVES;
}

void AP_TrapForceBoost(struct Driver *driver)
{
	if (!AP_TrapIsLocal(driver))
		return;
	if (g_active[AP_TRAP_USF_NOBRAKE])
	{
		// Genuinely USF-tier cap, derived from the real fire-level formula
		// (VehFire.c:358-359): cap = singleTurbo + (fireLevel * (sacred - singleTurbo))
		// >> 8, evaluated at a super turbo pad's fireLevel (0x800). A pin to
		// const_SacredFireSpeed would only grant red fire, which is not USF: the
		// engine defines USF as fireSpeedCap above sacred (VehFire.c:378).
		int usfCap = ((int)driver->const_SingleTurboSpeed +
		              ((AP_TRAP_USF_FIRELEVEL *
		                ((int)driver->const_SacredFireSpeed - (int)driver->const_SingleTurboSpeed)) >> 8));
		if (usfCap > 32767)
			usfCap = 32767; // fireSpeedCap is s16
		AP_TrapFloorReserves(driver);
		// Floor, not pin: VehFire_Increment demotes the cap on any non-super-pad
		// boost while above sacred (VehFire.c:375-381); re-raising here each frame
		// restores the trap tier without ever downgrading a higher cap.
		if (driver->fireSpeedCap < usfCap)
			driver->fireSpeedCap = (s16)usfCap;
	}
	else if (g_active[AP_TRAP_BOOST])
	{
		AP_TrapFloorReserves(driver);
		// Floor, not pin: a hard pin to const_SingleTurboSpeed would downgrade a
		// red fire the player earns mid-trap. Only raise up to the milder tier.
		if (driver->fireSpeedCap < driver->const_SingleTurboSpeed)
			driver->fireSpeedCap = driver->const_SingleTurboSpeed;
	}
}

// Mirror one steering axis about the centre the engine will measure it from.
// stickLX runs 0 (full left) through 0x80 (neutral) to 0xFF (full right), and
// VehPhysJoystick_GetStrengthAbsolute takes its centre from the racing-wheel
// calibration when one is attached (VehPhysJoystick.c:80-84), so the reflection
// has to use the same centre or a wheel would pick up a steady drift.
static int AP_TrapMirrorStick(int value, int centre)
{
	int mirrored = centre * 2 - value;
	if (mirrored < 0)
		mirrored = 0;
	if (mirrored > 0xFF)
		mirrored = 0xFF;
	return mirrored;
}

void AP_TrapDriveInput(struct Driver *driver, struct GamepadBuffer *pad,
                       u32 *buttonsHeld, u32 *buttonsTapped, u32 *cross, u32 *square)
{
	int usf = g_active[AP_TRAP_USF_NOBRAKE];
	int boost = g_active[AP_TRAP_BOOST];

	if (!AP_TrapIsLocal(driver))
		return;

	// Reverse Steering. One write covers both ruled input paths: GAMEPAD.c:628
	// folds the D-pad's BTN_LEFT and BTN_RIGHT into stickLX before physics runs,
	// and the steering section later in this same PhysLinear call reads only
	// stickLX (VehPhysProc.c:985). Acceleration, braking, hopping, weapon use and
	// menus are untouched because none of them read this axis, and the menus are
	// read out of GAMEPAD.c earlier in the frame, before this override exists.
	// Airborne steering inverts too, for free, because it goes through the same
	// axis wherever CTR accepts it.
	//
	// LATCHED, and that is load-bearing. The engine may call this hook more than
	// once in a frame, which the two boost-control writes below survive because
	// they assign constants. A mirror is its own inverse, so applying it twice
	// would hand the player back normal steering and the trap would silently do
	// nothing. The latch is cleared once per frame in AP_TrapTick.
	if (g_active[AP_TRAP_REVERSE_STEERING] && pad != 0 && !g_trap_steer_mirrored)
	{
		int centre = pad->rwd != 0 ? (int)pad->rwd->gamepadCenter : AP_STICK_NEUTRAL;
		pad->stickLX = (s16)AP_TrapMirrorStick((int)pad->stickLX, centre);
		g_trap_steer_mirrored = 1;
	}

	// Forced Use. The weapon path fires on a TAPPED circle in a controllable kart
	// state (VehPhysProc.c:485-489), so the trap presses the button for the player
	// and lets the engine decide the rest: projectiles fire, TNT and beakers drop,
	// shields and masks activate, a triple set loses exactly one round, and the
	// held item keeps whatever juiced state it had. An incompatible state simply
	// ignores the press, and the effect stays active and tries again next frame,
	// which is the ruled "wait rather than consume unsuccessfully" behaviour.
	if (g_trap_use_pending && g_active[AP_TRAP_FORCED_USE] && buttonsTapped != 0)
		*buttonsTapped |= BTN_CIRCLE;

	if (!usf && !boost)
		return;

	// Both traps disable braking. Forced Boost stops there: the player may release
	// acceleration and coast, which is what keeps it the milder of the two.
	*square = 0;
	*buttonsHeld &= ~BTN_SQUARE;

	if (usf)
	{
		*buttonsHeld |= BTN_CROSS;
		*cross = BTN_CROSS;
	}

	// Neutralise the analog sticks so the pull-back reverse/brake path (read later
	// in this same PhysLinear call, VehPhysProc.c:638/686) goes dead too. The pad
	// buffer is repopulated from input every frame before physics, so this only
	// affects the current frame's reads.
	//
	// The recovery guard releases both the analog path and, for Forced USF, the
	// forced throttle, once the kart has sat nearly still for about a second. A
	// wedged kart can then reverse out; a moving one still cannot brake.
	if (AP_TrapReverseRestored())
	{
		if (usf)
		{
			*buttonsHeld &= ~BTN_CROSS;
			*cross = 0;
		}
		return;
	}

	if (pad != 0)
	{
		pad->stickLY = AP_STICK_NEUTRAL;
		pad->stickRY = AP_STICK_NEUTRAL;
	}
}

#endif // CTR_AP
