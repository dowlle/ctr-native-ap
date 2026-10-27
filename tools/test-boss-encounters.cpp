// Boss opponent and kit shuffle (#169 tiers 2 and 3, slot_data schema 18).
// Compiles the REAL parser (ap/ap_seedcfg.cpp is linked in) and the real
// decisions in ap/ap_boss_venue.h, and checks:
//
//   1. The shared fixture tools/fixtures/boss_encounters.json (a byte-
//      identical copy of the apworld's worlds/ctr/test/fixtures/
//      boss_encounters.json) equals this build's opponent table and codes.
//   2. Round trip: slot_data the apworld generated for all eight switch
//      masks (tools/fixtures/boss-encounters/) parses to the seed's venues,
//      opponents and composed kits, or to retail for mask 000.
//   3. Refusals: every version 2 shape this build cannot race refuses all
//      four encounters, never a retail fallback.
//   4. Isolation: a composed kit runs only for its garage's boss race on the
//      recorded venue, never on Oxide, a cup leg or an ordinary race; the
//      opponent model plan keeps the garage boss's pack and adds at most one
//      extra hi model.
//
//   c++ -m32 -std=c++17 -DCTR_AP -Iap -Iap/vendor/json/include
//     tools/test-boss-encounters.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-boss-encounters && /tmp/test-boss-encounters
//
// Run from the repository root (the fixture paths are relative to it).
// Exit 0 = every assertion held.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"
#include "../ap/ap_boss_venue.h"

extern void ap_seedcfg_parse_json(const nlohmann::json &j);

extern "C" void AP_LogLine(const char *) {}

static int checks;
static int failures;

static void expect(bool ok, const std::string &name)
{
	checks++;
	if (!ok)
	{
		failures++;
		std::printf("FAIL %s\n", name.c_str());
	}
}

static nlohmann::json load(const std::string &path)
{
	std::ifstream in(path);
	if (!in)
	{
		std::printf("FAIL cannot open %s\n", path.c_str());
		failures++;
		return nlohmann::json::object();
	}
	return nlohmann::json::parse(in);
}

static const char *const MASKS[] = {"mask000", "mask001", "mask010", "mask011", "mask100",
                                    "mask101", "mask110", "mask110_independent", "mask111",
                                    "mask111_signature"};

static void shared_fixture()
{
	nlohmann::json doc = load("tools/fixtures/boss_encounters.json");
	expect(doc["block_version"] == AP_BOSS_ENCOUNTERS_VERSION_2, "fixture block version");
	expect(doc["opponents"].size() == AP_BOSS_OPPONENTS, "fixture opponent count");
	for (auto &kv : doc["opponents"].items())
	{
		const std::string key = kv.value()[0];
		int id = kv.value()[1];
		expect(AP_BossOpponentCharacter(key.c_str()) == id, "opponent " + key);
	}
	expect(AP_BossOpponentCharacter("nitros_oxide") == -1, "Oxide is not an opponent");
	expect(doc["segments"] == AP_BOSS_VENUE_SEGMENTS, "segment count");
	expect(doc["cooldown_range"][0] == AP_BOSS_KIT_COOLDOWN_MIN &&
	           doc["cooldown_range"][1] == AP_BOSS_KIT_COOLDOWN_MAX,
	       "cooldown range");
	for (auto &w : doc["weapon_codes"])
		expect(AP_BossKitWeaponKnown(w.get<int>()), "weapon code known");
	expect(!AP_BossKitWeaponKnown(0x06) && !AP_BossKitWeaponKnown(0x09), "shield and warp orb refused");
	for (auto &fam : doc["families"].items())
		for (auto &row : fam.value())
			expect(AP_BossKitWeaponKnown(row[0].get<int>()), "family weapon " + fam.key());
	for (int g = 0; g < AP_BOSS_VENUE_GARAGES; g++)
		expect(AP_BossOpponentCharacter(AP_BOSS_VENUE_GARAGE[g].key) == AP_BOSS_VENUE_GARAGE[g].character,
		       std::string("garage boss is an opponent: ") + AP_BOSS_VENUE_GARAGE[g].key);
}

