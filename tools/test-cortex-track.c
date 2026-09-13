// Cortex Vortex as a full pad track (schema 15): the freestanding decisions and
// a LevelID-13 leak guard, compiled out of engine.
//
//   cc -Wall -Wextra -Werror -DCTR_AP -I ap -I . -I include -o /tmp/test-cortex-track
//      tools/test-cortex-track.c && /tmp/test-cortex-track
//
// Covers: destination-ID mapping and the serving latch
// (include/platform/native_cortex_track_latch.h), the pseudo-bit code lookup,
// result dispatch, the package-owned relic targets (pinned against
// game/zGlobal_DATA.c), the Wumpa resolver (ap/ap_wumpa_dispatch.h) and two
// leak guards: (1) no Cortex Vortex decision can ever produce one of Oxide
// Station's LevelID-13 location codes, and (2) every production LevelID-13
// consumer carries its Cortex Vortex guard ahead of the Oxide Station read.
// Exit 0 = every assertion held. No disc, no display, no seed, no network.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ap/ap_cortex_track.h"
#include "../ap/ap_wumpa_dispatch.h"
#include "../ap/ap_locations.h"
#include "../include/platform/native_cortex_track_latch.h"

static int checks, failures;

#define EXPECT(cond, name) do { checks++; if (!(cond)) { failures++; printf("FAIL %s\n", name); } } while (0)
#define EXPECT_EQ(got, want, name) do { long _g = (long)(got), _w = (long)(want); checks++; \
	if (_g != _w) { failures++; printf("FAIL %s (got %ld, want %ld)\n", name, _g, _w); } } while (0)

static ctr_seed_config cfg;

static void fill_valid(void)
{
	ctr_cortex_track *cv = &cfg.cortex_track;
	int t;
	memset(&cfg, 0, sizeof cfg);
	cv->option = 1;
	cv->seen = 1;
	cv->valid = 1;
	cv->dropped_destination = 7;
	cv->trophy = CTR_CFG_CORTEX_TROPHY;
	for (t = 0; t < 3; t++)
	{
		cv->relic[t] = CTR_CFG_CORTEX_RELIC_FIRST + t;
		cv->letters[t] = CTR_CFG_CORTEX_LETTER_FIRST + t;
		cv->letter_items[t] = CTR_CFG_CORTEX_LETTER_ITEM_FIRST + t;
	}
	cv->ctr_token = CTR_CFG_CORTEX_CTR_TOKEN;
	cv->podium.held_1st = CTR_CFG_CORTEX_PODIUM_FIRST + 0;
	cv->podium.held_3rd = CTR_CFG_CORTEX_PODIUM_FIRST + 1;
	cv->podium.held_5th = CTR_CFG_CORTEX_PODIUM_FIRST + 2;
	cv->podium.finish_podium = CTR_CFG_CORTEX_PODIUM_FIRST + 3;
	cv->podium.finish_any = CTR_CFG_CORTEX_PODIUM_FIRST + 4;
	cv->wumpa = CTR_CFG_CORTEX_WUMPA;
	// Oxide Station's own LevelID-13 identities, populated so a leak would show.
	cfg.wumpa.mode = CTR_CFG_WUMPA_PER_TRACK;
	for (t = 0; t < CTR_CFG_WUMPA_TRACK_COUNT; t++)
		cfg.wumpa.tracks[t] = 35016101 + t;
	cfg.podium[13].held_1st = 35015065;
	cfg.podium[13].finish_any = 35015069;
	cfg.lettersanity_locations[13][0] = 35012545;
}

// Every Oxide Station (LevelID 13) location code this build knows.
static int is_oxide_station_code(long code)
{
	static const int bits[] = {13 + 6, 13 + 22, 13 + 40, 13 + 58, 13 + 76};
	unsigned i, k;
	for (i = 0; i < sizeof bits / sizeof bits[0]; i++)
		for (k = 0; k < sizeof AP_LOCATION_TABLE / sizeof AP_LOCATION_TABLE[0]; k++)
			if (AP_LOCATION_TABLE[k].bit_index == bits[i] &&
			    AP_LOCATION_TABLE[k].location_code == code)
				return 1;
	return code == cfg.wumpa.tracks[13] || code == cfg.podium[13].held_1st ||
	       code == cfg.podium[13].finish_any || code == cfg.lettersanity_locations[13][0];
}

