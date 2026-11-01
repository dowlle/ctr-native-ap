// cc -std=c11 -Wall -Wextra -Werror -o /tmp/test-editstat-rank tools/test-editstat-rank.c
//
// Editable stats as a choice between the rank values (owner ruling, 2026-10-29), driven
// through ap/ap_editstat_rank_logic.h and the real ladder (ap/ap_stat_ladder.h).
#include <stdio.h>

#include "../ap/ap_stat_ladder.h"
#include "../ap/ap_editstat_rank_logic.h"

static int checks, failures;
#define EXPECT(actual, wanted, name) do { int a_ = (actual), w_ = (wanted); checks++; \
	if (a_ != w_) { failures++; printf("FAIL %-48s got %d want %d\n", name, a_, w_); } } while (0)

static void ladder(const int anchors[4], int out[AP_EDITRANK_COUNT])
{
	for (int r = 0; r < AP_EDITRANK_COUNT; r++)
		out[r] = AP_StatLadderValue(anchors, r);
}

int main(void)
{
	const int speed[4] = {12950, 13140, 13520, 13900};
	const int turn[4] = {24, 26, 28, 30};
	int sp[AP_EDITRANK_COUNT], tu[AP_EDITRANK_COUNT];
	int v, presses;

	ladder(speed, sp);
	ladder(turn, tu);

	// Five distinct rank values per stat.
	EXPECT(sp[4], 14280, "VERY HIGH top speed from the ladder");
	EXPECT(tu[4], 32, "VERY HIGH turn rate from the ladder");

	// A press lands on the next rank value, in both directions.
	EXPECT(AP_EditRank_Step(sp, 13520, 1), 13900, "up from MEDIUM is HIGH");
	EXPECT(AP_EditRank_Step(sp, 13520, -1), 13140, "down from MEDIUM is LOW");
	EXPECT(AP_EditRank_Step(sp, 13900, 1), 14280, "up from HIGH is VERY HIGH");
	EXPECT(AP_EditRank_Step(tu, 28, 1), 30, "turn up is the next rank value, not +1");

	// The ends hold.
	EXPECT(AP_EditRank_Step(sp, 14280, 1), 14280, "VERY HIGH is the top");
	EXPECT(AP_EditRank_Step(sp, 12950, -1), 12950, "VERY LOW is the bottom");

	// Every value reachable by presses is a rank value, from every start.
	for (int start = 0; start < AP_EDITRANK_COUNT; start++)
	{
		v = sp[start];
		for (presses = 0; presses < 12; presses++)
		{
			v = AP_EditRank_Step(sp, v, presses < 6 ? 1 : -1);
			int ok = 0;
			for (int r = 0; r < AP_EDITRANK_COUNT; r++)
				ok |= v == sp[r];
			EXPECT(ok, 1, "a press only reaches a rank value");
		}
	}

	// A stored free value (an older package) snaps to the nearest rank value.
	EXPECT(AP_EditRank_Snap(sp, 13300), 13140, "free value below the midpoint snaps down");
	EXPECT(AP_EditRank_Snap(sp, 13400), 13520, "free value above the midpoint snaps up");
	EXPECT(AP_EditRank_Snap(sp, 13330), 13140, "a tie goes to the lower rank");
	EXPECT(AP_EditRank_Snap(sp, 0), 12950, "zero snaps to VERY LOW");
	EXPECT(AP_EditRank_Snap(sp, 30000), 14280, "a huge value snaps to VERY HIGH");
	EXPECT(AP_EditRank_Step(sp, 13300, 1), 13520, "a press from a free value moves from its snapped rank");
	EXPECT(AP_EditRank_Snap(sp, 13520), 13520, "a rank value is its own snap");

	// A flat ladder (no spread) stays put and does not loop.
	{
		const int flat[AP_EDITRANK_COUNT] = {7, 7, 7, 7, 7};
		EXPECT(AP_EditRank_Step(flat, 7, 1), 7, "flat ladder up");
		EXPECT(AP_EditRank_Step(flat, 7, -1), 7, "flat ladder down");
	}

	printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures != 0;
}
