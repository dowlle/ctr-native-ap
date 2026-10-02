// Race-loss DeathLink stakes (native #449): evaluator, win sets, the stakes
// decision and its race-start lifecycle. Blocks are parsed by the REAL parser
// (ap/ap_seedcfg.cpp is linked in); evaluation, win-set resolution, the stakes
// decision and the latch are the exact ap_win_logic.h functions production
// calls, and the loss send decision is ap_race_attempt_logic.h's.
//
//   c++ -m32 -std=c++17 -Wall -DCTR_AP -DAP_WIN_LOGIC_FREESTANDING_ONLY -Iap -Iap/vendor/json/include
//     tools/test-win-logic-stakes.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-win-logic-stakes && /tmp/test-win-logic-stakes
//
// Production wiring that this harness stands in for (ap/ap_win_logic.c):
// item_count -> the received-items-by-id tally (progression-flagged copies
// only, AP_ItemIdTallyReceive; req counts from it too), bosses_won ->
// AP_ComposedBossesWon, collected -> ap_net_location_checked.

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"
#include "../ap/ap_win_logic.h"
#include "../ap/ap_race_attempt_logic.h"
#include "../ap/ap_locations.h"

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

static void expect_int(long got, long want, const std::string &name)
{
	checks++;
	if (got != want)
	{
		failures++;
		std::printf("FAIL %s (got %ld, want %ld)\n", name.c_str(), got, want);
	}
}

// ── Fixture world ──
struct World
{
	std::map<long, int> items;   // received count by AP item id
	std::set<long> collected;    // checked locations
	int bosses = 0;
};

static int w_items(void *u, long id)
{
	World *w = (World *)u;
	auto it = w->items.find(id);
	return it == w->items.end() ? 0 : it->second;
}
static int w_bosses(void *u) { return ((World *)u)->bosses; }
static int w_collected(void *u, long code) { return ((World *)u)->collected.count(code) ? 1 : 0; }

static AP_WinLogicEnv env_for(World &w, int boostMode = 0, int start = 0, int unlocks = 1)
{
	AP_WinLogicEnv e;
	e.user = &w;
	e.item_count = w_items;
	e.bosses_won = w_bosses;
	e.collected = w_collected;
	e.boost_mode = boostMode;
	e.starting_character = start;
	e.character_unlocks = unlocks;
	e.unreliable = 0;
	return e;
}

static void parse_block(const nlohmann::json &b)
{
	nlohmann::json j = {{"ctr_options", {{"schema_version", 16}}}, {"win_logic", b}};
	ap_seedcfg_parse_json(j);
}

// A block with one check 35011000 (Crash Cove Trophy) whose rule is `rule` and
// region is `region`, plus any extra checks.
static nlohmann::json one_rule(const nlohmann::json &rule, const nlohmann::json &region = true)
{
	nlohmann::json b = {{"version", 1},
	                    {"families", {{35010101}, {35010097, 35010098}, {35010095, 35010096}}},
	                    {"regions", nlohmann::json::array({region})},
	                    {"checks", {{"35011000", {{"kind", "trophy"}, {"region", 0}, {"rule", rule}}}}}};
	return b;
}

static int eval_rule(const nlohmann::json &rule, AP_WinLogicEnv &env,
                     const nlohmann::json &region = true)
{
	parse_block(one_rule(rule, region));
	if (ctr_win_logic.state != AP_WL_VALID)
	{
		std::printf("FAIL fixture block refused: %s\n", ctr_win_logic.problem);
		failures++;
		return -1;
	}
	return AP_WinLogicInLogic(&ctr_win_logic, &env, AP_WinLogicFindCheck(&ctr_win_logic, 35011000L), 0);
}

using J = nlohmann::json;
#define arr(...) J::array(__VA_ARGS__)

static long table_code(int bit)
{
	for (int i = 0; i < AP_LOCATION_TABLE_LEN; i++)
		if (AP_LOCATION_TABLE[i].bit_index == bit)
			return AP_LOCATION_TABLE[i].location_code;
	return -1;
}

static AP_WinRaceFacts facts(void)
{
	AP_WinRaceFacts f;
	std::memset(&f, 0, sizeof f);
	f.adventure = 1;
	f.trialTrophy = f.trialCtr = -1;
	for (int i = 0; i < 5; i++)
		f.cortexCodes[i] = -1;
	f.customTrophy = f.customCtr = -1;
	return f;
}

static std::string winset(const AP_WinRaceFacts &f, int *modeOut = nullptr)
{
	long set[AP_WS_WINSET_MAX];
	int mode;
	int n = AP_WinSetResolve(&f, table_code, set, &mode);
	if (modeOut)
		*modeOut = mode;
	std::string s;
	for (int i = 0; i < n; i++)
		s += (i ? "," : "") + std::to_string(set[i]);
	return s;
}