static int q_true(long code, void *ctx) { (void)code; (void)ctx; return 1; }
static int q_false(long code, void *ctx) { (void)code; (void)ctx; return 0; }
static int q_trophy_checked(long code, void *ctx) { (void)ctx; return code == CTR_CFG_CORTEX_TROPHY; }

static void test_latch(void)
{
	struct CortexTrackLatch l;
	int cortex = -1;

	EXPECT_EQ(CortexTrackLatch_ResolveDest(110, &cortex), 13, "110 loads host level 13");
	EXPECT_EQ(cortex, 1, "110 selects Cortex Vortex");
	EXPECT_EQ(CortexTrackLatch_ResolveDest(13, &cortex), 13, "13 stays 13");
	EXPECT_EQ(cortex, 0, "13 selects retail");
	EXPECT_EQ(CortexTrackLatch_ResolveDest(104, &cortex), 104, "cups unchanged");

	CortexTrackLatch_Reset(&l);
	EXPECT(!CortexTrackLatch_Identity(&l, 13, 0), "boot: Oxide Station");

	// Pad -> Cortex Vortex, retry, exit to hub.
	CortexTrackLatch_Select(&l, 1);
	CortexTrackLatch_OnRequest(&l, 13);
	EXPECT(CortexTrackLatch_Identity(&l, 13, 0), "pad entry serves Cortex Vortex");
	EXPECT(!CortexTrackLatch_Identity(&l, 13, 1), "never during a boss race");
	EXPECT(!CortexTrackLatch_Identity(&l, 12, 0), "only on level 13");
	CortexTrackLatch_OnRequest(&l, 13); // QueueLoadTrack retry, no selection
	EXPECT(CortexTrackLatch_Identity(&l, 13, 0), "retry keeps Cortex Vortex");
	CortexTrackLatch_OnRequest(&l, 25); // hub
	EXPECT(!CortexTrackLatch_Identity(&l, 13, 0), "hub return ends serving");
	EXPECT_EQ(l.prevServed, 1, "hub knows it left Cortex Vortex");
	CortexTrackLatch_OnRequest(&l, 25); // repeated hub request
	EXPECT_EQ(l.prevServed, 1, "repeat request keeps prevServed");

	// Oxide Station pad from the hub.
	CortexTrackLatch_Select(&l, 0);
	CortexTrackLatch_OnRequest(&l, 13);
	EXPECT(!CortexTrackLatch_Identity(&l, 13, 0), "Oxide Station pad is retail");
	CortexTrackLatch_OnRequest(&l, 25);
	EXPECT_EQ(l.prevServed, 0, "hub knows it left Oxide Station");

	// Oxide boss race from the garage: no selection, hub before it.
	CortexTrackLatch_OnRequest(&l, 13);
	EXPECT(!CortexTrackLatch_Identity(&l, 13, 1), "Oxide 1/2 never Cortex Vortex pad track");

	// Gem Cup: Cortex Vortex leg, then an Oxide Station leg, then 110 again.
	CortexTrackLatch_OnRequest(&l, 25);
	CortexTrackLatch_Select(&l, 1);
	CortexTrackLatch_OnRequest(&l, 13);
	EXPECT(CortexTrackLatch_Identity(&l, 13, 0), "cup leg 110 serves Cortex Vortex");
	CortexTrackLatch_Select(&l, 0);
	CortexTrackLatch_OnRequest(&l, 13);
	EXPECT(!CortexTrackLatch_Identity(&l, 13, 0), "next leg 13 is Oxide Station");
	CortexTrackLatch_Select(&l, 1);
	CortexTrackLatch_OnRequest(&l, 13);
	EXPECT(CortexTrackLatch_Identity(&l, 13, 0), "leg 110 after leg 13");
	CortexTrackLatch_Select(&l, 0);
	CortexTrackLatch_OnRequest(&l, 6);
	EXPECT(!CortexTrackLatch_Identity(&l, 13, 0), "other leg ends serving");

	// A stale selection never survives a different request.
	CortexTrackLatch_Select(&l, 1);
	CortexTrackLatch_OnRequest(&l, 25);
	EXPECT(!l.current && l.pending == -1, "selection consumed by a non-13 request");
}

