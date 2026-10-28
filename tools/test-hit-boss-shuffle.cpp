// g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include -I ap/vendor/json/include
//     tools/test-hit-boss-shuffle.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-hit-boss-shuffle && /tmp/test-hit-boss-shuffle
//
// Hit Character checks under boss opponent shuffle (J03, decision 9 of
// 2026-10-28). Links the REAL parser and the real Hit gather and checks:
//
//   1. With boss_opponent_shuffle on, the Hit block's `bosses` table and the
//      guest unlock wins must be the ones derived from the boss record
//      (ap/ap_hit_boss_shuffle.h): a drawn guest unlocks through the garage
//      it was drawn for, an undrawn boss guest falls back to Keys (the
//      content-plan table), N. Oxide drawn for a garage unlocks through it,
//      and the final race's racer unlocks through both Oxide wins.
//   2. The canonical retail tables are refused for such a seed, and the
//      derived tables are refused for a seed without the shuffle.
//   3. Dispatch: a hit on the racer seated in a boss race sends that racer's
//      Hit code (the boss race seats the drawn racer as P2).
//
// Run from the repository root.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"
#include "../ap/ap_hit_boss_shuffle.h"

static std::set<long long> g_checked;
static int g_emitCount;
static long g_lastEmit;
static int g_heldKeys;

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

extern "C" void AP_LogLine(const char *msg)
{
	(void)msg;
}

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

static int g_checks;
static int g_failures;
static const unsigned kAccepted = 1u | 2u | 16u | 32u | 128u;

static void expect(bool ok, const std::string &what)
{
	g_checks++;
	if (!ok)
	{
		std::printf("FAIL: %s\n", what.c_str());
		g_failures++;
	}
}

static nlohmann::json load(const char *path)
{
	std::ifstream in(path);
	if (!in)
	{
		std::printf("FAIL cannot open %s\n", path);
		g_failures++;
		return nlohmann::json::object();
	}
	return nlohmann::json::parse(in);
}

// The drawn seed: Penta in Ripper Roo's garage, Crash in Papu's, N. Oxide in
// Komodo Joe's (boss_oxide_in_pool), Pinstripe keeps his own, and Ripper Roo
// races the final race.
static const char *kDrawn[4] = {"penta_penguin", "crash_bandicoot", "nitros_oxide", "pinstripe"};

static nlohmann::json shuffled_seed(bool derived)
{
	nlohmann::json sd = load("tools/fixtures/boss-encounters/mask010_slot_data.json");
	nlohmann::json hit = load("tools/fixtures/ctr_hit_character_seed2101.json")["hit_character_encounters"];
	sd["ctr_options"]["boss_oxide_in_pool"] = true;
	sd["ctr_options"]["oxide_attack_profile"] = "retail";
	const char *keys[4] = {"ripper_roo", "papu_papu", "komodo_joe", "pinstripe"};
	for (int g = 0; g < 4; g++)
		sd["boss_encounters"]["garages"][keys[g]]["opponent"] = kDrawn[g];
	sd["boss_encounters"]["final"] = {{"locations", {35011104, 35011105}}, {"opponent", "ripper_roo"},
	                                  {"attack", "inherited"}, {"schedule", nlohmann::json::array()}};
	sd["ctr_options"]["hit_character"] = true;
	hit["schema"] = 3;
	if (derived)
	{
		hit["bosses"] = {{"35011100", 13}, {"35011101", 0}, {"35011102", 15},
		                 {"35011103", 8},  {"35011104", 10}, {"35011105", 10}};
		hit["unlock_triggers"] = {
		    {"8", {{"kind", "boss"}, {"any_of", {35011103}}}},
		    {"9", {{"kind", "boss"}, {"any_of", nlohmann::json::array()}, {"fallback_keys", 2}}},
		    {"10", {{"kind", "boss"}, {"any_of", {35011104, 35011105}}}},
		    {"11", {{"kind", "boss"}, {"any_of", nlohmann::json::array()}, {"fallback_keys", 3}}},
		    {"12", {{"kind", "track"}, {"any_of", {35016200, 35016201}}}},
		    {"13", {{"kind", "boss"}, {"any_of", {35011008, 35011010, 35011100}}}},
		    {"14", {{"kind", "track"}, {"any_of", {35011000, 35011003}}}},
		    {"15", {{"kind", "boss"}, {"any_of", {35011102}}}},
		};
	}
	sd["hit_character_encounters"] = hit;
	return sd;
}

static void reset()
{
	g_checked.clear();
	g_emitCount = 0;
	g_lastEmit = -1;
	g_heldKeys = 0;
	AP_HitEncounterConnectReset();
}

