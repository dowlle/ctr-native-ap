#ifndef AP_GOAL_LOGIC_H
#define AP_GOAL_LOGIC_H

// Pure composed-goal predicate. Every active condition is ANDed; zero disables
// that arm. The apworld rejects the all-zero shape, but returning true for it is
// the ordinary logical identity and keeps this helper free of generation policy.
static inline int AP_ComposedGoalMet(int oxideRequirement,
	int oxideFirstBeaten, int oxideFinalBeaten,
	int bossesRequired, int bossesWon,
	int gemsRequired, int gemsHeld)
{
	int done = 1;
	if (oxideRequirement == 1)
		done = done && oxideFirstBeaten;
	else if (oxideRequirement == 2)
		done = done && oxideFinalBeaten;
	if (bossesRequired > 0)
		done = done && bossesWon >= bossesRequired;
	if (gemsRequired > 0)
		done = done && gemsHeld >= gemsRequired;
	return done;
}

#endif
