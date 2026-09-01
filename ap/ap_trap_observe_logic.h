#ifndef AP_TRAP_OBSERVE_LOGIC_H
#define AP_TRAP_OBSERVE_LOGIC_H

// Pure observation predicates for the trap runtime (#280, wave 2).
//
// WHY THIS HEADER EXISTS. ap_trap_sched_logic.h made the SCHEDULE testable, and
// tools/test-trap-scheduler.c has covered it since wave 1. It did not make the
// OBSERVATION testable: deciding whether a weapon really left the slot, whether
// a roulette can still resolve, and whether a lead timer should keep counting
// all lived inside ap/ap_traps.c behind a Driver and a GameTracker, so nothing
// pinned them. The wave 2 batch 1 review found two real defects in exactly that
// unpinned code, which is the argument for the split.
//
// Everything here is integers in, integers out. No engine, no network, no
// config, same rule as the scheduler header: ap/ap_traps.c reads the fields off
// the Driver and hands them in, and the harness hands in the same numbers.
//
// WHAT DOES NOT BELONG HERE. Anything that needs to WALK engine state stays in
// ap_traps.c: the LEV crate census has to iterate an InstDef array, and picking
// a valid AI shooter has to walk gGT->drivers. Those are honestly untestable
// without an engine mock, and a mock deep enough to fake them would be testing
// itself. The predicates below are the parts that genuinely reduce to numbers.

// ── Held-item slot ──
//
// Engine sentinels, not AP values. 0xF is "no weapon" and 0x10 is "the roulette
// is spinning" (VehPhysProc.c:316/536, RB_Crate.c:286).
#define AP_TRAP_ITEM_NONE    0x0F
#define AP_TRAP_ITEM_ROLLING 0x10

enum AP_TrapSlotContents
{
	AP_TRAP_ITEM_STATE_EMPTY = 0, // nothing held
	AP_TRAP_ITEM_STATE_ROLLING,   // a roll is in flight and can still resolve
	AP_TRAP_ITEM_STATE_LOCKED,    // a weapon is held but the post-use lockout runs
	AP_TRAP_ITEM_STATE_RESOLVED   // a weapon the player could use this instant
};

// Classify the slot from the three Driver fields that describe it. itemRollTimer
// is checked independently of the 0x10 placeholder because the engine clears the
// placeholder and the timer at different moments, and a slot with either one
// live is not a settled weapon.
static int AP_TrapSlotContentsOf(int heldItemID, int itemRollTimer, int noItemTimer)
{
	if (heldItemID == AP_TRAP_ITEM_ROLLING)
		return AP_TRAP_ITEM_STATE_ROLLING;
	if (heldItemID == AP_TRAP_ITEM_NONE)
		return AP_TRAP_ITEM_STATE_EMPTY;
	if (itemRollTimer != 0)
		return AP_TRAP_ITEM_STATE_ROLLING;
	if (noItemTimer != 0)
		return AP_TRAP_ITEM_STATE_LOCKED;
	return AP_TRAP_ITEM_STATE_RESOLVED;
}

// The AP_TRAP_COND_HELD_ITEM predicate. Both inventory traps are ruled to need a
// FULLY RESOLVED weapon, and the engine's own fire path additionally requires
// the post-use lockout to be clear (VehPhysProc.c:536), so "resolved" means
// usable right now rather than merely present.
static int AP_TrapHeldItemIsResolved(int heldItemID, int itemRollTimer, int noItemTimer)
{
	return AP_TrapSlotContentsOf(heldItemID, itemRollTimer, noItemTimer) ==
	       AP_TRAP_ITEM_STATE_RESOLVED;
}

// Empty Crates suppresses only the reward for the local human who received the
// effect. Crate breakage stays in the engine call site; this pure predicate pins
// the player/AI ownership boundary in the host harness.
static int AP_TrapCrateRewardSuppressed(int effectActive, int driverIsLocal,
                                        int driverIsBot)
{
	return effectActive && driverIsLocal && !driverIsBot;
}

// Boost Blocker rejects every new boost grant for the local receiver while
// leaving AI and remote/non-local drivers on the untouched engine path.
static int AP_TrapBoostGrantAllowed(int effectActive, int driverIsLocal)
{
	return !effectActive || !driverIsLocal;
}

static int AP_TrapWeakenedBoostTier(int effectActive, int permanentTier,
                                    int vanillaUsfTier)
{
	int tier;
	if (!effectActive)
		return permanentTier;
	tier = permanentTier < 0 ? vanillaUsfTier : permanentTier;
	return tier > 0 ? tier - 1 : 0;
}

static int AP_TrapHazardDistance(int speedApprox, int travelMs)
{
	int distance;
	if (speedApprox < 0)
		speedApprox = -speedApprox;
	distance = (speedApprox * travelMs) >> 16;
	if (distance < 160) distance = 160;
	if (distance > 420) distance = 420;
	return distance;
}

