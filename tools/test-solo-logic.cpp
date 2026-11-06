// Host harness for ap/ap_solo_logic.hpp (#89 solo mode): the JSON-lines
// protocol with the runtime, the check journal and its resend set, the
// session identity check, the stop sequence, the generation time limit and
// the options screen model.
#include "ap_solo_logic.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

static int g_fail = 0;
#define CHECK(cond)                                                                  \
	do                                                                               \
	{                                                                                \
		if (!(cond))                                                                 \
		{                                                                            \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
			g_fail++;                                                                \
		}                                                                            \
	} while (0)

static void test_parse_lines()
{
	APSoloEvent ev = AP_SoloParseLine("{\"event\": \"progress\", \"phase\": \"Placing items\"}");
	CHECK(ev.kind == AP_SOLO_EV_PROGRESS && ev.phase == "Placing items");

	ev = AP_SoloParseLine("{\"event\":\"error\",\"kind\":\"option\",\"message\":\"bad\",\"option\":\"oxide_goal\"}");
	CHECK(ev.kind == AP_SOLO_EV_ERROR && ev.errorKind == "option" && ev.option == "oxide_goal");

	ev = AP_SoloParseLine("{\"event\":\"error\",\"message\":\"no kind\"}");
	CHECK(ev.kind == AP_SOLO_EV_ERROR && ev.errorKind == "internal");

	ev = AP_SoloParseLine("{\"event\":\"ready\",\"port\":38302,\"password\":\"pw\",\"seed_name\":\"123\","
	                      "\"slot\":\"Solo\",\"room_id\":\"0123456789abcdef\"}");
	CHECK(ev.kind == AP_SOLO_EV_READY && ev.port == 38302 && ev.password == "pw" && ev.seedName == "123");

	// A ready line without a port, seed or slot is ignored.
	CHECK(AP_SoloParseLine("{\"event\":\"ready\",\"seed_name\":\"1\",\"slot\":\"s\"}").kind == AP_SOLO_EV_NONE);
	CHECK(AP_SoloParseLine("{\"event\":\"ready\",\"port\":70000,\"seed_name\":\"1\",\"slot\":\"s\"}").kind ==
	      AP_SOLO_EV_NONE);
	CHECK(AP_SoloParseLine("{\"event\":\"ready\",\"port\":1,\"seed_name\":\"\",\"slot\":\"s\"}").kind ==
	      AP_SOLO_EV_NONE);

	// Garbage: warnings, half lines, arrays, unknown events.
	CHECK(AP_SoloParseLine("UserWarning: _speedups not available").kind == AP_SOLO_EV_NONE);
	CHECK(AP_SoloParseLine("{\"event\": \"progr").kind == AP_SOLO_EV_NONE);
	CHECK(AP_SoloParseLine("[1,2]").kind == AP_SOLO_EV_NONE);
	CHECK(AP_SoloParseLine("{\"event\":\"dance\"}").kind == AP_SOLO_EV_NONE);
	CHECK(AP_SoloParseLine("").kind == AP_SOLO_EV_NONE);

	ev = AP_SoloParseLine("{\"event\":\"result\",\"room_id\":\"0123456789abcdef\"}");
	CHECK(ev.kind == AP_SOLO_EV_RESULT && AP_SoloJsonString(ev.body, "room_id") == "0123456789abcdef");
}

static void test_take_lines()
{
	std::string buf = "{\"a\":1}\r\n{\"b\"";
	std::vector<std::string> lines = AP_SoloTakeLines(buf);
	CHECK(lines.size() == 1 && lines[0] == "{\"a\":1}");
	CHECK(buf == "{\"b\"");
	buf += ":2}\n\n";
	lines = AP_SoloTakeLines(buf);
	CHECK(lines.size() == 2 && lines[0] == "{\"b\":2}" && lines[1].empty() && buf.empty());
	buf = std::string(64, 'x');
	lines = AP_SoloTakeLines(buf, 32);
	CHECK(lines.empty() && buf.empty()); // an unbounded line is dropped
}

static void test_room_id()
{
	CHECK(AP_SoloRoomIdValid("0123456789abcdef"));
	CHECK(!AP_SoloRoomIdValid("0123456789ABCDEF"));
	CHECK(!AP_SoloRoomIdValid("0123456789abcde"));
	CHECK(!AP_SoloRoomIdValid("../../etc/passwd"));
	CHECK(!AP_SoloRoomIdValid(""));
}

