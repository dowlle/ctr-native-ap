// cc -std=c99 -Wall -Wextra -Werror -o /tmp/test-oxide-cutscene tools/test-oxide-cutscene.c
//
// WO-A4: Oxide cutscene trigger characterization.
//
// WHAT IS ACTUALLY EXECUTED HERE. Two pure predicates:
//   ap/ap_oxide_cutscene.h  AP_OxideFinalPresentationReady -- the shared gate
//                           the three relic-cutscene sites now ask
//   ap/ap_relic_goal.h      AP_RelicGoalMet -- what AP_OxideFinalOpen() resolves
//                           the per-seed mode + count to
// plus a MODEL of the surrounding engine flow (BossCutsceneModel below). The
// model is a transcription of the real control flow, line-referenced against
// the source it mirrors; it is NOT the engine. It exists so the trigger map is
// checkable and so a later change to those functions has something to fail
// against. Rows that the model can only assert by construction are labelled.
//
// WHAT IS NOT COVERED HEADLESSLY, and is therefore left as runtime debt:
//   * that the engine really reaches these predicates in the frame order the
//     model assumes (needs a Steam session);
//   * the ordinary boss-door teleport class (VehBirth_ShouldSpawnOutsideBoss),
//     which is characterized below as a FINDING but deliberately NOT changed.
#include <stdio.h>
#include <string.h>

#include "../ap/ap_oxide_cutscene.h"
#include "../ap/ap_relic_goal.h"

static int failures;

#define CHECK(label, expression) do { \
	int passed = !!(expression); \
	printf("%s  %s\n", passed ? "ok  " : "FAIL", label); \
	failures += !passed; \
} while (0)

// --- the cutscene classes the trigger map distinguishes -------------------
enum CutsceneClass
{
	CS_NONE = 0,        // resume driving; no boss cutscene
	CS_BOSS_HUB_INTRO,  // (hub*2)+0 -- ordinary hub boss intro / OXIDE_TROPHIES
	CS_BOSS_HUB_OUTRO,  // (hub*2)+1 -- ordinary post-Key presentation
	CS_OXIDE_RELICS     // 9..13, OXIDE_RELICS_<hub>: Final Challenge is open
};

// Reward ids, matching the STATIC_* values the engine compares against.
enum { RW_NONE = 0, RW_TROPHY, RW_RELIC, RW_KEY, RW_BIG1 };

struct PodiumState
{
	int rewardID;        // gGT->podiumRewardID
	int cfgActive;       // ctr_cfg_active()
	int vanillaRelics;   // gGT->currAdvProfile.numRelics
	int apFinalOpen;     // AP_OxideFinalOpen()
	int beatOxideSecond; // CHECK_ADV_BIT(rewards, ADV_REWARD_BEAT_OXIDE_SECOND)
	int spawnedOnPodium; // driver matrix == ptrSpawnType2_PosRot[1] pos
	int hub;             // gGT->levelID - GEM_STONE_VALLEY (0 = Gemstone)
};

// Transcription of CS_Camera_BoolGotoBoss (game/233/CS_Camera.c:4-28) followed
// by the CS_Camera_ThTick_Podium tail (game/233/CS_Camera.c:~374-395) and the
// cutsceneID selection in CS_Camera_ThTick_Boss (game/233/CS_Camera.c:49-62).
static int BossCutsceneModel(const struct PodiumState *s)
{
	int gotoBoss = 0;

	// CS_Camera_BoolGotoBoss, relic term. The `!beatOxideSecond` guard is the
	// repeat suppression: once the Final Challenge is beaten this never fires
	// again. That bit is NOT in any AP item pool, so AP_ApplyItems does not
	// rewrite it and the suppression survives reconnect and profile load.
	if (s->rewardID == RW_RELIC &&
	    AP_OxideFinalPresentationReady(s->cfgActive, s->vanillaRelics,
	                                   s->apFinalOpen) &&
	    !s->beatOxideSecond)
		gotoBoss = 1;

	// CS_Camera_BoolGotoBoss, Key term: you just won a boss race.
	if (s->rewardID == RW_KEY)
		gotoBoss = 1;

	// CS_Camera_BoolGotoBoss, spawn term: TeleportSelf did not put you on the
	// podium, i.e. you were placed at the boss door.
	if (!s->spawnedOnPodium)
		gotoBoss = 1;

	if (!gotoBoss)
		return CS_NONE;

	// CS_Camera_ThTick_Podium tail: the OXIDE_RELICS_<hub> index is chosen only
	// on a relic reward whose Final-Challenge gate is met. Both conditions must
	// use the SAME predicate as BoolGotoBoss above or a relic win enters the
	// boss path and then falls through to the ordinary hub intro.
	if (s->rewardID == RW_RELIC &&
	    AP_OxideFinalPresentationReady(s->cfgActive, s->vanillaRelics,
	                                   s->apFinalOpen))
		return CS_OXIDE_RELICS;

	// bossCutsceneIndex stays -1 -> cutsceneID = hub*2 (+1 on a Key reward).
	return (s->rewardID == RW_KEY) ? CS_BOSS_HUB_OUTRO : CS_BOSS_HUB_INTRO;
}

