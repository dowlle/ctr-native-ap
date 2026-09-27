#ifdef CTR_AP

// Issue #377: the once-per-seed Oxide Final Challenge scene decision.
//
// Kept out of ap_hooks.c so host harnesses can link the real composition with
// stubbed inputs (tools/test-oxide-scene.c, tools/test-podium-skip.c), the same
// split as ap/ap_podium_skip.c. The pure rule is AP_OxideFinalSceneWanted in
// ap_oxide_cutscene.h; the go-mode gather is AP_OxideFinalGoMode in ap_hooks.c;
// the seen flag lives in server data storage (ap_net.cpp, ap_oxide_scene_seen.h).
//
// Consumers, all through AP_OxideFinalSceneReady:
//   game/233/CS_Camera.c CS_Camera_BoolGotoBoss   -- relic podium goes to a scene
//   game/233/CS_Camera.c CS_Camera_ThTick_Podium  -- that scene is OXIDE_RELICS
//   game/233/CS_Thread.c opcode 0x21              -- key-scene chain redirect
//   ap/ap_podium_skip.c  AP_ShouldSkipPodium      -- keep that relic podium

#include <common.h>

#include "ap_hooks.h"
#include "ap_net.h"
#include "ap_oxide_cutscene.h"

int AP_OxideFinalSceneReady(int vanillaRelics)
{
	int cfgActive = ctr_cfg_active();

	if (!cfgActive)
		return AP_OxideFinalSceneWanted(0, vanillaRelics, 0, 0, 0);

	return AP_OxideFinalSceneWanted(1, vanillaRelics, AP_OxideFinalGoMode(),
	                                ap_net_oxide_scene_known(),
	                                ap_net_oxide_scene_seen());
}

// Record the play the moment a site commits to the Oxide relic scene index.
// Retail sessions (no slot_data) keep the retail repeat and record nothing.
void AP_OxideFinalSceneMarkPlayed(void)
{
	if (!ctr_cfg_active())
		return;
	ap_net_oxide_scene_record();
}

#endif // CTR_AP
