/*
 * Host harness for the local podium-skip runtime (issue #285).
 *
 * Behavioral assertions on PRODUCTION code, no source-string checks:
 *
 *   - ap/ap_podium_skip.c is compiled in and driven directly with stubbed engine
 *     state (the ap/ap_hit_bots.c gather pattern). That means the harness runs
 *     the real wrapper AP_ShouldSkipPodium and the real mutation AP_SkipPodium,
 *     not a copy of their policy: a defect in the gather, in the shared Oxide
 *     predicate, or in the mutation is a failure here.
 *   - ap/ap_podium_skip_logic.h supplies the freestanding decision and the
 *     0xd call-site gate; ap/ap_oxide_cutscene.h supplies the production Oxide
 *     presentation predicate. Both are the same units the game build consumes.
 *   - platform/native_config.c is compiled in for the skip_podium round trip.
 *
 * The one thing that cannot be linked off-engine is game/233/CS_Camera.c
 * itself, so the BoolGotoBoss truth table below composes the SAME production
 * helpers the real site uses in one column, and transcribes the kept-verbatim
 * non-AP expression in the other, then asserts the two agree with the retail
 * rule for every input. The transcription is line-referenced to the source it
 * mirrors (the tools/test-oxide-cutscene.c precedent) and exists only to pin the
 * "sharing must not change a non-AP result" requirement.
 *
 * Build line (parsed by tools/ci/run-harnesses.py):
 */

// cc -m32 -Wall -Wextra -DCTR_AP -DCTR_NATIVE -DBUILD=926 -I ap -I . -I include \
//    -o /tmp/test-podium-skip tools/test-podium-skip.c -lm && \
//    /tmp/test-podium-skip


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <common.h>
#include <platform/native_config.h>

// ── stubbed engine / network state ──────────────────────────────────────────

struct sData sdata_static;

static struct GameTracker gGT;

// The three AP inputs AP_ShouldSkipPodium reads through ctr_cfg_active() and the
// Oxide selectors. Each case sets them directly so the test drives the SHIPPED
// composition rather than re-deriving it.
static int g_cfgActive;
static int g_oxideOffersFinal;
static int g_oxideFinalOpen;

int ctr_cfg_active(void)
{
	return g_cfgActive;
}

int AP_OxideOffersFinalChallenge(void)
{
	return g_oxideOffersFinal;
}

int AP_OxideFinalOpen(void)
{
	return g_oxideFinalOpen;
}

// The production runtime under test. This is the same translation unit
// game/game_unity.h compiles into the game.
#include "../ap/ap_relic_goal.h" // AP_RelicGoalMet: what AP_OxideFinalOpen resolves
#include "../ap/ap_podium_skip.c"

// The config table and persistence store (the same unit the game links).
#include "../platform/native_config.c"

// Engine model ids the podium can carry (namespace_Instance.h). The trophy and
// relic ids are also pinned inside ap_hooks.c against the engine constants.
#define TEST_TROPHY 0x62
#define TEST_RELIC  0x61
#define TEST_KEY    0x63
#define TEST_GEM    0x5f
#define TEST_BIG1   0x38
#define TEST_NOFUNC 0x00

// gameMode1 words (namespace_Main.h).
#define TEST_ADVENTURE_MODE 0x80000
#define TEST_RELIC_RACE     0x4000000
#define TEST_TOKEN_RACE     0x2000000 // gameMode2
#define TEST_ADVENTURE_CUP  0x10000000

// gameMode2 bits a watched ceremony clears by its end (namespace_Main.h).
#define TEST_INC_RELIC      0x1000000
#define TEST_INC_KEY        0x2000000
#define TEST_INC_TROPHY     0x4000000
#define TEST_FREEZE_PODIUM  0x4
#define TEST_FREEZE_DOOR    0x4000
#define TEST_CUP_NEW_WIN    0x1000
#define TEST_SPAWN_RETAINED 0x2

static int g_failures;

static void expect(const char *what, int got, int want)
{
	printf("%-4s %s (got %d, want %d)\n", got == want ? "ok" : "FAIL", what,
	       got, want);
	if (got != want)
		g_failures++;
}

// Set an AdvProgress reward bit the way the game does (namespace_Memcard.h).
static void SetRewardBit(int bit)
{
	sdata->advProgress.rewards[bit / 32] |= (u32)1 << (bit & 31);
}

