#ifndef AP_CHARFREEZE_H
#define AP_CHARFREEZE_H

// ---------------------------------------------------------------------------
// The hub character picker's hold on the kart (#54/#209, #238).
//
// WHY THIS IS NOT JUST A FLAG. The picker's first cut took VEH_FREEZE_DOOR and
// called that a freeze. That bit is tested in exactly one place,
// VehPhysProc_Driving_PhysLinear (game/Vehicle/VehPhysProc.c:407-411), which
// returns before the driving code -- so it stops INPUT, which is all
// game/232/AH_Door.c:169 ever claimed for it. Every stage after it in
// PlayerDrivingFuncTable still runs (game/Vehicle/VehPhysProc.c:1221-1234):
// VehPhysGeneral_PhysAngular, VehPhysForce_OnApplyForces,
// COLL_MOVED_PlayerSearch, VehPhysGeneral_JumpAndFriction and
// VehPhysForce_TranslateMatrix. Those move the kart from d->baseSpeed and
// d->speed, and nothing decays either while the bit is set, because the only
// code that would is the part PhysLinear just skipped. A kart that was MOVING
// when the picker opened therefore keeps moving under it, uncontrollably.
//
// The dev key hid that for the whole prototype: you are parked when you reach
// for the keyboard, so baseSpeed is already 0 and a bit-only hold looks like a
// full freeze. The #238 pause row is entered at speed. PAUSE_ALL stops
// MainFrame_GameLogic wholesale (game/MAIN/MainFrame.c:137), so the pre-pause
// speed survives the pause untouched and the RESUME branch hands it back one or
// two frames before the picker opens on it. Found in live play, 2026-08-12:
// picker up, kart driving around behind it.
//
// WHAT A FREEZE ACTUALLY IS HERE. The engine already has one for "a modal owns
// the screen", the one the Aku Aku mask hint takes: install
// VehPhysProc_FreezeEndEvent_Init in the driver's INIT slot
// (game/MAIN/MainFrame.c:836). It zeroes speed and speedApprox and swaps in
// PlayerFreezeFuncTable, whose PhysLinear then zeroes baseSpeed, fireSpeed and
// the turn states every frame (game/Vehicle/VehPhysProc.c:1240-1295). Its
// release is retail's own pairing, used by both hub modals that take it: write
// VehPhysProc_Driving_Init back into that slot (game/232/AH_MaskHint.c:497,
// game/232/AH_Door.c:187), which restores PlayerDrivingFuncTable and KS_NORMAL.
// The picker now takes BOTH halves, the flag and the table.
//
// WHY THE DECISION IS PURE AND LIVES HERE. Three of its four outcomes are
// invisible from a build machine, and the fourth is the one that shipped wrong:
//
//   * HOLD is per FRAME, not per open. AH_Door_ThTick releases this exact flag
//     and this exact INIT slot whenever a hub door is open (issue #51,
//     game/232/AH_Door.c:184-189), and AH_MaskHint does the same at its state 7.
//     A hold applied once at open is a hold something else can take away.
//   * RELEASE must happen exactly once, on the close, so a second close cannot
//     hand a driving table to a driver that is already driving.
//   * DROP is not RELEASE. When the hub goes away the level load takes the flag
//     and the driver struct with it, so the hold is over, but writing a funcPtr
//     through gGT->drivers[0] at that point is a write through a stale pointer.
//
// So the engine half is only "call the applier with what this says", and
// tools/test-character-persistence.cpp can drive the frame sequences -- open at
// speed, hold across a door that fights for the flag, close, and lose the hub
// mid-picker -- against the same header the game calls.
// ---------------------------------------------------------------------------

enum AP_CharFreezeAction
{
	// Nothing to do this frame.
	AP_CHARFREEZE_NONE = 0,

	// Take or re-assert the hold: set VEH_FREEZE_DOOR and install the freeze
	// table. Idempotent by design, and issued every frame the picker is up.
	AP_CHARFREEZE_HOLD,

	// Give the kart back: clear VEH_FREEZE_DOOR and restore the driving table.
	AP_CHARFREEZE_RELEASE,

	// The hub went away while we held it. Forget the hold and touch NOTHING:
	// both the flag and the driver went with the level.
	AP_CHARFREEZE_DROP
};

struct AP_CharFreezeState
{
	// 1 while the picker owns the kart.
	int held;
};

struct AP_CharFreezeInput
{
	// The picker owns the screen this frame.
	int pickerOpen;

	// The hub is loaded and idle with a live local driver: ap_cs_hubReady().
	// The one input that separates RELEASE from DROP.
	int hubReady;
};

// One frame of the hold. Returns the action the caller must apply.
static inline int AP_CharFreeze_Step(struct AP_CharFreezeState *s, const struct AP_CharFreezeInput *in)
{
	if ((s == 0) || (in == 0))
		return AP_CHARFREEZE_NONE;

	if (!in->hubReady)
	{
		if (!s->held)
			return AP_CHARFREEZE_NONE;

		s->held = 0;
		return AP_CHARFREEZE_DROP;
	}

	if (in->pickerOpen)
	{
		s->held = 1;
		return AP_CHARFREEZE_HOLD;
	}

	if (!s->held)
		return AP_CHARFREEZE_NONE;

	s->held = 0;
	return AP_CHARFREEZE_RELEASE;
}

#endif // AP_CHARFREEZE_H