// ── Production send sequence (AP_DeathLinkSendLoss + AP_WinStakesAllowLossSend) ──
struct Session
{
	AP_WinStakesLatch latch;
	World *w;
	AP_WinRaceFacts race;
	int sendMask = AP_DL_TRIG_FALL | AP_DL_TRIG_LOSS;
	int forcedLoss = 0;
	int sent = 0;
	int exempt = 0;

	AP_WinStakes decide()
	{
		long set[AP_WS_WINSET_MAX];
		int mode;
		int n = AP_WinSetResolve(&race, table_code, set, &mode);
		AP_WinLogicEnv e = env_for(*w, 1, 0, 1);
		return AP_WinStakesDecide(&ctr_win_logic, &e, set, n);
	}
	void level_start(int isRace)
	{
		int need = AP_WinStakesLevelStart(&latch, isRace, isRace && race.cup);
		if (need != AP_WS_NEED_NONE)
			AP_WinStakesRecord(&latch, need, decide());
	}
	void loss_event(int event, int lost, int inLiveRace)
	{
		int cause = AP_DeathLinkLossSendDecision(sendMask, 1, forcedLoss, event, lost, inLiveRace);
		if (cause == AP_DL_CAUSE_NONE)
			return;
		int inCup = event == AP_DL_EV_CUP_END || race.cup;
		AP_WinStakes s;
		if (!AP_WinStakesCurrent(&latch, inCup, &s))
		{
			s = decide();
			AP_WinStakesRecord(&latch, inCup ? AP_WS_NEED_CUP : AP_WS_NEED_RACE, s);
		}
		if (s.stakes)
			sent++;
		else
			exempt++;
	}
};

