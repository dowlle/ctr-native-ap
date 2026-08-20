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

#endif // AP_TRAP_OBSERVE_LOGIC_H
