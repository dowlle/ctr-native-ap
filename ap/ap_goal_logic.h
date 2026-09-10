#ifndef AP_GOAL_LOGIC_H
#define AP_GOAL_LOGIC_H

// Pure composed-goal predicate. Every active condition is ANDed; zero disables
// that arm. The apworld rejects the all-zero shape, but returning true for it is
// the ordinary logical identity and keeps this helper free of generation policy.
//
// `oxideRequirement` is ctr_cfg.goal_oxide. Values 0 (`optional`) and 3
// (`disabled`, issue #320) both contribute NO Oxide arm: in neither is an Oxide
// race a completion condition, and under `disabled` the seed does not even
// contain them. That falls out of the else-if chain below rather than needing a
// case of its own, but it is stated here because it is a contract a reader must
// be able to check -- ap_verify.c's goal verdict is kept in lockstep with it.
// What `disabled` DOES change is the garage, which is ap/ap_oxide_encounter.h's
// job, not this predicate's.
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