int main(void)
{
	// ════ Leaves ════
	{
		World w;
		AP_WinLogicEnv e = env_for(w);

		// req: fields pass through unchanged, gate decides.
		w.items[35010014] = 1;
		expect_int(eval_rule(arr({"req", 2, 2, -1}), e), 0, "req keys 2 with 1 key -> false");
		w.items[35010014] = 2;
		expect_int(eval_rule(arr({"req", 2, 2, -1}), e), 1, "req keys 2 with 2 keys -> true");
		w.items[35010003] = 1;
		expect_int(eval_rule(arr({"req", 4, 1, 2}), e), 1, "req type 4 tier 2 (Platinum) uses its own count");
		expect_int(eval_rule(arr({"req", 4, 1, 0}), e), 0, "req type 4 tier 0 (Sapphire) separate");
		w.items[35010008] = 3; // purple tokens
		expect_int(eval_rule(arr({"req", 6, 3, -1}), e), 1, "req type 6 sums Purple too");
		w.items[35010014] = 0;
		expect_int(eval_rule(arr({"req", 2, 0, -1}), e), 1, "req count 0 always satisfied");
		w.items[35010000] = 4;
		expect_int(eval_rule(arr({"req", 1, 4, -1}), e), 1, "req type 1 Trophies");
		expect_int(eval_rule(arr({"req", 1, 5, -1}), e), 0, "req type 1 short");
		expect_int(eval_rule(arr({"req", 3, 3, 4}), e), 1, "req type 3 Purple token colour");
		expect_int(eval_rule(arr({"req", 3, 1, 0}), e), 0, "req type 3 Red token colour");
		expect_int(eval_rule(arr({"req", 4, 1, -1}), e), 0, "req type 4 legacy -1 is Sapphire");
		w.items[35010001] = 2;
		expect_int(eval_rule(arr({"req", 4, 2, -1}), e), 1, "req type 4 legacy -1 Sapphire count");
		expect_int(eval_rule(arr({"req", 7, 3, -1}), e), 1, "req type 7 sums all tiers");
		expect_int(eval_rule(arr({"req", 7, 4, -1}), e), 0, "req type 7 short");
		w.items[35010013] = 1; // Purple gem
		expect_int(eval_rule(arr({"req", 5, 1, 4}), e), 1, "req type 5 Purple gem");
		expect_int(eval_rule(arr({"req", 5, 1, 0}), e), 0, "req type 5 Red gem missing");
		expect_int(eval_rule(arr({"req", 8, 1, -1}), e), 1, "req type 8 sums Purple too");
		w.items[35010009] = 1;
		expect_int(eval_rule(arr({"req", 5, 2, -1}), e), 1, "req type 5 legacy -1 sums");

		// tokens_no_purple: Purple never counts.
		w.items[35010008] = 9;
		expect_int(eval_rule(arr({"tokens_no_purple", 1}), e), 0, "tokens_no_purple ignores Purple");
		w.items[35010004] = 1;
		w.items[35010005] = 1;
		w.items[35010006] = 1;
		expect_int(eval_rule(arr({"tokens_no_purple", 4}), e), 0, "tokens_no_purple 3 of 4");
		w.items[35010007] = 1;
		expect_int(eval_rule(arr({"tokens_no_purple", 4}), e), 1, "tokens_no_purple R+G+B+Y = 4");
		expect_int(eval_rule(arr({"tokens_no_purple", 0}), e), 1, "count 0 always satisfied");

		// items sum / distinct.
		w.items[35010139] = 2;
		expect_int(eval_rule(arr({"items", "sum", 3, {35010139, 35010140}}), e), 0, "items sum 2 < 3");
		w.items[35010140] = 1;
		expect_int(eval_rule(arr({"items", "sum", 3, {35010139, 35010140}}), e), 1, "items sum 3 >= 3");
		expect_int(eval_rule(arr({"items", "distinct", 3, {35010139, 35010140, 35010141}}), e), 0,
		           "items distinct 2 of 3");
		expect_int(eval_rule(arr({"items", "distinct", 2, {35010139, 35010140, 35010141}}), e), 1,
		           "items distinct duplicates count once");
		expect_int(eval_rule(arr({"items", "sum", 0, {35099999}}), e), 1, "items count 0 always true");

		// bosses.
		w.bosses = 2;
		expect_int(eval_rule(arr({"bosses", 3}), e), 0, "bosses 2 < 3");
		expect_int(eval_rule(arr({"bosses", 2}), e), 1, "bosses 2 >= 2");

		// families: one id of a family is enough; the table is the block's.
		expect_int(eval_rule(arr({"families", 1}), e), 0, "no family held");
		w.items[35010098] = 1;
		expect_int(eval_rule(arr({"families", 1}), e), 1, "second id holds family 1");
		w.items[35010097] = 1;
		expect_int(eval_rule(arr({"families", 2}), e), 0, "two ids of one family are one family");
		w.items[35010101] = 1;
		expect_int(eval_rule(arr({"families", 2}), e), 1, "two families held");
		expect_int(eval_rule(arr({"families", 4}), e), 0, "only 3 families in the table");
	}

	// ════ cap ════
	{
		World w;
		// boost_mode 0: boost never matters, racer only needs to be driveable.
		AP_WinLogicEnv e = env_for(w, 0, 3 /* Coco */, 1);
		expect_int(eval_rule(arr({"cap", 3, -1}), e), 1, "mode off: cap -1 always true");
		expect_int(eval_rule(arr({"cap", 2, 3}), e), 1, "mode off: starting racer driveable");
		expect_int(eval_rule(arr({"cap", 0, 0}), e), 0, "Crash locked (unlock item missing)");
		w.items[35010123] = 1; // Crash unlock
		expect_int(eval_rule(arr({"cap", 0, 0}), e), 1, "Crash unlocked by item");
		AP_WinLogicEnv all = env_for(w, 0, 3, 0);
		expect_int(eval_rule(arr({"cap", 0, 15}), all), 1, "character_unlocks false: every racer driveable");

		// boost_mode 1 (shared): reduces to the shared count.
		World s;
		AP_WinLogicEnv sh = env_for(s, 1, 0, 1);
		expect_int(eval_rule(arr({"cap", 1, -1}), sh), 0, "shared: no boost");
		expect_int(eval_rule(arr({"cap", 0, -1}), sh), 1, "shared: boost 0 true");
		s.items[35010027] = 1;
		expect_int(eval_rule(arr({"cap", 1, -1}), sh), 1, "shared: 1 boost");
		expect_int(eval_rule(arr({"cap", 2, -1}), sh), 0, "shared: 1 < 2");
		expect_int(eval_rule(arr({"cap", 1, 0}), sh), 1, "shared: starting racer + shared boost");
		expect_int(eval_rule(arr({"cap", 1, 6}), sh), 0, "shared: Polar not driveable");
		s.items[35010125] = 1; // Polar unlock
		expect_int(eval_rule(arr({"cap", 1, 6}), sh), 1, "shared: Polar unlocked");
		s.items[35010039] = 5; // Polar's own boost: ignored in shared mode
		expect_int(eval_rule(arr({"cap", 2, 6}), sh), 0, "shared: per-character items ignored");

		// boost_mode 2 (per character).
		World p;
		AP_WinLogicEnv pc = env_for(p, 2, 0, 1);
		p.items[35010027] = 3; // shared item ignored in per-character mode
		expect_int(eval_rule(arr({"cap", 1, -1}), pc), 0, "per-char: shared item ignored");
		p.items[35010047] = 2; // Cortex's boost, but Cortex locked
		expect_int(eval_rule(arr({"cap", 1, -1}), pc), 0, "per-char: boost on a locked racer does not count");
		p.items[35010127] = 1; // Cortex unlock
		expect_int(eval_rule(arr({"cap", 2, -1}), pc), 1, "per-char: some racer has 2 boosts");
		expect_int(eval_rule(arr({"cap", 3, -1}), pc), 0, "per-char: nobody has 3");
		expect_int(eval_rule(arr({"cap", 2, 1}), pc), 1, "per-char: Cortex bound");
		expect_int(eval_rule(arr({"cap", 1, 0}), pc), 0, "per-char: Crash has no boost");
		p.items[35010031] = 1;
		expect_int(eval_rule(arr({"cap", 1, 0}), pc), 1, "per-char: Crash's own boost");
		// unlocks off + per-char: any racer with enough boosts.
		World q;
		q.items[35010087] = 3; // Oxide's boosts
		AP_WinLogicEnv qa = env_for(q, 2, 0, 0);
		expect_int(eval_rule(arr({"cap", 3, -1}), qa), 1, "per-char + all unlocked: Oxide qualifies");
		expect_int(eval_rule(arr({"cap", 3, 15}), qa), 1, "per-char + all unlocked: Oxide bound");
		// table spot checks against SCHEMA.md
		expect_int(AP_WL_UNLOCK_ITEM[13], 35010138L, "Penta unlock id");
		expect_int(AP_WL_BOOST_ITEM[8], 35010067L, "Pinstripe boost id");
	}

	// ════ Combinators ════
	{
		World w;
		AP_WinLogicEnv e = env_for(w);
		expect_int(eval_rule(arr({"all"}), e), 1, "all of zero children = true");
		expect_int(eval_rule(arr({"any"}), e), 0, "any of zero children = false");
		expect_int(eval_rule(arr({"all", true, false}), e), 0, "all with a false child");
		expect_int(eval_rule(arr({"any", false, true}), e), 1, "any with a true child");
		expect_int(eval_rule(arr({"any", arr({"all", true, arr({"bosses", 1})}), arr({"all", false})}), e), 0,
		           "nested any/all false");
		w.bosses = 1;
		expect_int(eval_rule(arr({"any", arr({"all", true, arr({"bosses", 1})}), arr({"all", false})}), e), 1,
		           "nested any/all true");
		expect_int(eval_rule(false, e), 0, "literal false rule");
		// region is part of in_logic
		expect_int(eval_rule(true, e, false), 0, "region false -> not in logic");
		expect_int(eval_rule(true, e, arr({"bosses", 1})), 1, "region true + rule true");
	}

	// ════ ref (region of the target included) ════
	{
		World w;
		AP_WinLogicEnv e = env_for(w);
		J b = {{"version", 1}, {"families", J::array()},
		       {"regions", arr({true, arr({"bosses", 1})})},
		       {"checks", {{"35011000", {{"kind", "trophy"}, {"region", 1}, {"rule", arr({"bosses", 0})}}},
		                   {"35012000", {{"kind", "sapphire"}, {"region", 0}, {"rule", arr({"ref", 35011000})}}},
		                   {"35012100", {{"kind", "gold"}, {"region", 0},
		                                 {"rule", arr({"all", arr({"ref", 35011000}), arr({"bosses", 2})})}}}}}};
		parse_block(b);
		expect_int(ctr_win_logic.state, AP_WL_VALID, "ref block valid");
		int s = AP_WinLogicFindCheck(&ctr_win_logic, 35012000L);
		int g = AP_WinLogicFindCheck(&ctr_win_logic, 35012100L);
		expect_int(AP_WinLogicInLogic(&ctr_win_logic, &e, s, 0), 0, "ref: target's REGION false -> false");
		w.bosses = 1;
		expect_int(AP_WinLogicInLogic(&ctr_win_logic, &e, s, 0), 1, "ref: target region and rule true");
		expect_int(AP_WinLogicInLogic(&ctr_win_logic, &e, g, 0), 0, "ref inside all, other child false");
		w.bosses = 2;
		expect_int(AP_WinLogicInLogic(&ctr_win_logic, &e, g, 0), 1, "ref inside all, both true");
	}

	// ════ Win sets (the codes native sends for each race's win) ════
	{
		AP_WinRaceFacts f = facts();
		int mode;
		f.levelID = 3; // Crash Cove
		expect(winset(f, &mode) == "35011000", "retail trophy Crash Cove");
		expect_int(mode, AP_WS_MODE_TROPHY, "mode trophy");
		f.levelID = 1; // Dragon Mines (bit 7)
		expect(winset(f) == "35011009", "retail trophy Dragon Mines");
		f.levelID = 9; // Mystery Caves (bit 15): LevelID order is not location order
		expect(winset(f) == "35011002", "retail trophy Mystery Caves");
		f.levelID = 1;
		f.token = 1;
		expect(winset(f, &mode) == "35012309", "CTR Token Challenge Dragon Mines");
		expect_int(mode, AP_WS_MODE_CTR, "mode ctr");
		f.token = 0;
		f.relic = 1;
		expect(winset(f, &mode) == "35012009,35012109,35012209", "Relic Race Dragon Mines: three tiers");
		expect_int(mode, AP_WS_MODE_RELIC, "mode relic");
		f.levelID = 16;
		expect(winset(f) == "35012016,35012116,35012216", "Relic Race Slide Coliseum");
		f.relic = 0;
		f.trial = 1;
		f.trialTrophy = 35016200;
		f.trialCtr = 35016210;
		expect(winset(f) == "35016200", "Slide Coliseum Trophy (trial direct code)");
		f.token = 1;
		expect(winset(f) == "35016210", "Slide Coliseum CTR (trial direct code)");
		f = facts();
		f.levelID = 16;
		expect(winset(f) == "", "unconfigured trial track: no win identified");

		f = facts();
		f.levelID = 13;
		f.cortex = 1;
		f.cortexCodes[0] = 35026000;
		f.cortexCodes[1] = 35026001;
		f.cortexCodes[2] = 35026002;
		f.cortexCodes[3] = 35026003;
		f.cortexCodes[4] = 35026004;
		expect(winset(f) == "35026000", "Cortex Vortex Trophy, not Oxide Station's");
		f.token = 1;
		expect(winset(f) == "35026004", "Cortex Vortex CTR");
		f.token = 0;
		f.relic = 1;
		expect(winset(f) == "35026001,35026002,35026003", "Cortex Vortex relics");
		f.cortexCodes[2] = -1;
		expect(winset(f) == "35026001,35026003", "absent Cortex tier skipped");

		f = facts();
		f.boss = 1;
		for (int b = 0; b < 4; b++)
		{
			f.bossID = b;
			expect(winset(f, &mode) == std::to_string(35011100 + b), "key boss " + std::to_string(b));
		}
		expect_int(mode, AP_WS_MODE_BOSS, "mode boss");
		f.bossID = 4;
		expect(winset(f) == "35011104", "Oxide first challenge");
		f.bossID = 5;
		expect(winset(f) == "35011105", "Oxide final challenge");

		f = facts();
		f.cup = 1;
		f.levelID = 3; // a leg on Crash Cove: the cup's gem, not the track's trophy
		for (int c = 0; c < 5; c++)
		{
			f.cupID = c;
			expect(winset(f, &mode) == std::to_string(35011200 + c), "gem cup " + std::to_string(c));
		}
		expect_int(mode, AP_WS_MODE_GEM_CUP, "mode gem cup");
		f.cupCustom = 1;
		f.customTrophy = 35016300;
		f.customCtr = 35023000;
		expect(winset(f) == "35016300", "cup redirected to the custom track: custom Trophy");
		f.token = 1;
		expect(winset(f) == "35023000", "custom CTR challenge in a redirected cup");

		f = facts();
		f.crystal = 1;
		int arenas[4][2] = {{21, 35013000}, {19, 35013001}, {23, 35013002}, {18, 35013003}};
		for (auto &a : arenas)
		{
			f.levelID = a[0];
			expect(winset(f, &mode) == std::to_string(a[1]), "crystal arena " + std::to_string(a[0]));
		}
		expect_int(mode, AP_WS_MODE_CRYSTAL, "mode crystal");

		f = facts();
		f.adventure = 0;
		f.levelID = 3;
		expect(winset(f) == "", "not adventure: no win set");
	}

	// ════ Stakes decisions ════
	{
		J b = {{"version", 1}, {"families", J::array()},
		       {"regions", arr({true, arr({"req", 2, 1, -1})})},
		       {"checks",
		        {{"35011009", {{"kind", "trophy"}, {"region", 0}, {"rule", true}}},
		         {"35012009", {{"kind", "sapphire"}, {"region", 0}, {"rule", arr({"ref", 35011009})}}},
		         {"35012109", {{"kind", "gold"}, {"region", 0}, {"rule", arr({"cap", 1, -1})}}},
		         {"35012209", {{"kind", "platinum"}, {"region", 0}, {"rule", arr({"cap", 2, -1})}}},
		         {"35012309", {{"kind", "ctr"}, {"region", 0},
		                       {"rule", arr({"items", "distinct", 3, {35010160, 35010161, 35010162}})}}},
		         {"35011012", {{"kind", "trophy"}, {"region", 1}, {"rule", true}}},
		         {"35011202", {{"kind", "gem"}, {"region", 0}, {"rule", arr({"tokens_no_purple", 4})}}}}}};
		parse_block(b);
		expect_int(ctr_win_logic.state, AP_WL_VALID, "stakes block valid");
		World w;
		AP_WinLogicEnv e = env_for(w, 1, 0, 1);
		long dm[1] = {35011009};
		long hs[1] = {35011012};
		long relic[3] = {35012009, 35012109, 35012209};
		long ctr[1] = {35012309};
		long gem[1] = {35011202};
		long none[1] = {35011003};
		AP_WinStakes s;

		s = AP_WinStakesDecide(&ctr_win_logic, &e, dm, 1);
		expect(s.stakes && s.why == AP_WS_WHY_STAKES && s.code == 35011009, "in logic and uncollected -> stakes");
		w.collected.insert(35011009);
		s = AP_WinStakesDecide(&ctr_win_logic, &e, dm, 1);
		expect(!s.stakes && s.why == AP_WS_WHY_COLLECTED, "win collected -> exempt (collected)");
		s = AP_WinStakesDecide(&ctr_win_logic, &e, hs, 1);
		expect(!s.stakes && s.why == AP_WS_WHY_OUT_OF_LOGIC, "region closed -> exempt (out of logic)");
		w.items[35010014] = 1;
		s = AP_WinStakesDecide(&ctr_win_logic, &e, hs, 1);
		expect(s.stakes, "region opens -> stakes");

		// Relic: stakes while ANY tier is open and in logic.
		s = AP_WinStakesDecide(&ctr_win_logic, &e, relic, 3);
		expect(s.stakes && s.code == 35012009, "relic: Sapphire open (trophy collected) -> stakes");
		w.collected.insert(35012009);
		s = AP_WinStakesDecide(&ctr_win_logic, &e, relic, 3);
		expect(!s.stakes && s.why == AP_WS_WHY_COLLECTED_OR_OUT, "relic: Sapphire done, Gold and Platinum need boosts");
		w.items[35010027] = 1;
		s = AP_WinStakesDecide(&ctr_win_logic, &e, relic, 3);
		expect(s.stakes && s.code == 35012109, "relic: Gold opens with 1 boost -> stakes");
		w.collected.insert(35012109);
		s = AP_WinStakesDecide(&ctr_win_logic, &e, relic, 3);
		expect(!s.stakes, "relic: only Platinum open, out of logic -> exempt");
		w.items[35010027] = 2;
		expect(AP_WinStakesDecide(&ctr_win_logic, &e, relic, 3).stakes, "relic: Platinum in logic -> stakes");
		w.collected.insert(35012209);
		s = AP_WinStakesDecide(&ctr_win_logic, &e, relic, 3);
		expect(!s.stakes && s.why == AP_WS_WHY_COLLECTED, "relic: all tiers collected -> exempt (collected)");

		// CTR Challenge without all letters: out of logic.
		w.items[35010160] = 1;
		w.items[35010161] = 1;
		expect(!AP_WinStakesDecide(&ctr_win_logic, &e, ctr, 1).stakes, "CTR: 2 of 3 letters -> exempt");
		w.items[35010162] = 1;
		expect(AP_WinStakesDecide(&ctr_win_logic, &e, ctr, 1).stakes, "CTR: all letters -> stakes");

		// Gem Cup.
		expect(!AP_WinStakesDecide(&ctr_win_logic, &e, gem, 1).stakes, "gem cup without tokens -> exempt");
		w.items[35010004] = w.items[35010005] = w.items[35010006] = w.items[35010007] = 1;
		expect(AP_WinStakesDecide(&ctr_win_logic, &e, gem, 1).stakes, "gem cup with 4 tokens -> stakes");

		// No entry for the race's win, and no win identified.
		s = AP_WinStakesDecide(&ctr_win_logic, &e, none, 1);
		expect(!s.stakes && s.why == AP_WS_WHY_NO_ENTRY, "win location has no entry -> exempt");
		s = AP_WinStakesDecide(&ctr_win_logic, &e, none, 0);
		expect(!s.stakes && s.why == AP_WS_WHY_NO_ENTRY, "empty win set -> exempt");

		// Tally overflow: fall back to 0.2.3.
		e.unreliable = 1;
		s = AP_WinStakesDecide(&ctr_win_logic, &e, dm, 1);
		expect(s.stakes && s.why == AP_WS_WHY_UNRELIABLE, "unreliable inputs -> 0.2.3 (sends)");
	}

	// ════ Missing, newer or invalid block: 0.2.3, every loss sends ════
	{
		World w;
		w.collected.insert(35011009);
		AP_WinLogicEnv e = env_for(w);
		long dm[1] = {35011009};
		ap_seedcfg_parse_json(J{{"ctr_options", {{"schema_version", 16}}}});
		AP_WinStakes s = AP_WinStakesDecide(&ctr_win_logic, &e, dm, 1);
		expect(s.stakes && s.why == AP_WS_WHY_NO_BLOCK, "absent block -> sends even when collected");
		parse_block(J{{"version", 2}});
		s = AP_WinStakesDecide(&ctr_win_logic, &e, dm, 1);
		expect(s.stakes && s.why == AP_WS_WHY_NEWER, "newer block -> sends");
		parse_block(J{{"version", 1}, {"families", J::array()}, {"regions", arr({true})},
		              {"checks", {{"35011009", {{"kind", "trophy"}, {"region", 0}, {"rule", arr({"bad"})}}}}}});
		s = AP_WinStakesDecide(&ctr_win_logic, &e, dm, 1);
		expect(s.stakes && s.why == AP_WS_WHY_INVALID, "invalid block -> sends");
		s = AP_WinStakesDecide(&ctr_win_logic, &e, dm, 0);
		expect(s.stakes, "invalid block, empty win set -> still sends (0.2.3)");
	}

	// ════ Lifecycle: decided at race start, cup on its first leg ════
	{
		J b = {{"version", 1}, {"families", J::array()}, {"regions", arr({true})},
		       {"checks",
		        {{"35011009", {{"kind", "trophy"}, {"region", 0}, {"rule", arr({"req", 2, 1, -1})}}},
		         {"35012009", {{"kind", "sapphire"}, {"region", 0}, {"rule", true}}},
		         {"35012109", {{"kind", "gold"}, {"region", 0}, {"rule", arr({"cap", 1, -1})}}},
		         {"35012209", {{"kind", "platinum"}, {"region", 0}, {"rule", arr({"cap", 2, -1})}}},
		         {"35011202", {{"kind", "gem"}, {"region", 0}, {"rule", true}}}}}};
		parse_block(b);
		World w;
		Session ses;
		AP_WinStakesLatchReset(&ses.latch);
		ses.w = &w;

		// Trophy race out of logic at start; the Key arrives mid-race: still exempt.
		ses.race = facts();
		ses.race.levelID = 1;
		ses.level_start(1);
		w.items[35010014] = 1;
		ses.loss_event(AP_DL_EV_RACE_END, 1, 0);
		expect(ses.sent == 0 && ses.exempt == 1, "out of logic at start: loss exempt even after the item lands");
		// RESTART is a new race start: now in logic.
		ses.level_start(1);
		ses.loss_event(AP_DL_EV_PAUSE_RESTART, 1, 1);
		expect_int(ses.sent, 1, "restart after the item: in logic -> RESTART sends");
		ses.level_start(1);
		ses.loss_event(AP_DL_EV_PAUSE_QUIT, 1, 1);
		expect_int(ses.sent, 2, "EXIT TO MAP while in logic and open -> sends");
		// Win it: collected mid-race does not change the started race...
		ses.level_start(1);
		w.collected.insert(35011009);
		ses.loss_event(AP_DL_EV_PAUSE_QUIT, 1, 1);
		expect_int(ses.sent, 3, "collected mid-race: the started race keeps its stakes");
		// ...but the next race on that track is exempt (Dex's Dragon Mines rerun).
		ses.level_start(0); // hub
		ses.level_start(1);
		ses.loss_event(AP_DL_EV_PAUSE_QUIT, 1, 1);
		ses.loss_event(AP_DL_EV_PAUSE_RESTART, 1, 1);
		ses.loss_event(AP_DL_EV_RACE_END, 1, 0);
		expect(ses.sent == 3 && ses.exempt == 4, "won track rerun: EXIT, RESTART and a loss are all exempt");
		// A pause outside a live race and a won race never reach the stakes.
		ses.loss_event(AP_DL_EV_PAUSE_QUIT, 1, 0);
		ses.loss_event(AP_DL_EV_RACE_END, 0, 0);
		expect(ses.sent == 3 && ses.exempt == 4, "non-loss events unchanged");

		// Forced loss (received DeathLink) never sends, even with stakes.
		w.collected.clear();
		ses.level_start(1);
		ses.forcedLoss = 1;
		ses.loss_event(AP_DL_EV_RACE_END, 1, 0);
		ses.loss_event(AP_DL_EV_PAUSE_QUIT, 1, 1);
		expect(ses.sent == 3 && ses.exempt == 4, "forced loss: nothing sends");
		ses.forcedLoss = 0;
		// race_loss trigger off: nothing sends.
		ses.sendMask = AP_DL_TRIG_FALL;
		ses.loss_event(AP_DL_EV_PAUSE_QUIT, 1, 1);
		expect_int(ses.sent, 3, "race_loss bit off: nothing sends");
		ses.sendMask = AP_DL_TRIG_FALL | AP_DL_TRIG_LOSS;

		// Relic Race: no loss state; only RESTART / EXIT can send.
		ses.race = facts();
		ses.race.levelID = 1;
		ses.race.relic = 1;
		w.collected.insert(35012009);
		ses.level_start(0);
		ses.level_start(1);
		int sent0 = ses.sent, ex0 = ses.exempt;
		ses.loss_event(AP_DL_EV_RACE_END,
		               AP_DeathLinkRaceEndLost(1, 0, 1, 0, 5, 0, 0), 0);
		expect(ses.sent == sent0 && ses.exempt == ex0, "relic race end is never a loss");
		ses.loss_event(AP_DL_EV_PAUSE_QUIT, 1, 1);
		expect(ses.exempt == ex0 + 1, "relic: Sapphire done, Gold/Platinum out of logic -> EXIT exempt");
		w.items[35010027] = 1; // arrives mid-race
		ses.loss_event(AP_DL_EV_PAUSE_QUIT, 1, 1);
		expect(ses.exempt == ex0 + 2, "relic: boost arriving mid-race does not change it");
		ses.level_start(1); // RESTART: new start, Gold now in logic
		ses.loss_event(AP_DL_EV_PAUSE_RESTART, 1, 1);
		expect(ses.sent == sent0 + 1, "relic: any tier open and in logic -> RESTART sends");

		// Gem Cup: decided on the first leg, kept for every leg and the result.
		ses.race = facts();
		ses.race.cup = 1;
		ses.race.cupID = 2;
		ses.race.levelID = 3;
		ses.level_start(0);       // hub
		AP_WinStakesCupEntered(&ses.latch);
		ses.level_start(1);       // leg 1 decides: gem open, in logic -> stakes
		w.collected.insert(35011202); // collected by the server mid-cup (!collect)
		int sent1 = ses.sent;
		ses.loss_event(AP_DL_EV_RACE_END,
		               AP_DeathLinkRaceEndLost(1, 1, 0, 0, 7, 0, 0), 0);
		expect_int(ses.sent, sent1, "gem cup: a lost leg never sends");
		ses.race.levelID = 5;
		ses.level_start(1);       // leg 2 keeps the cup decision
		ses.loss_event(AP_DL_EV_PAUSE_RESTART, 1, 1);
		expect_int(ses.sent, sent1 + 1, "gem cup: leg 2 RESTART uses the cup's start decision");
		ses.level_start(1);       // leg restart
		ses.loss_event(AP_DL_EV_CUP_END, 1, 0);
		expect_int(ses.sent, sent1 + 2, "gem cup: whole cup lost -> sends (decided on leg 1)");
		// Next cup entry: gem now collected -> exempt.
		ses.level_start(0);
		AP_WinStakesCupEntered(&ses.latch);
		ses.level_start(1);
		int ex1 = ses.exempt;
		ses.loss_event(AP_DL_EV_CUP_END, 1, 0);
		ses.loss_event(AP_DL_EV_PAUSE_QUIT, 1, 1);
		expect(ses.sent == sent1 + 2 && ses.exempt == ex1 + 2, "gem cup with the gem collected: exempt");

		// No decision at race start (connected mid-race): decided at the loss.
		AP_WinStakesLatchReset(&ses.latch);
		ses.race = facts();
		ses.race.levelID = 1;
		w.collected.clear();
		int sent2 = ses.sent;
		ses.loss_event(AP_DL_EV_PAUSE_QUIT, 1, 1);
		expect_int(ses.sent, sent2 + 1, "no start decision: decided late, in logic -> sends");
		w.collected.insert(35011009);
		ses.loss_event(AP_DL_EV_PAUSE_QUIT, 1, 1);
		expect_int(ses.sent, sent2 + 2, "late decision is then kept for the race");

		// Absent block: every loss sends again (0.2.3).
		ap_seedcfg_parse_json(J{{"ctr_options", {{"schema_version", 16}}}});
		ses.level_start(0);
		ses.level_start(1);
		int sent3 = ses.sent;
		ses.loss_event(AP_DL_EV_PAUSE_QUIT, 1, 1);
		ses.loss_event(AP_DL_EV_PAUSE_RESTART, 1, 1);
		ses.loss_event(AP_DL_EV_RACE_END, 1, 0);
		expect_int(ses.sent, sent3 + 3, "absent block: collected win still sends (0.2.3)");
	}

	// ════ Tally ════
	{
		static AP_ItemIdTally t;
		AP_ItemIdTallyReset(&t);
		AP_ItemIdTallyAdd(&t, 35010027);
		AP_ItemIdTallyAdd(&t, 35010027);
		AP_ItemIdTallyAdd(&t, 35021395);
		expect_int(AP_ItemIdTallyCount(&t, 35010027), 2, "tally counts duplicates");
		expect_int(AP_ItemIdTallyCount(&t, 35021395), 1, "tally sparse id");
		expect_int(AP_ItemIdTallyCount(&t, 35010028), 0, "tally unknown id 0");
		for (long long id = 35010000; id < 35010000 + 1500; id++)
			AP_ItemIdTallyAdd(&t, id);
		expect_int(t.overflow, 0, "1500 distinct ids fit");
		expect_int(AP_ItemIdTallyCount(&t, 35010027), 3, "tally still exact after load");
		expect_int(AP_ItemIdTallyCount(&t, 35011499), 1, "last of the batch");
		for (long long id = 36000000; id < 36000000 + 700; id++)
			AP_ItemIdTallyAdd(&t, id);
		expect_int(t.overflow, 1, "overflow is flagged, not silent");
		AP_ItemIdTallyReset(&t);
		AP_ItemIdTallyReceive(&t, 35010001, 0u); // filler
		AP_ItemIdTallyReceive(&t, 35010001, 2u); // useful (vanilla relic under minimal)
		AP_ItemIdTallyReceive(&t, 35010001, 4u); // trap
		expect_int(AP_ItemIdTallyCount(&t, 35010001), 0, "non-progression copies never count");
		AP_ItemIdTallyReceive(&t, 35010001, 1u);
		AP_ItemIdTallyReceive(&t, 35010001, 3u); // progression + useful
		expect_int(AP_ItemIdTallyCount(&t, 35010001), 2, "progression-flagged copies count");
		AP_ItemIdTallyReset(&t);
		expect(t.overflow == 0 && AP_ItemIdTallyCount(&t, 35010027) == 0, "reset clears");
	}

	std::printf("%s: %d checks, %d failure(s)\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