static void round_trip()
{
	for (const char *mask : MASKS)
	{
		std::string base = std::string("tools/fixtures/boss-encounters/") + mask;
		nlohmann::json sd = load(base + "_slot_data.json");
		nlohmann::json want = load(base + "_expect.json");
		ap_seedcfg_parse_json(sd);
		const AP_BossEncounters *e = &ctr_boss_encounters;
		std::string m = mask;
		if (m == "mask000")
		{
			expect(e->state == AP_BOSS_ENC_RETAIL, m + " retail");
			continue;
		}
		expect(e->state == AP_BOSS_ENC_VALID, m + " valid: " + e->reason);
		bool v2 = m != "mask100";
		expect(e->version == (v2 ? 2 : 1), m + " version");
		bool kit = m[6] == '1';
		bool opp = m[5] == '1';
		expect(e->kit == (kit ? 1 : 0), m + " kit flag");
		for (int g = 0; g < AP_BOSS_VENUE_GARAGES; g++)
		{
			const char *key = AP_BOSS_VENUE_GARAGE[g].key;
			std::string gk = m + " " + key;
			expect(e->track[g] == want["tracks"][key].get<int>(), gk + " track");
			int oppId = AP_BossOpponentCharacter(want["opponents"][key].get<std::string>().c_str());
			expect(e->opponent[g] == oppId, gk + " opponent");
			if (!opp)
				expect(oppId == AP_BOSS_VENUE_GARAGE[g].character, gk + " opponent stays the boss");
			expect(AP_BossEncounterOpponent(e, g) == (v2 ? oppId : -1), gk + " opponent accessor");
			expect(e->composed[g] == (kit ? 1 : 0), gk + " composed");
			if (kit)
			{
				auto &rows = want["kits"][key];
				bool same = rows.size() == AP_BOSS_VENUE_SEGMENTS;
				for (int i = 0; same && i < AP_BOSS_VENUE_SEGMENTS; i++)
				{
					const AP_BossKitSegment &s = e->schedule[g][i];
					same = s.checkpoint == rows[i][0] && s.throwFlag == rows[i][1] && s.weapon == rows[i][2] &&
					       s.pathDisabled == rows[i][3] && s.cooldown == rows[i][4] && s.juice == rows[i][5];
				}
				expect(same, gk + " schedule");
				expect(AP_BossKitScheduleProblem(e->schedule[g], AP_BOSS_VENUE_SEGMENTS,
				                                 AP_BOSS_VENUE_RESTART_POINTS[e->track[g]]) == 0,
				       gk + " schedule fits venue");
			}
		}
		if (m == "mask111_signature")
			expect(e->lossEasing == 0, m + " loss easing off");
		else
			expect(e->lossEasing == 1, m + " loss easing on");
	}
}

static void refuse(const char *name, void (*mutate)(nlohmann::json &), const char *mask = "mask111")
{
	nlohmann::json sd = load(std::string("tools/fixtures/boss-encounters/") + mask + "_slot_data.json");
	mutate(sd);
	ap_seedcfg_parse_json(sd);
	expect(ctr_boss_encounters.state == AP_BOSS_ENC_REFUSED, std::string("refuse ") + name);
	for (int g = 0; g < AP_BOSS_VENUE_GARAGES; g++)
		expect(AP_BossEncounterVenue(&ctr_boss_encounters, g) == -1, std::string("refused shuts ") + name);
}

