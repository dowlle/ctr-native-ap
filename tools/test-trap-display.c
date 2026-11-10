// Host harness for Trap Display (ap/ap_trap_display_logic.h, ruled 2026-11-06).
//
//   cc -std=c99 -Wall -Wextra -o /tmp/tdisp tools/test-trap-display.c && /tmp/tdisp

#include <stdio.h>
#include <string.h>

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
	expect("shown keeps the ordinary name", AP_TrapDisplayName(0, 0, 35014000, T, 0) == NULL, 1);
	expect("unknown mode keeps the ordinary name", AP_TrapDisplayName(7, 0, 35014000, T, 0) == NULL, 1);
	expect("as_progression hides the trap name",
	       strcmp(AP_TrapDisplayName(1, 0, 35014000, T, 0), "PROGRESSION ITEM"), 0);
	expect("collected trap keeps the ordinary name", AP_TrapDisplayName(1, 0, 35014000, T, 1) == NULL, 1);
	expect("ordinary progression keeps its name", AP_TrapDisplayName(1, 0, 35014000, P, 0) == NULL, 1);
	expect("ordinary filler keeps its name", AP_TrapDisplayName(1, 0, 35014000, 0, 0) == NULL, 1);
	expect("useful trap keeps its ordinary name", AP_TrapDisplayName(1, 0, 35014000, U | T, 0) == NULL, 1);
	for (loc = 35014000; loc < 35014200; loc++)
	{
		const char *name = AP_TrapDisplayName(2, 12345u, loc, T, 0);
		unsigned display = AP_TrapDisplayFlags(2, 12345u, loc, T);
		if (!name || strcmp(name, display & P ? "PROGRESSION ITEM" : "FILLER ITEM"))
			failures++;
		if (AP_TrapDisplayName(2, 12345u, loc, T, 1) != NULL)
			failures++;
	}
	checks += 400;
	{
		int differ = 0;
		for (loc = 35014000; loc < 35014064; loc++)
			differ += AP_TrapDisplayMixedLooksProgression(1u, loc) != AP_TrapDisplayMixedLooksProgression(2u, loc);
		expect("mixed: another salt picks differently", differ > 8, 1);
	}

	// A4-50 (ruled 2026-11-10): in the Wumpa Shop a disguised trap picks its
	// label per location in either disguise mode; other displays keep the
	// mode's own mapping.
	expect("shop: shown keeps a trap", (int)AP_TrapDisplayShopFlags(0, 0, 35014000, T), (int)T);
	expect("shop: unknown mode keeps a trap", (int)AP_TrapDisplayShopFlags(7, 0, 35014000, T), (int)T);
	expect("shop: filler untouched", (int)AP_TrapDisplayShopFlags(1, 0, 35014000, 0), 0);
	expect("shop: useful trap keeps showing useful",
	       (int)AP_TrapDisplayShopFlags(1, 0, 35014000, U | T), (int)(U | T));
	expect("shop: collected trap keeps the ordinary name",
	       AP_TrapDisplayShopName(1, 0, 35014000, T, 1) == NULL, 1);
	expect("shop: shown keeps the ordinary name",
	       AP_TrapDisplayShopName(0, 0, 35014000, T, 0) == NULL, 1);
	{
		int shopProg = 0, shopFiller = 0, agree = 0;
		for (loc = 35014000; loc < 35014200; loc++)
		{
			unsigned f1 = AP_TrapDisplayShopFlags(1, 12345u, loc, T);
			unsigned f2 = AP_TrapDisplayShopFlags(2, 12345u, loc, T);
			const char *n1 = AP_TrapDisplayShopName(1, 12345u, loc, T, 0);
			const char *n2 = AP_TrapDisplayShopName(2, 12345u, loc, T, 0);
			if (f1 & P)
				shopProg++;
			else
				shopFiller++;
			if (f1 != f2 || !n1 || !n2 || strcmp(n1, n2) ||
			    strcmp(n1, f1 & P ? "PROGRESSION ITEM" : "FILLER ITEM"))
				failures++;
			if (AP_ItemFlagsClass(f1) == AP_ItemFlagsClass(AP_TrapDisplayFlags(2, 12345u, loc, T)))
				agree++;
		}
		checks += 400;
		expect("shop: as_progression splits per location too", shopProg > 60 && shopFiller > 60, 1);
		expect("shop: both modes pick from one hash", agree, 200);
	}
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
