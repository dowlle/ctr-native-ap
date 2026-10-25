// g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include -I ap/vendor/json/include
//     tools/test-content-global-checks-production.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-content-global-checks-production && /tmp/test-content-global-checks-production
//
// Production coverage for Itemsanity and Hit Character on a content-plan room
// (#133 PR 4d). The real parser (ap/ap_seedcfg.cpp, which includes the content
// plan admission) reads the apworld-emitted fixture
// tools/fixtures/content-plan/global-checks.json, and the real Hit Character
// gather (ap/ap_hit_encounter.c) runs on it with only the network state and
// the check-send sink stubbed:
//
//   * the room binds, and its location set holds the first Itemsanity use
//     check, the membership signal the use hook, the crate filter and the
//     Turbo Grant and Tizi Helper companions key on (AP_ItemsanityActive,
//     AP_TurboGrantItemsanityOn, ap_tizi.c);
//   * the Hit block is live with the content-plan Key fallback: each boss
//     guest joins at its Key count (Ripper Roo 1, Papu Papu 2, Komodo Joe 3,
//     Pinstripe 4), Penta Penguin at 2 because his wins are not in the plan,
//     Oxide at 4, and Fake Crash and N. Tropy only through their wins in the
//     plan (Crash Cove and Slide Coliseum Trophy Races), never through Keys;
//   * the draw on a retail destination seats an eligible guest, and a hit on a
//     default racer or a joined guest sends that racer's Hit check once.
//
// Engine state is a controlled seam; this does not claim a Steam race.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <vector>

#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"
#include "../ap/ap_content_plan.h"

static std::set<long long> g_room;    // the room's location set (bound plan checks)
static std::set<long long> g_checked; // checked on the server
static int g_heldKeys, g_emitCount;
static long g_lastEmit;

extern "C" int ap_net_location_checked(long long code) { return g_checked.count(code) ? 1 : 0; }
extern "C" int ap_net_location_exists(long long code) { return g_room.count(code) ? 1 : 0; }
extern "C" int AP_GateCount(int itemType) { return itemType == 14 /* AP_IDX_KEY */ ? g_heldKeys : 0; }
extern "C" int AP_EmitHitCharacterCheck(long code) { g_emitCount++; g_lastEmit = code; return 1; }
extern "C" void AP_LogLine(const char *) {}
extern "C" int ap_net_seed_name(char *buf, int n) { std::snprintf(buf, n, "%s", "seed"); return 1; }
extern "C" int ap_net_slot_name(char *buf, int n) { std::snprintf(buf, n, "%s", "slot"); return 1; }

#include "../ap/ap_hit_encounter.c"

static int g_checks, g_failures;
static void expect(bool ok, const char *what)
{
	g_checks++;
	if (!ok) { std::printf("FAIL: %s\n", what); g_failures++; }
}

// An accepted local-P1 hit on a live AI in a supported race.
static const unsigned kAccepted = 1u | 2u | 16u | 32u | 128u;

static bool eligible_at(int guest, int keys)
{
	g_heldKeys = keys;
	return AP_HitEncounterGuestEligible(guest) != 0;
}

int main()
{
	int vanilla[20]{};
	ctr_cfg_set_vanilla_cup_legs(vanilla);
	nlohmann::json room;
	std::ifstream("tools/fixtures/content-plan/global-checks.json") >> room;
	ap_seedcfg_parse_json(room);
	std::vector<int64_t> ids;
	for (const auto &c : room["content_plan"]["checks"]) ids.push_back(c["location"].get<int64_t>());
	g_room.insert(ids.begin(), ids.end());
	expect(!ap_seedcfg_rejected() && ap_content_plan_bind("seed", 0, 1, ids.data(), ids.size()) && ap_content_plan_active(),
	       "the global-checks room parses and binds");
	expect(ctr_cfg_active() && ctr_cfg.itemsanity == 1, "Itemsanity is on for the tracker panel");
	expect(ap_net_location_exists(35016000) && ap_net_location_exists(35016021),
	       "the room holds the Itemsanity use checks the runtime keys on");
	expect(AP_HitEncounterEnabled() == 1, "Hit Character is live on the content-plan room");

	// Content-plan Key fallback.
	static const struct { int guest, keys; const char *name; } fallback[] = {
		{10, 1, "Ripper Roo joins at 1 Key"}, {9, 2, "Papu Papu joins at 2 Keys"},
		{11, 3, "Komodo Joe joins at 3 Keys"}, {8, 4, "Pinstripe joins at 4 Keys"},
		{13, 2, "Penta Penguin joins at 2 Keys (no win in the plan)"}, {15, 4, "Oxide joins at 4 Keys"},
	};
	for (const auto &f : fallback)
		expect(!eligible_at(f.guest, f.keys - 1) && eligible_at(f.guest, f.keys), f.name);
	expect(!eligible_at(14, 4) && !eligible_at(12, 4), "Fake Crash and N. Tropy never join through Keys");
	g_checked.insert(35011000); // Crash Cove: Trophy Race, Fake Crash's win
	expect(eligible_at(14, 0) && !eligible_at(12, 0), "Fake Crash joins through his Crash Cove win in the plan");
	g_checked.insert(35016200); // Slide Coliseum: Trophy Race, N. Tropy's win
	expect(eligible_at(12, 0), "N. Tropy joins through his Slide Coliseum win in the plan");

	// The draw on a retail destination (Crash Cove, level 3) seats the joined
	// unhit guests, and a hit sends the victim's check once.
	g_heldKeys = 0;
	AP_HitEncounterResetDrawState();
	int field[AP_HIT_FIELD_MAX];
	AP_HitLoadBegin();
	AP_HitLoadBegin();
	const int n = AP_HitRaceField(3, 0, 7, field, nullptr);
	bool tropy = false, fake = false;
	for (int i = 0; i < n; ++i) { tropy |= field[i] == 12; fake |= field[i] == 14; }
	expect(n == 7 && tropy && fake, "a Crash Cove race seats both joined guests");
	expect(AP_HitEncounterOnDamage(14, 1, kAccepted) == 1 && g_emitCount == 1 && g_lastEmit == 35025014,
	       "a hit on Fake Crash sends Hit Fake Crash");
	expect(AP_HitEncounterOnDamage(14, 1, kAccepted) == 1 && g_emitCount == 1, "a second hit sends nothing more");
	expect(AP_HitEncounterOnDamage(3, 1, kAccepted) == 1 && g_lastEmit == 35025003, "a default racer's hit sends its check");

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
