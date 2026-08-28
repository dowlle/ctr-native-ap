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
	expect("same room reconnect matches",
	       AP_FxSeenRowMatches(&row, "archipelago.gg:59513", "seed", "slot"), 1);
	expect("fresh public room does not inherit",
	       AP_FxSeenRowMatches(&row, "archipelago.gg:53935", "seed", "slot"), 0);
	expect("fresh local room does not inherit public room",
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