static void test_codes(void)
{
	int slot;
	fill_valid();
	for (slot = 0; slot < AP_CV_SLOT_COUNT; slot++)
	{
		long code = AP_CortexPseudoCode(&cfg.cortex_track, AP_CortexPseudoBit(slot));
		EXPECT(code > 0, "every slot resolves on a full block");
		EXPECT(!is_oxide_station_code(code), "LEAK GUARD: slot code is never Oxide Station's");
		EXPECT((code >= 35026000 && code <= 35026014) || code == 35016121,
		       "slot code is in the Cortex Vortex block");
	}
	EXPECT_EQ(AP_CortexPseudoCode(&cfg.cortex_track, AP_CV_PSEUDO_BASE - 1), -1, "below range");
	EXPECT_EQ(AP_CortexPseudoCode(&cfg.cortex_track, AP_CV_PSEUDO_BASE + AP_CV_SLOT_COUNT), -1, "above range");
	EXPECT(AP_CV_PSEUDO_BASE > 0x213 && 0x100 + 50 * 5 <= 0x200, "pseudo range does not overlap podium/custom/trial");
	EXPECT_EQ(AP_CortexPseudoRewardGroup(AP_CortexPseudoBit(AP_CV_SLOT_TROPHY)), 0, "trophy group");
	EXPECT_EQ(AP_CortexPseudoRewardGroup(AP_CortexPseudoBit(AP_CV_SLOT_RELIC0 + 2)), 1, "relic group");
	EXPECT_EQ(AP_CortexPseudoRewardGroup(AP_CortexPseudoBit(AP_CV_SLOT_TOKEN)), 2, "token group");
	EXPECT_EQ(AP_CortexPseudoRewardGroup(AP_CortexPseudoBit(AP_CV_SLOT_RUNG0 + 4)), 0, "rung group");
	EXPECT_EQ(AP_CortexPseudoRelicTier(AP_CortexPseudoBit(AP_CV_SLOT_RELIC0 + 1)), 1, "gold tier");

	cfg.cortex_track.relic[1] = -1;
	EXPECT_EQ(AP_CortexSlotCode(&cfg.cortex_track, AP_CV_SLOT_RELIC0 + 1), -1, "absent tier");
	cfg.cortex_track.valid = 0;
	for (slot = 0; slot < AP_CV_SLOT_COUNT; slot++)
		EXPECT_EQ(AP_CortexSlotCode(&cfg.cortex_track, slot), -1, "refused block sends nothing");
}

static void test_pad_lists(void)
{
	int bits[24], n, i;
	fill_valid();
	n = AP_CortexPadAppendOpen(&cfg.cortex_track, AP_CV_APPEND_TIERS, bits, 24, 0, q_true, q_false, 0);
	EXPECT_EQ(n, 5, "tiers: trophy + 3 relics + token");
	n = AP_CortexPadAppendOpen(&cfg.cortex_track, AP_CV_APPEND_TIERS | AP_CV_APPEND_RUNGS, bits, 24, 0, q_true, q_false, 0);
	EXPECT_EQ(n, 10, "tiers + 5 rungs");
	n = AP_CortexPadAppendOpen(&cfg.cortex_track, AP_CV_APPEND_RUNGS, bits, 24, 0, q_true, q_false, 0);
	EXPECT_EQ(n, 5, "cup leg exposes rungs only");
	for (i = 0; i < n; i++)
		EXPECT(bits[i] >= AP_CortexPseudoBit(AP_CV_SLOT_RUNG0), "cup leg never exposes the trophy family");
	n = AP_CortexPadAppendOpen(&cfg.cortex_track, AP_CV_APPEND_TIERS, bits, 24, 0, q_true, q_trophy_checked, 0);
	EXPECT_EQ(n, 4, "checked trophy drops out");
	n = AP_CortexPadAppendOpen(&cfg.cortex_track, AP_CV_APPEND_TIERS, bits, 24, 0, q_false, q_false, 0);
	EXPECT_EQ(n, 0, "server-absent codes are not advertised");
	EXPECT_EQ(AP_CortexOpenCount(&cfg.cortex_track, AP_CV_SLOT_LETTER0, 3, q_true, q_false, 0), 3, "three letters open");
	EXPECT_EQ(AP_CortexOpenCount(&cfg.cortex_track, AP_CV_SLOT_WUMPA, 1, q_true, q_false, 0), 1, "wumpa open");
}

