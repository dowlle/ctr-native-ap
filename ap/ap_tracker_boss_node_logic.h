#ifndef AP_TRACKER_BOSS_NODE_LOGIC_H
#define AP_TRACKER_BOSS_NODE_LOGIC_H

// Map colour state for a boss garage node on the Adventure tracker's hub map.
//
// Freestanding on purpose: ap/ap_tracker.c gathers the two facts and
// tools/test-tracker-boss-node.c pins the decision on the host.
//
// A boss node has no physical pad, so it cannot go through AP_PadState. It
// returns the state codes of ap/ap_pad_state.h where they mean the same, plus
// its own code for a beaten garage, which ap_pad_colour.h turns into the
// shared boss-beaten colour:
//   6 GOLD   the boss check on the node's bit is checked (boss beaten; for
//            N. Oxide the first challenge, the node's AP_GOAL_BIT_OXIDE_FIRST)
//   2 GREEN  the garage is open under this seed's rules
//   1 RED    the garage is locked
// Gold is only for beaten garages (ruling of 2026-11-02): pad state 3 (Trophy
// won, second stage not met) is RED on the tracker and hub map from alpha4.
//
// bitState is AP_TrackerBitState(node bit): 0 no such location, 1 unchecked,
// 2 checked. garageOpen is AP_BossGarageOpen(hub - 1) for the four boss hubs
// and AP_OxideGarageOpen() for N. Oxide.
#include "ap_pad_colour.h"

#define AP_TRACKER_NODE_BEATEN AP_PAD_LOOK_STATE_BEATEN

static inline int AP_TrackerBossNodeState(int bitState, int garageOpen)
{
	if (bitState == 2)
		return AP_TRACKER_NODE_BEATEN;
	return garageOpen ? 2 : 1;
}

// The tracker map colour of a node: its display state of ap_pad_colour.h
// (AP_PadLookFromState of a pad state 1 to 5 or AP_TRACKER_NODE_BEATEN above),
// looked up in the shared pad colour table by the caller. A node with no state
// draws as open, as it always has.
static inline int AP_TrackerNodeColour(int look)
{
	return look == AP_PAD_LOOK_NONE ? AP_PAD_LOOK_OPEN : look;
}

#endif
