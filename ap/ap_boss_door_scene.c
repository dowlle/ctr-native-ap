#ifdef CTR_AP

// Issue #377: the once-per-hub boss-door scene decision after a Trophy podium.
//
// Kept out of ap_hooks.c so a host harness can link the real composition with
// stubbed inputs (tools/test-boss-door-scene.c), the same split as
// ap/ap_oxide_scene.c. The pure rule is AP_BossDoorSceneWanted in
// ap_boss_door_scene_logic.h; the seen flag lives in server data storage
// (ap_net.cpp, ap_boss_door_scene_seen.h).
//
// Consumers:
//   game/Vehicle/VehBirth.c VehBirth_ShouldSpawnOutsideBoss -- spawn at the
//       boss door instead of on the podium (AP_BossDoorSceneReady). Everything
//       downstream reads that spawn position, not this predicate:
//       CS_Camera_BoolGotoBoss, the podium camera hand-over and the mask hint
//       in CS_Podium.c, so they cannot disagree with it.
//   game/233/CS_Camera.c CS_Camera_ThTick_Podium -- records the hub as seen
//       when the Trophy podium hands over to the scene, watched or skipped
//       (AP_BossDoorSceneMarkPlayed).

#include <common.h>

#include "ap_hooks.h"
#include "ap_net.h"
#include "ap_boss_door_scene_logic.h"

// Only called with slot_data active; without it VehBirth keeps the retail rule.
int AP_BossDoorSceneReady(int hub)
{
	if (!AP_BossDoorHubValid(hub))
		return 0;

	return AP_BossDoorSceneWanted(
	    hub, AP_BossGarageOpen(hub),
	    AP_LocationCheckedByBit(ADV_REWARD_FIRST_BOSS_KEY + hub),
	    ap_net_boss_door_scene_known(), ap_net_boss_door_scene_seen(hub));
}

// Retail sessions (no slot_data) keep the retail behaviour and record nothing.
void AP_BossDoorSceneMarkPlayed(int hub)
{
	if (!ctr_cfg_active() || !AP_BossDoorHubValid(hub))
		return;
	ap_net_boss_door_scene_record(hub);
}

#endif // CTR_AP
