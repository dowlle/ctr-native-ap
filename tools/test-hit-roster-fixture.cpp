// g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include -I ap/vendor/json/include
//     tools/test-hit-roster-fixture.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-hit-roster-fixture && /tmp/test-hit-roster-fixture
//
// Shared deterministic roster vectors (ticket 10) driven through the REAL gather
// (ap/ap_hit_encounter.c). The fixture tools/fixtures/ctr_hit_roster_vectors.json
// is contract-derived (tools/gen-hit-roster-vectors.py) and is the same file the
// manager runs against the apworld.
//
// Coverage:
//   - selection exactly per the frozen contract (pin priority, reserve fill,
//     player exclusion, seed 0 / 4294967295, all sixteen player choices)
//   - all six required pinned opportunities, including trials 16/17
//   - the encounter-availability invariant: over all eighteen destinations, the
//     union of selected rosters contains every eligible non-player target.
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

static bool field_has(const int *ids, int n, int id)
{
	for (int i = 0; i < n; i++)
		if (ids[i] == id)
			return true;
	return false;
}

// Install one case's candidate lists and canonical identities into ctr_cfg.hit.
static void install(const nlohmann::json &base, const nlohmann::json &pinned,
                    const nlohmann::json &reserve, int level,
                    const nlohmann::json &locations, const nlohmann::json &triggers)
{
	std::memset(&ctr_cfg.hit, 0, sizeof ctr_cfg.hit);
	ctr_cfg.schema_version = 16;
	ctr_cfg.hit.valid = 1;
	ctr_cfg.hit.enabled = 1;
	ctr_cfg.hit.seen = 1;

	for (auto it = locations.begin(); it != locations.end(); ++it)
		ctr_cfg.hit.locations[std::stoi(it.key())] = it.value().get<long>();

	for (auto it = triggers.begin(); it != triggers.end(); ++it)
	{
		int guest = std::stoi(it.key());
		const auto &t = it.value();
		ctr_hit_trigger *out = &ctr_cfg.hit.triggers[guest - 8];
		out->kind = t["kind"].get<std::string>() == "boss"
		                ? CTR_CFG_HIT_KIND_BOSS
		                : CTR_CFG_HIT_KIND_TRACK;
		out->count = 0;
		for (const auto &code : t["any_of"])
			out->any_of[out->count++] = code.get<long>();
	}

	ctr_hit_candidates *c = (level >= 100)
	                        ? &ctr_cfg.hit.cups[level - 100]
	                        : &ctr_cfg.hit.tracks[level];
	c->base.count = (int)base.size();
	for (int i = 0; i < c->base.count; i++)
		c->base.ids[i] = base[i].get<int>();
	c->pinned.count = (int)pinned.size();
	for (int i = 0; i < c->pinned.count; i++)
		c->pinned.ids[i] = pinned[i].get<int>();
	c->reserve.count = (int)reserve.size();
	for (int i = 0; i < c->reserve.count; i++)
		c->reserve.ids[i] = reserve[i].get<int>();
}

static void set_checked(const nlohmann::json &checked)
{
	g_checked.clear();
	for (const auto &code : checked)
		g_checked.insert(code.get<long long>());
}

static void test_cases(void)
{
	const auto &locations = g_fixture["locations"];
	const auto &triggers = g_fixture["triggers"];

	for (const auto &c : g_fixture["cases"])
	{
		int ids[AP_HIT_FIELD_MAX];
		const int level = c["level"].get<int>();
		const int player = c["player"].get<int>();
		set_checked(c["checked"]);
		install(c["base"], c["pinned"], c["reserve"], level, locations, triggers);

		int n;
		int fieldSize = c.contains("field_size") ? c["field_size"].get<int>() : 7;
		const auto &want = c["expected"];

		if (level >= 100)
		{
			// Gem Cup: resolve a fresh snapshot for this case.
			AP_HitCupSnapshotReset();
			AP_HitCupSnapshotBegin(level - 100);
			n = AP_HitCupSnapshotField(level - 100, 0, player, fieldSize, ids);
		}
		else
		{
			n = AP_HitEncounterBuildField(level, player, 7, ids);
		}

		expect_eq(n, (long long)want.size(), c["name"].get<std::string>().c_str());
		for (int i = 0; i < n && i < (int)want.size(); i++)
			expect_eq(ids[i], want[i].get<int>(), c["name"].get<std::string>().c_str());

		// Never seat the player, never duplicate.
		for (int i = 0; i < n; i++)
		{
			expect(ids[i] != player, "case never seats the player");
			for (int j = i + 1; j < n; j++)
				expect(ids[i] != ids[j], "case never duplicates an opponent");
		}

		// Correction A: the opportunity is the FIRST seated non-player target
		// whose Hit location is present and unchecked. Ordinary destinations
		// only: cups are not pad Hit opportunities (ticket 11).
		if (level < 100)
		{
			int opp = AP_HitEncounterOpportunity(level, player);
			if (n > 0)
				expect_eq(opp, want[0].get<int>(), "opportunity is the first seated target");
			else
				expect_eq(opp, -1, "empty field -> no opportunity");
		}
	}
}

