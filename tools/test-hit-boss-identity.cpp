// g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include -I ap/vendor/json/include
//     tools/test-hit-boss-identity.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-hit-boss-identity && /tmp/test-hit-boss-identity
//
// Native half of ticket 12: the boss resolved-identity harness. Proves the
// native dispatch resolves the victim's engine ID at the event, so a future
// resolved boss (seated by the loader) sends that identity's mapped Hit check,
// and that the parsed `bosses` table agrees with the retail encounters. Boss
// hits count before the clear. No randomized boss loading is implemented and no
// future home-track table is touched.
//
// The shared fixture tools/fixtures/ctr_hit_boss_identity.json is the same file
// the manager runs against the apworld consumer.
//
// Build + run (from the repo root):
//   g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include \
//       -I ap/vendor/json/include tools/test-hit-boss-identity.cpp \
//       ap/ap_seedcfg.cpp -o /tmp/test-hit-boss-identity && \
//       /tmp/test-hit-boss-identity

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"

// ── stubbed network / sink ─────────────────────────────────────────────────
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

extern "C" int AP_EmitHitCharacterCheck(long code)
{
	g_emitCount++;
	g_lastEmit = code;
	return 1;
}

extern "C" void AP_LogLine(const char *msg)
{
	(void)msg;
}

// The real gather, compiled as C++ in this translation unit.
// Cup snapshot seed/slot identity (the gather references these through
// AP_HitEncounterConnectReset; these harnesses do not exercise reconnects).
extern "C" int ap_net_seed_name(char *buf, int n)
{
	std::snprintf(buf, n, "%s", "seed");
	return 1;
}

extern "C" int ap_net_slot_name(char *buf, int n)
{
	std::snprintf(buf, n, "%s", "slot");
	return 1;
}

#include "../ap/ap_hit_encounter.c"

// ── harness plumbing ───────────────────────────────────────────────────────
static nlohmann::json g_seed;   // the real encounter block fixture (seed 2101)
static nlohmann::json g_boss;   // the shared boss identity fixture
static int g_checks;
static int g_failures;

static const long kKeys[6] = {35011100, 35011101, 35011102, 35011103, 35011104, 35011105};

static const unsigned kAccepted = 1u | 2u | 16u | 32u | 128u; // AI|live|attacker|localP1|race

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

static void reset(void)
{
	g_checked.clear();
	g_emitCount = 0;
	g_lastEmit = -1;
	AP_HitEncounterConnectReset();
}

// The parsed `bosses` table agrees with the shared fixture for all five bosses
// (both Oxide win codes map to Oxide).
static void test_retail_table(void)
{
	const ctr_hit_encounters *h;
	ap_seedcfg_parse_json(g_seed);
	h = ap_seedcfg_hit_encounters();
	expect(h != NULL, "encounters parsed");
	if (!h)
		return;

	for (int i = 0; i < 6; i++)
	{
		std::string key = std::to_string(kKeys[i]);
		int want = g_boss["retail_bosses"][key].get<int>();
		expect_eq(h->boss_identity[i], want, "retail boss identity matches fixture");
		expect_eq(g_boss["expected_hit_codes"][key].get<long long>(),
		          35025000LL + want, "fixture expected hit code matches identity");
	}
	expect_eq(h->boss_identity[4], 15, "first Oxide win code -> Oxide");
	expect_eq(h->boss_identity[5], 15, "second Oxide win code -> Oxide");
}

// Native dispatch maps the ACTUAL victim engine id through the parsed locations,
// so every retail boss sends its own Hit code.
static void test_dispatch_follows_victim(void)
{
	ap_seedcfg_parse_json(g_seed);
	for (int i = 0; i < 6; i++)
	{
		std::string key = std::to_string(kKeys[i]);
		int engine = g_boss["retail_bosses"][key].get<int>();
		reset();
		g_emitCount = 0;
		g_lastEmit = -1;
		AP_HitEncounterOnDamage(engine, 1, kAccepted);
		expect_eq(g_emitCount, 1, "retail boss victim emits");
		expect_eq(g_lastEmit, 35025000LL + engine, "retail boss victim maps to its Hit code");
	}
}

// A future resolved boss is seated as a different engine id; native sends that
// identity's Hit, not the retail boss's.
//
// Fix B: the parser now refuses a non-retail `bosses` table, so this is
// exercised at DISPATCH level only. The loader is what seats a resolved
// identity; native dispatch follows the actual victim engine id.
static void test_substituted_identity(void)
{
	int engine = g_boss["substituted_case"]["engine_id"].get<int>();
	long hit = g_boss["substituted_case"]["hit_code"].get<long long>();

	ap_seedcfg_parse_json(g_seed); // retail table (accepted)

	// A future resolved boss seated as `engine` sends that identity's Hit.
	reset();
	g_emitCount = 0;
	g_lastEmit = -1;
	AP_HitEncounterOnDamage(engine, 1, kAccepted);
	expect_eq(g_emitCount, 1, "substituted victim emits");
	expect_eq(g_lastEmit, hit, "substituted victim sends the new identity's Hit");

	// The retail identity still maps to its own code; only the loader changes who
	// is seated on the route.
	reset();
	AP_HitEncounterOnDamage(10, 1, kAccepted);
	expect_eq(g_lastEmit, 35025010LL, "retail identity still maps to Ripper Roo");
}

// Boss Hit checks do not require the boss clear (the clear only unlocks ordinary
// appearances).
static void test_boss_before_clear(void)
{
	reset();
	ap_seedcfg_parse_json(g_seed);
	g_checked.clear();
	g_emitCount = 0;
	g_lastEmit = -1;
	AP_HitEncounterOnDamage(15, 1, kAccepted);
	expect_eq(g_emitCount, 1, "boss hit before clear emits");
	expect_eq(g_lastEmit, 35025015LL, "Oxide Hit code before clear");
}

int main(int argc, char **argv)
{
	const char *seedPath = argc > 1 ? argv[1]
	                                : "tools/fixtures/ctr_hit_character_seed2101.json";
	const char *bossPath = argc > 2 ? argv[2]
	                                : "tools/fixtures/ctr_hit_boss_identity.json";
	std::ifstream inSeed(seedPath), inBoss(bossPath);
	if (!inSeed || !inBoss)
	{
		std::fprintf(stderr, "cannot open fixtures\n");
		return 2;
	}
	try
	{
		inSeed >> g_seed;
		inBoss >> g_boss;
	}
	catch (const std::exception &e)
	{
		std::fprintf(stderr, "fixture is not valid JSON: %s\n", e.what());
		return 2;
	}

	test_retail_table();
	test_dispatch_follows_victim();
	test_substituted_identity();
	test_boss_before_clear();

	std::printf("%s: %d checks, %d failures\n",
	            g_failures ? "FAIL" : "PASS", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
