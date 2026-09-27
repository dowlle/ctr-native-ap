// cc -std=c99 -Wall -Wextra -Werror -o /tmp/test-oxide-cutscene tools/test-oxide-cutscene.c
//
// WO-A4 / issue #377: Oxide cutscene trigger characterization.
//
// WHAT IS ACTUALLY EXECUTED HERE. Pure production predicates:
//   ap/ap_oxide_cutscene.h  AP_OxideFinalPresentationReady -- retail/AP split
//                           AP_OxideFinalBoostMin, AP_OxideFinalGoModePure --
//                           go mode for the Final Challenge (#377)
//                           AP_OxideFinalSceneWanted -- the once-per-seed
//                           decision the three scene sites and the podium-skip
//                           keep rule share (via ap/ap_oxide_scene.c)
//   ap/ap_relic_goal.h      AP_RelicGoalMet -- what AP_OxideFinalOpen() resolves
//                           the per-seed mode + count to
//   ap/ap_oxide_encounter.h AP_OxideGarageEvaluate -- the offered encounter and
//                           whether its door is open
// plus a MODEL of the surrounding engine flow (BossCutsceneModel below). The
// model is a transcription of the real control flow, line-referenced against
// the source it mirrors; it is NOT the engine. It exists so the trigger map is
// checkable and so a later change to those functions has something to fail
// against. Rows that the model can only assert by construction are labelled.
//
// The linked composition (ap/ap_oxide_scene.c) is exercised in
// tools/test-podium-skip.c; the server flag in tools/test-oxide-scene-seen.cpp.
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
#include "../ap/ap_oxide_encounter.h"

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
	int goMode;          // AP_OxideFinalGoMode()
	int seenKnown;       // ap_net_oxide_scene_known()
	int seen;            // ap_net_oxide_scene_seen(); set when the scene plays
	int beatOxideSecond; // CHECK_ADV_BIT(rewards, ADV_REWARD_BEAT_OXIDE_SECOND)
	int spawnedOnPodium; // driver matrix == ptrSpawnType2_PosRot[1] pos
	int hub;             // gGT->levelID - GEM_STONE_VALLEY (0 = Gemstone)
};

// AP_OxideFinalSceneReady (ap/ap_oxide_scene.c) over the modelled inputs.
static int SceneReady(const struct PodiumState *s)
{
	return AP_OxideFinalSceneWanted(s->cfgActive, s->vanillaRelics, s->goMode,
	                                s->seenKnown, s->seen);
}

// Transcription of CS_Camera_BoolGotoBoss (game/233/CS_Camera.c:8-35) followed
// by the CS_Camera_ThTick_Podium tail (game/233/CS_Camera.c:~414-445) and the
// cutsceneID selection in CS_Camera_ThTick_Boss (game/233/CS_Camera.c:~66-80).
// Records the play (AP_OxideFinalSceneMarkPlayed) where the tail selects the
// Oxide index, so calling it twice models two consecutive podiums.
static int BossCutsceneModel(struct PodiumState *s)
{
	int gotoBoss = 0;

	// CS_Camera_BoolGotoBoss, relic term. The `!beatOxideSecond` guard is the
	// retail repeat suppression after the Final Challenge is beaten.
	if (s->rewardID == RW_RELIC && SceneReady(s) && !s->beatOxideSecond)
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
	// on a relic reward whose scene decision holds. Same frame, same inputs as
	// BoolGotoBoss above, and the play is recorded only after both have asked.
	if (s->rewardID == RW_RELIC && SceneReady(s))
	{
		if (s->cfgActive)
			s->seen = 1; // AP_OxideFinalSceneMarkPlayed
		return CS_OXIDE_RELICS;
	}

	// bossCutsceneIndex stays -1 -> cutsceneID = hub*2 (+1 on a Key reward).
	return (s->rewardID == RW_KEY) ? CS_BOSS_HUB_OUTRO : CS_BOSS_HUB_INTRO;
}

// Transcription of CS_Thread.c opcode 0x21 (~729-752): the PINSTRIPE_BEAT key
// scene asks for OXIDE_TROPHIES (0); redirect to OXIDE_RELICS_GEMSTONE (9)
// only on the same decision, and record the play when redirecting.
static int Opcode21Model(struct PodiumState *s)
{
	int index = 0;
	if (SceneReady(s))
	{
		index = 9;
		if (s->cfgActive)
			s->seen = 1;
	}
	return index;
}

static struct PodiumState Base(void)
{
	struct PodiumState s;
	memset(&s, 0, sizeof s);
	s.spawnedOnPodium = 1; // the ordinary case: resume driving
	return s;
}

