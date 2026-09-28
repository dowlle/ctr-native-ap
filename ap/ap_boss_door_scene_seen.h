#ifndef AP_BOSS_DOOR_SCENE_SEEN_H
#define AP_BOSS_DOOR_SCENE_SEEN_H

// Issue #377: "the boss-door scene has played for this hub", per slot.
//
// After a Trophy podium in a boss hub whose garage is open and whose boss is
// not beaten yet, the driver spawns at the boss door and the boss intro scene
// plays (ROO_START, PAPU_START, KJOE_START, PINSTRIPE_START). Under AP that
// condition stays true for every later Trophy win in the hub until the boss
// race is won, so the scene plays once per hub per seed instead. Whether it
// already played is per-slot server state, stored exactly like the Oxide Final
// Challenge scene flag (ap_oxide_scene_seen.h): same scoping, same Get barrier,
// same fail-safe, a separate key.
//
// Value: an integer bitmask, 0..15. Bit h (AP_BOSS_DOOR_HUB_BIT(h)) is hub h,
// 0 N. Sanity Beach (Ripper Roo), 1 Lost Ruins (Papu Papu), 2 Glacier Park
// (Komodo Joe), 3 Citadel City (Pinstripe), the same order as boss_req[0..3]
// and ADV_REWARD_FIRST_BOSS_KEY + h. A scene skipped by the local Skip
// Cutscenes option counts as played.
#define AP_BOSS_DOOR_HUB_COUNT 4
#define AP_BOSS_DOOR_FLAG_ALL  0xFu
#define AP_BOSS_DOOR_HUB_BIT(h) \
	(((h) >= 0 && (h) < AP_BOSS_DOOR_HUB_COUNT) ? (1u << (h)) : 0u)

#ifdef __cplusplus
#include "ap_scene_seen_flags.h"

struct APBossDoorSceneSeen : APSceneSeenFlags {
    APBossDoorSceneSeen() : APSceneSeenFlags("ctr_boss_door_scene_v1", AP_BOSS_DOOR_FLAG_ALL) {}
    bool seenHub(int hub) const {
        unsigned b = AP_BOSS_DOOR_HUB_BIT(hub);
        return b != 0 && has(b);
    }
    // Returns true when this call is the one that recorded the hub.
    bool recordHub(int hub) {
        unsigned b = AP_BOSS_DOOR_HUB_BIT(hub);
        return b != 0 && record(b);
    }
};
#endif
#endif
