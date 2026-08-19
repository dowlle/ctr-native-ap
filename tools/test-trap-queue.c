#include <stdio.h>

#include "../ap/ap_trap_queue_logic.h"

static int failures;

static void expect(const char *name, int got, int want)
{
	if (got != want)
	{
		printf("FAIL %-42s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

int main(void)
{
	expect("first mid-race receipt fires",
	       AP_TrapPrimedActionFor(1, 1, 0, 0, 0, 0), AP_TRAP_PRIMED_FIRE);
	expect("duplicate mid-race receipt queues",
	       AP_TrapPrimedActionFor(1, 1, 0, 0, 0, 1), AP_TRAP_PRIMED_WAIT);
	expect("queued duplicate fires after predecessor",
	       AP_TrapPrimedActionFor(1, 1, 0, 0, 0, 0), AP_TRAP_PRIMED_FIRE);
	expect("race exit demotes instant receipt",
	       AP_TrapPrimedActionFor(1, 0, 0, 0, 0, 1), AP_TRAP_PRIMED_DEMOTE_INSTANT);
	expect("outside-race receipt stays primed",
	       AP_TrapPrimedActionFor(0, 0, 0, 0, 0, 0), AP_TRAP_PRIMED_WAIT);
	expect("lap window rolls once",
	       AP_TrapPrimedActionFor(0, 1, 1, 0, 0, 0), AP_TRAP_PRIMED_ROLL_DELAY);
	expect("unexpired delay advances",
	       AP_TrapPrimedActionFor(0, 1, 1, 1, 0, 0), AP_TRAP_PRIMED_TICK_DELAY);
	expect("mature duplicate waits",
	       AP_TrapPrimedActionFor(0, 1, 1, 1, 1, 1), AP_TRAP_PRIMED_WAIT);
	expect("mature duplicate fires after predecessor",
	       AP_TrapPrimedActionFor(0, 1, 1, 1, 1, 0), AP_TRAP_PRIMED_FIRE);

	printf("%s: 9 checks\n", failures ? "FAIL" : "PASS");
	return failures != 0;
}
