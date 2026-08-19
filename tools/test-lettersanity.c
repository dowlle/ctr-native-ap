// Out-of-engine assertions for the native Lettersanity pickup and token gates.
// Compiles the real freestanding decisions used by ap_hooks.c.
//
//   cc -Wall -Wextra -o /tmp/test-lettersanity tools/test-lettersanity.c && /tmp/test-lettersanity

#include <stdio.h>
#include "../ap/ap_lettersanity.h"

static int failures;
#define EXPECT(expr, why) do { int ok = !!(expr); printf("%-4s %s\n", ok ? "ok" : "FAIL", why); if (!ok) failures++; } while (0)

int main(void)
{
	static const int expected_level_ids[16] = {
		3, 6, 9, 8, 14, 4, 5, 0, 2, 1, 12, 15, 7, 10, 11, 13
	};
	const long selected[3] = {1001, -1, 1003};
	const unsigned char none[3] = {0, 0, 0};
	const unsigned char selected_received[3] = {1, 0, 1};
	const unsigned char all_received[3] = {1, 1, 1};
	int seen = 0;
	int row;

	for (row = 0; row < 16; row++)
	{
		int level = AP_LetterItemRowToLevelIDPure(row);
		EXPECT(level == expected_level_ids[row], "canonical letter row maps to its frozen level ID");
		EXPECT(level >= 0 && level < 16 && (seen & (1 << level)) == 0,
		       "letter row mapping is an in-range bijection");
		if (level >= 0 && level < 16)
			seen |= 1 << level;
	}
	EXPECT(seen == 0xffff, "letter row mapping covers every token track exactly once");
	EXPECT(AP_LetterItemRowToLevelIDPure(-1) == -1, "negative letter row rejected");
	EXPECT(AP_LetterItemRowToLevelIDPure(16) == -1, "past-end letter row rejected");
	EXPECT(AP_LetterItemRowToLevelIDPure(5) == 4, "Tiger Temple row maps to level ID 4");

	EXPECT(AP_LetterAvailablePure(0, 0, -1, 0), "inactive preserves vanilla pickup");
	EXPECT(AP_LetterAvailablePure(1, 1, 1001, 0), "locations-only preserves vanilla pickup");
	EXPECT(!AP_LetterAvailablePure(1, 2, -1, 1), "both mode hides an unselected letter");
	EXPECT(!AP_LetterAvailablePure(1, 2, 1001, 0), "both mode waits for selected item");
	EXPECT(AP_LetterAvailablePure(1, 2, 1001, 1), "both mode enables received selected item");
	EXPECT(!AP_LetterAvailablePure(1, 3, -1, 0), "items-only waits for every item");
	EXPECT(AP_LetterAvailablePure(1, 3, -1, 1), "items-only enables a received item");
	EXPECT(AP_LettersRequiredCountPure(1, 2, selected) == 2, "both mode requires selected count");
	EXPECT(AP_LettersRequiredCountPure(1, 3, selected) == 3, "items-only requires all three");
	EXPECT(AP_LettersRequiredMetPure(1, 2, selected, selected_received), "both mode received set is complete");
	EXPECT(!AP_LettersRequiredMetPure(1, 2, selected, none), "both mode rejects missing required items");
	EXPECT(AP_LetterTokenEarnedPure(1, 2, 1, 2, selected, selected_received), "both mode awards exact count plus received set");
	EXPECT(!AP_LetterTokenEarnedPure(0, 2, 1, 2, selected, selected_received), "loss never awards token");
	EXPECT(!AP_LetterTokenEarnedPure(1, 3, 1, 2, selected, selected_received), "wrong collected count never awards token");
	EXPECT(AP_LetterTokenEarnedPure(1, 3, 1, 3, selected, all_received), "items-only awards after all three");

	printf("\n%s\n", failures ? "FAILURES PRESENT" : "all assertions passed");
	return failures != 0;
}