static void test_journal()
{
	CHECK(AP_SoloJournalRecord(35011000) == "35011000 35011000\n");
	std::string text = AP_SoloJournalRecord(35011000) + AP_SoloJournalRecord(35011001) +
	                   "35011000 35011000\r\n" + "junk\n-5 -5\n0 0\n\n";
	std::vector<int64_t> codes = AP_SoloJournalParse(text);
	CHECK(codes.size() == 2 && codes[0] == 35011000 && codes[1] == 35011001);
	CHECK(AP_SoloJournalParse("").empty());
	CHECK(AP_SoloJournalParse("99999999999999999999 99999999999999999999\n").empty()); // too long
	// A torn write: "3501" left by a crash, then the next record appended on
	// the same line. Neither half passes; the next clean record does.
	codes = AP_SoloJournalParse("35011000 3501" + AP_SoloJournalRecord(35011002) + AP_SoloJournalRecord(35011003));
	CHECK(codes.size() == 1 && codes[0] == 35011003);
	// The old single-number form and an unfinished last line are not records.
	CHECK(AP_SoloJournalParse("35011000\n").empty());
	CHECK(AP_SoloJournalParse("35011000 35011000").empty());

	// The solo port range.
	CHECK(AP_SoloPortAllowed(38301) && AP_SoloPortAllowed(38320));
	CHECK(!AP_SoloPortAllowed(38300) && !AP_SoloPortAllowed(38321) && !AP_SoloPortAllowed(38281));

	// After a deleted .apsave the server lists the checks as missing again:
	// every journalled check it still misses is resent, nothing else.
	std::set<int64_t> checked = {1};
	std::set<int64_t> missing = {2, 3, 4};
	std::vector<int64_t> resend = AP_SoloResendSet({1, 2, 3, 9}, checked, missing);
	CHECK(resend.size() == 2 && resend[0] == 2 && resend[1] == 3);
	CHECK(AP_SoloResendSet({1}, checked, missing).empty()); // already checked: no duplicate
	CHECK(AP_SoloResendSet({9}, checked, missing).empty()); // unknown to this room: never sent
}

static void test_identity()
{
	CHECK(AP_SoloIdentity("", "123") == AP_SOLO_ID_PENDING);
	CHECK(AP_SoloIdentity("123", "123") == AP_SOLO_ID_MATCH);
	// A listener on the solo port that serves another room is never used.
	CHECK(AP_SoloIdentity("999", "123") == AP_SOLO_ID_FOREIGN);
}

static void test_stop_and_timeout()
{
	CHECK(AP_SoloStopStep(false, 0) == AP_SOLO_STOP_WAIT);
	CHECK(AP_SoloStopStep(false, 4999) == AP_SOLO_STOP_WAIT);
	CHECK(AP_SoloStopStep(false, 5000) == AP_SOLO_STOP_KILL); // a server that ignores /exit
	CHECK(AP_SoloStopStep(true, 10) == AP_SOLO_STOP_DONE);
	CHECK(!AP_SoloJobTimedOut(119999));
	CHECK(AP_SoloJobTimedOut(120000));
}

static void test_error_text()
{
	CHECK(AP_SoloErrorText("runtime", "x") == "Solo runtime is damaged. Reinstall or update the client.");
	CHECK(AP_SoloErrorText("fill", "x") ==
	      "This combination could not be generated. Try again or change options.");
	CHECK(AP_SoloErrorText("timeout", "") ==
	      "Generation took too long. Try fewer custom tracks or a smaller preset.");
	CHECK(AP_SoloErrorText("game", "This YAML is for X.") == "This YAML is for X.");
}