// A seed-configured state that has the flag read and unseen.
static struct PodiumState ApRelic(int goMode)
{
	struct PodiumState s = Base();
	s.rewardID = RW_RELIC;
	s.cfgActive = 1;
	s.goMode = goMode;
	s.seenKnown = 1;
	return s;
}

// AP_OxideFinalOpen()'s slot_data half, so the rows below can be written in
// the terms a seed is actually configured with. Modes: 0 sapphire, 1 gold,
// 2 platinum, 3 any, 4 total.
static int FinalOpen(int mode, int count, int sapph, int gold, int plat)
{
	return AP_RelicGoalMet(mode, count, sapph, gold, plat);
}

// Go mode for a seed without the boost pack, from the relic gate alone
// (Oxide 1 cleared, four Keys held, no companion arms).
static int GoModeFromRelics(int finalOpen)
{
	AP_OxideGarageInputs in;
	AP_OxideGarageState st;
	memset(&in, 0, sizeof in);
	in.garageReqMet = 1;
	in.firstCleared = 1;
	in.finalRelicMet = finalOpen;
	st = AP_OxideGarageEvaluate(&in);
	return AP_OxideFinalGoModePure(st.encounter == AP_OXIDE_ENCOUNTER_FINAL,
	                               st.open, AP_OxideFinalBoostMin(0, 1, 0), -1);
}

static void TestGoMode(void)
{
	// =====================================================================
	// GO MODE. The apworld's rule for "N. Oxide's Final Challenge" in logic
	// (Rules.add_oxide_access_contract final_win_rule), term by term.
	// =====================================================================

	// Boost requirement: boost_term(boost_min=1) AND the venue's finish term.
	// usf_finish.track_finish_term: Cortex Vortex always USF (2); Oxide
	// Station USF unless shortcut_knowledge is hard, where the floor (1) stays.
	CHECK("no boost pack: no boost term (vacuous)",
	      AP_OxideFinalBoostMin(0, 0, 0) == 0 && AP_OxideFinalBoostMin(0, 1, 2) == 0);
	CHECK("Cortex Vortex venue: USF at every shortcut knowledge",
	      AP_OxideFinalBoostMin(1, 0, 0) == 2 && AP_OxideFinalBoostMin(1, 0, 1) == 2 &&
	      AP_OxideFinalBoostMin(1, 0, 2) == 2);
	CHECK("Oxide Station venue, easy/medium: USF",
	      AP_OxideFinalBoostMin(1, 1, 0) == 2 && AP_OxideFinalBoostMin(1, 1, 1) == 2);
	CHECK("Oxide Station venue, hard: first boost rank only",
	      AP_OxideFinalBoostMin(1, 1, 2) == 1);

	// Capability: one driveable racer at the needed tier.
	CHECK("USF needed, best racer at USF: go",
	      AP_OxideFinalGoModePure(1, 1, 2, 2));
	CHECK("USF needed, best racer at blue fire: go",
	      AP_OxideFinalGoModePure(1, 1, 2, 3));
	CHECK("USF needed, best racer at boost: not go",
	      !AP_OxideFinalGoModePure(1, 1, 2, 1));
	CHECK("floor needed, bare kart: not go",
	      !AP_OxideFinalGoModePure(1, 1, 1, 0));
	CHECK("pack live but no racer tier readable: not go",
	      !AP_OxideFinalGoModePure(1, 1, 1, -1));
	CHECK("no boost term: tier ignored",
	      AP_OxideFinalGoModePure(1, 1, 0, -1));

	// Encounter and door: the garage must be OFFERING the Final Challenge and
	// its door must be open. Sweep every goal, clear state and companion arm.
	for (int goal = 0; goal <= 3; goal++)
	for (int first = 0; first <= 1; first++)
	for (int optional = 0; optional <= 1; optional++)
	for (int keys = 0; keys <= 1; keys++)
	for (int relic = 0; relic <= 1; relic++)
	for (int comp = 0; comp <= 1; comp++)
	{
		AP_OxideGarageInputs in;
		AP_OxideGarageState st;
		int go, want;
		memset(&in, 0, sizeof in);
		in.goalOxide = goal;
		in.firstCleared = first;
		in.firstOptional = optional;
		in.garageReqMet = keys;
		in.finalRelicMet = relic;
		in.goalBosses = 2;
		in.bossesWon = comp ? 2 : 1;
		st = AP_OxideGarageEvaluate(&in);
		go = AP_OxideFinalGoModePure(st.encounter == AP_OXIDE_ENCOUNTER_FINAL,
		                             st.open, 0, -1);
		// apworld final_rule: Key 4 AND relics AND (companions on 101%), and
		// Oxide 1's rule; native also needs Oxide 1 CLEARED before it offers
		// the Final, unless 101% with oxide_1_optional lets it skip ahead.
		want = goal != AP_OXIDE_GOAL_DISABLED && keys && relic &&
		       (goal != AP_OXIDE_GOAL_FINAL || comp) &&
		       (first || (optional && goal == AP_OXIDE_GOAL_FINAL));
		if (go != want)
		{
			printf("FAIL  go mode: goal=%d first=%d opt=%d keys=%d relic=%d "
			       "comp=%d got=%d want=%d\n",
			       goal, first, optional, keys, relic, comp, go, want);
			failures++;
		}
	}
	printf("ok    go-mode encounter/door matrix\n");
}

