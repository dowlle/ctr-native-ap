#ifndef AP_TRAP_QUEUE_LOGIC_H
#define AP_TRAP_QUEUE_LOGIC_H

enum AP_TrapPrimedAction
{
	AP_TRAP_PRIMED_WAIT = 0,
	AP_TRAP_PRIMED_DEMOTE_INSTANT,
	AP_TRAP_PRIMED_ROLL_DELAY,
	AP_TRAP_PRIMED_TICK_DELAY,
	AP_TRAP_PRIMED_FIRE
};

// Decide one primed instance's transition. A matured duplicate waits behind a
// firing instance of the same effect, so every receipt receives a full effect
// window instead of overlapping into one boolean application flag.
static inline int AP_TrapPrimedActionFor(int instant, int raceActive,
	int lapWindow, int rolled, int delayExpired, int sameEffectFiring)
{
	if (instant)
	{
		if (!raceActive)
			return AP_TRAP_PRIMED_DEMOTE_INSTANT;
		return sameEffectFiring ? AP_TRAP_PRIMED_WAIT : AP_TRAP_PRIMED_FIRE;
	}

	if (!lapWindow)
		return AP_TRAP_PRIMED_WAIT;
	if (!rolled)
		return AP_TRAP_PRIMED_ROLL_DELAY;
	if (!delayExpired)
		return AP_TRAP_PRIMED_TICK_DELAY;
	return sameEffectFiring ? AP_TRAP_PRIMED_WAIT : AP_TRAP_PRIMED_FIRE;
}

#endif
