// Host checks for the boss garage node colour on the Adventure tracker's hub map:
// gold once the boss check is checked (and only then), green while the garage is open, red while
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
		EXPECT(AP_TrackerBossNodeState(2, open) == AP_TRACKER_NODE_BEATEN, "a checked boss check is beaten, open or not");

	EXPECT(AP_TrackerBossNodeState(1, 1) == 2, "unchecked boss behind an open garage is green");
	EXPECT(AP_TrackerBossNodeState(1, 0) == 1, "unchecked boss behind a locked garage is red");

	/* No location on the bit (an optional Oxide first challenge, say): the
	 * garage gate alone decides. */
	EXPECT(AP_TrackerBossNodeState(0, 1) == 2, "no boss location, open garage is green");
	EXPECT(AP_TrackerBossNodeState(0, 0) == 1, "no boss location, locked garage is red");

	/* Colours: gold only for a beaten garage; pad state 3 (Trophy won, second
	 * stage not met) is orange like the hub map (ruling of 2026-11-02). */
	EXPECT(AP_TrackerNodeColour(AP_TrackerBossNodeState(2, 0)) == AP_TRACKER_COLOUR_GOLD, "beaten garage is gold");
	EXPECT(AP_TrackerNodeColour(3) == AP_TRACKER_COLOUR_ORANGE, "pad waiting on stage 2 is orange");
	EXPECT(AP_TrackerNodeColour(1) == AP_TRACKER_COLOUR_RED, "locked is red");
	EXPECT(AP_TrackerNodeColour(2) == AP_TRACKER_COLOUR_GREEN, "raceable is green");
	EXPECT(AP_TrackerNodeColour(4) == AP_TRACKER_COLOUR_PERIWINKLE, "tier 2 open is periwinkle");
	EXPECT(AP_TrackerNodeColour(5) == AP_TRACKER_COLOUR_GRAY, "done is gray");
	EXPECT(AP_TrackerNodeColour(0) == AP_TRACKER_COLOUR_GREEN, "no state draws green, as before");
	for (open = 0; open < 7; open++)
		EXPECT(open == AP_TRACKER_NODE_BEATEN || AP_TrackerNodeColour(open) != AP_TRACKER_COLOUR_GOLD,
		       "no pad state is gold");

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
