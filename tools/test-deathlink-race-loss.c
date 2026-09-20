// cc -std=c99 -Wall -Wextra -Werror -o /tmp/test-deathlink-race-loss tools/test-deathlink-race-loss.c
//
// #286 DeathLink receive policy: the pure decision production uses to choose
// between the existing mask reset (modes 1/2), the new race-loss (mode 3), and
// the queued wait. Asserts the value-3 behavior and that values 0/1/2 and the
// unknown-future-value fallback are unchanged.
#include <stdio.h>
#include "../ap/ap_race_attempt_logic.h"

static int failures;
static void expect(int got, int want, const char *name)
{
	printf("%s %s (got %d, want %d)\n", got == want ? "PASS" : "FAIL",
	       name, got, want);
	if (got != want)
		failures++;
}

int main(void)
{
	int mode;

	/* A pending death outside a live adventure race window never acts. */
	for (mode = 0; mode <= 4; mode++)
	{
		expect(AP_DeathLinkReceiveDecision(mode, 0, 1, 1), AP_DL_RECV_WAIT,
		       "no pending death -> wait");
		expect(AP_DeathLinkReceiveDecision(mode, 1, 0, 1), AP_DL_RECV_WAIT,
		       "non-adventure -> wait");
		expect(AP_DeathLinkReceiveDecision(mode, 1, 1, 0), AP_DL_RECV_WAIT,
		       "outside race window -> wait");
	}

	/* Off ignores a pending death. */
	expect(AP_DeathLinkReceiveDecision(0, 1, 1, 1), AP_DL_RECV_WAIT,
	       "mode 0 (off) ignores the death");

	/* Existing enabled values keep the mask reset. */
	expect(AP_DeathLinkReceiveDecision(1, 1, 1, 1), AP_DL_RECV_MASK_RESET,
	       "mode 1 (mask_reset) unchanged");
	expect(AP_DeathLinkReceiveDecision(2, 1, 1, 1), AP_DL_RECV_MASK_RESET,
	       "mode 2 (any_hit) unchanged");

	/* The new value selects the race loss only in a valid race window. */
	expect(AP_DeathLinkReceiveDecision(3, 1, 1, 1), AP_DL_RECV_RACE_LOSS,
	       "mode 3 (race_loss) in window -> race loss");
	expect(AP_DeathLinkReceiveDecision(3, 1, 1, 0), AP_DL_RECV_WAIT,
	       "mode 3 outside window stays queued");

	/* An unknown future nonzero value degrades to the old-client mask reset. */
	expect(AP_DeathLinkReceiveDecision(4, 1, 1, 1), AP_DL_RECV_MASK_RESET,
	       "unknown future mode -> mask-reset fallback");

	printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
