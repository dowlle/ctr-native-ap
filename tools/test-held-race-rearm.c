// cc -std=c99 -Wall -Wextra -Werror -o /tmp/test-held-race-rearm tools/test-held-race-rearm.c
#include <stdio.h>
#include "../ap/ap_held_race_rearm.h"

static int failures;
static void expect(int got, int want, const char *name)
{
	printf("%s %s (got %d, want %d)\n", got == want ? "PASS" : "FAIL", name, got, want);
	if (got != want) failures++;
}

static int step(int onTrack, int idle, int end, int lights,
                int *seen, int *best, int *cand, int *ms)
{
	int rearm = AP_HeldRaceRearmStep(onTrack, idle, end, lights, seen);
	if (rearm) { *best = 99; *cand = -1; *ms = 0; }
	return rearm;
}

static void restart_without_finish(void)
{
	int seen = 1, best = 1, cand = 1, ms = 700;
	expect(step(1, 0, 0, -1000, &seen, &best, &cand, &ms), 0, "restart load does not rearm early");
	expect(seen, 0, "restart load disarms latch");
	expect(best, 1, "prior best remains until next real attempt");
	expect(step(1, 1, 0, 3000, &seen, &best, &cand, &ms), 1, "countdown rearms after unfinished restart");
	expect(best, 99, "new attempt forgets prior first place");
	expect(cand, -1, "new attempt clears candidate");
	expect(ms, 0, "new attempt clears debounce time");
	expect(step(1, 1, 0, 2500, &seen, &best, &cand, &ms), 0, "same countdown rearms only once");
}

static void exit_and_track_change(void)
{
	int seen = 1, best = 3, cand = 3, ms = 500;
	expect(step(0, 1, 0, 0, &seen, &best, &cand, &ms), 0, "Exit to Map only disarms");
	expect(seen, 0, "off-track frame clears latch");
	expect(step(1, 1, 0, 3000, &seen, &best, &cand, &ms), 1, "next track countdown rearms");
	expect(best, 99, "next track can earn fifth after earlier third");
}

static void cup_leg_transition(void)
{
	int seen = 1, best = 1, cand = -1, ms = 0;
	expect(step(1, 1, 1, 0, &seen, &best, &cand, &ms), 0, "end of cup leg disarms");
	expect(step(1, 0, 0, -1000, &seen, &best, &cand, &ms), 0, "cup-leg load stays disarmed");
	expect(step(1, 1, 0, 3000, &seen, &best, &cand, &ms), 1, "next cup leg rearms");
	expect(best, 99, "next cup leg forgets prior first place");
}

static void pause_is_not_a_boundary(void)
{
	int seen = 1, best = 3, cand = -1, ms = 0;
	expect(step(1, 1, 0, 0, &seen, &best, &cand, &ms), 0, "live or paused frame does not rearm");
	expect(seen, 1, "live track keeps latch armed");
	expect(best, 3, "pause preserves best within attempt");
}

int main(void)
{
	restart_without_finish();
	exit_and_track_change();
	cup_leg_transition();
	pause_is_not_a_boundary();
	printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
