#ifdef CTR_AP

// Local podium-skip runtime (issue #285). The composition AP_ShouldSkipPodium
// and the mutation AP_SkipPodium live here rather than inside ap_hooks.c so a
// host harness can link the REAL production path (tools/test-podium-skip.c),
// with stubbed engine state, exactly as ap/ap_hit_bots.c is linked by its own
// gather harness. The freestanding policy itself stays in
// ap_podium_skip_logic.h; this unit only gathers the live engine facts and
// applies the decision.
//
// Split out on 2026-09-21 because the previous harness only exercised
// AP_PodiumSkipDecision and therefore missed two production defects: the Oxide
// predicate omitted the beat-Oxide suppression, and the call site did not
// enforce its own Adventure-relic invariant. See ap_podium_skip_logic.h
// AP_PodiumRelicWillGotoOxide for the shared predicate both the skip and the
// cutscene selector consume.

#include <common.h>

#include "ap_hooks.h"
#include "ap_podium_skip_logic.h"
#include "ap_oxide_cutscene.h" // AP_OxideFinalEncounterPresentationReady (WO-A4)

// Issue #285. The vanilla Oxide relic threshold counts the 18 sapphire relic
// bits. GAMEPROG_AdvPercent folds them into currAdvProfile.numRelics only on
// the NEXT hub load (UI_INSTANCE_InitAll), so at the race-end skip point that
// counter is one race stale. Count the raw bits instead: RR_EndEvent_UnlockAward
// has already set the just-won relic, so this reads the threshold as the podium
// will. AP-active sessions ignore this value (the per-seed gate decides), but a
// session without slot_data still needs the retail rule to stay exact.
static int AP_VanillaRelicCountNow(void)
{
	int count = 0;
	int i;

	for (i = 0; i < 18; i++)
		if (CHECK_ADV_BIT(sdata->advProgress.rewards,
		                  ADV_REWARD_FIRST_SAPPHIRE_RELIC + i) != 0)
			count++;

	return count;
}

// Issue #285. Classify the pending hub podium from the live mode words and the
// reward id, apply the local Skip Podium Ceremonies preference, and preserve
// every ceremony the ruling keeps. Boss and cup classification takes priority;
// a relic whose Oxide predicate is true keeps STATIC_RELIC so
// CS_Camera_BoolGotoBoss can still select the Oxide transition. The decision
// itself is the freestanding AP_PodiumSkipDecision in ap_podium_skip_logic.h.
int AP_ShouldSkipPodium(int rewardId)
{
	struct GameTracker *gGT;
	int oxideRelicQualifying = 0;

	if (sdata == NULL || sdata->gGT == NULL)
		return 0;
	if (!g_config.skipPodium)
		return 0;

	gGT = sdata->gGT;

	// Only a relic can open the Oxide Final Challenge, so only a relic needs the
	// predicate. It is the SAME composition CS_Camera_BoolGotoBoss consumes,
	// including the ADV_REWARD_BEAT_OXIDE_SECOND suppression, so the skip
	// decision and the cutscene selector can never disagree: after Oxide's
	// second defeat no transition can play and the relic podium is skippable.
	oxideRelicQualifying = AP_PodiumRelicWillGotoOxide(
	    rewardId == STATIC_RELIC,
	    AP_OxideFinalEncounterPresentationReady(
	        ctr_cfg_active(), AP_VanillaRelicCountNow(),
	        AP_OxideOffersFinalChallenge(), AP_OxideFinalOpen()),
	    CHECK_ADV_BIT(sdata->advProgress.rewards, ADV_REWARD_BEAT_OXIDE_SECOND) != 0);

	return AP_PodiumSkipDecision(
	    g_config.skipPodium,
	    rewardId,
	    IS_BOSS_RACE(gGT->gameMode1),
	    ((gGT->gameMode1 & ADVENTURE_CUP) != 0) ||
	        ((gGT->gameMode2 & CUP_ANY_KIND) != 0),
	    (gGT->gameMode1 & RELIC_RACE) != 0,
	    (gGT->gameMode2 & TOKEN_RACE) != 0,
	    oxideRelicQualifying);
}

// Issue #285. Apply the skip: drop the pending podium selection and clear the
// count-up and freeze bits a watched ceremony clears by its end, so a skipped
// ceremony ends in exactly the same state as a watched one. Must be called
// AFTER the race's reward notification (AP_NotifyAdvReward and friends) so
// every check and item feed the ceremony would have produced still goes out.
void AP_SkipPodium(int rewardId)
{
	struct GameTracker *gGT;

	if (sdata == NULL || sdata->gGT == NULL)
		return;

	gGT = sdata->gGT;

	if (!AP_ShouldSkipPodium(rewardId))
		return;

	gGT->podiumRewardID = NOFUNC;
	gGT->gameMode2 = AP_PodiumSkipCleanGameMode2(gGT->gameMode2);
}

#endif // CTR_AP
