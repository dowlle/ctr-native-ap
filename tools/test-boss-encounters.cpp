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
#include <set>
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
	expect(doc["weapon_codes"].size() == 14, "fixture weapon code count");
	expect(!AP_BossKitWeaponKnown(0x05) && !AP_BossKitWeaponKnown(0x0a) && !AP_BossKitWeaponKnown(0x0e),
	       "spring, triple bomb and unknown codes refused");
	// J03: the final record's tables.
	expect(doc["oxide"][0] == AP_BOSS_OXIDE_KEY && doc["oxide"][1] == AP_BOSS_OXIDE_CHARACTER, "fixture Oxide key");
	expect(AP_BossOpponentCharacterEx("nitros_oxide", 1) == 15, "Oxide with the pool switch");
	expect(AP_BossOpponentCharacterEx("nitros_oxide", 0) == -1, "Oxide without the pool switch");
	expect(doc["oxide_checkpoints"].size() == AP_BOSS_FINAL_SEGMENTS, "Oxide segment count");
	for (int i = 0; i < AP_BOSS_FINAL_SEGMENTS; i++)
		expect(doc["oxide_checkpoints"][i] == AP_BOSS_OXIDE_CHECKPOINTS[i], "Oxide checkpoint");
	expect(doc["final_locations"][0] == 35011104 && doc["final_locations"][1] == 35011105, "final locations");
	expect(doc["gauntlet_legs"] == AP_BOSS_GAUNTLET_LEGS, "gauntlet legs");
	expect(doc["oxide_only_families"].size() == 1 && doc["oxide_only_families"][0] == "super_engine",
	       "super engine is Oxide's only");
	expect(doc["certified_rows"].size() == AP_BOSS_KIT_CERTIFIED_ROWS, "certified row count");
	for (auto &r : doc["certified_rows"])
		expect(AP_BossKitRowCertified(r[0], r[1], r[2], r[3]), "certified row known");
	expect(!AP_BossKitRowCertified(0x02, 2, 1, 2), "uncertified missile variant refused");
	// Both directions: every native row is in the fixture too.
	for (int i = 0; i < AP_BOSS_KIT_CERTIFIED_ROWS; i++)
	{
		bool found = false;
		for (auto &r : doc["certified_rows"])
			found = found || (r[0] == AP_BOSS_KIT_CERTIFIED[i][0] && r[1] == AP_BOSS_KIT_CERTIFIED[i][1] &&
			                  r[2] == AP_BOSS_KIT_CERTIFIED[i][2] && r[3] == AP_BOSS_KIT_CERTIFIED[i][3]);
		expect(found, "native certified row is in the fixture");
	}
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
	refuse("checkpoint off the mapped segment", [](nlohmann::json &j) {
		auto &seg = j["boss_encounters"]["garages"]["papu_papu"]["schedule"][1];
		seg[0] = seg[0].get<int>() + 1;
	});
	refuse("uncertified row", [](nlohmann::json &j) {
		auto &seg = j["boss_encounters"]["garages"]["papu_papu"]["schedule"][1];
		seg = {seg[0], 2, 2, 1, 15, 2};
	});
	refuse("never attacks", [](nlohmann::json &j) {
		for (auto &seg : j["boss_encounters"]["garages"]["papu_papu"]["schedule"])
			seg = {seg[0], 2, 15, 1, 12, 2};
	});
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
	refuse("venue moved with the track switch off", [](nlohmann::json &j) {
		j["boss_encounters"]["garages"]["ripper_roo"]["track"] = 3;
	}, "mask011");
	refuse("location wraps in 32 bits", [](nlohmann::json &j) {
		j["boss_encounters"]["garages"]["ripper_roo"]["location"] = 35011100LL + 4294967296LL;
	});
	refuse("track wraps in 32 bits", [](nlohmann::json &j) {
		j["boss_encounters"]["garages"]["ripper_roo"]["track"] = 4294967302LL;
	});
	refuse("v2 block with only the track switch", [](nlohmann::json &j) {
		j["ctr_options"]["boss_opponent_shuffle"] = false;
	}, "mask110");
}

