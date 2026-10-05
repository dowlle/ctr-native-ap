#ifndef AP_BOSS_DOOR_SCENE_LOGIC_H
#define AP_BOSS_DOOR_SCENE_LOGIC_H

#ifdef CTR_AP

#include "ap_boss_door_scene_seen.h"

// Issue #377: freestanding rules for the boss-door scene after a Trophy podium.
//
// Retail (game/Vehicle/VehBirth.c VehBirth_ShouldSpawnOutsideBoss) spawns the
// driver at the boss door instead of on the podium when the hub's four Trophy
// bits are set and its boss Key bit is not. CS_Camera_BoolGotoBoss then sees
// the driver off the podium and plays the hub's boss intro. Under AP both bit
// sets mirror RECEIVED items (AP_ApplyItems), not races won, so once enough
// Trophies arrive the condition is true after every Trophy win in that hub
// until a Key for it arrives: the scene replays each time (the #111 class).
//
// Under AP the spawn instead requires, for the podium's hub:
//   * the boss garage open under this seed's rules (AP_BossGarageOpen, the
//     same helper the garage door uses),
//   * the boss not personally beaten yet (#458 boss-won flag, read from the
//     server; never the checked location, never the received-Key mirror),
//   * this slot's seen flag read from the server (fail safe, as #421), and
//   * the scene not yet played or skipped for this hub.
// `hub` is levelID - N_SANITY_BEACH: 0..3 for the four boss hubs. Any other
// value (Gem Stone Valley, a race track) never spawns at a boss door.
static inline int AP_BossDoorHubValid(int hub)
{
	return hub >= 0 && hub < AP_BOSS_DOOR_HUB_COUNT;
}

static inline int AP_BossDoorSceneWanted(int hub, int garageOpen, int bossBeaten,
                                         int flagKnown, int seenForHub)
{
	return AP_BossDoorHubValid(hub) && garageOpen && !bossBeaten &&
	       flagKnown && !seenForHub;
}

// When a Trophy podium hands over to the boss scene (CS_Camera_BoolGotoBoss:
// the driver was spawned off the podium), the scene is taken for this hub,
// whether it then plays or the Skip Cutscenes option skips it. Either way it
// counts as seen.
static inline int AP_BossDoorSceneTaken(int isTrophyPodium, int wouldGotoBoss)
{
	return isTrophyPodium && wouldGotoBoss;
}

#endif // CTR_AP
#endif // AP_BOSS_DOOR_SCENE_LOGIC_H
