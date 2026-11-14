// cc -std=c99 -Wall -Wextra -Werror -o /tmp/test-relic-goal tools/test-relic-goal.c
#include <stdio.h>

#include "../ap/ap_relic_goal.h"

static int failures;

#define CHECK(label, expression) do { \
	int passed = !!(expression); \
	printf("%s  %s\n", passed ? "ok  " : "FAIL", label); \
	failures += !passed; \
} while (0)

int main(void)
{
	CHECK("total 53 of 54 stays closed", !AP_RelicGoalMet(4, 54, 18, 18, 17));
	CHECK("total 54 of 54 opens", AP_RelicGoalMet(4, 54, 18, 18, 18));
	CHECK("total mode sums uneven tiers", AP_RelicGoalMet(4, 19, 18, 1, 0));
	CHECK("single-tier mode does not sum", !AP_RelicGoalMet(0, 19, 18, 18, 18));
	CHECK("any-tier mode requires one full tier", !AP_RelicGoalMet(3, 18, 17, 17, 17));
	CHECK("any-tier mode opens on one full tier", AP_RelicGoalMet(3, 18, 17, 18, 17));

	// A4-79: a count of 0 asks no relic, so every mode is met with nothing held.
	CHECK("count 0 opens sapphire mode with no relic", AP_RelicGoalMet(0, 0, 0, 0, 0));
	CHECK("count 0 opens gold mode with no relic", AP_RelicGoalMet(1, 0, 0, 0, 0));
	CHECK("count 0 opens platinum mode with no relic", AP_RelicGoalMet(2, 0, 0, 0, 0));
	CHECK("count 0 opens any-tier mode with no relic", AP_RelicGoalMet(3, 0, 0, 0, 0));
	CHECK("count 0 opens total mode with no relic", AP_RelicGoalMet(4, 0, 0, 0, 0));
	CHECK("count 1 still needs a relic", !AP_RelicGoalMet(4, 1, 0, 0, 0));

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