static const char *kDescribe = R"({
  "options": [
    {"name": "oxide_goal", "display_name": "Oxide Goal", "kind": "choice", "default": "any_percent",
     "choices": [{"name": "none", "value": 0}, {"name": "any_percent", "value": 1},
                 {"name": "101_percent", "value": 2}, {"name": "disabled", "value": 3}], "doc": "What finishing means."},
    {"name": "bosses_required_goal", "display_name": "Bosses Required Goal", "kind": "range", "default": 0,
     "range_start": 0, "range_end": 4},
    {"name": "gems_required_goal", "display_name": "Gems Required Goal", "kind": "range", "default": 0,
     "range_start": 0, "range_end": 5},
    {"name": "oxide_final_challenge_unlock", "display_name": "Unlock", "kind": "choice", "default": "gold_relics",
     "choices": [{"name": "sapphire_relics", "value": 0}, {"name": "gold_relics", "value": 1}]},
    {"name": "shuffle_keys", "display_name": "Shuffle Keys", "kind": "toggle", "default": true},
    {"name": "death_link", "display_name": "DeathLink", "kind": "choice", "default": "off",
     "choices": [{"name": "off", "value": 0}, {"name": "race_loss", "value": 3}]},
    {"name": "trap_weights", "display_name": "Trap Weights", "kind": "dict", "default": {}}
  ],
  "overlay": {
    "groups": [
      {"label": "Goal", "options": ["oxide_goal", "bosses_required_goal", "gems_required_goal",
                                     "oxide_final_challenge_unlock", "no_such_option"]},
      {"label": "Extras", "options": ["shuffle_keys", "death_link", "trap_weights"]}
    ],
    "rules": [
      {"option": "death_link", "forced": "off", "reason": "No other players in solo"},
      {"option": "oxide_final_challenge_unlock", "visible_when": {"oxide_goal": ["101_percent"]}},
      {"option": "bosses_required_goal", "warn_when": {"oxide_goal": ["none", "disabled"], "value": 0,
       "also_zero": "gems_required_goal"}, "reason": "Set Bosses or Gems above 0"}
    ]
  },
  "values": {"oxide_goal": "any_percent", "bosses_required_goal": 0, "gems_required_goal": 0,
             "oxide_final_challenge_unlock": "sapphire_relics", "shuffle_keys": false, "death_link": "race_loss"}
})";

static APSoloOption *find(std::vector<APSoloOption> &rows, const char *name)
{
	for (auto &r : rows)
		if (r.name == name)
			return &r;
	return nullptr;
}

static void test_options()
{
	std::vector<APSoloOption> rows = AP_SoloBuildOptions(nlohmann::json::parse(kDescribe));
	// Unknown names and dict options are not rows.
	CHECK(rows.size() == 6);
	CHECK(find(rows, "no_such_option") == nullptr && find(rows, "trap_weights") == nullptr);

	APSoloOption *goal = find(rows, "oxide_goal");
	APSoloOption *bosses = find(rows, "bosses_required_goal");
	APSoloOption *unlock = find(rows, "oxide_final_challenge_unlock");
	APSoloOption *keys = find(rows, "shuffle_keys");
	APSoloOption *dl = find(rows, "death_link");
	CHECK(goal && bosses && unlock && keys && dl);
	if (!(goal && bosses && unlock && keys && dl))
		return;
	CHECK(goal->group == "Goal" && goal->doc == "What finishing means.");
	CHECK(keys->value == "false"); // the preset's value, not the default

	// DeathLink is fixed off in solo, whatever the preset says.
	CHECK(dl->value == "off" && !dl->forced.empty() && dl->forcedReason == "No other players in solo");
	AP_SoloOptionStep(*dl, 1);
	CHECK(dl->value == "off");

	// Choice wraps both ways; range wraps; toggle flips.
	AP_SoloOptionStep(*goal, -1);
	CHECK(goal->value == "none");
	AP_SoloOptionStep(*goal, -1);
	CHECK(goal->value == "disabled");
	AP_SoloOptionStep(*goal, 1);
	CHECK(goal->value == "none");
	AP_SoloOptionStep(*bosses, -1);
	CHECK(bosses->value == "4");
	AP_SoloOptionStep(*bosses, 1);
	CHECK(bosses->value == "0");
	AP_SoloOptionStep(*keys, 1);
	CHECK(keys->value == "true");

	// visible_when and warn_when.
	CHECK(!AP_SoloOptionVisible(rows, *unlock));
	CHECK(AP_SoloOptionWarns(rows, *bosses)); // goal none, bosses 0, gems 0
	AP_SoloOptionStep(*bosses, 1);
	CHECK(!AP_SoloOptionWarns(rows, *bosses));
	goal->value = "101_percent";
	CHECK(AP_SoloOptionVisible(rows, *unlock));
	CHECK(!AP_SoloOptionWarns(rows, *bosses));

	// --set: changed visible rows and forced rows; choices quoted.
	unlock->value = "gold_relics";
	std::vector<std::string> sets = AP_SoloSetArgs(rows);
	std::set<std::string> got(sets.begin(), sets.end());
	CHECK(got.count("oxide_goal=\"101_percent\"") == 1);
	CHECK(got.count("bosses_required_goal=1") == 1);
	CHECK(got.count("shuffle_keys=true") == 1);
	CHECK(got.count("death_link=\"off\"") == 1);
	CHECK(got.count("oxide_final_challenge_unlock=\"gold_relics\"") == 1);
	CHECK(got.count("gems_required_goal=0") == 0); // unchanged
	CHECK(sets.size() == 5);

	// Hidden rows are not sent even when changed.
	goal->value = "any_percent";
	sets = AP_SoloSetArgs(rows);
	for (const auto &s : sets)
		CHECK(s.rfind("oxide_final_challenge_unlock=", 0) != 0);

	// A broken describe result gives no rows instead of a crash.
	CHECK(AP_SoloBuildOptions(nlohmann::json()).empty());
	CHECK(AP_SoloBuildOptions(nlohmann::json::parse("{\"overlay\": 5}")).empty());
}

