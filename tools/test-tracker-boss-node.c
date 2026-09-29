// Host checks for the boss garage node colour on the Adventure tracker's hub map:
// gold once the boss check is checked, green while the garage is open, red while
// it is locked. Before this, every boss node drew green.
//
// cc -std=c99 -Wall -Wextra -Wno-unused-function -I . tools/test-tracker-boss-node.c -o /tmp/test-tracker-boss-node
// /tmp/test-tracker-boss-node

#include <stdio.h>
#include "../ap/ap_tracker_boss_node_logic.h"

static int checks, failures;
#define EXPECT(expr, why) do { checks++; if (!(expr)) { printf("FAIL %s\n", why); failures++; } } while (0)

int main(void)
{
	int open;

	for (open = 0; open < 2; open++)
		EXPECT(AP_TrackerBossNodeState(2, open) == 3, "a checked boss check is gold, open or not");

	EXPECT(AP_TrackerBossNodeState(1, 1) == 2, "unchecked boss behind an open garage is green");
	EXPECT(AP_TrackerBossNodeState(1, 0) == 1, "unchecked boss behind a locked garage is red");

	/* No location on the bit (an optional Oxide first challenge, say): the
	 * garage gate alone decides. */
	EXPECT(AP_TrackerBossNodeState(0, 1) == 2, "no boss location, open garage is green");
	EXPECT(AP_TrackerBossNodeState(0, 0) == 1, "no boss location, locked garage is red");

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