static void refusals()
{
	refuse("v1 with opponent on", [](nlohmann::json &j) { j["boss_encounters"]["version"] = 1; });
	refuse("v3", [](nlohmann::json &j) { j["boss_encounters"]["version"] = 3; });
	refuse("kit record missing", [](nlohmann::json &j) { j["boss_encounters"].erase("kit"); });
	refuse("extra top-level field", [](nlohmann::json &j) { j["boss_encounters"]["extra"] = 1; });
	refuse("bad intensity", [](nlohmann::json &j) { j["boss_encounters"]["kit"]["intensity"] = "extreme"; });
	refuse("loss easing not bool", [](nlohmann::json &j) { j["boss_encounters"]["kit"]["loss_easing"] = 1; });
	refuse("unknown opponent", [](nlohmann::json &j) { j["boss_encounters"]["garages"]["pinstripe"]["opponent"] = "nitros_oxide"; });
	refuse("inherited with kit on", [](nlohmann::json &j) { j["boss_encounters"]["garages"]["pinstripe"]["attack"] = "inherited"; });
	refuse("short schedule", [](nlohmann::json &j) { j["boss_encounters"]["garages"]["pinstripe"]["schedule"].erase(0); });
	refuse("schedule not list", [](nlohmann::json &j) { j["boss_encounters"]["garages"]["pinstripe"]["schedule"] = 5; });
	refuse("first checkpoint not 0", [](nlohmann::json &j) { j["boss_encounters"]["garages"]["papu_papu"]["schedule"][0][0] = 1; });
	refuse("checkpoint past venue", [](nlohmann::json &j) { j["boss_encounters"]["garages"]["papu_papu"]["schedule"][4][0] = 250; });
	refuse("decreasing checkpoints", [](nlohmann::json &j) {
		auto &s = j["boss_encounters"]["garages"]["papu_papu"]["schedule"];
		s[2][0] = s[1][0];
	});
	refuse("shield weapon", [](nlohmann::json &j) { j["boss_encounters"]["garages"]["papu_papu"]["schedule"][1][2] = 6; });
	refuse("bad throw", [](nlohmann::json &j) { j["boss_encounters"]["garages"]["papu_papu"]["schedule"][1][1] = 1; });
	refuse("zero cooldown", [](nlohmann::json &j) { j["boss_encounters"]["garages"]["papu_papu"]["schedule"][1][4] = 0; });
	refuse("negative field", [](nlohmann::json &j) { j["boss_encounters"]["garages"]["papu_papu"]["schedule"][1][5] = -1; });
	refuse("row without schedule", [](nlohmann::json &j) { j["boss_encounters"]["garages"]["papu_papu"].erase("schedule"); });
	refuse("ai profile", [](nlohmann::json &j) { j["boss_encounters"]["garages"]["papu_papu"]["ai_profile"] = "boss"; });
	refuse("kit switch off with composed rows", [](nlohmann::json &j) { j["ctr_options"]["boss_kit_shuffle"] = false; });
	refuse("opponent switch not bool", [](nlohmann::json &j) { j["ctr_options"]["boss_opponent_shuffle"] = 1; });
	// Opponent-only seed: a swapped racer with the switch off, and a schedule
	// on an inherited row.
	refuse("swap with switch off", [](nlohmann::json &j) {
		j["ctr_options"]["boss_opponent_shuffle"] = false;
		j["ctr_options"]["boss_kit_shuffle"] = true;
	}, "mask010");
	refuse("schedule on inherited row", [](nlohmann::json &j) {
		j["boss_encounters"]["garages"]["ripper_roo"]["schedule"] = {{0, 3, 3, 0, 15, 0}};
	}, "mask010");
	refuse("v2 block with only the track switch", [](nlohmann::json &j) {
		j["ctr_options"]["boss_opponent_shuffle"] = false;
	}, "mask110");
}

static void isolation()
{
	ap_seedcfg_parse_json(load("tools/fixtures/boss-encounters/mask011_slot_data.json"));
	const AP_BossEncounters *e = &ctr_boss_encounters;
	expect(e->state == AP_BOSS_ENC_VALID, "isolation seed valid");
	for (int g = 0; g < AP_BOSS_VENUE_GARAGES; g++)
	{
		int home = AP_BOSS_VENUE_GARAGE[g].homeLevel;
		expect(AP_BossEncounterKitGarage(e, 1, 0, g, home) == g, "kit runs on the home track");
		expect(AP_BossEncounterKitGarage(e, 1, 1, g, home) == -1, "kit never on a cup leg");
		expect(AP_BossEncounterKitGarage(e, 0, 0, g, home) == -1, "kit never on an ordinary race");
		expect(AP_BossEncounterKitGarage(e, 1, 0, g, (home + 1) % 18) == -1, "kit only on the recorded venue");
		expect(AP_BossEncounterShuffledGarage(e, 1, 0, g, home) == -1, "no venue adapter on the home track");
	}
	expect(AP_BossEncounterKitGarage(e, 1, 0, 4, 13) == -1, "Oxide keeps his own attack");
	expect(AP_BossEncounterKitGarage(e, 1, 0, 5, 13) == -1, "Oxide Final keeps his own attack");

	ap_seedcfg_parse_json(load("tools/fixtures/boss-encounters/mask100_slot_data.json"));
	expect(AP_BossEncounterKitGarage(e, 1, 0, 0, e->track[0]) == -1, "tier 1 seed has no kit");
	expect(AP_BossEncounterOpponent(e, 0) == -1, "tier 1 seed keeps the retail opponent");

	int extra;
	expect(AP_BossOpponentModels(10, 10, 0, &extra) == 10 && extra == -1, "own boss: no extra");
	expect(AP_BossOpponentModels(10, 3, 0, &extra) == 10 && extra == 3, "swap: one extra, boss pack kept");
	expect(AP_BossOpponentModels(9, 0, 0, &extra) == 9 && extra == -1, "player's racer: slot 0 has it");
	expect(AP_BossOpponentModels(8, 12, 14, &extra) == 8 && extra == 12, "N. Tropy extra");
}

int main()
{
	shared_fixture();
	round_trip();
	refusals();
	isolation();
	std::printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