// Alpha2: every option is a row. Large ranges move by the overlay's step and
// stop at both ends; help text drops its Markdown and hard line breaks.
static void test_option_steps_and_help()
{
	const nlohmann::json d = nlohmann::json::parse(R"({
	  "options": [
	    {"name": "goal_wumpa", "display_name": "Goal Wumpa", "kind": "range", "default": 0, "range_start": 0, "range_end": 5000,
	     "doc": "Bank this many Wumpa Fruit\nover the whole seed.\n\nUse `1000` for a **long** goal:\n- first item\n- second item\n\nLast line."},
	    {"name": "wumpa_shop_prices", "display_name": "Prices", "kind": "range", "default": 100, "range_start": 25, "range_end": 400},
	    {"name": "relic_count", "display_name": "Relics", "kind": "named_range", "default": 18, "range_start": 1, "range_end": 54},
	    {"name": "plando", "display_name": "Plando", "kind": "list", "default": []},
	    {"name": "tags", "display_name": "Tags", "kind": "set", "default": []}
	  ],
	  "overlay": {"groups": [{"label": "A", "options": ["goal_wumpa", "plando"]},
	                         {"label": "B", "options": ["wumpa_shop_prices", "relic_count", "tags"]}],
	              "rules": [{"option": "goal_wumpa", "step": 50}, {"option": "wumpa_shop_prices", "step": 25},
	                        {"option": "relic_count", "label": "Short Relic Name"}]},
	  "values": {"goal_wumpa": 0, "wumpa_shop_prices": 100, "relic_count": 18}
	})");
	std::vector<APSoloOption> rows = AP_SoloBuildOptions(d);
	CHECK(rows.size() == 3); // lists and sets are never rows
	APSoloOption *gw = find(rows, "goal_wumpa"), *pr = find(rows, "wumpa_shop_prices"), *rc = find(rows, "relic_count");
	CHECK(gw && pr && rc);
	if (!(gw && pr && rc))
		return;
	CHECK(gw->step == 50 && pr->step == 25 && rc->step == 1);
	CHECK(rc->kind == "range" && rc->lo == 1 && rc->hi == 54);
	CHECK(rc->label == "Short Relic Name" && gw->label == "Goal Wumpa"); // a rule may shorten a row name
	AP_SoloOptionStep(*gw, 1);
	CHECK(gw->value == "50");
	for (int i = 0; i < 99; i++)
		AP_SoloOptionStep(*gw, 1);
	CHECK(gw->value == "5000");
	AP_SoloOptionStep(*gw, 1); // past the top wraps to the bottom
	CHECK(gw->value == "0");
	AP_SoloOptionStep(*gw, -1);
	CHECK(gw->value == "5000");
	// A value off the grid still reaches the end instead of overshooting it.
	gw->value = "4990";
	AP_SoloOptionStep(*gw, 1);
	CHECK(gw->value == "5000");
	gw->value = "20";
	AP_SoloOptionStep(*gw, -1);
	CHECK(gw->value == "0");
	// Prices: 25 to 400 in steps of 25, always inside the range.
	pr->value = "25";
	AP_SoloOptionStep(*pr, -1);
	CHECK(pr->value == "400");
	AP_SoloOptionStep(*pr, 1);
	CHECK(pr->value == "25");
	for (int i = 0; i < 40; i++)
	{
		AP_SoloOptionStep(*pr, 1);
		const int v = atoi(pr->value.c_str());
		CHECK(v >= 25 && v <= 400);
	}
	// A named range without a step moves by one.
	AP_SoloOptionStep(*rc, 1);
	CHECK(rc->value == "19");
	// Help text.
	CHECK(gw->doc == "Bank this many Wumpa Fruit over the whole seed.\nUse 1000 for a long goal:\n- first item\n- second item\nLast line.");
	CHECK(AP_SoloHelpText("").empty());
	CHECK(AP_SoloHelpText("\n\n  one  \n  two\n\n") == "one two");
}