static void test_results(void)
{
	int bits[4], n;
	fill_valid();
	n = AP_CortexResultBits(&cfg.cortex_track, AP_CV_RACE_TROPHY, 1, 0, bits, 4);
	EXPECT(n == 1 && bits[0] == AP_CortexPseudoBit(AP_CV_SLOT_TROPHY), "trophy win sends trophy");
	n = AP_CortexResultBits(&cfg.cortex_track, AP_CV_RACE_TROPHY, 0, 0, bits, 4);
	EXPECT_EQ(n, 0, "trophy loss sends nothing");
	n = AP_CortexResultBits(&cfg.cortex_track, AP_CV_RACE_TOKEN, 1, 0, bits, 4);
	EXPECT(n == 1 && bits[0] == AP_CortexPseudoBit(AP_CV_SLOT_TOKEN), "token win sends token");
	n = AP_CortexResultBits(&cfg.cortex_track, AP_CV_RACE_RELIC, 1, 7, bits, 4);
	EXPECT_EQ(n, 3, "platinum run sends all three tiers");
	n = AP_CortexResultBits(&cfg.cortex_track, AP_CV_RACE_RELIC, 1, 1, bits, 4);
	EXPECT(n == 1 && AP_CortexPseudoRelicTier(bits[0]) == 0, "sapphire only");
	cfg.cortex_track.relic[0] = -1;
	n = AP_CortexResultBits(&cfg.cortex_track, AP_CV_RACE_RELIC, 1, 7, bits, 4);
	EXPECT_EQ(n, 2, "absent sapphire tier skipped");
	cfg.cortex_track.ctr_token = -1;
	n = AP_CortexResultBits(&cfg.cortex_track, AP_CV_RACE_TOKEN, 1, 0, bits, 4);
	EXPECT_EQ(n, 0, "absent token sends nothing, never Oxide Station's");
}

static int relic_time_from_source(int index)
{
	FILE *f = fopen("game/zGlobal_DATA.c", "rb");
	static char buf[1 << 22];
	size_t len;
	char *p;
	int i;
	if (!f)
		return -1;
	len = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[len] = 0;
	p = strstr(buf, ".RelicTime =");
	if (!p)
		return -1;
	p = strchr(p, '{');
	for (i = 0; p && i <= index; i++)
	{
		p = strstr(p, "0x");
		if (!p)
			return -1;
		if (i == index)
			return (int)strtol(p, 0, 16);
		p += 2;
	}
	return -1;
}

static void test_relic_targets(void)
{
	int t;
	EXPECT(AP_CORTEX_RELIC_TARGETS.placeholder == 1, "targets are still marked placeholders");
	EXPECT(!strcmp(AP_CORTEX_RELIC_TARGETS.levSha256, CTR_CFG_CORTEX_LEV_SHA256), "keyed by the pinned LEV");
	for (t = 0; t < 3; t++)
		EXPECT_EQ(AP_CortexRelicTime(t), relic_time_from_source(13 * 3 + t),
		          "placeholder equals Oxide Station retail data.RelicTime[39+t]");
	EXPECT(AP_CortexRelicTime(0) > AP_CortexRelicTime(1) && AP_CortexRelicTime(1) > AP_CortexRelicTime(2),
	       "sapphire > gold > platinum");
	EXPECT_EQ(AP_CortexRelicTiersBeaten(AP_CortexRelicTime(2)), 7, "exact platinum beats all tiers");
	EXPECT_EQ(AP_CortexRelicTiersBeaten(AP_CortexRelicTime(1)), 3, "exact gold");
	EXPECT_EQ(AP_CortexRelicTiersBeaten(AP_CortexRelicTime(0) + 1), 0, "too slow");
	EXPECT_EQ(AP_CortexLetterItemIndexPure(199), -1, "trial letter item is not Cortex Vortex");
	EXPECT_EQ(AP_CortexLetterItemIndexPure(200), 0, "C");
	EXPECT_EQ(AP_CortexLetterItemIndexPure(202), 2, "R");
	EXPECT_EQ(AP_CortexLetterItemIndexPure(203), -1, "above");
}

