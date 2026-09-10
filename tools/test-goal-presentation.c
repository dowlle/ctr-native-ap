#include <stdio.h>

#include "../ap/ap_goal_presentation.h"
#include "../ap/ap_goal_logic.h"

static int failures;

#define EXPECT(what, expr) do { \
	int ok = !!(expr); \
	printf("%-4s %s\n", ok ? "ok" : "FAIL", what); \
	if (!ok) failures++; \
} while (0)

int main(void)
{
	AP_GoalPresentationState state;

	EXPECT("first Oxide only: unmet before first win",
	       !AP_ComposedGoalMet(1, 0, 0, 0, 0, 0, 0));
	EXPECT("first Oxide only: met after first win",
	       AP_ComposedGoalMet(1, 1, 0, 0, 0, 0, 0));
	EXPECT("four bosses only: three is short",
	       !AP_ComposedGoalMet(0, 0, 0, 4, 3, 0, 0));
	EXPECT("four bosses only: four completes",
	       AP_ComposedGoalMet(0, 0, 0, 4, 4, 0, 0));
	EXPECT("five gems only: four is short",
	       !AP_ComposedGoalMet(0, 0, 0, 0, 0, 5, 4));
	EXPECT("five gems only: five completes",
	       AP_ComposedGoalMet(0, 0, 0, 0, 0, 5, 5));
	EXPECT("first plus 2 bosses plus 3 gems: all required",
	       !AP_ComposedGoalMet(1, 1, 0, 2, 2, 3, 2));
	EXPECT("first plus 2 bosses plus 3 gems: all met",
	       AP_ComposedGoalMet(1, 1, 0, 2, 2, 3, 3));
	EXPECT("final Oxide only ignores first win",
	       !AP_ComposedGoalMet(2, 1, 0, 0, 0, 0, 0));
	EXPECT("final Oxide only completes on final win",
	       AP_ComposedGoalMet(2, 1, 1, 0, 0, 0, 0));
	EXPECT("final plus bosses plus gems is conjunctive",
	       !AP_ComposedGoalMet(2, 1, 1, 4, 3, 5, 5));
	EXPECT("final plus bosses plus gems completes together",
	       AP_ComposedGoalMet(2, 1, 1, 4, 4, 5, 5));

	// Issue #320 acceptance 6: `disabled` (oxideRequirement 3) must drive the
	// SAME #244 credits path as `none` (0) -- neither contributes an Oxide
	// arm, so a disabled seed still finishes and rolls credits from its
	// remaining Boss/Gem arms. Pinned explicitly rather than left to fall
	// out of the else-if chain unobserved, since a disabled seed cannot even
	// pass a first/final-beaten flag of 1 (both locations are absent), so a
	// regression that started treating 3 like 1 or 2 would otherwise only
	// show up as a seed that can never complete.
	EXPECT("disabled + bosses only: three of four is short",
	       !AP_ComposedGoalMet(3, 0, 0, 4, 3, 0, 0));
	EXPECT("disabled + bosses only (boss-last): four completes",
	       AP_ComposedGoalMet(3, 0, 0, 4, 4, 0, 0));
	EXPECT("disabled + gems only: four of five is short",
	       !AP_ComposedGoalMet(3, 0, 0, 0, 0, 5, 4));
	EXPECT("disabled + gems only (gem-last): five completes",
	       AP_ComposedGoalMet(3, 0, 0, 0, 0, 5, 5));
	EXPECT("disabled + combined bosses/gems is conjunctive",
	       !AP_ComposedGoalMet(3, 0, 0, 2, 2, 3, 2));
	EXPECT("disabled + combined bosses/gems completes together",
	       AP_ComposedGoalMet(3, 0, 0, 2, 2, 3, 3));
	EXPECT("disabled reads identically to none for the same tallies",
	       AP_ComposedGoalMet(3, 0, 0, 2, 2, 3, 3) ==
	       AP_ComposedGoalMet(0, 0, 0, 2, 2, 3, 3));

	AP_GoalPresentationReset(&state);
	AP_GoalPresentationEvaluate(&state, AP_ComposedGoalMet(3, 0, 0, 4, 4, 0, 0));
	EXPECT("disabled seed: an unarmed evaluation (e.g. reconnect) queues nothing",
	       !AP_GoalPresentationClaim(&state));
	AP_GoalPresentationArm(&state);
	AP_GoalPresentationEvaluate(&state, AP_ComposedGoalMet(3, 0, 0, 4, 3, 0, 0));
	EXPECT("disabled seed: one boss short does not queue credits",
	       !state.creditsPending);
	AP_GoalPresentationEvaluate(&state, AP_ComposedGoalMet(3, 0, 0, 4, 4, 0, 0));
	EXPECT("disabled seed: the boss-last completion edge queues credits",
	       state.creditsPending);
	EXPECT("disabled seed: the pending presentation is claimed once",
	       AP_GoalPresentationClaim(&state));
	EXPECT("disabled seed: reconnect after completion does not replay credits",
	       !AP_GoalPresentationClaim(&state));

	AP_GoalPresentationReset(&state);
	AP_GoalPresentationEvaluate(&state, 1);
	EXPECT("reconnect evaluation does not replay credits",
	       !AP_GoalPresentationClaim(&state));

	AP_GoalPresentationArm(&state);
	AP_GoalPresentationEvaluate(&state, 0);
	EXPECT("an early live event does not start credits", !state.creditsPending);
	AP_GoalPresentationEvaluate(&state, 1);
	EXPECT("the later completion edge queues credits", state.creditsPending);
	EXPECT("the pending presentation is claimed once",
	       AP_GoalPresentationClaim(&state));
	EXPECT("the same presentation cannot be claimed twice",
	       !AP_GoalPresentationClaim(&state));

	AP_GoalPresentationArm(&state);
	AP_GoalPresentationEvaluate(&state, 1);
	EXPECT("a direct live completion queues immediately", state.creditsPending);
	AP_GoalPresentationReset(&state);
	EXPECT("slot reset discards a stale pending presentation",
	       !AP_GoalPresentationClaim(&state));

	printf("\n%s (%d failures, 32 checks)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
