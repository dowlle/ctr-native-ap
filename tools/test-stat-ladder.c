// cc -std=c11 -Wall -Wextra -Werror -o /tmp/test-stat-ladder tools/test-stat-ladder.c
#include <stdio.h>

#include "../ap/ap_stat_ladder.h"

static int failures;

static void expect(int actual, int wanted, const char *name)
{
	printf("%s  %-34s %d\n", actual == wanted ? "ok  " : "FAIL", name, actual);
	if (actual != wanted)
		failures++;
}

int main(void)
{
	const int speed[4] = {12950, 13140, 13520, 13900};
	const int boost_speed[4] = {14450, 14640, 15020, 15400};
	const int accel[4] = {448, 480, 512, 544};
	const int turn_rate[4] = {24, 26, 28, 30};
	const int drift_turn[4] = {5, 10, 14, 18};
	const int response[4] = {4000, 4500, 5000, 5500};
	const int shuffled[4] = {30, 24, 28, 26};

	expect(AP_StatLadderValue(speed, 4), 14280, "VERY HIGH top speed");
	expect(AP_StatLadderValue(boost_speed, 4), 15780, "VERY HIGH boosted speed");
	expect(AP_StatLadderValue(accel, 4), 576, "VERY HIGH acceleration");
	expect(AP_StatLadderValue(turn_rate, 4), 32, "VERY HIGH turn rate");
	expect(AP_StatLadderValue(drift_turn, 4), 22, "VERY HIGH drift turn");
	expect(AP_StatLadderValue(response, 4), 6000, "VERY HIGH turn response");
	expect(AP_StatLadderValue(shuffled, 0), 24, "anchors sort weakest-first");
	expect(AP_StatLadderValue(shuffled, 3), 30, "rank 3 is best vanilla anchor");
	expect(AP_StatLadderValue(shuffled, 4), 32, "rank 4 continues top interval");
	expect(AP_StatLadderValue(shuffled, -1), 24, "negative rank clamps to floor");
	expect(AP_StatLadderValue(shuffled, 99), 32, "excess rank clamps to capstone");

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