static void test_wumpa(void)
{
	struct AP_WumpaDispatchFacts f;
	int reason;
	long code;
	fill_valid();

	memset(&f, 0, sizeof f);
	f.wumpa = &cfg.wumpa;
	f.destLevelID = 13;
	f.servingCupLevelID = -1;
	f.oxideFinalCode = -1;
	f.servingCortexTrack = 1;
	f.cortexTrackCode = cfg.cortex_track.wumpa;
	code = AP_WumpaResolveCode(&f, &reason);
	EXPECT_EQ(code, 35016121, "pad-track race sends 35016121");
	EXPECT(!is_oxide_station_code(code), "LEAK GUARD: never Oxide Station's Wumpa");

	f.isCupLeg = 1;
	EXPECT_EQ(AP_WumpaResolveCode(&f, &reason), 35016121, "cup leg of 110 sends 35016121");

	f.cortexTrackCode = -1;
	code = AP_WumpaResolveCode(&f, &reason);
	EXPECT_EQ(code, -1, "no pad-track code: nothing, never tracks[13]");
	EXPECT_EQ(reason, AP_WUMPA_REFUSE_NO_CORTEX_TRACK_CODE, "reason named");

	memset(&f, 0, sizeof f);
	f.wumpa = &cfg.wumpa;
	f.destLevelID = 13;
	f.servingOxideFinal = 1;
	f.oxideFinalCode = 35016121;
	EXPECT_EQ(AP_WumpaResolveCode(&f, &reason), 35016121, "Oxide 2 on Cortex Vortex unchanged");

	memset(&f, 0, sizeof f);
	f.wumpa = &cfg.wumpa;
	f.destLevelID = 13;
	EXPECT_EQ(AP_WumpaResolveCode(&f, &reason), cfg.wumpa.tracks[13], "Oxide Station pad keeps its own code");

	cfg.wumpa.mode = CTR_CFG_WUMPA_GLOBAL;
	cfg.wumpa.global_code = 35016100;
	memset(&f, 0, sizeof f);
	f.wumpa = &cfg.wumpa;
	f.destLevelID = 13;
	f.servingCortexTrack = 1;
	f.cortexTrackCode = -1;
	EXPECT_EQ(AP_WumpaResolveCode(&f, &reason), 35016100, "global mode unchanged on Cortex Vortex");
	EXPECT(strcmp(AP_WumpaRefusalText(AP_WUMPA_REFUSE_NO_CORTEX_TRACK_CODE), "unknown reason") != 0,
	       "refusal text present");
}

// ── production leak guard ──────────────────────────────────────────────────
// Each row: in `file`, the text `guard` must appear, and it must appear before
// `oxide` (the first LevelID-13-derived read it protects) within `window` bytes
// following `anchor`. Keeps a later edit from silently moving an Oxide Station
// read ahead of its Cortex Vortex guard.
struct guard_row
{
	const char *file, *anchor, *guard, *oxide;
};

