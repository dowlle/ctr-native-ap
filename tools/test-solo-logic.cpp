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
		CHECK(AP_SoloTrackArgs(Picks{{"package/a/1", 0}, {"package/b/1", 0}}).empty());
		CHECK((AP_SoloTrackArgs(Picks{{"package/a/1", 1}}) == Args{"--track", "package/a/1"}));
		CHECK((AP_SoloTrackArgs(Picks{{"package/a/1", 2}}) ==
		       Args{"--track", "package/a/1", "--guarantee", "package/a/1"}));
		CHECK((AP_SoloTrackArgs(Picks{{"package/a/1", 1}, {"package/b/1", 0}, {"package/c/2", 2}}) ==
		       Args{"--track", "package/a/1", "--track", "package/c/2", "--guarantee", "package/c/2"}));
		CHECK(AP_SoloTrackArgs(Picks{{"package/a/1", 9}, {"package/b/1", -1}}).empty());
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
	test_tracks();
	if (g_fail)
	{
		std::printf("test-solo-logic: %d failure(s)\n", g_fail);
		return 1;
	}
	std::printf("test-solo-logic: all checks passed\n");
	return 0;
}