// The exact engine state a case starts from: nothing set, all AP gates shut,
// option off. Each test turns on only what it is about.
static void ResetState(void)
{
	memset(&gGT, 0, sizeof gGT);
	memset(&sdata_static, 0, sizeof sdata_static);
	sdata->gGT = &gGT;
	g_config.skipPodium = false;
	g_cfgActive = 0;
	g_oxideOffersFinal = 0;
	g_oxideFinalOpen = 0;
}

// Grant the 18 sapphire relic bits AP_VanillaRelicCountNow counts, so a
// no-slot_data session reads the retail threshold as met.
static void SetSapphireRelics(int n)
{
	int i;
	for (i = 0; i < n; i++)
		SetRewardBit(ADV_REWARD_FIRST_SAPPHIRE_RELIC + i);
}

static const ConfigEntry *FindEntry(const char *section, const char *key)
{
	for (int i = 0; i < g_numConfigEntries; i++)
	{
		const ConfigEntry *e = &g_configEntries[i];
		if (strcmp(section, e->section) == 0 && strcmp(key, e->key) == 0)
			return e;
	}
	return NULL;
}

// Set a config value the way the generic menu writes an entry (CFG_BOOL 0/1).
static void SetEntry(const char *section, const char *key, int value)
{
	const ConfigEntry *e = FindEntry(section, key);
	if (e == NULL)
	{
		printf("FAIL SetEntry: no entry %s/%s\n", section, key);
		g_failures++;
		return;
	}
	if (e->type == CFG_BOOL)
		*(bool *)e->valuePtr = value != 0;
	else
		*(int *)e->valuePtr = value;
}

// ── The production wrapper: the Oxide relic predicate (correction 1) ─────────

static void TestOxideRelicPredicate(void)
{
	// ── A qualifying relic BEFORE Oxide's second defeat is preserved ─────────
	// AP active, the per-seed Final gate open (offersFinal + finalOpen), a relic
	// race, option on, second Oxide not yet beaten. The wrapper must NOT skip:
	// CS_Camera_BoolGotoBoss is about to select the Oxide transition.
	ResetState();
	g_config.skipPodium = true;
	gGT.gameMode1 = TEST_ADVENTURE_MODE | TEST_RELIC_RACE;
	gGT.gameMode2 = TEST_INC_RELIC;
	gGT.podiumRewardID = TEST_RELIC;
	g_cfgActive = 1;
	g_oxideOffersFinal = 1;
	g_oxideFinalOpen = 1;
	expect("qualifying relic before Oxide 2 is preserved",
	       AP_ShouldSkipPodium(TEST_RELIC), 0);

	// ── The SAME relic AFTER the second defeat is skipped ────────────────────
	// The beat-Oxide bit suppresses the transition, so nothing can play and the
	// ceremony is an ordinary relic. Before correction 1 this returned 0.
	ResetState();
	g_config.skipPodium = true;
	gGT.gameMode1 = TEST_ADVENTURE_MODE | TEST_RELIC_RACE;
	gGT.gameMode2 = TEST_INC_RELIC;
	gGT.podiumRewardID = TEST_RELIC;
	g_cfgActive = 1;
	g_oxideOffersFinal = 1;
	g_oxideFinalOpen = 1;
	SetRewardBit(ADV_REWARD_BEAT_OXIDE_SECOND);
	expect("relic after Oxide 2 is skipped", AP_ShouldSkipPodium(TEST_RELIC), 1);

	// The same holds without slot_data: 18 sapphire bits met, second Oxide
	// beaten -> the retail transition is suppressed and the relic is skippable.
	ResetState();
	g_config.skipPodium = true;
	gGT.gameMode1 = TEST_ADVENTURE_MODE | TEST_RELIC_RACE;
	gGT.podiumRewardID = TEST_RELIC;
	SetSapphireRelics(18);
	SetRewardBit(ADV_REWARD_BEAT_OXIDE_SECOND);
	expect("no-slot_data relic after Oxide 2 is skipped",
	       AP_ShouldSkipPodium(TEST_RELIC), 1);

	// ... and the identical no-slot_data relic BEFORE the second defeat is the
	// retail ceremony, so it must be preserved.
	ResetState();
	g_config.skipPodium = true;
	gGT.gameMode1 = TEST_ADVENTURE_MODE | TEST_RELIC_RACE;
	gGT.podiumRewardID = TEST_RELIC;
	SetSapphireRelics(18);
	expect("no-slot_data relic before Oxide 2 is preserved",
	       AP_ShouldSkipPodium(TEST_RELIC), 0);

	// A relic below the retail threshold is an ORDINARY relic (no Oxide
	// transition can fire), so with the option on it IS skippable.
	ResetState();
	g_config.skipPodium = true;
	gGT.gameMode1 = TEST_ADVENTURE_MODE | TEST_RELIC_RACE;
	gGT.podiumRewardID = TEST_RELIC;
	SetSapphireRelics(16);
	expect("relic below the retail threshold is an ordinary skip",
	       AP_ShouldSkipPodium(TEST_RELIC), 1);

	// ── Option off skips nothing, whatever the state ─────────────────────────
	ResetState();
	gGT.gameMode1 = TEST_ADVENTURE_MODE | TEST_RELIC_RACE;
	gGT.podiumRewardID = TEST_RELIC;
	SetSapphireRelics(18);
	SetRewardBit(ADV_REWARD_BEAT_OXIDE_SECOND);
	expect("option off preserves the relic", AP_ShouldSkipPodium(TEST_RELIC), 0);
}

