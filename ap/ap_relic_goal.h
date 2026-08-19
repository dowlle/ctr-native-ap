#ifndef AP_RELIC_GOAL_H
#define AP_RELIC_GOAL_H

// Pure Oxide Final relic-count rule shared by the live gate and the verifier.
// Keep this header engine-free so the exact 53/54 boundary can be exercised by
// a host test without linking the game runtime.
static inline int AP_RelicGoalMet(int mode, int required,
	int sapphire, int gold, int platinum)
{
	switch (mode)
	{
	case 1: return gold >= required;
	case 2: return platinum >= required;
	case 3: return sapphire >= required || gold >= required || platinum >= required;
	case 4: return sapphire + gold + platinum >= required;
	case 0:
	default: return sapphire >= required;
	}
}

#endif