static struct PodiumState Base(void)
{
	struct PodiumState s;
	memset(&s, 0, sizeof s);
	s.spawnedOnPodium = 1; // the ordinary case: resume driving
	return s;
}

// AP_OxideFinalOpen()'s slot_data half, so the rows below can be written in
// the terms a seed is actually configured with. Modes: 0 sapphire, 1 gold,
// 2 platinum, 3 any, 4 total.
static int FinalOpen(int mode, int count, int sapph, int gold, int plat)
{
	return AP_RelicGoalMet(mode, count, sapph, gold, plat);
}

int main(void)
{
	struct PodiumState s;

	// =====================================================================
	// 1. RETAIL PARITY. Without slot_data every answer must be the vanilla
	//    18-Sapphire rule, unchanged.
	// =====================================================================
	CHECK("no slot_data: 17 relics is not ready",
	      !AP_OxideFinalPresentationReady(0, 17, 0));
	CHECK("no slot_data: 18 relics is ready",
	      AP_OxideFinalPresentationReady(0, 18, 0));
	CHECK("no slot_data: 19 relics is ready",
	      AP_OxideFinalPresentationReady(0, 19, 0));
	CHECK("no slot_data ignores the AP gate entirely (0 relics, gate open)",
	      !AP_OxideFinalPresentationReady(0, 0, 1));

	s = Base();
	s.rewardID = RW_RELIC;
	s.vanillaRelics = 18;
	CHECK("no slot_data: 18th relic win plays OXIDE_RELICS",
	      BossCutsceneModel(&s) == CS_OXIDE_RELICS);
	s.vanillaRelics = 17;
	CHECK("no slot_data: 17th relic win resumes driving",
	      BossCutsceneModel(&s) == CS_NONE);

	// =====================================================================
	// 2. THE DEFECT. With slot_data the presentation must follow the SHIPPED
	//    gate (AP_OxideFinalOpen), not the received-Sapphire count.
	// =====================================================================

	// Platinum 5: the garage loads the Final Challenge with zero Sapphires,
	// so the retail rule would NEVER play the cutscene.
	{
		int open = FinalOpen(2 /* platinum */, 5, 0, 0, 5);
		CHECK("platinum-5 seed: gate is open with 0 Sapphires", open);
		CHECK("platinum-5 seed: presentation is ready despite 0 Sapphires",
		      AP_OxideFinalPresentationReady(1, 0, open));

		s = Base();
		s.rewardID = RW_RELIC;
		s.cfgActive = 1;
		s.vanillaRelics = 0;
		s.apFinalOpen = open;
		CHECK("platinum-5 seed: relic win plays OXIDE_RELICS",
		      BossCutsceneModel(&s) == CS_OXIDE_RELICS);
	}

	// Total 40: 18 received Sapphires is NOT the gate, so the retail rule
	// would play the cutscene while the Final Challenge is still shut.
	{
		int open = FinalOpen(4 /* total */, 40, 18, 0, 0);
		CHECK("total-40 seed: gate is shut on 18 Sapphires alone", !open);
		CHECK("total-40 seed: presentation is NOT ready on 18 Sapphires",
		      !AP_OxideFinalPresentationReady(1, 18, open));

		s = Base();
		s.rewardID = RW_RELIC;
		s.cfgActive = 1;
		s.vanillaRelics = 18;
		s.apFinalOpen = open;
		CHECK("total-40 seed: 18th Sapphire resumes driving, no Oxide scene",
		      BossCutsceneModel(&s) == CS_NONE);

		open = FinalOpen(4, 40, 18, 18, 4);
		CHECK("total-40 seed: gate opens at 40 summed relics", open);
		CHECK("total-40 seed: presentation ready at 40 summed relics",
		      AP_OxideFinalPresentationReady(1, 18, open));
	}

	// Default sapphire-18 seed still behaves exactly like retail.
	{
		CHECK("sapphire-18 seed: shut at 17",
		      !AP_OxideFinalPresentationReady(1, 17, FinalOpen(0, 18, 17, 0, 0)));
		CHECK("sapphire-18 seed: open at 18",
		      AP_OxideFinalPresentationReady(1, 18, FinalOpen(0, 18, 18, 0, 0)));
	}

	// =====================================================================
	// 3. THE TWO SITES CANNOT DISAGREE. BoolGotoBoss and the index selection
	//    ask one predicate, so a relic win never enters the boss path and
	//    then lands on the ordinary hub intro.
	// =====================================================================
	{
		int relics, open, mode;
		int rows = 0;

		for (mode = 0; mode <= 4; mode++)
		for (relics = 0; relics <= 18; relics++)
		{
			open = FinalOpen(mode, 18, relics, 0, 0);
			s = Base();
			s.rewardID = RW_RELIC;
			s.cfgActive = 1;
			s.vanillaRelics = relics;
			s.apFinalOpen = open;

			// A relic win either plays OXIDE_RELICS or resumes driving. It must
			// never resolve to the ordinary hub intro/outro.
			if (BossCutsceneModel(&s) == CS_BOSS_HUB_INTRO ||
			    BossCutsceneModel(&s) == CS_BOSS_HUB_OUTRO)
			{
				printf("FAIL  relic win fell through to a hub scene: mode=%d relics=%d\n",
				       mode, relics);
				failures++;
			}
			rows++;
		}
		printf("ok    relic-win consistency over %d mode/count rows\n", rows);
	}

	// =====================================================================
	// 4. REPEAT SUPPRESSION. Once the Final Challenge is beaten, the relic
	//    cutscene never fires again -- and that bit is not an AP item mirror,
	//    so the suppression survives reconnect and profile load.
	// =====================================================================
	s = Base();
	s.rewardID = RW_RELIC;
	s.cfgActive = 1;
	s.apFinalOpen = 1;
	s.beatOxideSecond = 1;
	CHECK("Final Challenge already beaten: relic win resumes driving",
	      BossCutsceneModel(&s) == CS_NONE);
	s.beatOxideSecond = 0;
	CHECK("Final Challenge not yet beaten: relic win plays OXIDE_RELICS",
	      BossCutsceneModel(&s) == CS_OXIDE_RELICS);

	// =====================================================================
	// 5. UNRELATED CLASSES stay unrelated. A Key win is the ordinary post-boss
	//    presentation and must not be diverted by any Oxide relic state.
	// =====================================================================
	s = Base();
	s.rewardID = RW_KEY;
	s.cfgActive = 1;
	s.apFinalOpen = 1;
	CHECK("Key win is the ordinary outro even with the Oxide gate open",
	      BossCutsceneModel(&s) == CS_BOSS_HUB_OUTRO);
	s.apFinalOpen = 0;
	CHECK("Key win is the ordinary outro with the Oxide gate shut",
	      BossCutsceneModel(&s) == CS_BOSS_HUB_OUTRO);

	s = Base();
	s.rewardID = RW_TROPHY;
	s.cfgActive = 1;
	s.spawnedOnPodium = 0; // teleported to the boss door
	CHECK("boss-door teleport plays the hub intro, not an Oxide scene",
	      BossCutsceneModel(&s) == CS_BOSS_HUB_INTRO);
	s.spawnedOnPodium = 1;
	CHECK("ordinary trophy win on the podium resumes driving",
	      BossCutsceneModel(&s) == CS_NONE);

	// =====================================================================
	// 6. STATELESSNESS -> reconnect, profile load and hub re-entry. The
	//    predicate holds no latch of its own, so the same inputs give the
	//    same answer no matter how much churn happened in between. This is
	//    what makes the three sites truthful across those three events: they
	//    recompute from server-derived state every time they are asked.
	// =====================================================================
	{
		int i, stable = 1;
		int first = AP_OxideFinalPresentationReady(1, 4, FinalOpen(1, 6, 0, 6, 0));
		for (i = 0; i < 64; i++)
		{
			// Interleave the opposite answer, mimicking a session that
			// disconnects (counts drop to zero) and reconnects.
			(void)AP_OxideFinalPresentationReady(1, 0, 0);
			(void)AP_OxideFinalPresentationReady(0, 18, 0);
			if (AP_OxideFinalPresentationReady(1, 4, FinalOpen(1, 6, 0, 6, 0)) != first)
				stable = 0;
		}
		CHECK("stateless across 64 interleaved disconnect/reconnect answers",
		      stable && first == 1);
	}

	// A reconnect that has not yet replayed the received items reads as zero
	// counts. The gate must then be SHUT, never optimistically open.
	CHECK("mid-reconnect (no items replayed yet) is shut",
	      !AP_OxideFinalPresentationReady(1, 0, FinalOpen(0, 18, 0, 0, 0)));

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