static const struct guard_row rows[] = {
	{"game/222.c", "#ifdef CTR_AP\n\t// Cortex Vortex pad track (schema 15)", "AP_NotifyCortexTrackRace",
	 "rewardBit = gGT->levelID + ADV_REWARD_FIRST_TROPHY;"},
	{"game/222.c", "rewardBit = gGT->levelID + ADV_REWARD_FIRST_CTR_TOKEN;", "AP_CortexTrackBit(AP_CV_SLOT_TOKEN)",
	 "CHECK_ADV_BIT(adv->rewards, rewardBit) == 0))"},
	{"game/223.c", "void RR_EndEvent_UnlockAward(void)", "AP_CortexTrackRelicAward(raceTime)",
	 "data.RelicTime[levelID * RR_RELIC_TIERS + relicIndex]"},
	{"ap/ap_boxes.c", "static int AP_BoxesCupLegAllows(", "AP_CortexTrackActive()",
	 "phys = ctr_cfg_warp_phys(level);"},
	{"ap/ap_hooks.c", "static int AP_RetailPodiumTrack(", "AP_CV_PODIUM_LOGICAL_TRACK",
	 "CustomTrack_RetailPodiumLevelID"},
	{"ap/ap_hooks.c", "static void AP_WumpaGatherFacts(", "facts->servingCortexTrack = AP_CortexTrackActive();",
	 "facts->isCupLeg = "},
	{"ap/ap_hooks.c", "static int AP_RelicTierEarnable(", "AP_CortexTrackActive()",
	 "return !AP_RelicRewardOwnedByBit(bit);"},
	{"ap/ap_hooks.c", "int AP_RelicTimeFor(", "AP_CortexRelicTime(tier)", "data.RelicTime[levelID * 3 + tier]"},
	{"ap/ap_hooks.c", "int AP_LetterAvailable(", "AP_CortexLetterTrack(track)", "ctr_cfg.lettersanity_locations[track][letter]"},
	{"ap/ap_hooks.c", "long AP_LetterLocation(", "AP_CortexLetterTrack(track)", "return ctr_cfg.lettersanity_locations[track][letter];"},
	{"ap/ap_hooks.c", "int AP_LetterTokenEarned(", "AP_CortexLetterTrack(track)", "ctr_cfg.lettersanity_locations[track],"},
	{"ap/ap_hooks.c", "static long AP_LookupLocationCode(", "AP_CortexPseudoDecode(globalBit, 0)", "AP_PODIUM_PSEUDO_BASE"},
	{"ap/ap_hooks.c", "static int AP_GlowBitRewardGroup(", "cortexGroup", "if (globalBit >= AP_PODIUM_PSEUDO_BASE)"},
	{"game/232/AH_WarpPad.c", "s16 *AH_WarpPad_GetSpawnPosRot(", "CustomTrack_CortexTrackPrevServed()", "->levelID == exitedLevel"},
	{"game/232/AH_WarpPad.c", "WarpPad_RequestLoad:\n", "AP_CortexTrackPrepareLoad(levelID)", "MainRaceTrack_RequestLoad(levelID);"},
	{"game/UI/UI_CupStandings.c", "index = data.ArcadeCups[cupID].CupTrack[cupTrack].trackID;",
	 "AP_CortexTrackPrepareLoad(index)", "MainRaceTrack_RequestLoad(index);"},
	{"game/MAIN/MainRaceTrack.c", "void MainRaceTrack_RequestLoad(", "CustomTrack_CortexTrackOnRequestLoad(levelID)", "return;"},
	{"platform/native_custom_tracks.c", "int CustomTrack_GetOverride(", "CustomTrack_CortexTrackServing(ctx->levelID",
	 "if (!s_customTrackConfig.contentVerified)"},
};

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	long len;
	char *buf;
	if (!f)
		return 0;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc((size_t)len + 1);
	if (fread(buf, 1, (size_t)len, f) != (size_t)len)
		len = 0;
	buf[len] = 0;
	fclose(f);
	return buf;
}

static void test_production_guards(void)
{
	unsigned i;
	for (i = 0; i < sizeof rows / sizeof rows[0]; i++)
	{
		char *text = slurp(rows[i].file);
		char *a, *g, *o;
		char name[256];
		snprintf(name, sizeof name, "LEAK GUARD %s: '%s' precedes '%s'", rows[i].file, rows[i].guard, rows[i].oxide);
		if (!text)
		{
			EXPECT(0, name);
			continue;
		}
		a = strstr(text, rows[i].anchor);
		g = a ? strstr(a, rows[i].guard) : 0;
		o = a ? strstr(a, rows[i].oxide) : 0;
		EXPECT(a && g && o && g < o, name);
		free(text);
	}
}

int main(void)
{
	test_latch();
	test_codes();
	test_pad_lists();
	test_results();
	test_relic_targets();
	test_wumpa();
	test_production_guards();
	printf("%s cortex track (%d checks, %d failures)\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
