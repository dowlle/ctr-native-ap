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

	printf("\n%s (%d failures, 19 checks)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