int main(void)
{
	struct PodiumState s;

	TestGoMode();

	// =====================================================================
	// 1. RETAIL PARITY. Without slot_data every answer must be the vanilla
	//    18-Sapphire rule, unchanged, including the retail repeat.
	// =====================================================================
	CHECK("no slot_data: 17 relics is not ready",
	      !AP_OxideFinalPresentationReady(0, 17, 0));
	CHECK("no slot_data: 18 relics is ready",
	      AP_OxideFinalPresentationReady(0, 18, 0));
	CHECK("no slot_data: 19 relics is ready",
	      AP_OxideFinalPresentationReady(0, 19, 0));
	CHECK("no slot_data ignores the AP gate entirely (0 relics, gate open)",
	      !AP_OxideFinalPresentationReady(0, 0, 1));
	CHECK("no slot_data ignores go mode and the seen flag",
	      AP_OxideFinalSceneWanted(0, 18, 0, 0, 1) &&
	      !AP_OxideFinalSceneWanted(0, 17, 1, 1, 0));

	s = Base();
	s.rewardID = RW_RELIC;
	s.vanillaRelics = 18;
	CHECK("no slot_data: 18th relic win plays OXIDE_RELICS",
	      BossCutsceneModel(&s) == CS_OXIDE_RELICS);
	CHECK("no slot_data: records nothing", s.seen == 0);
	CHECK("no slot_data: next relic win repeats, as retail",
	      BossCutsceneModel(&s) == CS_OXIDE_RELICS);
	s.vanillaRelics = 17;
	CHECK("no slot_data: 17th relic win resumes driving",
	      BossCutsceneModel(&s) == CS_NONE);

	// =====================================================================
	// 2. WO-A4. With slot_data the presentation follows the SHIPPED relic
	//    gate (inside go mode), not the received-Sapphire count.
	// =====================================================================

	// Platinum 5: the garage loads the Final Challenge with zero Sapphires,
	// so the retail rule would NEVER play the cutscene.
	{
		int open = FinalOpen(2 /* platinum */, 5, 0, 0, 5);
		CHECK("platinum-5 seed: gate is open with 0 Sapphires", open);
		s = ApRelic(GoModeFromRelics(open));
		s.vanillaRelics = 0;
		CHECK("platinum-5 seed: relic win plays OXIDE_RELICS",
		      BossCutsceneModel(&s) == CS_OXIDE_RELICS);
	}

	// Total 40: 18 received Sapphires is NOT the gate, so the retail rule
	// would play the cutscene while the Final Challenge is still shut.
	{
		int open = FinalOpen(4 /* total */, 40, 18, 0, 0);
		CHECK("total-40 seed: gate is shut on 18 Sapphires alone", !open);
		s = ApRelic(GoModeFromRelics(open));
		s.vanillaRelics = 18;
		CHECK("total-40 seed: 18th Sapphire resumes driving, no Oxide scene",
		      BossCutsceneModel(&s) == CS_NONE);

		open = FinalOpen(4, 40, 18, 18, 4);
		CHECK("total-40 seed: gate opens at 40 summed relics", open);
		CHECK("total-40 seed: go mode at 40 summed relics",
		      GoModeFromRelics(open));
	}

	// =====================================================================
	// 3. ISSUE #377: ONCE PER SEED, ON REACHING GO MODE.
	// =====================================================================
	s = ApRelic(0);
	CHECK("relic gate open but not in go mode: no scene",
	      BossCutsceneModel(&s) == CS_NONE);
	s.goMode = 1;
	CHECK("first relic podium in go mode plays OXIDE_RELICS",
	      BossCutsceneModel(&s) == CS_OXIDE_RELICS);
	CHECK("the play is recorded", s.seen == 1);
	CHECK("second relic podium in go mode resumes driving",
	      BossCutsceneModel(&s) == CS_NONE);
	CHECK("third relic podium still resumes driving",
	      BossCutsceneModel(&s) == CS_NONE);

	// The recorded flag comes back from the server after a client restart:
	// same answer as never having left.
	s = ApRelic(1);
	s.seen = 1;
	CHECK("seen flag from the server: no scene after a restart",
	      BossCutsceneModel(&s) == CS_NONE);

	// Fail safe: before the Get reply the flag is unknown, so no auto-play,
	// and nothing is recorded.
	s = ApRelic(1);
	s.seenKnown = 0;
	CHECK("flag not read yet: no scene", BossCutsceneModel(&s) == CS_NONE);
	CHECK("flag not read yet: nothing recorded", s.seen == 0);
	s.seenKnown = 1;
	CHECK("flag read: the next go-mode relic podium plays it",
	      BossCutsceneModel(&s) == CS_OXIDE_RELICS);

	// A fresh room starts without the key: plays again once, for that room.
	s = ApRelic(1);
	CHECK("fresh room: plays once", BossCutsceneModel(&s) == CS_OXIDE_RELICS &&
	                                BossCutsceneModel(&s) == CS_NONE);

	// The key-scene chain (opcode 0x21) is the same once-per-seed scene.
	s = ApRelic(1);
	s.rewardID = RW_KEY;
	CHECK("key-scene chain in go mode redirects to OXIDE_RELICS_GEMSTONE",
	      Opcode21Model(&s) == 9 && s.seen == 1);
	CHECK("key-scene chain after the play keeps OXIDE_TROPHIES",
	      Opcode21Model(&s) == 0);
	s.rewardID = RW_RELIC;
	CHECK("relic podium after the key-scene play resumes driving",
	      BossCutsceneModel(&s) == CS_NONE);
	s = ApRelic(0);
	CHECK("key-scene chain outside go mode keeps OXIDE_TROPHIES",
	      Opcode21Model(&s) == 0 && s.seen == 0);
	s = Base();
	s.vanillaRelics = 18;
	CHECK("no slot_data: key-scene chain redirects at 18, as retail",
	      Opcode21Model(&s) == 9 && Opcode21Model(&s) == 9);

	// =====================================================================
	// 4. THE TWO SITES CANNOT DISAGREE. BoolGotoBoss and the index selection
	//    ask one decision, so a relic win never enters the boss path and
	//    then lands on the ordinary hub intro.
	// =====================================================================
	{
		int rows = 0;
		for (int cfg = 0; cfg <= 1; cfg++)
		for (int relics = 0; relics <= 18; relics++)
		for (int bits = 0; bits < 8; bits++)
		{
			int cls;
			s = Base();
			s.rewardID = RW_RELIC;
			s.cfgActive = cfg;
			s.vanillaRelics = relics;
			s.goMode = bits & 1;
			s.seenKnown = (bits >> 1) & 1;
			s.seen = (bits >> 2) & 1;
			cls = BossCutsceneModel(&s);
			if (cls == CS_BOSS_HUB_INTRO || cls == CS_BOSS_HUB_OUTRO)
			{
				printf("FAIL  relic win fell through to a hub scene: cfg=%d "
				       "relics=%d bits=%d\n", cfg, relics, bits);
				failures++;
			}
			rows++;
		}
		printf("ok    relic-win consistency over %d rows\n", rows);
	}

	// =====================================================================
	// 5. REPEAT SUPPRESSION after the Final Challenge is beaten still holds.
	// =====================================================================
	s = ApRelic(1);
	s.beatOxideSecond = 1;
	CHECK("Final Challenge already beaten: relic win resumes driving",
	      BossCutsceneModel(&s) == CS_NONE);
	CHECK("Final Challenge already beaten: nothing recorded", s.seen == 0);
	s.beatOxideSecond = 0;
	CHECK("Final Challenge not yet beaten: relic win plays OXIDE_RELICS",
	      BossCutsceneModel(&s) == CS_OXIDE_RELICS);

	// =====================================================================
	// 6. UNRELATED CLASSES stay unrelated. A Key win is the ordinary post-boss
	//    presentation and must not be diverted by any Oxide relic state.
	// =====================================================================
	s = ApRelic(1);
	s.rewardID = RW_KEY;
	CHECK("Key win is the ordinary outro even in go mode",
	      BossCutsceneModel(&s) == CS_BOSS_HUB_OUTRO && s.seen == 0);
	s.goMode = 0;
	CHECK("Key win is the ordinary outro outside go mode",
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

	// A reconnect that has not yet replayed the received items reads as zero
	// counts. Go mode must then be SHUT, never optimistically open.
	CHECK("mid-reconnect (no items replayed yet) is not go mode",
	      !GoModeFromRelics(FinalOpen(0, 18, 0, 0, 0)));

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