// ── Engine-natural completion ──
enum AP_TrapOutcome
{
	AP_TRAP_OUTCOME_WAIT = 0, // hold the slot ACTIVE, the outcome has not landed
	AP_TRAP_OUTCOME_DONE      // report AP_TrapSchedEffectDone
};

// Item Reroll: has the roulette the trap started stopped mattering?
//
// REVIEW DEFECT 1. Waiting for a resolve is wrong, because the resolve can be
// destroyed rather than delivered: PlayLevel.c:172 and :435 confiscate the item
// slot the instant the local player crosses the finish line, overwriting the
// 0x10 placeholder with the empty sentinel while itemRollTimer still runs, so
// VehPhysGeneral_SetHeldItem is never reached. A resolve-only test therefore
// held the slot ACTIVE for the rest of the map, swallowed a serialized
// duplicate, and left the exclusion live against unrelated later rolls.
//
// Only a roll still in flight is a reason to wait. Resolved is the ordinary
// success, and empty means the weapon is gone, which is the harm this trap
// promised, so both are done.
static int AP_TrapRerollOutcome(int heldItemID, int itemRollTimer, int noItemTimer)
{
	if (AP_TrapSlotContentsOf(heldItemID, itemRollTimer, noItemTimer) ==
	    AP_TRAP_ITEM_STATE_ROLLING)
		return AP_TRAP_OUTCOME_WAIT;
	return AP_TRAP_OUTCOME_DONE;
}

// Forced Use: did the trap's injected press actually fire the weapon?
//
// REVIEW DEFECT 2. "The weapon left the slot" is not proof on its own, for the
// same reason: the finish line empties the slot without any press landing, and
// the ruling is to wait rather than be consumed unsuccessfully.
//
// A genuine use always starts the post-use lockout FIRST. VehPhysProc.c:547 and
// :554 are the only writers that set noItemTimer non-zero, and the engine does
// not empty the slot until :371, one frame later, while the timer still reads 1.
// So an empty slot with a clear lockout and an undiminished count can only be a
// confiscation, never a use.
static int AP_TrapForcedUseOutcome(int heldItemID, int numHeldItems, int noItemTimer,
                                   int armedItem, int armedCount)
{
	if (heldItemID == AP_TRAP_ITEM_NONE && noItemTimer == 0 && numHeldItems >= armedCount)
		return AP_TRAP_OUTCOME_WAIT;
	if (heldItemID != armedItem || numHeldItems < armedCount || noItemTimer != 0)
		return AP_TRAP_OUTCOME_DONE;
	return AP_TRAP_OUTCOME_WAIT;
}

// ── Flatten ──
//
// Kart states, repeated as plain integers so this header stays engine-free and
// the harness can drive it without the engine headers. Values are the decomp's
// own enum (namespace_Vehicle.h:60-69); ap_traps.c static-asserts them against it.
#define AP_TRAP_KS_NORMAL         0
#define AP_TRAP_KS_CRASHING       1
#define AP_TRAP_KS_DRIFTING       2
#define AP_TRAP_KS_SPINNING       3
#define AP_TRAP_KS_ENGINE_REVVING 4
#define AP_TRAP_KS_MASK_GRABBED   5
#define AP_TRAP_KS_BLASTED        6
#define AP_TRAP_KS_ANTIVSHIFT     9
#define AP_TRAP_KS_WARP_PAD       10
#define AP_TRAP_KS_FREEZE         11

