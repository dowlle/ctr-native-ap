// The 0.2.1 local effect ledger rows. Since #299 the marker lives in the room's
// DataStorage (tools/test-fxmarker.c); these rows are only read to migrate a
// room that has no key yet, so the row identity still decides which room a
// legacy row may seed.
#include <stdio.h>

#include "../ap/ap_fxseen_logic.h"

static int checks;
static int failures;

static void expect(const char *name, int got, int want)
{
	checks++;
	if (got != want)
	{
		printf("FAIL %s: got %d, want %d\n", name, got, want);
		failures++;
	}
}

int main(void)
{
	AP_FxSeenRow row;

	expect("four-column room row parses",
	       AP_FxSeenParseRow("archipelago.gg:59513\tseed\tslot\t25\n", &row), 1);
	expect("migration: same room row matches",
	       AP_FxSeenRowMatches(&row, "archipelago.gg:59513", "seed", "slot"), 1);
	expect("migration: other public room port does not match",
	       AP_FxSeenRowMatches(&row, "archipelago.gg:53935", "seed", "slot"), 0);
	expect("migration: local room does not match public row",
	       AP_FxSeenRowMatches(&row, "localhost:38281", "seed", "slot"), 0);
	expect("different slot stays isolated",
	       AP_FxSeenRowMatches(&row, "archipelago.gg:59513", "seed", "other"), 0);
	expect("different seed stays isolated",
	       AP_FxSeenRowMatches(&row, "archipelago.gg:59513", "other", "slot"), 0);
	expect("legacy ambiguous row fails open",
	       AP_FxSeenParseRow("seed\tslot\t25\n", &row), 0);

	printf("%s: %d checks\n", failures ? "FAIL" : "PASS", checks);
	return failures != 0;
}
