// g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include -I ap/vendor/json/include
//     tools/test-hit-content-cups.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-hit-content-cups && /tmp/test-hit-content-cups
//
// #133 PR 5b: Hit Character on content plans, driven through the REAL gather
// (ap/ap_hit_encounter.c). A content-plan cup runs on engine cup 0 and draws
// its frozen field from its own order (destination 200 + name index, the
// block's `content_cups`), once at its first leg, then reuses it for every
// leg and retry; two content-plan cups never share a snapshot or cursors; a
// retail Gem Cup keeps 100 + cupID. The pure load decision: a cup draws from the
// running content-plan cup's destination when there is one.

#include <cstdio>
#include <cstring>
#include <set>

#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"

static std::set<long long> g_checked;
static int g_emitCount;
static long g_lastEmit;

extern "C" int ap_net_location_checked(long long code)
{
	return g_checked.count(code) ? 1 : 0;
}

extern "C" int ap_net_location_exists(long long code)
{
	if (code >= 35025000LL && code <= 35025015LL)
		return 1;
	return g_checked.count(code) ? 1 : 0;
}

// Seed/slot identity the cup snapshot compares against on reconnect.
static const char *g_seedName = "seedA";
static const char *g_slotName = "slotA";

extern "C" int ap_net_seed_name(char *buf, int n)
{
	std::snprintf(buf, n, "%s", g_seedName);
	return g_seedName[0] ? 1 : 0;
}

extern "C" int ap_net_slot_name(char *buf, int n)
{
	std::snprintf(buf, n, "%s", g_slotName);
	return g_slotName[0] ? 1 : 0;
}

// Held Keys as the gather reads them: the RECEIVED Key item count
// (AP_GateCount(AP_IDX_KEY)), which drives the block schema 3 Key fallback.
static int g_heldKeys;

extern "C" int AP_GateCount(int itemType)
{
	return itemType == 14 /* AP_IDX_KEY */ ? g_heldKeys : 0;
}

extern "C" int AP_EmitHitCharacterCheck(long code)
{
	g_emitCount++;
	g_lastEmit = code;
	return 1;
}

extern "C" void AP_LogLine(const char *msg) { (void)msg; }

#include "../ap/ap_hit_encounter.c"

static int g_checks;
static int g_failures;

static void expect(int ok, const char *what)
{
	g_checks++;
	if (!ok)
	{
		std::printf("FAIL: %s\n", what);
		g_failures++;
	}
}

static void expect_eq(long long got, long long want, const char *what)
{
	g_checks++;
	if (got != want)
	{
		std::printf("FAIL: %s (got %lld, want %lld)\n", what, got, want);
		g_failures++;
	}
}


// Install a synthetic enabled content-plan block: the five retail cups with
// the identity order 0..15, content-plan cups 3 and 7 with their own orders
// (cup 3 reversed, cup 7 rotated by five), canonical locations and triggers.
static void install(void)
{
	std::memset(&ctr_cfg.hit, 0, sizeof ctr_cfg.hit);
	ctr_cfg.schema_version = 16;
	ctr_cfg.hit.valid = 1;
	ctr_cfg.hit.enabled = 1;
	ctr_cfg.hit.seen = 1;
	for (int i = 0; i < 16; i++)
		ctr_cfg.hit.locations[i] = 35025000 + i;
	for (int c = 0; c < 5; c++)
	{
		for (int i = 0; i < 16; i++)
			ctr_cfg.hit.cups[c].ids[i] = i;
	}
	for (int i = 0; i < 16; i++)
	{
		ctr_cfg.hit.content_cups[3].ids[i] = 15 - i;
		ctr_cfg.hit.content_cups[7].ids[i] = (i + 5) % 16;
	}
	ctr_cfg.hit.content_cup_present[3] = 1;
	ctr_cfg.hit.content_cup_present[7] = 1;
	for (int g = 8; g < 16; g++)
	{
		ctr_cfg.hit.triggers[g - 8].kind = CTR_CFG_HIT_KIND_BOSS;
		ctr_cfg.hit.triggers[g - 8].count = 1;
		// Only guest 14 is unlocked by 35011000; the rest never are.
		ctr_cfg.hit.triggers[g - 8].any_of[0] = (g == 14) ? 35011000 : 35019999;
	}
}


static int same(const int *a, int an, const int *b, int bn)
{
	if (an != bn)
		return 0;
	for (int i = 0; i < an; i++)
		if (a[i] != b[i])
			return 0;
	return 1;
}

static void test_orders(void)
{
	install();
	expect(AP_HitEncounterOrder(203) == ctr_cfg.hit.content_cups[3].ids, "content cup 3 order");
	expect(AP_HitEncounterOrder(207) == ctr_cfg.hit.content_cups[7].ids, "content cup 7 order");
	expect(AP_HitEncounterOrder(200) == NULL, "no order for a cup the plan lacks");
	expect(AP_HitEncounterOrder(227) == NULL, "no destination past name index 26");
	expect(AP_HitEncounterOrder(199) == NULL, "no destination below 200");
	expect(AP_HitEncounterOrder(100) == ctr_cfg.hit.cups[0].ids, "retail cup order unchanged");
	int out[AP_HIT_FIELD_MAX];
	expect_eq(AP_HitEncounterDrawFresh(200, 0, 7, out), 0, "a cup without an order draws nothing");
}

