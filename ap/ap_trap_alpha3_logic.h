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

// Always Jumping: write a one-frame jump buffer now? Only when the trap runs,
// the kart drives normally (no drift, damage, mask or warp state), no buffer is
// set already (a player's own buffer is never replaced), the kart is on the
// ground (coyote time left), the jump cooldown is over, and no rain cloud locks
// jumping out (Red Potion wins).
static inline int AP_TrapAlwaysJumpWants(int active, int normalState, int buffer,
                                         int coyoteMs, int cooldownMs, int lockout)
{
	return active && normalState && buffer == 0 && coyoteMs != 0 && cooldownMs == 0 &&
	       !lockout;
}

#endif // AP_TRAP_ALPHA3_LOGIC_H