// A2-41: a set option with a checklist is a row; its value is a JSON array in
// row order; guards refuse what the world refuses.
static void test_checklists_and_guards()
{
	const nlohmann::json d = nlohmann::json::parse(R"({
	  "options": [
	    {"name": "crystal_challenge_count", "display_name": "Crystal Challenge Count", "kind": "range", "default": 4,
	     "range_start": 0, "range_end": 7},
	    {"name": "crystal_arenas", "display_name": "Crystal Arenas", "kind": "set",
	     "default": ["Nitro Court", "Rampage Ruins", "Rocky Road", "Skull Rock"]},
	    {"name": "boss_opponent_shuffle", "display_name": "Boss Opponent Shuffle", "kind": "toggle", "default": false},
	    {"name": "boss_track_pool", "display_name": "Boss Track Pool", "kind": "set", "default": []},
	    {"name": "boss_weapon_pool", "display_name": "Boss Weapon Pool", "kind": "set", "default": ["bomb", "potion", "tnt"]},
	    {"name": "start_hints", "display_name": "Start Hints", "kind": "set", "default": []}
	  ],
	  "overlay": {"groups": [{"label": "Cups", "options": ["crystal_challenge_count", "crystal_arenas"]},
	                         {"label": "Bosses", "options": ["boss_opponent_shuffle", "boss_track_pool", "boss_weapon_pool",
	                                                         "start_hints"]}],
	              "rules": [],
	              "checklists": {
	                "crystal_arenas": [["Rocky Road"], ["Rampage Ruins"], ["Skull Rock"], ["Nitro Court"], ["Parking Lot"],
	                                   ["The North Bowl"], ["Lab Basement"]],
	                "boss_track_pool": [["Crash Cove"], ["Roo's Tubes"]],
	                "boss_weapon_pool": [["tnt", "TNT"], ["potion", "Potion"], ["bomb", "Bomb"]]},
	              "guards": [
	                {"option": "crystal_challenge_count", "at_most_count_of": "crystal_arenas", "reason": "count"},
	                {"option": "boss_weapon_pool", "when": {"boss_opponent_shuffle": [true]}, "at_least": 2, "reason": "two"},
	                {"option": "boss_weapon_pool", "when": {"boss_opponent_shuffle": [true]}, "has": "tnt",
	                 "needs_all": ["potion"], "reason": "potion"}]},
	  "values": {"crystal_challenge_count": 4, "crystal_arenas": ["Rocky Road", "Rampage Ruins", "Skull Rock", "Nitro Court"],
	             "boss_opponent_shuffle": false, "boss_track_pool": ["Crash Cove", "custom/my-track"],
	             "boss_weapon_pool": ["tnt", "potion", "bomb"]}
	})");
	std::vector<APSoloOption> rows = AP_SoloBuildOptions(d);
	CHECK(rows.size() == 5); // a set without a checklist is not a row
	APSoloOption *count = find(rows, "crystal_challenge_count"), *arenas = find(rows, "crystal_arenas");
	APSoloOption *shuffle = find(rows, "boss_opponent_shuffle"), *tracks = find(rows, "boss_track_pool");
	APSoloOption *weapons = find(rows, "boss_weapon_pool");
	CHECK(count && arenas && shuffle && tracks && weapons && !find(rows, "start_hints"));
	if (!(count && arenas && shuffle && tracks && weapons))
		return;
	CHECK(arenas->kind == "set" && arenas->keys.size() == 7 && arenas->keyLabels[0] == "Rocky Road");
	CHECK(weapons->keyLabels[0] == "TNT" && weapons->keys[0] == "tnt");
	CHECK(arenas->value == R"(["Rocky Road","Rampage Ruins","Skull Rock","Nitro Court"])");
	CHECK(AP_SoloSetSummary(*arenas) == "4 of 7");
	// The default rows are marked.
	CHECK(arenas->keyDefault.size() == 7 && arenas->keyDefault[0] && arenas->keyDefault[3] && !arenas->keyDefault[4]);
	// A YAML entry without a row is kept as a row of its own.
	CHECK(tracks->keys.size() == 3 && tracks->keys[2] == "custom/my-track" && tracks->keyLabels[2] == "custom/my-track");
	CHECK(AP_SoloSetSummary(*tracks) == "2 of 3");
	// Left / Right never change a set.
	AP_SoloOptionStep(*arenas, 1);
	CHECK(AP_SoloSetSummary(*arenas) == "4 of 7");

	// Seven Crystal Challenges with four arenas: the count guard refuses, on
	// the count row and on the arena row.
	count->value = "7";
	CHECK(AP_SoloGuardReason(rows, *count) == "count" && AP_SoloGuardReason(rows, *arenas) == "count");
	CHECK(AP_SoloFirstBlocked(rows) == 0);
	for (int i = 4; i < 7; i++)
		AP_SoloSetToggle(*arenas, i);
	CHECK(AP_SoloSetSummary(*arenas) == "All 7");
	CHECK(AP_SoloGuardReason(rows, *count).empty() && AP_SoloFirstBlocked(rows) == -1);
	// Checking in any order keeps row order; unchecking removes the key.
	AP_SoloSetToggle(*arenas, 0);
	CHECK(arenas->value == R"(["Rampage Ruins","Skull Rock","Nitro Court","Parking Lot","The North Bowl","Lab Basement"])");
	CHECK(!AP_SoloGuardReason(rows, *count).empty()); // 7 > 6
	AP_SoloSetToggle(*arenas, 0);
	AP_SoloSetToggle(*arenas, 99); // out of range: nothing
	CHECK(AP_SoloSetSummary(*arenas) == "All 7");

	// Guards with `when`: only while the switch is on.
	weapons->value = R"(["bomb"])";
	CHECK(AP_SoloGuardReason(rows, *weapons).empty());
	shuffle->value = "true";
	CHECK(AP_SoloGuardReason(rows, *weapons) == "two");
	weapons->value = R"(["tnt","bomb"])";
	CHECK(AP_SoloGuardReason(rows, *weapons) == "potion"); // has tnt, misses potion
	weapons->value = R"(["potion","bomb"])";
	CHECK(AP_SoloGuardReason(rows, *weapons).empty()); // no tnt: the needs_all guard is off
	weapons->value = "[]";
	CHECK(AP_SoloSetSummary(*weapons) == "None" && AP_SoloGuardReason(rows, *weapons) == "two");
	shuffle->value = "false";

	// --set passes a changed set as its JSON array; unchanged sets stay out.
	std::vector<std::string> sets = AP_SoloSetArgs(rows);
	std::set<std::string> got(sets.begin(), sets.end());
	CHECK(got.count("crystal_challenge_count=7") == 1);
	CHECK(got.count(R"(crystal_arenas=["Rocky Road","Rampage Ruins","Skull Rock","Nitro Court","Parking Lot","The North Bowl","Lab Basement"])") == 1);
	CHECK(got.count("boss_weapon_pool=[]") == 1);
	CHECK(sets.size() == 3);
	// Back to the preset's arenas: nothing to send for it.
	for (int i = 4; i < 7; i++)
		AP_SoloSetToggle(*arenas, i);
	CHECK(arenas->value == arenas->initial);
}