// ── The production wrapper: classification without the relic predicate ───────

static void TestWrapperClassification(void)
{
	// An ordinary Adventure Trophy skips when the option is on.
	ResetState();
	g_config.skipPodium = true;
	gGT.gameMode1 = TEST_ADVENTURE_MODE;
	gGT.podiumRewardID = TEST_TROPHY;
	expect("on skips an Adventure Trophy", AP_ShouldSkipPodium(TEST_TROPHY), 1);

	// A CTR Challenge (Trophy model + the token-race flag) skips too.
	ResetState();
	g_config.skipPodium = true;
	gGT.gameMode1 = TEST_ADVENTURE_MODE;
	gGT.gameMode2 = TEST_TOKEN_RACE;
	gGT.podiumRewardID = TEST_TROPHY;
	expect("on skips a CTR Challenge", AP_ShouldSkipPodium(TEST_TROPHY), 1);

	// A Gem Cup leg and a boss race never skip, even carrying a Trophy model.
	ResetState();
	g_config.skipPodium = true;
	gGT.gameMode1 = TEST_ADVENTURE_MODE | TEST_ADVENTURE_CUP;
	gGT.podiumRewardID = TEST_GEM;
	expect("on keeps a Gem Cup", AP_ShouldSkipPodium(TEST_GEM), 0);

	ResetState();
	g_config.skipPodium = true;
	gGT.gameMode1 = TEST_ADVENTURE_MODE | (int)0x80000000u; // IS_BOSS_RACE: gm < 0
	gGT.podiumRewardID = TEST_KEY;
	expect("on keeps a boss race", AP_ShouldSkipPodium(TEST_KEY), 0);

	// A Key, a BIG1 and a no-podium are never skippable.
	ResetState();
	g_config.skipPodium = true;
	gGT.gameMode1 = TEST_ADVENTURE_MODE;
	gGT.podiumRewardID = TEST_KEY;
	expect("on keeps a Key", AP_ShouldSkipPodium(TEST_KEY), 0);
	gGT.podiumRewardID = TEST_BIG1;
	expect("on keeps an Oxide BIG1", AP_ShouldSkipPodium(TEST_BIG1), 0);
	gGT.podiumRewardID = TEST_NOFUNC;
	expect("on keeps a no-podium", AP_ShouldSkipPodium(TEST_NOFUNC), 0);

	// A null engine state is never a skip.
	ResetState();
	g_config.skipPodium = true;
	sdata->gGT = NULL;
	expect("null gGT is never a skip", AP_ShouldSkipPodium(TEST_TROPHY), 0);
}

// ── The 0xd call-site gate (correction 2) ───────────────────────────────────

