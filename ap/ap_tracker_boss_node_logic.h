#ifndef AP_TRACKER_BOSS_NODE_LOGIC_H
#define AP_TRACKER_BOSS_NODE_LOGIC_H

// Map colour state for a boss garage node on the Adventure tracker's hub map.
//
// Freestanding on purpose: ap/ap_tracker.c gathers the two facts and
// tools/test-tracker-boss-node.c pins the decision on the host.
//
// A boss node has no physical pad, so it cannot go through AP_PadState. It
// returns the same state codes as ap/ap_pad_state.h so the tracker's one pad
// colour mapping draws it:
//   3 GOLD   the boss check on the node's bit is checked (boss beaten; for
//            N. Oxide the first challenge, the node's AP_GOAL_BIT_OXIDE_FIRST)
//   2 GREEN  the garage is open under this seed's rules
//   1 RED    the garage is locked
//
// bitState is AP_TrackerBitState(node bit): 0 no such location, 1 unchecked,
// 2 checked. garageOpen is AP_BossGarageOpen(hub - 1) for the four boss hubs
// and AP_OxideGarageOpen() for N. Oxide.
static inline int AP_TrackerBossNodeState(int bitState, int garageOpen)
{
	if (bitState == 2)
		return 3;
	return garageOpen ? 2 : 1;
}

#endif