// The custom-track step (decision 32): mode labels, the pick cycle and the
// descriptor file name the client keeps in solo/tracks.
static void test_tracks()
{
	CHECK(AP_SoloTrackModes(nlohmann::json::parse("[\"trophy\", \"ctr\", \"relic\"]")) == "Trophy, CTR, Relic");
	CHECK(AP_SoloTrackModes(nlohmann::json::parse("[\"trophy\", \"relic\"]")) == "Trophy, Relic");
	CHECK(AP_SoloTrackModes(nlohmann::json::parse("[\"other\", 3, \"ctr\"]")) == "CTR");
	CHECK(AP_SoloTrackModes(nlohmann::json()).empty());

	CHECK(AP_SoloTrackNextPick(0) == 1);
	CHECK(AP_SoloTrackNextPick(1) == 2);
	CHECK(AP_SoloTrackNextPick(2) == 0);
	CHECK(AP_SoloTrackNextPick(7) == 0);
	CHECK(AP_SoloTrackNextPick(-1) == 0);
	// Left on the page is two steps forward: off -> own pad -> in pool -> off.
	CHECK(AP_SoloTrackNextPick(AP_SoloTrackNextPick(0)) == 2);

	// The Custom Tracks page flow (round 2 decision 5): the picks become
	// --track and --guarantee arguments, for a preset and for a YAML alike.
	{
		using Picks = std::vector<std::pair<std::string, int>>;
		using Args = std::vector<std::string>;
		CHECK(AP_SoloTrackArgs(Picks{}).empty());
		// Any listed track means the page was shown, even with everything off.
		CHECK((AP_SoloTrackArgs(Picks{{"package/a/1", 0}, {"package/b/1", 0}}) == Args{"--tracks-page"}));
		CHECK((AP_SoloTrackArgs(Picks{{"package/a/1", 1}}) == Args{"--tracks-page", "--track", "package/a/1"}));
		CHECK((AP_SoloTrackArgs(Picks{{"package/a/1", 2}}) ==
		       Args{"--tracks-page", "--track", "package/a/1", "--guarantee", "package/a/1"}));
		CHECK((AP_SoloTrackArgs(Picks{{"package/a/1", 1}, {"package/b/1", 0}, {"package/c/2", 2}}) ==
		       Args{"--tracks-page", "--track", "package/a/1", "--track", "package/c/2", "--guarantee", "package/c/2"}));
		CHECK((AP_SoloTrackArgs(Picks{{"package/a/1", 9}, {"package/b/1", -1}}) == Args{"--tracks-page"}));
		// A YAML's own value is the starting pick.
		CHECK(AP_SoloTrackStart(nlohmann::json{{"start", 1}}) == 1);
		CHECK(AP_SoloTrackStart(nlohmann::json{{"start", 2}}) == 2);
		CHECK(AP_SoloTrackStart(nlohmann::json{{"start", 3}}) == 0);
		CHECK(AP_SoloTrackStart(nlohmann::json{{"id", "x"}}) == 0);
		// A preset's Custom Tracks page sits behind its options screen; a YAML's follows the start list.
		CHECK(AP_SoloTracksViaOptions(true));
		CHECK(!AP_SoloTracksViaOptions(false));
	}

	CHECK(AP_SoloDescriptorFileName("baby-t-park", "1.0.2") == "baby-t-park__1.0.2.json");
	CHECK(AP_SoloDescriptorFileName("fixture-loop", "2.1+lrg2_a") == "fixture-loop__2.1+lrg2_a.json");
	CHECK(AP_SoloDescriptorFileName("", "1").empty());
	CHECK(AP_SoloDescriptorFileName("Baby", "1").empty());
	CHECK(AP_SoloDescriptorFileName("a_b", "1").empty());
	CHECK(AP_SoloDescriptorFileName("-a", "1").empty());
	CHECK(AP_SoloDescriptorFileName("a--b", "1").empty());
	CHECK(AP_SoloDescriptorFileName("../x", "1").empty());
	CHECK(AP_SoloDescriptorFileName("a", "").empty());
	CHECK(AP_SoloDescriptorFileName("a", ".1").empty());
	CHECK(AP_SoloDescriptorFileName("a", "1/2").empty());
	CHECK(AP_SoloDescriptorFileName("a", "1\\2").empty());
	CHECK(AP_SoloDescriptorFileName("a", std::string(65, '1')).empty());
	CHECK(!AP_SoloDescriptorFileName("a", std::string(64, '1')).empty());
}