static void TestCallSiteGate(void)
{
	// The gate is the production predicate the call site now uses.
	expect("gate allows Adventure relic",
	       AP_PodiumSkipCallAllowed(TEST_ADVENTURE_MODE | TEST_RELIC_RACE), 1);
	expect("gate rejects non-Adventure relic",
	       AP_PodiumSkipCallAllowed(TEST_RELIC_RACE), 0);
	expect("gate rejects Adventure non-relic",
	       AP_PodiumSkipCallAllowed(TEST_ADVENTURE_MODE), 0);
	expect("gate rejects cup exit sharing 0xd",
	       AP_PodiumSkipCallAllowed(TEST_ADVENTURE_MODE | TEST_ADVENTURE_CUP), 0);
	expect("gate rejects empty mode", AP_PodiumSkipCallAllowed(0), 0);

	// The classification alone WOULD skip a relic whenever RELIC_RACE is set,
	// even with ADVENTURE_MODE absent. That is exactly why the call site must
	// hold the invariant itself: this documents the hole the gate closes.
	ResetState();
	g_config.skipPodium = true;
	gGT.gameMode1 = TEST_RELIC_RACE; // no ADVENTURE_MODE
	gGT.podiumRewardID = TEST_RELIC;
	expect("classification alone would skip a non-Adventure relic",
	       AP_ShouldSkipPodium(TEST_RELIC), 1);
	expect("...and the production gate refuses it",
	       AP_PodiumSkipCallAllowed(gGT.gameMode1), 0);
}

// ── The mutation AP_SkipPodium ──────────────────────────────────────────────

static void TestMutation(void)
{
	int before;

	// A real skip sets podiumRewardID to NOFUNC and clears ONLY the three INC_*
	// count-up bits and VEH_FREEZE_PODIUM, leaving every other gameMode2 bit
	// alone.
	ResetState();
	g_config.skipPodium = true;
	gGT.gameMode1 = TEST_ADVENTURE_MODE;
	gGT.podiumRewardID = TEST_TROPHY;
	gGT.gameMode2 = TEST_INC_RELIC | TEST_INC_KEY | TEST_INC_TROPHY |
	                TEST_FREEZE_PODIUM | TEST_FREEZE_DOOR | TEST_CUP_NEW_WIN |
	                TEST_SPAWN_RETAINED;
	before = gGT.gameMode2;

	AP_SkipPodium(TEST_TROPHY);

	expect("skip sets podiumRewardID to NOFUNC", gGT.podiumRewardID, TEST_NOFUNC);
	expect("skip clears INC_RELIC", (gGT.gameMode2 & TEST_INC_RELIC), 0);
	expect("skip clears INC_KEY", (gGT.gameMode2 & TEST_INC_KEY), 0);
	expect("skip clears INC_TROPHY", (gGT.gameMode2 & TEST_INC_TROPHY), 0);
	expect("skip clears VEH_FREEZE_PODIUM",
	       (gGT.gameMode2 & TEST_FREEZE_PODIUM), 0);
	expect("skip keeps VEH_FREEZE_DOOR",
	       (gGT.gameMode2 & TEST_FREEZE_DOOR), TEST_FREEZE_DOOR);
	expect("skip keeps CUP_NEW_WIN",
	       (gGT.gameMode2 & TEST_CUP_NEW_WIN), TEST_CUP_NEW_WIN);
	expect("skip keeps the retained spawn bit",
	       (gGT.gameMode2 & TEST_SPAWN_RETAINED), TEST_SPAWN_RETAINED);
	expect("skip removes exactly the four intended bits",
	       before & ~gGT.gameMode2,
	       TEST_INC_RELIC | TEST_INC_KEY | TEST_INC_TROPHY | TEST_FREEZE_PODIUM);

	// Option off mutates nothing: the reward id and every gameMode2 bit survive.
	ResetState();
	g_config.skipPodium = false;
	gGT.gameMode1 = TEST_ADVENTURE_MODE;
	gGT.podiumRewardID = TEST_TROPHY;
	gGT.gameMode2 = TEST_INC_TROPHY | TEST_FREEZE_PODIUM | TEST_FREEZE_DOOR;
	before = gGT.gameMode2;

	AP_SkipPodium(TEST_TROPHY);

	expect("option off keeps the reward id", gGT.podiumRewardID, TEST_TROPHY);
	expect("option off mutates no gameMode2 bit", gGT.gameMode2, before);

	// A preserved Oxide relic must also be left untouched by the mutation, so a
	// caller that skipped the call-site gate cannot damage the transition.
	ResetState();
	g_config.skipPodium = true;
	gGT.gameMode1 = TEST_ADVENTURE_MODE | TEST_RELIC_RACE;
	gGT.podiumRewardID = TEST_RELIC;
	gGT.gameMode2 = TEST_INC_RELIC | TEST_FREEZE_PODIUM;
	before = gGT.gameMode2;
	g_cfgActive = 1;
	g_oxideOffersFinal = 1;
	g_oxideFinalOpen = 1;

	AP_SkipPodium(TEST_RELIC);

	expect("preserved Oxide relic keeps its reward id",
	       gGT.podiumRewardID, TEST_RELIC);
	expect("preserved Oxide relic keeps its gameMode2 bits",
	       gGT.gameMode2, before);

	// A null engine state is a safe no-op.
	ResetState();
	g_config.skipPodium = true;
	sdata->gGT = NULL;
	AP_SkipPodium(TEST_TROPHY); // must not crash
	expect("null gGT mutation is a safe no-op", 1, 1);
}