// May the trap hand the engine a fresh squish this frame?
//
// The ruling suppresses Flatten during scripted movement, a mask rescue or
// another incompatible damage animation, and fires it when the driver state
// becomes valid. It DELIBERATELY allows activation while airborne, so nothing
// here asks about ground contact.
//
// ALLOW-LIST, not a deny-list, and that distinction is load-bearing. Naming the
// damage animations to refuse lets every SCRIPTED state through, because those
// are not damage: KS_WARP_PAD (a hub pad or door), KS_FREEZE (the end-of-event
// freeze) and KS_ENGINE_REVVING (the starting countdown) would all pass. Each of
// them installs its own driver func table, and the squish branch re-points
// DRIVER_FUNC_INIT at VehPhysProc_SpinFirst_Init, which overwrites the WHOLE
// table (VehPhysProc.c:2337-2340). For the warp pad that erases
// VehStuckProc_Warp_PhysAngular, the one stage the pad leaves installed
// (VehStuckProc.c:1657-1670), and the warp can then never complete.
//
// The three states allowed here are the engine's own definition of a kart under
// the player's control: the same test the weapon-fire path uses at
// VehPhysProc.c:489.
//
// The protection terms mirror VehPickState_NewState's own early returns
// (VehPickState.c:13-40) rather than inventing a policy. They are checked HERE,
// before the call, for one specific reason: that function's bubble-shield branch
// POPS the shield and then returns 0. Calling it while shielded would cost the
// player their shield without flattening them, which is neither the harm the trap
// promised nor anything its ruling asks for. Checking first makes the trap wait.
//
// squishTimer guards against re-squishing an already flattened kart, which the
// damageType 3 branch does not check for itself the way the blasted branch does.
//
// pendingDamage is the engine's DEFERRED damage slot, and it must be empty.
// VehPickState_NewState zeroes it at entry (VehPickState.c:9) before it does
// anything else, so dispatching while a collision has queued damage DELETES that
// queued hit: the player never takes it, its attacker is never credited, and its
// DeathLink never sends. The window is a whole frame and is structural rather
// than a race. VehPhysCrash_Attack queues the damage during the driver stages of
// MainFrame_GameLogic (VehPhysCrash.c:152/170/192), and nothing consumes it until
// VehPickupItem_ShootOnCirclePress at the top of the NEXT frame's
// MainFrame_GameLogic (MainFrame.c:312) -- with AP_TrapTick running in between,
// from AP_OnFrame at MainMain.c:323. A queued-but-unapplied hit is an
// incompatible damage event under the ruling, so the trap waits for it to land.
static int AP_TrapFlattenReady(int kartState, int squishTimer, int invincibleTimer,
                               int maskWeapon, int hasBubbleShield, int pendingDamage)
{
	if (kartState != AP_TRAP_KS_NORMAL && kartState != AP_TRAP_KS_DRIFTING &&
	    kartState != AP_TRAP_KS_ANTIVSHIFT)
		return 0;
	if (squishTimer != 0 || pendingDamage != 0)
		return 0;
	if (invincibleTimer != 0 || maskWeapon || hasBubbleShield)
		return 0;
	return 1;
}

// Has the engine finished with the squish it was given?
//
// Flatten is ruled to use the engine's natural flatten, follow-up spin and
// recovery instead of a duration timer, so completion is "the squish timer ran
// out and the kart is back under its own control". The follow-up spin is part of
// that recovery: the damageType 3 branch falls through to the spinout init, so a
// kart still spinning has not finished recovering.
//
// Allow-listed for the same reason the ready gate is: "back under its own
// control" is the controllable set, and a scripted state is not the end of a
// recovery. A squish whose timer runs out inside a warp pad or an end-of-event
// freeze holds the slot until the driver is genuinely driving again, or until
// the map boundary clears the copy.
static int AP_TrapFlattenRecovered(int kartState, int squishTimer)
{
	if (squishTimer != 0)
		return 0;
	return kartState == AP_TRAP_KS_NORMAL || kartState == AP_TRAP_KS_DRIFTING ||
	       kartState == AP_TRAP_KS_ANTIVSHIFT;
}

// ── Warpball Ambush lead timer ──
//
// REVIEW DEFECT 3. The ruling distinguishes two things the first cut collapsed
// into one. Losing first place RESETS the countdown completely. The excluded
// states -- starting countdown, pause, cutscene, finish ceremony -- are excluded
// from ELAPSED TIME, which is a freeze, and the shared lifecycle table says the
// same thing in general terms: pause freezes timed effects rather than clearing
// them. Resetting on those states meant a single pause discarded fourteen
// seconds of legitimately held lead.
//
// driverRank is 0-based, so first place is 0 (UI_Rank.c:86-87). A NEGATIVE rank
// is the engine's "not ranked this frame" sentinel (PlayLevel.c:247), which
// PlayLevel sets on every driver before it re-sorts them. Treating that as
// "not first" would reset the countdown on ordinary sort frames, so an unknown
// rank freezes exactly like an excluded state. Only a rank the engine has
// actually settled ABOVE first is a genuine loss of the lead.
static int AP_TrapLeadAccumulate(int prevMs, int driverRank, int aiPresent, int counting,
                                 int elapsedMs)
{
	if (driverRank > 0)
		return 0; // genuinely overtaken: the ruled complete reset
	if (driverRank < 0 || !aiPresent || !counting)
		return prevMs; // unknown rank, or an excluded state: freeze, never clear
	return prevMs + (elapsedMs > 0 ? elapsedMs : 0);
}

// Once the ruled lead has been earned, a pre-existing Warpball may keep this
// copy waiting without making the player earn the lead again. In particular,
// being overtaken during that singleton wait must not clear `earned`. The owner
// clears the state only after a successful birth or at a map/session boundary.
typedef struct AP_TrapLeadState
{
	int elapsedMs;
	int earned;
} AP_TrapLeadState;

static AP_TrapLeadState AP_TrapLeadUpdate(AP_TrapLeadState prev, int driverRank,
                                          int aiPresent, int counting, int elapsedMs,
                                          int requiredMs)
{
	if (prev.earned)
		return prev;
	prev.elapsedMs = AP_TrapLeadAccumulate(prev.elapsedMs, driverRank, aiPresent,
	                                      counting, elapsedMs);
	if (prev.elapsedMs >= requiredMs)
		prev.earned = 1;
	return prev;
}

#endif // AP_TRAP_OBSERVE_LOGIC_H
