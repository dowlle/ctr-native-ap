#ifndef AP_TRAP_ALPHA3_LOGIC_H
#define AP_TRAP_ALPHA3_LOGIC_H

// Pure rules of the seven alpha3 data package traps (ruled 2026-11-06). No
// engine, so ap/ap_traps.c and the host harness tools/test-trap-alpha3.c run the
// same arithmetic. The scheduler rows are in ap_trap_sched_logic.h.

#include "ap_trap_sched_logic.h"

// L1 and R1 as the engine reads them in the jump block (VehPhysProc.c,
// `buttonsTapped & 0xc00`): BTN_L1 0x800 and BTN_R1 0x400.
#define AP_TRAP_HOP_BUTTONS 0xc00u

// Wumpa Leak takes half the race fruit, rounded down, so 11 leave 6 and 1 is
// left alone (nothing to halve). The condition bit uses the same number.
static inline int AP_TrapWumpaLeakAmount(int fruit)
{
	return fruit >= 2 ? fruit / 2 : 0;
}

// No Drifting: the frame's buttons without L1 and R1.
static inline unsigned AP_TrapNoDriftButtons(unsigned buttons)
{
	return buttons & ~AP_TRAP_HOP_BUTTONS;
}

// Stuck Steering: the side a new copy kills, -1 left or +1 right.
static inline int AP_TrapStuckSidePick(unsigned timer, unsigned picks)
{
	return ((timer ^ (picks * 0x9e3779b9u)) >> 7) & 1u ? 1 : -1;
}

// Stuck Steering: stickLX runs 0 (full left) through the centre to 0xFF (full
// right). The dead side is held at the centre; the other side is untouched.
static inline int AP_TrapStuckStick(int value, int centre, int side)
{
	if (side < 0 && value < centre)
		return centre;
	if (side > 0 && value > centre)
		return centre;
	return value;
}

// Limp Mode: speed held to half the class top speed in either direction, so it
// never reaches 0 and no wedge guard is needed.
static inline int AP_TrapLimpClamp(int speed, int classTop)
{
	int half = classTop / 2;
	if (half < 1)
		half = 1;
	if (speed > half)
		return half;
	if (speed < -half)
		return -half;
	return speed;
}

// Rival Surge: an AI's nav progress this frame times 3/2.
static inline int AP_TrapRivalSurgeDelta(int deltaPos)
{
	if (deltaPos <= 0)
		return deltaPos;
	return deltaPos + deltaPos / 2;
}

// Mud Season fires but changes nothing while Ignore Dirt is held; the feed then
// says "<name> BLOCKED" (decision D6 A).
static inline int AP_TrapMudSeasonBlocked(int effect, int dirtHeld)
{
	return effect == AP_TRAP_MUD_SEASON && dirtHeld;
}

// Kart states as plain integers (the decomp enum, namespace_Vehicle.h:60-69;
// ap_traps.c static-asserts the same values). Guarded because the observe logic
// header repeats them.
#ifndef AP_TRAP_KS_NORMAL
#define AP_TRAP_KS_NORMAL   0
#endif
#ifndef AP_TRAP_KS_DRIFTING
#define AP_TRAP_KS_DRIFTING 2
#endif

// Always Jumping: may the kart hop in this state? The two grounded driving
// states: normal driving and a running drift. Every other state is damage, a
// scripted move or a freeze (crash, spinout, engine rev, mask grab, blast, V-shift
// reverse, warp pad, end freeze) and stays out.
static inline int AP_TrapAlwaysJumpState(int kartState)
{
	return kartState == AP_TRAP_KS_NORMAL || kartState == AP_TRAP_KS_DRIFTING;
}

// Always Jumping: write a one-frame jump buffer now? Only when the trap runs,
// the kart is in a hopping state (driving or drifting; a boost of any kind is not
// an input here, so it never blocks), no buffer is set already (a player's own
// buffer is never replaced), the kart is on the ground (coyote time left), the
// jump cooldown is over, and no rain cloud locks jumping out (Red Potion wins).
// A hop during a drift is the same engine hop a drift entry uses: the drift
// state only counts frames and never reads the ground flag, so it runs on
// through the air as it does over a crest.
static inline int AP_TrapAlwaysJumpWants(int active, int kartState, int buffer,
                                         int coyoteMs, int cooldownMs, int lockout)
{
	return active && AP_TrapAlwaysJumpState(kartState) && buffer == 0 &&
	       coyoteMs != 0 && cooldownMs == 0 && !lockout;
}

#endif // AP_TRAP_ALPHA3_LOGIC_H