// ── BoolGotoBoss truth table: AP and non-AP (correction 1, no-drift proof) ──
//
// game/233/CS_Camera.c cannot be linked off-engine, so the two branches are
// reproduced here from the SAME production helpers the real site calls:
//
//   AP branch (CS_Camera.c:22-29):  AP_PodiumRelicWillGotoOxide(
//                                      rewardId == STATIC_RELIC,
//                                      AP_OxideFinalEncounterPresentationReady(...),
//                                      beatOxideSecond != 0)
//   non-AP branch (CS_Camera.c:31-33, kept verbatim):
//       (rewardId == STATIC_RELIC) && (numRelics >= 18) && (beatOxideSecond == 0)
//
// The assertion is that the two agree with the retail rule
// (relic && numRelics >= 18 && !beatOxideSecond) whenever cfgActive is 0, so
// sharing the helper under CTR_AP left every non-AP answer exactly as today,
// and that the AP branch follows AP_OxideFinalEncounterPresentationReady.

static int BoolGotoBossAp(int cfgActive, int rewardId, int vanillaRelics,
                          int offersFinal, int finalOpen, int beatOxideSecond)
{
	return AP_PodiumRelicWillGotoOxide(
	    rewardId == TEST_RELIC,
	    AP_OxideFinalEncounterPresentationReady(cfgActive, vanillaRelics,
	                                            offersFinal, finalOpen),
	    beatOxideSecond != 0);
}

static int BoolGotoBossNonAp(int rewardId, int vanillaRelics,
                             int beatOxideSecond)
{
	// Verbatim transcription of the kept-verbatim non-AP expression.
	return (rewardId == TEST_RELIC) && (vanillaRelics >= 18) &&
	       (beatOxideSecond == 0);
}

static void TestBoolGotoBossTruthTable(void)
{
	int rows = 0;

	for (int relics = 0; relics <= 20; relics++)
	for (int beat = 0; beat <= 1; beat++)
	for (int mode = 0; mode <= 4; mode++)
	for (int count = 0; count <= 20; count += 5)
	{
		// AP_RelicGoalMet(modes): 0 sapphire, 1 gold, 2 platinum, 3 any,
		// 4 total. Sweep the same relic/goal inputs the production predicate
		// consumes. offersFinal mirrors a representative open/closed pair.
		int finalOpen = AP_RelicGoalMet(mode, count, relics, relics, relics);
		int offersFinal = finalOpen;
		int rewardId = (rows % 3 == 0) ? TEST_RELIC : TEST_TROPHY;

		int ap = BoolGotoBossAp(1, rewardId, relics, offersFinal, finalOpen, beat);
		int nonAp = BoolGotoBossNonAp(rewardId, relics, beat);
		int retail = (rewardId == TEST_RELIC) && (relics >= 18) && (beat == 0);

		// The non-AP branch must still be EXACTLY the retail rule. If sharing
		// had changed any non-AP answer this fails.
		if (nonAp != retail)
		{
			printf("FAIL non-AP drifted: relic=%d beat=%d got=%d retail=%d\n",
			       rewardId == TEST_RELIC, beat, nonAp, retail);
			g_failures++;
		}

		// The AP branch must follow the shipped gate: relic + ready + !beat.
		{
			int ready = AP_OxideFinalEncounterPresentationReady(
			    1, relics, offersFinal, finalOpen);
			int want = (rewardId == TEST_RELIC) && ready && (beat == 0);
			if (ap != want)
			{
				printf("FAIL AP branch: relics=%d mode=%d count=%d beat=%d "
				       "got=%d want=%d\n", relics, mode, count, beat, ap, want);
				g_failures++;
			}
		}
		rows++;
	}

	// The two branches must also be identical to each other WITHOUT slot_data,
	// which is the guarantee "sharing changes no non-AP result" ultimately means.
	for (int relics = 0; relics <= 20; relics++)
	for (int beat = 0; beat <= 1; beat++)
	for (int rewardId = 0; rewardId <= 1; rewardId++)
	{
		int rid = rewardId ? TEST_RELIC : TEST_TROPHY;
		// cfgActive == 0 falls back to the retail 18 rule inside the production
		// predicate, so the AP branch with no slot_data is the non-AP answer.
		// (Both branches read currAdvProfile.numRelics exactly the same way.)
		int apNoSlot = BoolGotoBossAp(0, rid, relics, 0, 0, beat);
		int nonAp = BoolGotoBossNonAp(rid, relics, beat);
		if (apNoSlot != nonAp)
		{
			printf("FAIL AP(no slot_data) != non-AP: relic=%d relics=%d beat=%d\n",
			       rewardId, relics, beat);
			g_failures++;
		}
	}

	expect("BoolGotoBoss truth table rows executed", rows > 1000, 1);
	printf("ok   BoolGotoBoss truth table over %d mode/count/relic/beat rows\n",
	       rows);
}