// An imported track without laps (ruling of 2026-11-02): listed from
// solo/needs-laps, laps picked 1 to 7 from 3, then kept with nothing else
// changed.
static void test_needs_laps()
{
	CHECK(AP_SOLO_LAPS_MIN == 1 && AP_SOLO_LAPS_MAX == 7 && AP_SOLO_LAPS_DEFAULT == 3);
	CHECK(AP_SoloLapsStep(3, 1) == 4 && AP_SoloLapsStep(3, -1) == 2);
	CHECK(AP_SoloLapsStep(7, 1) == 7 && AP_SoloLapsStep(1, -1) == 1); // stops at both ends
	CHECK(AP_SoloLapsStep(0, 0) == 1 && AP_SoloLapsStep(99, 0) == 7);

	const nlohmann::json d = {{"id", "package/long-road/1.0.0"}, {"content_id", "long-road"}, {"revision", "1.0.0"},
	                          {"display_name", "Long Road"}, {"author", "Unknown"}, {"capabilities", {{"relic", false}}}};
	const std::string name = "long-road__1.0.0.json";
	CHECK(AP_SoloNeedsLapsDescriptor(d, name));
	CHECK(!AP_SoloNeedsLapsDescriptor(d, "other__1.0.0.json")); // the file name must match
	nlohmann::json bad = d;
	bad["laps"] = 3;
	CHECK(!AP_SoloNeedsLapsDescriptor(bad, name)); // already has laps
	bad = d;
	bad["id"] = "package/long-road/2.0.0";
	CHECK(!AP_SoloNeedsLapsDescriptor(bad, name));
	bad = d;
	bad.erase("author");
	CHECK(!AP_SoloNeedsLapsDescriptor(bad, name));
	CHECK(!AP_SoloNeedsLapsDescriptor(nlohmann::json::array(), name));

	CHECK(AP_SoloDescriptorWithLaps(d, 0).empty() && AP_SoloDescriptorWithLaps(d, 8).empty());
	nlohmann::json kept = nlohmann::json::parse(AP_SoloDescriptorWithLaps(d, 5));
	nlohmann::json want = d;
	want["laps"] = 5;
	CHECK(kept == want);
	CHECK(AP_SoloDescriptorWithLaps(kept, 4).empty()); // never over laps it has
}

