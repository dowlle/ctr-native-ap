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

	/* Colours go through the shared pad colour table (ap_pad_colour.h, pinned by
	 * tools/test-pad-colour.py): gold only for a beaten garage; pad state 3
	 * (Trophy won, second stage not met) is orange like the hub map. */
#define NODE(state) AP_TrackerNodeColour(AP_PadLookFromState((state), 0))
	EXPECT(NODE(AP_TrackerBossNodeState(2, 0)) == AP_PAD_LOOK_BOSS_BEATEN, "beaten garage is gold");
	EXPECT(NODE(3) == AP_PAD_LOOK_STAGE2_WAITING, "pad waiting on stage 2 is orange");
	EXPECT(NODE(1) == AP_PAD_LOOK_CLOSED, "locked is red");
	EXPECT(NODE(2) == AP_PAD_LOOK_OPEN, "raceable is green");
	EXPECT(NODE(4) == AP_PAD_LOOK_STAGE2_OPEN, "tier 2 open is periwinkle");
	EXPECT(NODE(5) == AP_PAD_LOOK_DONE, "done is gray");
	EXPECT(NODE(0) == AP_PAD_LOOK_OPEN, "no state draws green, as before");
	EXPECT(NODE(AP_TrackerBossNodeState(1, 1)) == AP_PAD_LOOK_OPEN, "open garage is green");
	EXPECT(NODE(AP_TrackerBossNodeState(1, 0)) == AP_PAD_LOOK_CLOSED, "locked garage is red");
	for (open = 0; open < 7; open++)
		EXPECT(open == AP_TRACKER_NODE_BEATEN || NODE(open) != AP_PAD_LOOK_BOSS_BEATEN,
		       "no pad state is gold");

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
