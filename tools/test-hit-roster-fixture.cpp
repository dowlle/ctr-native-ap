// g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include -I ap/vendor/json/include
//     tools/test-hit-roster-fixture.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-hit-roster-fixture && /tmp/test-hit-roster-fixture
//
// Shared pool-draw vectors (block schema 2) driven through the REAL gather
// (ap/ap_hit_encounter.c) and the pure draw (ap/ap_hit_policy.h). The fixture
// tools/fixtures/ctr_hit_roster_vectors.json is spec-derived
// (tools/gen-hit-roster-vectors.py) and the apworld replays a byte-identical
// copy through its own independent reference (test_hit_pool_draw.py).
//
// Coverage:
//   - every draw_case through the pure draw AND through the gather (eligibility
//     rebuilt from the checked set, cursors set per case)
//   - every sequence_case through the ordinary race snapshot: each step is a
//     fresh race after a hub load; a retry between steps must reuse the field
//   - cup sessions: a seven- and four-seat cup snapshot drawn once, reused
//     across legs and retries
//   - every opportunity_case through AP_HitEncounterOpportunity
//
// Build + run (from the repo root):
//   g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include \
//       -I ap/vendor/json/include tools/test-hit-roster-fixture.cpp \
//       ap/ap_seedcfg.cpp -o /tmp/test-hit-roster-fixture && \
//       /tmp/test-hit-roster-fixture

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"

// ── stubbed network / sink ─────────────────────────────────────────────────
static std::set<long long> g_checked;

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

// Held Keys as the gather reads them: the RECEIVED Key item count
// (AP_GateCount(AP_IDX_KEY)), which drives the block schema 3 Key fallback.
static int g_heldKeys;

extern "C" int AP_GateCount(int itemType)
{
	return itemType == 14 /* AP_IDX_KEY */ ? g_heldKeys : 0;
}