// Cup 3's reversed order seats the walk from position 0: with no guest
// unlocked the stock field for player 0 is the defaults in order 7, 6, ... 1.
static void test_content_cup_frozen_across_legs(void)
{
	int leg[4][AP_HIT_FIELD_MAX], retry[AP_HIT_FIELD_MAX], n[4];
	install();
	g_checked.clear();
	g_heldKeys = 0;
	AP_HitEncounterResetDrawState();
	AP_HitCupSnapshotBegin(0); // pad entry, engine cup 0
	n[0] = AP_HitCupSnapshotFieldAt(203, 0, 0, 0, 7, leg[0]);
	expect_eq(n[0], 7, "content cup leg 1 seats seven");
	expect_eq(leg[0][0], 7, "content cup draws from its own order (reversed)");
	// A guest unlocks and a Hit is checked mid-cup: the field stays frozen.
	g_checked.insert(35011000);
	g_checked.insert(35025007);
	for (int k = 1; k < 4; k++)
	{
		n[k] = AP_HitCupSnapshotFieldAt(203, 0, k, 0, 7, leg[k]);
		expect(same(leg[0], n[0], leg[k], n[k]), "every later leg reuses the frozen field");
	}
	int nr = AP_HitCupSnapshotFieldAt(203, 0, 2, 0, 7, retry);
	expect(same(leg[0], n[0], retry, nr), "a retry reuses the frozen field");
	int cur[3]; unsigned draws = 0;
	AP_HitEncounterDrawState(203, cur, &draws);
	expect_eq(draws, 1, "one draw for the whole cup");
	AP_HitEncounterDrawState(100, cur, &draws);
	expect_eq(draws, 0, "the retail cup 100 cursors are untouched");
	// The next run of the same cup draws fresh and seats the new guest.
	int next[AP_HIT_FIELD_MAX];
	AP_HitCupSnapshotBegin(0);
	AP_HitCupSnapshotFieldAt(203, 0, 0, 0, 7, next);
	expect_eq(next[0], 14, "the next run seats the guest unlocked mid-cup");
	AP_HitEncounterDrawState(203, cur, &draws);
	expect_eq(draws, 2, "the second run is the cup's second draw");
}

static void test_two_content_cups_apart(void)
{
	int a[AP_HIT_FIELD_MAX], b[AP_HIT_FIELD_MAX], again[AP_HIT_FIELD_MAX];
	install();
	g_checked.clear();
	AP_HitEncounterResetDrawState();
	AP_HitCupSnapshotBegin(0);
	int na = AP_HitCupSnapshotFieldAt(203, 0, 0, 0, 7, a);
	// Another content-plan cup on the same engine cup 0, even without its
	// pad-entry signal, is a new cup: its own order and cursors.
	int nb = AP_HitCupSnapshotFieldAt(207, 0, 1, 0, 7, b);
	expect(!same(a, na, b, nb), "two content cups seat their own fields");
	expect_eq(b[0], 5, "cup 7 walks its own order (rotated by five)");
	int cur[3]; unsigned draws = 0;
	AP_HitEncounterDrawState(207, cur, &draws);
	expect_eq(draws, 1, "cup 7 drew once");
	int nr = AP_HitCupSnapshotFieldAt(207, 0, 2, 0, 7, again);
	expect(same(b, nb, again, nr), "cup 7 then keeps its field");
	// The legacy wrapper is the retail destination.
	int r[AP_HIT_FIELD_MAX];
	AP_HitCupSnapshotBegin(1);
	AP_HitCupSnapshotField(1, 0, 0, 7, r);
	AP_HitEncounterDrawState(101, cur, &draws);
	expect_eq(draws, 1, "a retail Gem Cup draws from 100 + cupID");
}

static void test_load_decisions(void)
{
	// Since #133 PR 5c every custom-served load takes the roster; the seat
	// count of a cup load is pinned in tools/test-hit-legacy-custom-production.py.
	expect_eq(AP_HitCupSeatsPure(0, 0), 7, "a content-plan cup (engine cup 0) seats seven");
	expect_eq(AP_HitCupDestinationPure(0, -1), 100, "no content cup: retail destination");
	expect_eq(AP_HitCupDestinationPure(4, -1), 104, "purple cup destination");
	expect_eq(AP_HitCupDestinationPure(0, 226), 226, "a running content cup's destination");
	expect_eq(AP_HitCupLegKindAtPure(203, 0, 1, 0, 1, 203, 0, 0), AP_HIT_CUP_LEG_CONTINUE, "same cup continues");
	expect_eq(AP_HitCupLegKindAtPure(207, 0, 1, 0, 1, 203, 0, 0), AP_HIT_CUP_LEG_NEW, "another destination is new");
	expect_eq(AP_HitCupLegKindAtPure(203, 0, 0, 0, 1, 203, 0, 0), AP_HIT_CUP_LEG_RETRY, "same leg retries");
}

int main(void)
{
	test_orders();
	test_content_cup_frozen_across_legs();
	test_two_content_cups_apart();
	test_load_decisions();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
