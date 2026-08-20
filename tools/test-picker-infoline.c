// cc -std=c11 -Wall -Wextra -Werror -o /tmp/test-picker-infoline tools/test-picker-infoline.c
//
// The hub picker's info line, held to a WIDTH.
//
// This harness exists because the line that shipped was 50 characters, drawn
// JUSTIFY_CENTER at x=256 on a 512-wide frame, so it lost characters off both
// ends and the player could not read what was cut. Every case below composes
// the real function the picker calls, and the width assertions are the point of
// the file: a future edit that puts another seed-wide label back on this line
// fails here rather than on someone's screen.

#include <stdio.h>
#include <string.h>

#include "../ap/ap_pickerline.h"

// Mirrors of the picker's boost constants (ap/ap_capability.h), named here so
// the harness does not drag the game headers in.
#define TIER_NONE 0
#define TIER_BOOST 1
#define TIER_USF 2
#define TIER_BLUEFIRE 3

static int failures;

static void expect_text(const char *actual, const char *wanted, const char *name)
{
	int ok = (strcmp(actual, wanted) == 0);
	printf("%s  %-44s \"%s\"\n", ok ? "ok  " : "FAIL", name, actual);
	if (!ok)
	{
		printf("      wanted \"%s\"\n", wanted);
		failures++;
	}
}

static void expect_width(const char *actual, const char *name)
{
	size_t len = strlen(actual);
	int ok = (len <= AP_PICKERLINE_MAX);
	printf("%s  %-44s %zu chars\n", ok ? "ok  " : "FAIL", name, len);
	if (!ok)
	{
		printf("      over the %d-character budget\n", AP_PICKERLINE_MAX);
		failures++;
	}
}

int main(void)
{
	char line[96];

	// The composition itself.
	AP_PickerLine_Compose(line, sizeof line, TIER_NONE, TIER_BLUEFIRE, "NONE", 1);
	expect_text(line, "BOOST 0/3 NONE", "unlocked racer, no tier yet");

	AP_PickerLine_Compose(line, sizeof line, TIER_NONE, TIER_BLUEFIRE, "NONE", 0);
	expect_text(line, "BOOST 0/3 NONE  LOCKED", "locked racer, no tier yet");

	AP_PickerLine_Compose(line, sizeof line, TIER_BLUEFIRE, TIER_BLUEFIRE, "BLUE", 1);
	expect_text(line, "BOOST 3/3 BLUE", "unlocked racer at the ceiling");

	AP_PickerLine_Compose(line, sizeof line, TIER_BOOST, TIER_USF, "BOOST", 0);
	expect_text(line, "BOOST 1/2 BOOST  LOCKED", "locked racer, USF ceiling");

	// No boost capability on the seed: no BOOST clause at all.
	AP_PickerLine_Compose(line, sizeof line, -1, TIER_USF, "", 1);
	expect_text(line, "", "unlocked racer, seed grants no boost");

	AP_PickerLine_Compose(line, sizeof line, -1, TIER_USF, "", 0);
	expect_text(line, "LOCKED", "locked racer, seed grants no boost");

	// Nothing seed-wide leaks back on.
	AP_PickerLine_Compose(line, sizeof line, TIER_NONE, TIER_BLUEFIRE, "NONE", 0);
	expect_text(strstr(line, "PER-CHARACTER") ? "present" : "absent", "absent", "no ownership label on the line");
	expect_text(strstr(line, "READ-ONLY") ? "present" : "absent", "absent", "no editability marker on the line");

	// ------------------------------------------------------------------
	// The width budget. The worst case is a locked racer at the Blue Fire
	// ceiling with the longest tier name, which is what the field report hit.
	// ------------------------------------------------------------------
	AP_PickerLine_Compose(line, sizeof line, TIER_NONE, TIER_BLUEFIRE, "NONE", 0);
	expect_width(line, "worst case: locked, ceiling 3, tier NONE");

	AP_PickerLine_Compose(line, sizeof line, TIER_BOOST, TIER_BLUEFIRE, "BOOST", 0);
	expect_width(line, "worst case: locked, ceiling 3, tier BOOST");

	// Every tier name the picker can produce, locked, at the highest ceiling.
	{
		const char *names[] = {"NONE", "BOOST", "USF", "BLUE", "?"};
		size_t i;
		for (i = 0; i < sizeof names / sizeof names[0]; i++)
		{
			AP_PickerLine_Compose(line, sizeof line, TIER_BLUEFIRE, TIER_BLUEFIRE, names[i], 0);
			expect_width(line, "locked at ceiling 3, every tier name");
		}
	}

	// A caller that hands over a short buffer must still get a terminated
	// string, not a truncated write past the end.
	{
		char small[8];
		int n = AP_PickerLine_Compose(small, sizeof small, TIER_NONE, TIER_BLUEFIRE, "NONE", 0);
		int ok = (small[sizeof small - 1] == '\0') && (n == (int)(sizeof small - 1));
		printf("%s  %-44s \"%s\"\n", ok ? "ok  " : "FAIL", "short buffer stays terminated", small);
		if (!ok)
			failures++;
	}

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