// J03: build a valid full-suite seed from mask111 (all three switches): N.
// Oxide in the pool, a final record and the gauntlet.
static nlohmann::json full_suite(bool profileAll, bool oxideDrawn)
{
	nlohmann::json sd = load("tools/fixtures/boss-encounters/mask111_slot_data.json");
	auto &b = sd["boss_encounters"];
	sd["ctr_options"]["boss_oxide_in_pool"] = true;
	sd["ctr_options"]["purple_cup_boss_gauntlet"] = true;
	sd["ctr_options"]["oxide_attack_profile"] = profileAll ? "all_compatible" : "retail";
	std::set<std::string> used;
	for (int g = 0; g < AP_BOSS_VENUE_GARAGES; g++)
		used.insert(b["garages"][AP_BOSS_VENUE_GARAGE[g].key]["opponent"].get<std::string>());
	if (oxideDrawn)
	{
		b["garages"]["komodo_joe"]["opponent"] = "nitros_oxide";
		used.insert("nitros_oxide");
	}
	std::string finalRacer;
	for (int i = 0; i < AP_BOSS_OPPONENTS && finalRacer.empty(); i++)
		if (!used.count(AP_BOSS_OPPONENT[i].key))
			finalRacer = AP_BOSS_OPPONENT[i].key;
	nlohmann::json sched = nlohmann::json::array();
	if (profileAll)
		for (int i = 0; i < AP_BOSS_FINAL_SEGMENTS; i++)
		{
			static const int rows[4][4] = {{0x0d, 3, 1, 0}, {0x08, 3, 1, 0}, {0x09, 2, 1, 0}, {0x0f, 3, 1, 0}};
			const int *r = rows[i % 4];
			sched.push_back({AP_BOSS_OXIDE_CHECKPOINTS[i], r[1], r[0], r[2], 12, r[3]});
		}
	b["final"] = {{"locations", {35011104, 35011105}}, {"opponent", finalRacer},
	              {"attack", profileAll ? "composed" : "inherited"}, {"schedule", sched}};
	nlohmann::json legs = nlohmann::json::array();
	static const int legTrack[4] = {6, 5, -1, 7};
	for (int i = 0; i < AP_BOSS_GAUNTLET_LEGS; i++)
	{
		nlohmann::json ls = nlohmann::json::array();
		if (legTrack[i] >= 0)
		{
			int homeLast = AP_BOSS_VENUE_RESTART_POINTS[AP_BOSS_VENUE_GARAGE[i].homeLevel] - 1;
			static const int rows[5][4] = {{0x07, 3, 1, 0}, {0x06, 3, 1, 0}, {0x00, 3, 1, 0}, {0x0c, 3, 1, 1}, {0x01, 3, 1, 0}};
			for (int k = 0; k < AP_BOSS_VENUE_SEGMENTS; k++)
				ls.push_back({AP_BossVenueCheckpoint(AP_BOSS_VENUE_GARAGE[i].attack[k], homeLast,
				                                     AP_BOSS_VENUE_RESTART_POINTS[legTrack[i]] - 1),
				              rows[k][1], rows[k][0], rows[k][2], 12, rows[k][3]});
		}
		legs.push_back({{"track", legTrack[i]},
		                {"attacker", b["garages"][AP_BOSS_VENUE_GARAGE[i].key]["opponent"]},
		                {"schedule", ls}});
	}
	b["gauntlet"] = {{"legs", legs}};
	return sd;
}

