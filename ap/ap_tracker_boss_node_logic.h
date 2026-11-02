#ifndef AP_TRACKER_BOSS_NODE_LOGIC_H
#define AP_TRACKER_BOSS_NODE_LOGIC_H

// Map colour state for a boss garage node on the Adventure tracker's hub map.
//
// Freestanding on purpose: ap/ap_tracker.c gathers the two facts and
// tools/test-tracker-boss-node.c pins the decision on the host.
//
// A boss node has no physical pad, so it cannot go through AP_PadState. It
// returns the state codes of ap/ap_pad_state.h where they mean the same, plus
// its own code for a beaten garage, so the tracker's one colour mapping
// (AP_TrackerNodeColour) draws it:
//   6 GOLD   the boss check on the node's bit is checked (boss beaten; for
//            N. Oxide the first challenge, the node's AP_GOAL_BIT_OXIDE_FIRST)
//   2 GREEN  the garage is open under this seed's rules
//   1 RED    the garage is locked
// Gold is only for beaten garages (ruling of 2026-11-02): pad state 3 (Trophy
// won, second stage not met) is ORANGE on the tracker, as on the hub map.
//
// bitState is AP_TrackerBitState(node bit): 0 no such location, 1 unchecked,
// 2 checked. garageOpen is AP_BossGarageOpen(hub - 1) for the four boss hubs
// and AP_OxideGarageOpen() for N. Oxide.
#define AP_TRACKER_NODE_BEATEN 6

static inline int AP_TrackerBossNodeState(int bitState, int garageOpen)
{
	if (bitState == 2)
		return AP_TRACKER_NODE_BEATEN;
	return garageOpen ? 2 : 1;
}

// The tracker map colour of a node state: pad states 1 to 5 of
// ap/ap_pad_state.h and AP_TRACKER_NODE_BEATEN above.
enum
{
	AP_TRACKER_COLOUR_GREEN = 0,
	AP_TRACKER_COLOUR_RED,
	AP_TRACKER_COLOUR_ORANGE,
	AP_TRACKER_COLOUR_PERIWINKLE,
	AP_TRACKER_COLOUR_GRAY,
	AP_TRACKER_COLOUR_GOLD,
};
static inline int AP_TrackerNodeColour(int state)
{
	switch (state)
	{
	case 1: return AP_TRACKER_COLOUR_RED;
	case 3: return AP_TRACKER_COLOUR_ORANGE;
	case 4: return AP_TRACKER_COLOUR_PERIWINKLE;
	case 5: return AP_TRACKER_COLOUR_GRAY;
	case AP_TRACKER_NODE_BEATEN: return AP_TRACKER_COLOUR_GOLD;
	default: return AP_TRACKER_COLOUR_GREEN;
	}
}

#endif
