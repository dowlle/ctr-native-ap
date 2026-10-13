// Host harness for the rank-driven stat bar (#466).
//   gcc -m32 -std=c99 -Wall -Wextra -DAP_STATBAR_PURE_ONLY tools/test-statbar-rank.c -o /tmp/test-statbar-rank && /tmp/test-statbar-rank
#define AP_STATBAR_PURE_ONLY
#include <stdio.h>
#include "../ap/ap_statbar.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

int main(void)
{
	int expect[5] = {13, 26, 39, 52, 78};
	int r, prev = 0;

	for (r = 0; r < 5; r++)
	{
		int len = AP_StatBar_LenForRank(r);
		CHECK(len == expect[r], "rank draws its table length");
		CHECK(len > prev, "every rank draws strictly longer than the one below");
		CHECK(len <= AP_STATBAR_MAX_LEN, "never past the fill ceiling");
		prev = len;
	}
	CHECK(AP_StatBar_LenForRank(3) != AP_StatBar_LenForRank(4), "ranks 3 and 4 are apart (#466)");
	CHECK(AP_StatBar_LenForRank(4) == AP_STATBAR_MAX_LEN, "rank 4 fills the bar");
	CHECK(AP_StatBar_LenForRank(-1) == 13, "negative rank clamps to rank 0");
	CHECK(AP_StatBar_LenForRank(9) == 78, "oversized rank clamps to rank 4");
	/* The value-based path cannot separate them: a rank 4 value above the class max. */
	CHECK(AP_StatBar_LenForValue(1200, 100, 1000) == AP_StatBar_LenForValue(1000, 100, 1000),
	      "value path saturates above the class max (the bug this table avoids)");

	if (failures) { printf("%d failure(s)\n", failures); return 1; }
	printf("test-statbar-rank: ok\n");
	return 0;
}
