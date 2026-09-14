// g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include -I ap/vendor/json/include
//     tools/test-hit-cup-snapshot.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-hit-cup-snapshot && /tmp/test-hit-cup-snapshot
//
// Ticket 11: the Gem Cup roster snapshot lifecycle, driven through the REAL
// gather (ap/ap_hit_encounter.c). A cup resolves its roster once at the pad
// entry and reuses it for every leg and same-session retry; exiting/abandoning
// and starting a new cup resolves fresh, so an unlock mid-cup enters the next
// cup. Cups are not pad Hit opportunities, and player-attributed cup hits award
// checks. Uses a synthetic cup (order 0..15) so it pins the lifecycle, not the
// draw rotation (the shared fixture covers that).
//
// Build + run (from the repo root):
//   g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include \
//       -I ap/vendor/json/include tools/test-hit-cup-snapshot.cpp \
//       ap/ap_seedcfg.cpp -o /tmp/test-hit-cup-snapshot && \
//       /tmp/test-hit-cup-snapshot

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

static void same_roster(const int *a, int an, const int *b, int bn)
{
	expect_eq(an, bn, "roster size unchanged");
	for (int i = 0; i < an && i < bn; i++)
		expect_eq(a[i], b[i], "roster element unchanged");
}

// Install a synthetic enabled block: every cup with the identity order 0..15,
// canonical locations and triggers.
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
	for (int g = 8; g < 16; g++)
	{
		ctr_cfg.hit.triggers[g - 8].kind = CTR_CFG_HIT_KIND_BOSS;
		ctr_cfg.hit.triggers[g - 8].count = 1;
		// Only guest 14 is unlocked by 35011000; the rest never are.
		ctr_cfg.hit.triggers[g - 8].any_of[0] = (g == 14) ? 35011000 : 35019999;
	}
}

static void test_field_sizes(void)
{
	expect_eq(AP_HitEncounterCupFieldSize(4), 4, "purple cup four seats");
	expect_eq(AP_HitEncounterCupFieldSize(0), 7, "red cup seven seats");
	expect_eq(AP_HitEncounterCupFieldSize(3), 7, "yellow cup seven seats");
	expect_eq(AP_HitCupFieldSizePure(4), 4, "pure purple four seats");
}

static void test_resolve_once_and_preserve(void)
{
	int leg0[AP_HIT_FIELD_MAX], leg1[AP_HIT_FIELD_MAX], retry[AP_HIT_FIELD_MAX];
	int n0, n1, nr;

	install();
	g_checked.clear();
	AP_HitEncounterResetDrawState();
	AP_HitCupSnapshotBegin(0); // pad entry

	n0 = AP_HitCupSnapshotField(0, 0, 0, 7, leg0);
	expect_eq(n0, 7, "cup leg 0 seats seven");

	// A later leg reuses the snapshot.
	n1 = AP_HitCupSnapshotField(0, 1, 0, 7, leg1);
	same_roster(leg0, n0, leg1, n1);

	// A retry of the same leg reuses it too.
	nr = AP_HitCupSnapshotField(0, 1, 0, 7, retry);
	same_roster(leg0, n0, retry, nr);
}

static void test_midcup_unlock_waits(void)
{
	int before[AP_HIT_FIELD_MAX], during[AP_HIT_FIELD_MAX], next[AP_HIT_FIELD_MAX];
	int nb, nd, nn;

	install();
	g_checked.clear();
	AP_HitCupSnapshotReset();
	AP_HitCupSnapshotBegin(0);
	nb = AP_HitCupSnapshotField(0, 0, 0, 7, before);
	expect_eq(nb, 7, "cup before unlock seven");

	// Unlock a guest mid-cup (guest 14 trigger).
	g_checked.insert(35011000);
	nd = AP_HitCupSnapshotField(0, 1, 0, 7, during);
	same_roster(before, nb, during, nd);

	// Exit and start a NEW cup of the same id: resolves fresh, now with the
	// guest seated.
	AP_HitCupSnapshotBegin(0);
	nn = AP_HitCupSnapshotField(0, 0, 0, 7, next);
	expect_eq(nn, 7, "next cup seven");
	expect_eq(next[0], 14, "next cup seats the newly unlocked guest");
}

// A new cup of the same id draws fresh with the cup's own cursors, so the stock
// seats rotate from one cup run to the next; an ordinary race in between does
// not touch the cup cursors.
static void test_new_cup_rotates(void)
{
	int a[AP_HIT_FIELD_MAX], b[AP_HIT_FIELD_MAX];
	install();
	g_checked.clear();
	AP_HitEncounterResetDrawState();
	AP_HitCupSnapshotBegin(0);
	int na = AP_HitCupSnapshotField(0, 0, 0, 7, a);
	int race[AP_HIT_FIELD_MAX];
	AP_HitLoadBegin();
	AP_HitLoadBegin();
	AP_HitRaceField(3, 0, 7, race, NULL); // no order on track 3 here -> empty
	AP_HitCupSnapshotBegin(0);
	int nb = AP_HitCupSnapshotField(0, 0, 0, 7, b);
	expect_eq(na, 7, "first cup seven");
	expect_eq(nb, 7, "second cup seven");
	static const int wantA[7] = {1, 2, 3, 4, 5, 6, 7};
	static const int wantB[7] = {1, 2, 3, 4, 5, 6, 7};
	for (int i = 0; i < 7; i++)
	{
		expect_eq(a[i], wantA[i], "first cup field");
		expect_eq(b[i], wantB[i], "second cup field (stock pool of 7 fills all 7 seats)");
	}
	int cur[3];
	unsigned draws = 0;
	AP_HitEncounterDrawState(100, cur, &draws);
	expect_eq(draws, 2, "two fresh cup draws counted on cup 100");
}