static void full_suite_cases()
{
	for (int variant = 0; variant < 4; variant++)
	{
		bool profileAll = variant & 1, oxideDrawn = variant & 2;
		std::string v = "full suite " + std::to_string(variant);
		ap_seedcfg_parse_json(full_suite(profileAll, oxideDrawn));
		const AP_BossEncounters *e = &ctr_boss_encounters;
		expect(e->state == AP_BOSS_ENC_VALID, v + " valid: " + e->reason);
		expect(e->oxideInPool && e->gauntletOn && e->oxideProfileAll == profileAll, v + " scalars");
		expect(e->finalPresent && e->finalOpponent >= 0 && e->finalOpponent != 15, v + " final racer");
		expect(AP_BossEncounterFinalOpponent(e) == e->finalOpponent, v + " final accessor");
		expect(e->finalComposed == (profileAll ? 1 : 0), v + " final composed");
		if (profileAll)
			expect(e->finalSchedule[0].weapon == 0x0d && e->finalSchedule[12].checkpoint == 0x7f, v + " final schedule");
		if (oxideDrawn)
			expect(e->opponent[2] == 15, v + " Oxide drawn for Joe's garage");
		expect(e->gauntletTrack[2] == -1 && !e->gauntletComposed[2], v + " leg without a track");
		expect(e->gauntletTrack[0] == 6 && e->gauntletComposed[0] && e->gauntletSchedule[0][0].weapon == 0x07,
		       v + " leg 0 armed");
		for (int i = 0; i < AP_BOSS_GAUNTLET_LEGS; i++)
			expect(e->gauntletAttacker[i] == e->opponent[i], v + " leg attacker is the garage racer");
	}
	// Profile only: no garage switch, a version 2 block of retail rows and a
	// composed final record for N. Oxide himself.
	{
		nlohmann::json sd = load("tools/fixtures/boss-encounters/mask000_slot_data.json");
		nlohmann::json v2 = load("tools/fixtures/boss-encounters/mask010_slot_data.json")["boss_encounters"];
		for (int g = 0; g < AP_BOSS_VENUE_GARAGES; g++)
			v2["garages"][AP_BOSS_VENUE_GARAGE[g].key]["opponent"] = AP_BOSS_VENUE_GARAGE[g].key;
		nlohmann::json fin = full_suite(true, false)["boss_encounters"]["final"];
		fin["opponent"] = "nitros_oxide";
		v2["final"] = fin;
		sd["boss_encounters"] = v2;
		sd["ctr_options"]["oxide_attack_profile"] = "all_compatible";
		ap_seedcfg_parse_json(sd);
		const AP_BossEncounters *e = &ctr_boss_encounters;
		expect(e->state == AP_BOSS_ENC_VALID, std::string("profile only valid: ") + e->reason);
		expect(e->finalComposed && e->finalOpponent == 15 && AP_BossEncounterFinalOpponent(e) == -1,
		       "profile only keeps N. Oxide");
		for (int g = 0; g < AP_BOSS_VENUE_GARAGES; g++)
			expect(AP_BossEncounterKitGarage(e, 1, 0, g, AP_BOSS_VENUE_GARAGE[g].homeLevel) == -1,
			       "profile only composes no garage kit");
	}

	auto refuseFull = [](const char *name, void (*mutate)(nlohmann::json &)) {
		nlohmann::json sd = full_suite(true, true);
		mutate(sd);
		ap_seedcfg_parse_json(sd);
		expect(ctr_boss_encounters.state == AP_BOSS_ENC_REFUSED, std::string("refuse ") + name);
	};
	refuseFull("pool switch not bool", [](nlohmann::json &j) { j["ctr_options"]["boss_oxide_in_pool"] = 1; });
	refuseFull("unknown profile", [](nlohmann::json &j) { j["ctr_options"]["oxide_attack_profile"] = "mayhem"; });
	refuseFull("pool without opponent switch", [](nlohmann::json &j) { j["ctr_options"]["boss_opponent_shuffle"] = false; });
	refuseFull("gauntlet without kit", [](nlohmann::json &j) { j["ctr_options"]["boss_kit_shuffle"] = false; });
	refuseFull("final missing", [](nlohmann::json &j) { j["boss_encounters"].erase("final"); });
	refuseFull("gauntlet missing", [](nlohmann::json &j) { j["boss_encounters"].erase("gauntlet"); });
	refuseFull("gauntlet without its switch", [](nlohmann::json &j) { j["ctr_options"]["purple_cup_boss_gauntlet"] = false; });
	refuseFull("final extra field", [](nlohmann::json &j) { j["boss_encounters"]["final"]["x"] = 1; });
	refuseFull("final wrong locations", [](nlohmann::json &j) { j["boss_encounters"]["final"]["locations"] = {35011104}; });
	refuseFull("final is N. Oxide with the pool", [](nlohmann::json &j) { j["boss_encounters"]["final"]["opponent"] = "nitros_oxide"; });
	refuseFull("final repeats a garage racer", [](nlohmann::json &j) {
		j["boss_encounters"]["final"]["opponent"] = j["boss_encounters"]["garages"]["ripper_roo"]["opponent"];
	});
	refuseFull("two garages share a racer", [](nlohmann::json &j) {
		j["boss_encounters"]["garages"]["papu_papu"]["opponent"] = j["boss_encounters"]["garages"]["ripper_roo"]["opponent"];
	});
	refuseFull("final inherited with all_compatible", [](nlohmann::json &j) {
		j["boss_encounters"]["final"]["attack"] = "inherited";
		j["boss_encounters"]["final"]["schedule"] = nlohmann::json::array();
	});
	refuseFull("final schedule short", [](nlohmann::json &j) { j["boss_encounters"]["final"]["schedule"].erase(0); });
	refuseFull("final checkpoint moved", [](nlohmann::json &j) { j["boss_encounters"]["final"]["schedule"][3][0] = 48; });
	refuseFull("final uncertified row", [](nlohmann::json &j) { j["boss_encounters"]["final"]["schedule"][1][1] = 2; });
	refuseFull("super engine in a garage kit", [](nlohmann::json &j) {
		auto &seg = j["boss_encounters"]["garages"]["papu_papu"]["schedule"][1];
		seg = {seg[0], 3, 13, 1, 12, 0};
	});
	refuseFull("super engine in a gauntlet leg", [](nlohmann::json &j) {
		auto &seg = j["boss_encounters"]["gauntlet"]["legs"][0]["schedule"][1];
		seg = {seg[0], 3, 13, 1, 12, 0};
	});
	refuseFull("three legs", [](nlohmann::json &j) { j["boss_encounters"]["gauntlet"]["legs"].erase(3); });
	refuseFull("leg attacker not the garage racer", [](nlohmann::json &j) {
		j["boss_encounters"]["gauntlet"]["legs"][0]["attacker"] = j["boss_encounters"]["final"]["opponent"];
	});
	refuseFull("leg track out of range", [](nlohmann::json &j) { j["boss_encounters"]["gauntlet"]["legs"][0]["track"] = 18; });
	refuseFull("leg without track with schedule", [](nlohmann::json &j) {
		j["boss_encounters"]["gauntlet"]["legs"][2]["schedule"] = j["boss_encounters"]["gauntlet"]["legs"][0]["schedule"];
	});
	refuseFull("leg checkpoint off the mapped segment", [](nlohmann::json &j) {
		auto &seg = j["boss_encounters"]["gauntlet"]["legs"][1]["schedule"][2];
		seg[0] = seg[0].get<int>() + 1;
	});
	refuseFull("gauntlet extra field", [](nlohmann::json &j) { j["boss_encounters"]["gauntlet"]["x"] = 1; });
	refuseFull("two garages share a venue", [](nlohmann::json &j) {
		// Same venue in two garages (the schedules follow the venue).
		j["boss_encounters"]["garages"]["papu_papu"]["track"] = j["boss_encounters"]["garages"]["ripper_roo"]["track"];
	});
	// Venues never repeat (decision 7): a track and opponent seed whose two
	// garages share a venue.
	{
		nlohmann::json sd = load("tools/fixtures/boss-encounters/mask110_slot_data.json");
		sd["boss_encounters"]["garages"]["papu_papu"]["track"] = sd["boss_encounters"]["garages"]["ripper_roo"]["track"];
		ap_seedcfg_parse_json(sd);
		expect(ctr_boss_encounters.state == AP_BOSS_ENC_REFUSED &&
		           std::string(ctr_boss_encounters.reason) == "two garages share a venue",
		       "refuse two garages sharing a venue");
	}
	// N. Oxide drawn for a garage without the pool switch (and no final).
	{
		nlohmann::json sd = load("tools/fixtures/boss-encounters/mask010_slot_data.json");
		sd["boss_encounters"]["garages"]["pinstripe"]["opponent"] = "nitros_oxide";
		ap_seedcfg_parse_json(sd);
		expect(ctr_boss_encounters.state == AP_BOSS_ENC_REFUSED, "refuse Oxide without the pool switch");
	}
	// A version 2 seed from before the new scalars still parses (absent =
	// off) and has no final record.
	ap_seedcfg_parse_json(load("tools/fixtures/boss-encounters/mask111_slot_data.json"));
	expect(ctr_boss_encounters.state == AP_BOSS_ENC_VALID && !ctr_boss_encounters.finalPresent &&
	           AP_BossEncounterFinalOpponent(&ctr_boss_encounters) == -1,
	       "older version 2 seed parses without a final record");
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
	full_suite_cases();
	isolation();
	std::printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