// Ticket 11: the cup snapshot lifecycle. Each session drives the REAL gather
// snapshot through begin/load/unlock steps and checks the frozen roster.
static void test_cup_sessions(void)
{
	const auto &locations = g_fixture["locations"];
	const auto &triggers = g_fixture["triggers"];

	for (const auto &s : g_fixture["cup_sessions"])
	{
		const int cup = s["cup"].get<int>();       // wire key 100..104
		const int cupIdx = cup - 100;              // engine cup id 0..4
		const int player = s["player"].get<int>();
		const int fieldSize = (cup == 104) ? 4 : 7;
		const long long seed = s["seed"].get<long long>();

		// The candidate lists for this cup/seed come from the matching no_guest
		// case the generator emitted (same contract algorithm).
		const nlohmann::json *src = nullptr;
		for (const auto &c : g_fixture["cases"])
			if (c["level"].get<int>() == cup &&
			    c["seed"].get<long long>() == seed &&
			    c["name"].get<std::string>().find("no_guest") != std::string::npos)
			{
				src = &c;
				break;
			}
		if (src == nullptr)
		{
			expect(false, "cup session has candidate lists");
			continue;
		}

		AP_HitCupSnapshotReset();
		for (const auto &step : s["steps"])
		{
			const std::string event = step["event"].get<std::string>();
			if (event == "begin")
			{
				set_checked(step["checked"]);
				install((*src)["base"], (*src)["pinned"], (*src)["reserve"],
				        cup, locations, triggers);
				AP_HitCupSnapshotBegin(cupIdx);
			}
			else if (event == "unlock")
			{
				for (const auto &code : step["checked"])
					g_checked.insert(code.get<long long>());
			}
			else if (event == "load")
			{
				int ids[AP_HIT_FIELD_MAX];
				const auto &want = step["expected"];
				int n = AP_HitCupSnapshotField(cupIdx, step["trackIndex"].get<int>(),
				                               player, fieldSize, ids);
				expect_eq(n, (long long)want.size(), "cup session roster size");
				for (int i = 0; i < n && i < (int)want.size(); i++)
					expect_eq(ids[i], want[i].get<int>(), "cup session roster");
			}
		}
	}
}

// The encounter-availability invariant: over all eighteen destinations, the
// union of selected rosters contains every eligible non-player id, so an unlock
// never permanently removes the last route to an unchecked target.
static void test_invariants(void)
{
	const auto &locations = g_fixture["locations"];
	const auto &triggers = g_fixture["triggers"];

	for (const auto &inv : g_fixture["invariants"])
	{
		std::set<int> seen;
		const int player = inv["player"].get<int>();
		set_checked(inv["checked"]);

		for (int level = 0; level <= 17; level++)
		{
			int ids[AP_HIT_FIELD_MAX];
			const auto &lv = inv["levels"][std::to_string(level)];
			install(lv["base"], lv["pinned"], lv["reserve"], level, locations, triggers);
			int n = AP_HitEncounterBuildField(level, player, 7, ids);
			for (int i = 0; i < n; i++)
				seen.insert(ids[i]);
		}

		for (const auto &id : inv["eligible"])
			expect(seen.count(id.get<int>()) != 0,
			       "invariant: every eligible target appears on some level");
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

	test_cases();
	test_invariants();
	test_cup_sessions();

	std::printf("%s: %d checks, %d failures\n",
	            g_failures ? "FAIL" : "PASS", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