static void test_store_descriptor_rescan()
{
	nlohmann::json original = {{"files", {{{"role", "lev"}, {"sha256", "aa"}},
	                                         {{"role", "vrm"}, {"sha256", "bb"}}}},
	                           {"laps", 3}, {"author", "First"},
	                           {"relic_targets", {{"gold", 500}}}};
	CHECK(AP_SoloDescriptorUnchanged(original, original));
	for (const char *field : {"laps", "author", "relic_targets"})
	{
		nlohmann::json changed = original;
		changed[field] = "changed";
		CHECK(!AP_SoloDescriptorUnchanged(original, changed));
	}
	CHECK(!AP_SoloDescriptorUnchanged(nlohmann::json(), original));
	nlohmann::json reinstall = original;
	reinstall.erase("laps");
	reinstall["author"] = "New author";
	reinstall["display_name"] = "New title";
	reinstall["capabilities"] = {{"arcade", true}};
	auto merged = AP_SoloMergeNeedsLaps(original, reinstall);
	CHECK(merged.is_object() && merged["laps"] == 3 && merged["author"] == "New author" &&
	      merged["display_name"] == "New title" && merged["capabilities"]["arcade"] == true);
	CHECK(!AP_SoloDescriptorUnchanged(original, merged));
	CHECK(AP_SoloDescriptorUnchanged(merged, AP_SoloMergeNeedsLaps(merged, reinstall)));
	reinstall["files"][0]["sha256"] = "other";
	CHECK(AP_SoloMergeNeedsLaps(original, reinstall).is_null());
}

// A3-29: one track installed again under another name is one Play Solo row.
static void test_fold_store_copies()
{
	using Copy = APSoloStoreCopy;
	// Two names of one track: the newest install stays, whatever the pin order.
	std::vector<Copy> copies = {{"generic-slot", "1.0.0", "u1", "aa", 100}, {"my-name", "1.0.0", "u1", "bb", 200}};
	auto into = AP_SoloFoldStoreCopies(copies);
	CHECK(into[0] == 1 && into[1] == 1);
	// Importing the earlier name again makes it the newest.
	copies[0].installed = 300;
	into = AP_SoloFoldStoreCopies(copies);
	CHECK(into[0] == 0 && into[1] == 0);
	// Same time: the larger pin stays, the same on every pass.
	copies[0].installed = copies[1].installed = 5;
	into = AP_SoloFoldStoreCopies(copies);
	CHECK(into[0] == 1 && into[1] == 1);
	// Three copies fold to one; another revision, another uuid and a copy without a uuid stay.
	copies = {{"a", "1.0.0", "u1", "p1", 1}, {"b", "1.0.0", "u1", "p2", 3}, {"c", "1.0.0", "u1", "p3", 2},
	          {"a", "1.1.0", "u1", "p4", 1}, {"d", "1.0.0", "u2", "p5", 1}, {"e", "1.0.0", "", "p6", 1},
	          {"f", "1.0.0", "", "p7", 9}};
	into = AP_SoloFoldStoreCopies(copies);
	CHECK(into[0] == 1 && into[1] == 1 && into[2] == 1);
	CHECK(into[3] == 3 && into[4] == 4 && into[5] == 5 && into[6] == 6);
	CHECK(AP_SoloFoldStoreCopies({}).empty());
}

int main()
{
	test_parse_lines();
	test_take_lines();
	test_room_id();
	test_journal();
	test_identity();
	test_stop_and_timeout();
	test_error_text();
	test_options();
	test_option_steps_and_help();
	test_checklists_and_guards();
	test_tracks();
	test_needs_laps();
	test_store_descriptor_rescan();
	test_fold_store_copies();
	if (g_fail)
	{
		std::printf("test-solo-logic: %d failure(s)\n", g_fail);
		return 1;
	}
	std::printf("test-solo-logic: all checks passed\n");
	return 0;
}
