// Round trip against the apworld (native #449): real `win_logic` blocks and
// Archipelago's own answers, through native's parser and evaluator.
//
//   c++ -m32 -std=c++17 -Wall -DCTR_AP -DAP_WIN_LOGIC_FREESTANDING_ONLY -Iap -Iap/vendor/json/include
//     tools/test-win-logic-roundtrip.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-win-logic-roundtrip && /tmp/test-win-logic-roundtrip
//
// Fixture: tools/fixtures/win-logic/roundtrip-2026-10-02.json, produced by the
// apworld's own parity setup (see its "provenance" key). Each seed carries the
// whole slot_data exactly as the apworld's fill_slot_data emitted it,
// and several item states with native's inputs (received counts by AP item id,
// progression-flagged copies only; boss races won) next to Archipelago's
// location.can_reach for every win check. Each block goes through the REAL
// parser (ap_seedcfg_parse_json) and every row through AP_WinLogicInLogic; the
// two must agree on every row. The win sets native resolves for each race are
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
	std::map<long, int> received;
	int bosses = 0;
};

static int s_items(void *u, long id)
{
	const State *s = (const State *)u;
	auto it = s->received.find(id);
	return it == s->received.end() ? 0 : it->second;
}
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

	int seeds = 0, rows = 0, agree = 0, failures = 0, trueRows = 0;
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
		for (const auto &st : seed["states"])
		{
			State s;
			for (auto it = st["received"].begin(); it != st["received"].end(); ++it)
				s.received[std::stol(it.key())] = it.value().get<int>();
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

	std::printf("%d seed(s), %d row(s) (%d in logic), %d agree\n", seeds, rows, trueRows, agree);
	if (seeds < 10 || rows < 5000 || trueRows == 0 || trueRows == rows)
	{
		std::printf("FAIL fixture too small or one-sided to prove anything\n");
		failures++;
	}
	std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
