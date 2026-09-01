// Host harness for the custom Trophy cross-load ceremony latch.
//
// Build with cc -Wall -Wextra -Werror and run the resulting host executable.

#include <stdio.h>

#include "../ap/ap_custom_trophy_ceremony_logic.h"

static int failures;

static void expect(const char *name, int got, int want)
{
	printf("%s %s (got %d, want %d)\n", got == want ? "PASS" : "FAIL",
	       name, got, want);
	if (got != want)
		failures++;
}

int main(void)
{
	ap_custom_trophy_ceremony_state state;

	AP_CustomTrophyCeremonyReset(&state);
	expect("fresh session is inactive",
	       AP_CustomTrophyCeremonyActive(&state), 0);
	expect("a replay that sends no new check does not arm",
	       AP_CustomTrophyCeremonyArm(&state, 0), 0);
	expect("a newly sent custom Trophy arms across the hub load",
	       AP_CustomTrophyCeremonyArm(&state, 1), 1);
	expect("ordinary frames preserve the armed podium",
	       AP_CustomTrophyCeremonyActive(&state), 1);
	AP_CustomTrophyCeremonyEnd(&state);
	expect("podium completion clears the latch",
	       AP_CustomTrophyCeremonyActive(&state), 0);
	expect("later retail/replay podium stays inactive",
	       AP_CustomTrophyCeremonyArm(&state, 0), 0);
	AP_CustomTrophyCeremonyArm(&state, 1);
	AP_CustomTrophyCeremonyReset(&state);
	expect("fresh reconnect clears an interrupted podium",
	       AP_CustomTrophyCeremonyActive(&state), 0);

	if (failures)
	{
		printf("%d FAILURE(S)\n", failures);
		return 1;
	}
	printf("ALL PASS\n");
	return 0;
}