extern "C" int AP_EmitHitCharacterCheck(long code)
{
	(void)code;
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
static nlohmann::json g_fixture;
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

// The first authoritative win code of each guest's trigger (index guest - 8).
static const long kTrigger[8] = {35011103, 35011101, 35011100, 35011102,
                                 35016200, 35011008, 35011000, 35011104};

// Install a canonical block with `order` on track 3 and every cup, and set the
// checked set so exactly `guests` are unlocked and exactly `unchecked` Hits are
// still open.
static void install(const nlohmann::json &order, const nlohmann::json &guests,
                    const nlohmann::json &unchecked)
{
	std::memset(&ctr_cfg.hit, 0, sizeof ctr_cfg.hit);
	ctr_cfg.schema_version = 16;
	ctr_cfg.hit.valid = 1;
	ctr_cfg.hit.enabled = 1;
	ctr_cfg.hit.seen = 1;
	ctr_cfg.hit.schema = CTR_CFG_HIT_BLOCK_SCHEMA_KNOWN;
	ctr_cfg.hit.max_guests = CTR_CFG_HIT_MAX_GUESTS;
	for (int i = 0; i < 16; i++)
		ctr_cfg.hit.locations[i] = 35025000L + i;
	for (int g = 0; g < 8; g++)
	{
		ctr_cfg.hit.triggers[g].kind = CTR_CFG_HIT_KIND_BOSS;
		ctr_cfg.hit.triggers[g].count = 1;
		ctr_cfg.hit.triggers[g].any_of[0] = kTrigger[g];
	}
	for (int i = 0; i < 16; i++)
	{
		ctr_cfg.hit.tracks[3].ids[i] = order[i].get<int>();
		for (int c = 0; c < CTR_CFG_HIT_CUP_COUNT; c++)
			ctr_cfg.hit.cups[c].ids[i] = order[i].get<int>();
	}
	g_checked.clear();
	for (const auto &g : guests)
		g_checked.insert(kTrigger[g.get<int>() - 8]);
	std::set<int> open;
	for (const auto &u : unchecked)
		open.insert(u.get<int>());
	for (int i = 0; i < 16; i++)
		if (!open.count(i))
			g_checked.insert(35025000LL + i);
}

static void expect_ids(const int *got, int n, const nlohmann::json &want, const std::string &what)
{
	expect_eq(n, (long long)want.size(), what.c_str());
	for (int i = 0; i < n && i < (int)want.size(); i++)
		expect_eq(got[i], want[i].get<int>(), what.c_str());
}

static void test_draw_cases(void)
{
	for (const auto &c : g_fixture["draw_cases"])
	{
		const std::string name = c["name"].get<std::string>();
		const int player = c["player"].get<int>();
		const int seats = c["ai_seats"].get<int>();
		install(c["order"], c["eligible_guests"], c["unchecked"]);

		// Pure draw with explicit inputs.
		{
			unsigned char elig[16], unc[16];
			AP_HitEncounterGather(elig, unc);
			ap_hit_cursors cur;
			cur.unhit = c["cursors_in"][0].get<int>();
			cur.other = c["cursors_in"][1].get<int>();
			cur.stock = c["cursors_in"][2].get<int>();
			int order[16];
			for (int i = 0; i < 16; i++)
				order[i] = c["order"][i].get<int>();
			ap_hit_field f;
			AP_HitDrawFieldPure(order, elig, unc, player, seats, &cur, &f);
			expect_ids(f.ids, f.count, c["field"], "pure: " + name);
			expect_eq(cur.unhit, c["cursors_out"][0].get<int>(), ("pure cursor: " + name).c_str());
			expect_eq(cur.other, c["cursors_out"][1].get<int>(), ("pure cursor: " + name).c_str());
			expect_eq(cur.stock, c["cursors_out"][2].get<int>(), ("pure cursor: " + name).c_str());
		}

		// Production gather: a fresh draw at track 3 (or cup 104 for four seats)
		// with the case's cursors installed.
		{
			const int dest = seats == 7 ? 3 : 104;
			const int key = ap_hit_dest_key(dest);
			AP_HitEncounterResetDrawState();
			s_cursors[key].unhit = c["cursors_in"][0].get<int>();
			s_cursors[key].other = c["cursors_in"][1].get<int>();
			s_cursors[key].stock = c["cursors_in"][2].get<int>();
			int ids[AP_HIT_FIELD_MAX];
			int n = AP_HitEncounterDrawFresh(dest, player, seats, ids);
			expect_ids(ids, n, c["field"], "gather: " + name);
			int cur[3];
			unsigned draws = 0;
			AP_HitEncounterDrawState(dest, cur, &draws);
			for (int k = 0; k < 3; k++)
				expect_eq(cur[k], c["cursors_out"][k].get<int>(), ("gather cursor: " + name).c_str());
			expect_eq(draws, 1, "one fresh draw counted");

			int extras[3];
			int need = AP_HitEncounterExtras(ids, n, player, extras, 3);
			expect_ids(extras, need, c["extras"], "extras: " + name);
		}
	}
}

// Each step is a fresh race (a hub load in between); a retry after each step
// must reload the same field without advancing the cursors.
static void test_sequence_cases(void)
{
	for (const auto &c : g_fixture["sequence_cases"])
	{
		const std::string name = c["name"].get<std::string>();
		const int player = c["player"].get<int>();
		const int seats = c["ai_seats"].get<int>();
		AP_HitEncounterResetDrawState();
		int step = 0;
		for (const auto &st : c["steps"])
		{
			install(c["order"], st["eligible_guests"], st["unchecked"]);
			int ids[AP_HIT_FIELD_MAX], fresh = -1;
			int n;
			if (seats == 7)
			{
				AP_HitLoadBegin(); // hub load
				AP_HitLoadBegin(); // race load
				n = AP_HitRaceField(3, player, seats, ids, &fresh);
				expect_eq(fresh, 1, ("fresh after a hub load: " + name).c_str());
			}
			else
			{
				AP_HitCupSnapshotBegin(4); // purple cup pad entry
				n = AP_HitCupSnapshotField(4, 0, player, seats, ids);
			}
			expect_ids(ids, n, st["field"], name + " step " + std::to_string(step));
			int cur[3];
			unsigned draws = 0;
			AP_HitEncounterDrawState(seats == 7 ? 3 : 104, cur, &draws);
			for (int k = 0; k < 3; k++)
				expect_eq(cur[k], st["cursors_out"][k].get<int>(), (name + " cursor").c_str());

			// A retry (race load right after the race load) reuses the field even
			// if a Hit was checked meanwhile.
			if (seats == 7)
			{
				g_checked.insert(35025000LL + ids[0]);
				int again[AP_HIT_FIELD_MAX];
				AP_HitLoadBegin();
				int m = AP_HitRaceField(3, player, seats, again, &fresh);
				expect_eq(fresh, 0, ("retry reuses: " + name).c_str());
				expect_eq(m, n, "retry same count");
				for (int i = 0; i < n; i++)
					expect_eq(again[i], ids[i], "retry same field");
				unsigned d2 = 0;
				AP_HitEncounterDrawState(3, cur, &d2);
				expect_eq(d2, draws, "retry does not advance the draw count");
			}
			else
			{
				int leg[AP_HIT_FIELD_MAX];
				int m = AP_HitCupSnapshotField(4, 1, player, seats, leg);
				expect_eq(m, n, "cup next leg same count");
				for (int i = 0; i < n; i++)
					expect_eq(leg[i], ids[i], "cup next leg same field");
			}
			step++;
		}
	}
}

static void test_opportunity_cases(void)
{
	nlohmann::json identity = nlohmann::json::array();
	for (int i = 0; i < 16; i++)
		identity.push_back(i);
	for (const auto &c : g_fixture["opportunity_cases"])
	{
		install(identity, c["eligible_guests"], c["unchecked"]);
		expect_eq(AP_HitEncounterOpportunity(3, c["player"].get<int>()),
		          c["target"].get<int>(), c["name"].get<std::string>().c_str());
		expect_eq(AP_HitEncounterOpportunity(40, c["player"].get<int>()), -1,
		          "unsupported destination -> none");
	}
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1]
	                            : "tools/fixtures/ctr_hit_roster_vectors.json";
	std::ifstream in(path);
	if (!in)
	{
		std::fprintf(stderr, "cannot open fixture: %s\n", path);
		return 2;
	}
	try
	{
		in >> g_fixture;
	}
	catch (const std::exception &e)
	{
		std::fprintf(stderr, "fixture is not valid JSON: %s\n", e.what());
		return 2;
	}

	if (g_fixture["schema"].get<int>() != 2 ||
	    g_fixture["algorithm"].get<std::string>() != "unhit_first_rotation" ||
	    g_fixture["cursor_init"].get<int>() != AP_HIT_CURSOR_INIT ||
	    g_fixture["max_guests"].get<int>() != CTR_CFG_HIT_MAX_GUESTS)
	{
		std::fprintf(stderr, "fixture header does not match this build\n");
		return 2;
	}
	test_draw_cases();
	test_sequence_cases();
	test_opportunity_cases();

	std::printf("%s: %d checks, %d failures\n",
	            g_failures ? "FAIL" : "PASS", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
