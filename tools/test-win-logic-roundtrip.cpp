// Round trip against the apworld (native #449): real `win_logic` blocks and
// Archipelago's own answers, through native's parser and evaluator.
//
//   c++ -m32 -std=c++17 -Wall -DCTR_AP -DAP_WIN_LOGIC_FREESTANDING_ONLY -Iap -Iap/vendor/json/include
//     tools/test-win-logic-roundtrip.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-win-logic-roundtrip && /tmp/test-win-logic-roundtrip
//
// Fixture: tools/fixtures/win-logic/roundtrip-2026-10-02.json, produced by the
// apworld's own parity setup (see its "provenance" key). Each seed carries the
// whole slot_data exactly as the apworld's fill_slot_data emitted it, the start
// inventory exactly as the server sends it (`start_entries`: item ids at
// location -2 with flags 0), and several item states with native's inputs
// (the tally: progression-flagged copies off location -2; boss races won) next
// to Archipelago's location.can_reach for every win check. Each block goes
// through the REAL parser (ap_seedcfg_parse_json); every state is fed through
// the REAL tally (AP_ItemIdTallyReceive), start entries included, and every row
// through AP_WinLogicInLogic, which adds the block's `start` table. The two
// must agree on every row. Seeds with a start inventory (YAML, the
// start_inventory_from_pool mechanism, the tight-fill backstop) are included. The win sets native resolves for each race are
// also checked to have an entry in every block where the location exists.

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"
#include "../ap/ap_win_logic.h"

extern void ap_seedcfg_parse_json(const nlohmann::json &j);
extern "C" void AP_LogLine(const char *) {}

static const char *kFixture = "tools/fixtures/win-logic/roundtrip-2026-10-02.json";

struct State
{
	AP_ItemIdTally tally;
	int bosses = 0;
};

static int s_items(void *u, long id) { return AP_ItemIdTallyCount(&((State *)u)->tally, id); }
static int s_bosses(void *u) { return ((const State *)u)->bosses; }
static int s_collected(void *, long) { return 0; }

int main(void)
{
	std::ifstream in(kFixture);
	if (!in)
	{
		std::printf("FAIL cannot open %s (run from the repo root)\n", kFixture);
		return 1;
	}
	std::stringstream buf;
	buf << in.rdbuf();
	nlohmann::json fx = nlohmann::json::parse(buf.str());

	int seeds = 0, rows = 0, agree = 0, failures = 0, trueRows = 0, startSeeds = 0;
	static State s; // the tally is large: keep it off the stack
	for (const auto &seed : fx["seeds"])
	{
		const std::string label = seed["label"].get<std::string>();
		// The whole slot_data, as the server hands it to native.
		const nlohmann::json &sd = seed["slot_data"];
		ap_seedcfg_parse_json(sd);
		seeds++;
		if (ctr_win_logic.state != AP_WL_VALID)
		{
			std::printf("FAIL %s: block not VALID (state %d, %s)\n", label.c_str(),
			            ctr_win_logic.state, ctr_win_logic.problem);
			failures++;
			continue;
		}
		if (ctr_win_logic.check_count != (int)sd["win_logic"]["checks"].size())
		{
			std::printf("FAIL %s: %d checks parsed, block has %zu\n", label.c_str(),
			            ctr_win_logic.check_count, sd["win_logic"]["checks"].size());
			failures++;
		}
		const nlohmann::json &opt = sd["ctr_options"];
		if (ctr_win_logic.start_count > 0)
			startSeeds++;
		if (ctr_win_logic.start_count != (int)sd["win_logic"]["start"].size())
		{
			std::printf("FAIL %s: %d start ids parsed, block has %zu\n", label.c_str(),
			            ctr_win_logic.start_count, sd["win_logic"]["start"].size());
			failures++;
		}
		for (const auto &st : seed["states"])
		{
			AP_ItemIdTallyReset(&s.tally);
			// The server's start inventory: location -2, flags 0. Fed twice more
			// with the progression flag to prove location -2 never counts.
			for (const auto &id : seed["start_entries"])
			{
				AP_ItemIdTallyReceive(&s.tally, id.get<long long>(), AP_WL_LOCATION_START, 0u);
				AP_ItemIdTallyReceive(&s.tally, id.get<long long>(), AP_WL_LOCATION_START, 1u);
			}
			for (auto it = st["received"].begin(); it != st["received"].end(); ++it)
				for (int k = 0; k < it.value().get<int>(); k++)
					AP_ItemIdTallyReceive(&s.tally, std::stoll(it.key()), 1, 1u);
			s.bosses = st["bosses"].get<int>();
			AP_WinLogicEnv env;
			env.user = &s;
			env.item_count = s_items;
			env.bosses_won = s_bosses;
			env.collected = s_collected;
			env.boost_mode = opt.value("boost_mode", 0);
			env.starting_character = opt.value("starting_character", 0);
			env.character_unlocks = opt.contains("character_unlocks") &&
			                        (opt["character_unlocks"].is_boolean()
			                             ? opt["character_unlocks"].get<bool>()
			                             : opt["character_unlocks"].get<int>() != 0);
			env.unreliable = 0;
			// Production reads these through ctr_cfg; they must agree.
			if (env.boost_mode != ctr_cfg.boost_mode ||
			    env.starting_character != ctr_cfg.starting_character ||
			    env.character_unlocks != (ctr_cfg.character_unlocks ? 1 : 0))
			{
				std::printf("FAIL %s: ctr_cfg cap inputs differ from ctr_options\n", label.c_str());
				failures++;
			}
			for (auto it = st["can_reach"].begin(); it != st["can_reach"].end(); ++it)
			{
				long code = std::stol(it.key());
				int want = it.value().get<bool>() ? 1 : 0;
				int idx = AP_WinLogicFindCheck(&ctr_win_logic, code);
				int got = idx >= 0 ? AP_WinLogicInLogic(&ctr_win_logic, &env, idx, 0) : -1;
				rows++;
				trueRows += want;
				if (got == want)
					agree++;
				else if (failures++ < 20)
					std::printf("FAIL %s: location %ld native %d, Archipelago %d\n",
					            label.c_str(), code, got, want);
			}
		}
	}

	std::printf("%d seed(s) (%d with a start table), %d row(s) (%d in logic), %d agree\n", seeds,
	            startSeeds, rows, trueRows, agree);
	if (seeds < 10 || rows < 5000 || trueRows == 0 || trueRows == rows || startSeeds < 8)
	{
		std::printf("FAIL fixture too small or one-sided to prove anything\n");
		failures++;
	}
	std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
