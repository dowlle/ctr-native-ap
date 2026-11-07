// Host harness for Trap Display (ap/ap_trap_display_logic.h, ruled 2026-11-06).
//
//   cc -std=c99 -Wall -Wextra -o /tmp/tdisp tools/test-trap-display.c && /tmp/tdisp

#include <stdio.h>

#include "../ap/ap_trap_display_logic.h"

static int failures;
static int checks;

static void expect(const char *name, int got, int want)
{
	checks++;
	if (got != want)
	{
		printf("FAIL %-60s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

int main(void)
{
	const unsigned T = AP_ITEM_FLAG_TRAP, P = AP_ITEM_FLAG_PROGRESSION, U = AP_ITEM_FLAG_USEFUL;
	long long loc;
	int prog = 0, filler = 0;

	expect("shown keeps a trap", (int)AP_TrapDisplayFlags(0, 0, 35014000, T), (int)T);
	expect("unknown mode keeps a trap", (int)AP_TrapDisplayFlags(7, 0, 35014000, T), (int)T);
	expect("as_progression: trap looks progression",
	       AP_ItemFlagsClass(AP_TrapDisplayFlags(1, 0, 35014000, T)), AP_ITEM_CLASS_PROGRESSION);
	expect("as_progression: filler untouched", (int)AP_TrapDisplayFlags(1, 0, 35014000, 0), 0);
	expect("as_progression: useful untouched", (int)AP_TrapDisplayFlags(1, 0, 35014000, U), (int)U);
	expect("as_progression: progression untouched", (int)AP_TrapDisplayFlags(1, 0, 35014000, P), (int)P);
	expect("useful trap keeps showing useful", (int)AP_TrapDisplayFlags(1, 0, 35014000, U | T), (int)(U | T));

	for (loc = 35014000; loc < 35014200; loc++)
	{
		int c = AP_ItemFlagsClass(AP_TrapDisplayFlags(2, 12345u, loc, T));
		if (c == AP_ITEM_CLASS_PROGRESSION)
			prog++;
		else if (c == AP_ITEM_CLASS_FILLER)
			filler++;
	}
	expect("mixed: never shows a trap", prog + filler, 200);
	expect("mixed: some look progression", prog > 60, 1);
	expect("mixed: some look filler", filler > 60, 1);
	expect("mixed: one location always looks the same",
	       (int)AP_TrapDisplayFlags(2, 99u, 35014007, T), (int)AP_TrapDisplayFlags(2, 99u, 35014007, T));
	expect("mixed: filler untouched", (int)AP_TrapDisplayFlags(2, 99u, 35014007, 0), 0);
	{
		int differ = 0;
		for (loc = 35014000; loc < 35014064; loc++)
			differ += AP_TrapDisplayMixedLooksProgression(1u, loc) != AP_TrapDisplayMixedLooksProgression(2u, loc);
		expect("mixed: another salt picks differently", differ > 8, 1);
	}
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