static void test_player_change_preserves_snapshot(void)
{
	int p0[AP_HIT_FIELD_MAX], p1[AP_HIT_FIELD_MAX];
	int n0, n1;

	install();
	g_checked.clear();
	AP_HitCupSnapshotReset();
	AP_HitCupSnapshotBegin(0);
	n0 = AP_HitCupSnapshotField(0, 0, 0, 7, p0);

	// A supported player-character change does not redraw the frozen roster.
	n1 = AP_HitCupSnapshotField(0, 1, 5, 7, p1);
	same_roster(p0, n0, p1, n1);
}

static void test_purple_cup_four_seats(void)
{
	int ids[AP_HIT_FIELD_MAX];
	int n;

	install();
	g_checked.clear();
	AP_HitCupSnapshotReset();
	AP_HitCupSnapshotBegin(4);
	n = AP_HitCupSnapshotField(4, 0, 0, AP_HitEncounterCupFieldSize(4), ids);
	expect_eq(n, 4, "purple cup four seats");
	// A retry keeps four.
	n = AP_HitCupSnapshotField(4, 1, 0, AP_HitEncounterCupFieldSize(4), ids);
	expect_eq(n, 4, "purple cup retry four seats");
}

static void test_dispatch_in_cups(void)
{
	install();
	AP_HitEncounterConnectReset();
	g_emitCount = 0;
	g_lastEmit = -1;
	// A player-attributed hit on a cup AI victim awards a check.
	AP_HitEncounterOnDamage(9, 1, 1u | 2u | 16u | 32u | 128u);
	expect_eq(g_emitCount, 1, "cup victim emits");
	expect_eq(g_lastEmit, 35025009LL, "cup victim maps to its Hit code");
}

static void test_cups_not_pad_opportunities(void)
{
	// The cups block exists, but a cup destination is never a pad opportunity.
	install();
	expect(AP_HitEncounterOrder(100) != NULL, "cup order exists");
	expect_eq(AP_HitPadDestEligiblePure(100, 1), 0, "cup is not a pad opportunity");
	expect_eq(AP_HitPadDestEligiblePure(0, 1), 1, "track 0 is a pad opportunity");
}

// Fix A: a reconnect to the SAME seed must not redraw the active cup, even when
// a held offline win flushes and makes a guest newly eligible. A different
// room/slot or roster seed clears the snapshot.
static void test_reconnect_preserves_snapshot(void)
{
	int leg0[AP_HIT_FIELD_MAX], leg1[AP_HIT_FIELD_MAX];
	int n0, n1;

	install();
	ctr_cfg.hit.seed = 111;
	g_seedName = "seedA";
	g_slotName = "slotA";
	g_checked.clear();
	// Fresh session: the draw state captures its seed identity on the first
	// draw after this connect.
	AP_HitEncounterResetDrawState();
	AP_HitCupSnapshotBegin(0);
	n0 = AP_HitCupSnapshotField(0, 0, 0, 7, leg0);

	// A held offline win flushes on the reconnect and unlocks guest 14.
	g_checked.insert(35011000);

	// Same seed/slot reconnect: the active cup keeps its frozen roster.
	AP_HitEncounterConnectReset();
	n1 = AP_HitCupSnapshotField(0, 1, 0, 7, leg1);
	same_roster(leg0, n0, leg1, n1);

	// A different room seed clears the snapshot; the next leg resolves fresh and
	// now seats the newly eligible guest.
	g_seedName = "seedB";
	AP_HitEncounterConnectReset();
	n1 = AP_HitCupSnapshotField(0, 1, 0, 7, leg1);
	expect_eq(n1, 7, "different seed resolves a fresh cup");
	expect_eq(leg1[0], 14, "different seed seats the newly unlocked guest");

	// A different roster policy seed (same room) also clears it.
	install();
	ctr_cfg.hit.seed = 222;
	g_seedName = "seedB";
	g_checked.clear();
	AP_HitCupSnapshotReset();
	AP_HitCupSnapshotBegin(0);
	n0 = AP_HitCupSnapshotField(0, 0, 0, 7, leg0);
	g_checked.insert(35011000);
	ctr_cfg.hit.seed = 333;
	AP_HitEncounterConnectReset();
	n1 = AP_HitCupSnapshotField(0, 1, 0, 7, leg1);
	expect_eq(leg1[0], 14, "different policy seed resolves fresh");
}

int main(void)
{
	test_field_sizes();
	test_resolve_once_and_preserve();
	test_midcup_unlock_waits();
	test_new_cup_rotates();
	test_player_change_preserves_snapshot();
	test_purple_cup_four_seats();
	test_dispatch_in_cups();
	test_cups_not_pad_opportunities();
	test_reconnect_preserves_snapshot();

	std::printf("%s: %d checks, %d failures\n",
	            g_failures ? "FAIL" : "PASS", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