// ── Config persistence ──────────────────────────────────────────────────────

static void TestConfig(void)
{
	const ConfigEntry *e = FindEntry("Video & QoL", "skip_podium");
	expect("skip_podium entry present", e != NULL, 1);
	if (e != NULL)
	{
		expect("skip_podium is CFG_BOOL", e->type == CFG_BOOL, 1);
		expect("skip_podium points at g_config.skipPodium",
		       e->valuePtr == &g_config.skipPodium, 1);
		expect("skip_podium lives in the visible Video & QoL section",
		       strcmp(e->section, "Video & QoL") == 0, 1);
	}

	char tmpdir[] = "/tmp/test-podium-skip-XXXXXX";
	if (mkdtemp(tmpdir) == NULL)
	{
		printf("FAIL config: cannot create temp dir\n");
		g_failures++;
		return;
	}
	if (chdir(tmpdir) != 0)
	{
		printf("FAIL config: cannot chdir\n");
		g_failures++;
		return;
	}

	SetEntry("Video & QoL", "skip_podium", 1);
	NativeConfig_Save();
	g_config.skipPodium = false;
	NativeConfig_Load();
	expect("persisted skipPodium survives a load", g_config.skipPodium ? 1 : 0, 1);

	{
		FILE *f = fopen("config.ini", "r");
		char buf[256];
		int inVideo = 0, sawSkip = 0;
		if (f == NULL)
		{
			printf("FAIL config.ini missing after save\n");
			g_failures++;
		}
		else
		{
			while (fgets(buf, sizeof buf, f))
			{
				if (buf[0] == '[')
					inVideo = strncmp(buf, "[Video & QoL]", 13) == 0;
				else if (inVideo && strstr(buf, "skip_podium") != NULL &&
				         strstr(buf, "= true") != NULL)
					sawSkip = 1;
			}
			fclose(f);
		}
		expect("config.ini has skip_podium = true", sawSkip, 1);
	}

	{
		FILE *f = fopen("config.ini", "w");
		if (f == NULL)
		{
			printf("FAIL config: cannot write minimal config.ini\n");
			g_failures++;
		}
		else
		{
			fputs("[Video & QoL]\nskip_intro = false\n", f);
			fclose(f);
		}
		g_config.skipPodium = false;
		NativeConfig_Load();
		expect("missing key leaves skipPodium off",
		       g_config.skipPodium ? 1 : 0, 0);
	}
}

int main(void)
{
	// The built-in default must be off, so an unset option never changes
	// behavior. Asserted before any test touches g_config.
	expect("default skipPodium is off", g_config.skipPodium ? 1 : 0, 0);

	TestOxideRelicPredicate();
	TestWrapperClassification();
	TestCallSiteGate();
	TestMutation();
	TestBoolGotoBossTruthTable();
	TestConfig();

	if (g_failures)
	{
		printf("podium skip: FAIL (%d)\n", g_failures);
		return 1;
	}
	puts("podium skip: PASS");
	return 0;
}