static void test_derivation()
{
	nlohmann::json sd = shuffled_seed(true);
	ap_seedcfg_parse_json(sd);
	expect(ctr_boss_encounters.state == AP_BOSS_ENC_VALID, std::string("boss record valid: ") + ctr_boss_encounters.reason);
	expect(AP_HitBossShuffleActive(&ctr_boss_encounters), "shuffle active");
	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	expect(h != NULL && h->valid, "derived Hit block accepted");
	if (h == NULL || !h->valid)
		return;
	const int want[6] = {13, 0, 15, 8, 10, 10};
	for (int i = 0; i < 6; i++)
		expect(h->boss_identity[i] == want[i], "boss identity " + std::to_string(i));
	expect(h->triggers[13 - 8].count == 3 && h->triggers[13 - 8].any_of[2] == 35011100, "Penta unlocks through Roo's garage");
	expect(h->triggers[9 - 8].count == 0 && h->triggers[9 - 8].fallback_keys == 2, "undrawn Papu falls back to 2 Keys");
	expect(h->triggers[15 - 8].any_of[0] == 35011102, "N. Oxide unlocks through Joe's garage");

	// Pure helper agrees with the hand-written table.
	int ident[6];
	AP_HitBossIdentity(&ctr_boss_encounters, ident);
	long any[8];
	int kb;
	expect(AP_HitBossGuestTrigger(ident, 10, any, &kb) == 2 && kb == 1 && any[0] == 35011104, "Roo via the final race");
	expect(AP_HitBossGuestTrigger(ident, 14, any, &kb) == 2 && kb == 0, "Fake Crash keeps its track wins");

	// Unlock follows the drawn garage win, not the physical boss.
	reset();
	expect(!AP_HitEncounterGuestEligible(13), "Penta locked before the win");
	g_checked.insert(35011100);
	expect(AP_HitEncounterGuestEligible(13), "beating Roo's garage unlocks Penta");
	expect(!AP_HitEncounterGuestEligible(10), "beating Roo's garage does not unlock Ripper Roo");
	g_checked.insert(35011104);
	expect(AP_HitEncounterGuestEligible(10), "beating the final race unlocks Ripper Roo");
	expect(!AP_HitEncounterGuestEligible(9), "Papu locked without Keys");
	g_heldKeys = 2;
	expect(AP_HitEncounterGuestEligible(9), "Papu joins with 2 Keys");

	// Dispatch: the seated racer's Hit code.
	reset();
	AP_HitEncounterOnDamage(13, 1, kAccepted);
	expect(g_emitCount == 1 && g_lastEmit == 35025013L, "hitting Penta in Roo's garage sends Hit Penta");
	reset();
	AP_HitEncounterOnDamage(15, 1, kAccepted);
	expect(g_lastEmit == 35025015L, "hitting N. Oxide in Joe's garage sends Hit N. Oxide");
}

static void refuse_hit(const std::string &name, nlohmann::json sd)
{
	ap_seedcfg_parse_json(sd);
	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	expect(h == NULL || !h->valid, "refuse " + name);
}

static void test_refusals()
{
	refuse_hit("canonical tables under the shuffle", shuffled_seed(false));
	{
		nlohmann::json sd = shuffled_seed(true);
		sd["hit_character_encounters"]["unlock_triggers"]["13"]["any_of"] = {35011008, 35011010};
		refuse_hit("Penta without the drawn garage win", sd);
	}
	{
		nlohmann::json sd = shuffled_seed(true);
		sd["hit_character_encounters"]["unlock_triggers"]["13"]["kind"] = "track";
		refuse_hit("Penta kind track", sd);
	}
	{
		nlohmann::json sd = shuffled_seed(true);
		sd["hit_character_encounters"]["unlock_triggers"]["9"]["fallback_keys"] = 1;
		refuse_hit("wrong fallback count", sd);
	}
	{
		nlohmann::json sd = shuffled_seed(true);
		sd["hit_character_encounters"]["unlock_triggers"]["11"].erase("fallback_keys");
		refuse_hit("undrawn boss guest without fallback", sd);
	}
	{
		nlohmann::json sd = shuffled_seed(true);
		sd["hit_character_encounters"]["bosses"]["35011105"] = 15;
		refuse_hit("final race identity not the drawn racer", sd);
	}
	// Without the shuffle the derived tables are wrong and the canonical ones
	// stand.
	{
		nlohmann::json sd = shuffled_seed(true);
		sd["boss_encounters"] = load("tools/fixtures/boss-encounters/mask100_slot_data.json")["boss_encounters"];
		sd["ctr_options"]["boss_opponent_shuffle"] = false;
		sd["ctr_options"]["boss_kit_shuffle"] = false;
		sd["ctr_options"]["boss_track_shuffle"] = true;
		sd["ctr_options"]["boss_oxide_in_pool"] = false;
		refuse_hit("derived tables without the shuffle", sd);
		nlohmann::json canon = shuffled_seed(false);
		canon["boss_encounters"] = sd["boss_encounters"];
		canon["ctr_options"] = sd["ctr_options"];
		ap_seedcfg_parse_json(canon);
		const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
		expect(h != NULL && h->valid, "canonical tables without the shuffle accepted");
	}
}

// Opponent switch on, but the draw put the four retail bosses in their own
// garages: the identity is retail, so the Hit block stays canonical.
static void test_retail_identity_under_switch()
{
	nlohmann::json sd = shuffled_seed(false);
	const char *keys[4] = {"ripper_roo", "papu_papu", "komodo_joe", "pinstripe"};
	for (int g = 0; g < 4; g++)
		sd["boss_encounters"]["garages"][keys[g]]["opponent"] = keys[g];
	sd["boss_encounters"].erase("final");
	sd["ctr_options"]["boss_oxide_in_pool"] = false;
	ap_seedcfg_parse_json(sd);
	expect(ctr_boss_encounters.state == AP_BOSS_ENC_VALID && ctr_boss_encounters.opponentOn, "switch on, retail draw");
	expect(!AP_HitBossShuffleActive(&ctr_boss_encounters), "retail identity is not shuffled");
	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	expect(h != NULL && h->valid, "canonical Hit block accepted for a retail draw");
}

int main()
{
	test_retail_identity_under_switch();
	test_derivation();
	test_refusals();
	std::printf("%s: %d checks, %d failures\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
