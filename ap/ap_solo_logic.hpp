// Solo mode (#89): pure logic shared by ap_solo.cpp, ap_net.cpp and the host
// harness tools/test-solo-logic.cpp. No SDL, no sockets, no files.
//
// The client runs <client>/solo-runtime/ctr_solo.py with the bundled Python.
// The wrapper answers in JSON lines on stdout:
//   {"event":"progress","phase":"Placing items"}
//   {"event":"result", ...command specific...}
//   {"event":"error","kind":"option","message":"...","option":"oxide_goal"}
//   {"event":"ready","port":38301,"password":"...","seed_name":"...","slot":"...","room_id":"..."}
//   {"event":"saved","ok":true}
//   {"event":"stopped","reason":"exit"}
// Anything that is not one JSON object per line is ignored.
#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

// Package ids freeze the content key between the first and last slash. Two
// revisions of that content cannot occupy one seed's track entry together.
static inline bool AP_SoloTrackSameContent(const std::string &a, const std::string &b)
{
	if (a.compare(0, 8, "package/") != 0 || b.compare(0, 8, "package/") != 0)
		return false;
	const size_t endA = a.rfind('/'), endB = b.rfind('/');
	return endA > 8 && endA == endB && endA + 1 < a.size() && endB + 1 < b.size() &&
	       a.compare(0, endA, b, 0, endB) == 0;
}

enum APSoloEventKind
{
	AP_SOLO_EV_NONE = 0,   // garbage or unknown: ignore
	AP_SOLO_EV_PROGRESS,
	AP_SOLO_EV_RESULT,
	AP_SOLO_EV_ERROR,
	AP_SOLO_EV_READY,
	AP_SOLO_EV_SAVED,
	AP_SOLO_EV_STOPPED,
};

struct APSoloEvent
{
	APSoloEventKind kind = AP_SOLO_EV_NONE;
	std::string phase;      // progress
	std::string errorKind;  // error: yaml, game, players, option, fill, runtime, room, exists, running, ports, internal ...
	std::string message;    // error
	std::string option;     // error: the option to highlight, may be empty
	int port = 0;           // ready / error running
	std::string password;   // ready
	std::string seedName;   // ready
	std::string slot;       // ready
	std::string roomId;     // ready
	nlohmann::json body;    // the whole object (result payloads)
};

static inline std::string AP_SoloJsonString(const nlohmann::json &obj, const char *key)
{
	auto it = obj.find(key);
	if (it == obj.end() || !it->is_string())
		return std::string();
	return it->get<std::string>();
}

static inline int AP_SoloJsonInt(const nlohmann::json &obj, const char *key, int fallback)
{
	auto it = obj.find(key);
	if (it == obj.end() || !it->is_number_integer())
		return fallback;
	return it->get<int>();
}

// Parse one stdout line. Never throws.
static inline APSoloEvent AP_SoloParseLine(const std::string &line)
{
	APSoloEvent ev;
	nlohmann::json obj = nlohmann::json::parse(line, nullptr, false);
	if (obj.is_discarded() || !obj.is_object())
		return ev;
	const std::string event = AP_SoloJsonString(obj, "event");
	if (event == "progress")
	{
		ev.kind = AP_SOLO_EV_PROGRESS;
		ev.phase = AP_SoloJsonString(obj, "phase");
	}
	else if (event == "result")
		ev.kind = AP_SOLO_EV_RESULT;
	else if (event == "error")
	{
		ev.kind = AP_SOLO_EV_ERROR;
		ev.errorKind = AP_SoloJsonString(obj, "kind");
		ev.message = AP_SoloJsonString(obj, "message");
		ev.option = AP_SoloJsonString(obj, "option");
		ev.port = AP_SoloJsonInt(obj, "port", 0);
		if (ev.errorKind.empty())
			ev.errorKind = "internal";
	}
	else if (event == "ready")
	{
		ev.kind = AP_SOLO_EV_READY;
		ev.port = AP_SoloJsonInt(obj, "port", 0);
		ev.password = AP_SoloJsonString(obj, "password");
		ev.seedName = AP_SoloJsonString(obj, "seed_name");
		ev.slot = AP_SoloJsonString(obj, "slot");
		ev.roomId = AP_SoloJsonString(obj, "room_id");
		// A ready line without a usable port or seed is not a ready line.
		if (ev.port <= 0 || ev.port > 65535 || ev.seedName.empty() || ev.slot.empty())
			ev.kind = AP_SOLO_EV_NONE;
	}
	else if (event == "saved")
		ev.kind = AP_SOLO_EV_SAVED;
	else if (event == "stopped")
		ev.kind = AP_SOLO_EV_STOPPED;
	if (ev.kind != AP_SOLO_EV_NONE)
		ev.body = std::move(obj);
	return ev;
}

// Splits a growing stdout buffer into complete lines. Keeps the unfinished
// tail. A single line longer than maxLine is dropped (a broken child must not
// grow the buffer without bound).
static inline std::vector<std::string> AP_SoloTakeLines(std::string &buffer, size_t maxLine = 1u << 20)
{
	std::vector<std::string> lines;
	size_t start = 0;
	for (;;)
	{
		size_t nl = buffer.find('\n', start);
		if (nl == std::string::npos)
			break;
		std::string line = buffer.substr(start, nl - start);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		lines.push_back(std::move(line));
		start = nl + 1;
	}
	buffer.erase(0, start);
	if (buffer.size() > maxLine)
		buffer.clear();
	return lines;
}

// A room id is the first 16 hex digits of the room file's SHA-256.
static inline bool AP_SoloRoomIdValid(const std::string &id)
{
	if (id.size() != 16)
		return false;
	for (char c : id)
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return false;
	return true;
}

// The solo server's port range (design: above the 38281 to 38300 range of
// the hosted test servers). A ready line naming another port is not dialed.
#define AP_SOLO_PORT_FIRST 38301
#define AP_SOLO_PORT_LAST 38320

static inline bool AP_SoloPortAllowed(int port)
{
	return port >= AP_SOLO_PORT_FIRST && port <= AP_SOLO_PORT_LAST;
}

// Check journal record: "<code> <code>\n", the location id written twice. A
// torn write (a crash mid-line, possibly followed by the next record on the
// same line) can never pass as a record, because both halves must be equal
// decimal numbers on one complete line.
static inline std::string AP_SoloJournalRecord(int64_t code)
{
	const std::string n = std::to_string((long long)code);
	return n + " " + n + "\n";
}

static inline bool AP_SoloJournalDigits(const std::string &s)
{
	if (s.empty() || s.size() > 19)
		return false;
	for (char c : s)
		if (c < '0' || c > '9')
			return false;
	return true;
}

// Check journal: one record per line, appended before the check is sent.
// Lines that are not a complete record (a torn write) are skipped.
static inline std::vector<int64_t> AP_SoloJournalParse(const std::string &text)
{
	std::vector<int64_t> codes;
	std::set<int64_t> seen;
	size_t start = 0;
	while (start < text.size())
	{
		size_t nl = text.find('\n', start);
		if (nl == std::string::npos)
			break; // unfinished last line: not trusted
		std::string line = text.substr(start, nl - start);
		start = nl + 1;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		const size_t space = line.find(' ');
		if (space == std::string::npos)
			continue;
		const std::string first = line.substr(0, space), second = line.substr(space + 1);
		if (first != second || !AP_SoloJournalDigits(first))
			continue;
		const int64_t code = (int64_t)std::strtoll(first.c_str(), nullptr, 10);
		if (code > 0 && seen.insert(code).second)
			codes.push_back(code);
	}
	return codes;
}

// The checks to resend on connect: journalled, still missing on the server.
// A code the server does not know (not checked, not missing) is never sent.
static inline std::vector<int64_t> AP_SoloResendSet(const std::vector<int64_t> &journal,
                                                    const std::set<int64_t> &checked,
                                                    const std::set<int64_t> &missing)
{
	std::vector<int64_t> out;
	for (int64_t code : journal)
		if (checked.count(code) == 0 && missing.count(code) != 0)
			out.push_back(code);
	return out;
}

// Session identity: after connecting, the server's RoomInfo seed must equal
// the room's seed. Empty means not known yet.
enum APSoloIdentity
{
	AP_SOLO_ID_PENDING = 0,
	AP_SOLO_ID_MATCH,
	AP_SOLO_ID_FOREIGN,
};

static inline APSoloIdentity AP_SoloIdentity(const std::string &serverSeed, const std::string &roomSeed)
{
	if (serverSeed.empty())
		return AP_SOLO_ID_PENDING;
	return serverSeed == roomSeed ? AP_SOLO_ID_MATCH : AP_SOLO_ID_FOREIGN;
}

// The message the menu shows for an error event, by kind (design note,
// "Invalid options and other errors").
static inline std::string AP_SoloErrorText(const std::string &kind, const std::string &message)
{
	if (kind == "link")
		return message;
	if (kind == "runtime" || kind == "internal" || kind == "start")
		return "Solo runtime is damaged. Reinstall or update the client.";
	if (kind == "fill" || kind == "generate")
		return "This combination could not be generated. Try again or change options.";
	if (kind == "timeout")
		return "Generation took too long. Try fewer custom tracks or a smaller preset.";
	if (kind == "foreign")
		return message.empty() ? "Another program is using the solo port." : message;
	return message.empty() ? "The solo runtime refused this." : message;
}

// ---------------------------------------------------------------------------
// Custom-track step (decision 32)

// The modes a track can run, from the wrapper's list ("trophy", "ctr",
// "relic"), as the step shows them.
static inline std::string AP_SoloTrackModes(const nlohmann::json &modes)
{
	std::string out;
	if (!modes.is_array())
		return out;
	for (const auto &m : modes)
	{
		if (!m.is_string())
			continue;
		const std::string mode = m.get<std::string>();
		const char *label = mode == "trophy" ? "Trophy" : mode == "ctr" ? "CTR" : mode == "relic" ? "Relic" : nullptr;
		if (!label)
			continue;
		if (!out.empty())
			out += ", ";
		out += label;
	}
	return out;
}

// Cross on a track: off (0), in the pool (1), guaranteed a pad (2), off.
static inline int AP_SoloTrackNextPick(int pick)
{
	return pick >= 0 && pick < 2 ? pick + 1 : 0;
}

// How the YAML has a track: its starting pick. Anything else starts off.
static inline int AP_SoloTrackStart(const nlohmann::json &track)
{
	const int start = track.is_object() ? track.value("start", 0) : 0;
	return start == 1 || start == 2 ? start : 0;
}

// The --track and --guarantee arguments for the picks (id, pick 0 off, 1 in
// the pool, 2 own pad). They go with a preset's options and with an imported
// or inbox YAML alike (round 2 decision 5); a YAML gets no --set arguments.
static inline std::vector<std::string> AP_SoloTrackArgs(const std::vector<std::pair<std::string, int>> &picks)
{
	std::vector<std::string> out;
	if (!picks.empty())
		out.push_back("--tracks-page"); // a listed track means the page was shown
	for (const auto &p : picks)
	{
		if (p.second != 1 && p.second != 2)
			continue;
		out.push_back("--track");
		out.push_back(p.first);
		if (p.second == 2)
		{
			out.push_back("--guarantee");
			out.push_back(p.first);
		}
	}
	return out;
}

// Back from Custom Tracks, a refused pick's message and the Review page's
// "options as set" line: every YAML now comes through its options screen.
static inline bool AP_SoloTracksViaOptions(bool /*isPreset*/)
{
	return true;
}

// solo/tracks/<track key>__<revision>.json, or "" when either part is not
// the plain text a package descriptor allows (ap_content_plan_impl.hpp: a
// track key is lowercase letters and digits in hyphenated groups; a revision
// starts with a letter or digit and then has only those and . _ + -). The
// key has no "_", so "__" cannot be part of it.
static inline std::string AP_SoloDescriptorFileName(const std::string &key, const std::string &revision)
{
	if (key.empty() || key.size() > 64 || revision.empty() || revision.size() > 64)
		return std::string();
	for (char c : key)
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
			return std::string();
	if (key.front() == '-' || key.back() == '-' || key.find("--") != std::string::npos)
		return std::string();
	for (char c : revision)
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
		      c == '-' || c == '+'))
			return std::string();
	if (!std::isalnum((unsigned char)revision.front()))
		return std::string();
	return key + "__" + revision + ".json";
}

// A store rescan may skip the atomic writer only when the complete descriptor
// already matches. Authored race settings can change without changing LEV/VRM.
static inline bool AP_SoloDescriptorUnchanged(const nlohmann::json &old, const nlohmann::json &fresh)
{
	return old.is_object() && fresh.is_object() && old == fresh;
}

// Laps the player picks for an imported track without them (ruling of
// 2026-11-02): the xdelta importer keeps such a track's descriptor without
// "laps" in solo/needs-laps, the Custom Tracks page lists it, and Left/Right
// pick 1 to 7 laps (the descriptor's range, worlds/ctr/custom_packages.py),
// starting at 3, retail's race length.
#define AP_SOLO_LAPS_MIN 1
#define AP_SOLO_LAPS_MAX 7
#define AP_SOLO_LAPS_DEFAULT 3

// A needs-laps reinstall refreshes package metadata but leaves the player's
// chosen lap count in the complete descriptor. Null means incompatible pins
// or an invalid old choice; the caller must leave the old file alone.
static inline nlohmann::json AP_SoloMergeNeedsLaps(const nlohmann::json &old, const nlohmann::json &fresh)
{
	if (!old.is_object() || !fresh.is_object() || !old.contains("files") || !fresh.contains("files") ||
	    old["files"] != fresh["files"] || !old.contains("laps") || !old["laps"].is_number_integer() ||
	    old["laps"] < AP_SOLO_LAPS_MIN || old["laps"] > AP_SOLO_LAPS_MAX)
		return nullptr;
	auto merged = fresh;
	merged["laps"] = old["laps"];
	return merged;
}

// Left (-1) or Right (+1) on the lap count: it stops at both ends.
static inline int AP_SoloLapsStep(int laps, int direction)
{
	laps += direction < 0 ? -1 : direction > 0 ? 1 : 0;
	return laps < AP_SOLO_LAPS_MIN ? AP_SOLO_LAPS_MIN : laps > AP_SOLO_LAPS_MAX ? AP_SOLO_LAPS_MAX : laps;
}

// A descriptor kept in solo/needs-laps: an object without "laps" whose id is
// package/<content_id>/<revision>, with a title and an author. Its file name
// is AP_SoloDescriptorFileName of the two; anything else is not listed.
static inline bool AP_SoloNeedsLapsDescriptor(const nlohmann::json &d, const std::string &fileName)
{
	if (!d.is_object() || d.contains("laps"))
		return false;
	const std::string key = AP_SoloJsonString(d, "content_id"), revision = AP_SoloJsonString(d, "revision");
	const std::string name = AP_SoloDescriptorFileName(key, revision);
	return !name.empty() && name == fileName && AP_SoloJsonString(d, "id") == "package/" + key + "/" + revision &&
	       !AP_SoloJsonString(d, "display_name").empty() && !AP_SoloJsonString(d, "author").empty();
}

// The descriptor for solo/tracks: the kept one with the picked laps added and
// nothing else changed, or "" when the laps are out of range.
static inline std::string AP_SoloDescriptorWithLaps(const nlohmann::json &d, int laps)
{
	if (!d.is_object() || d.contains("laps") || laps < AP_SOLO_LAPS_MIN || laps > AP_SOLO_LAPS_MAX)
		return std::string();
	nlohmann::json out = d;
	out["laps"] = laps;
	return out.dump();
}

// Stop sequence for a running server: /save and /exit on stdin, then wait,
// then end the child. Returns what to do this tick.
enum APSoloStopStep
{
	AP_SOLO_STOP_WAIT = 0,
	AP_SOLO_STOP_KILL,
	AP_SOLO_STOP_DONE,
};

static inline APSoloStopStep AP_SoloStopStep(bool exited, uint64_t elapsedMs, uint64_t graceMs = 5000)
{
	if (exited)
		return AP_SOLO_STOP_DONE;
	return elapsedMs >= graceMs ? AP_SOLO_STOP_KILL : AP_SOLO_STOP_WAIT;
}

// Generation hard limit (design: 120 s).
static inline bool AP_SoloJobTimedOut(uint64_t elapsedMs, uint64_t limitMs = 120000)
{
	return elapsedMs >= limitMs;
}

// ---------------------------------------------------------------------------
// Options screen model: describe result (option facts) + overlay (which rows,
// groups, rules) + values (the preset's current values).

struct APSoloOption
{
	std::string name, label, group, kind, doc;
	std::vector<std::string> choices; // choice names, in value order
	int lo = 0, hi = 0;               // range
	int step = 1;                     // range: how far Left / Right move (overlay rule "step")
	std::string value;                // shown and passed as --set name=value
	std::string initial;              // the preset's value
	bool previewOnly = false;        // untouched weighted/random expression
	std::string forced;               // non-empty: fixed to this value
	std::string forcedReason;
	nlohmann::json visibleWhen;       // {"other": ["v1", "v2"]}
	nlohmann::json warnWhen;
	std::string warnReason;
	// kind "set" (a checklist, A2-41): the rows, their labels and which rows
	// the option's default checks. `value` is the checked keys as a JSON array
	// in row order, which is also what --set passes.
	std::vector<std::string> keys, keyLabels;
	std::vector<bool> keyDefault;
	// kind "weights" (a dict of per-key numbers, A3-22): one number row per
	// key, 0 to weightMax. `value` is the whole table as a JSON object in row
	// order, which is also what --set passes when a weight changed. Written
	// whole, never per key: apply_overrides replaces the option, and a partial
	// table would drop weights a YAML set and the player left alone.
	std::vector<int> keyWeights, keyDefaultWeights;
	// The weights the screen started with (the preset's or the YAML's table):
	// OptionsReset returns to these, not to the option's defaults.
	std::vector<int> keyInitialWeights;
	int weightMax = 0;
	// Overlay guards on this row (also on the set a count guard names): each
	// one refuses generation on the screen, as the world would refuse it.
	std::vector<nlohmann::json> guards;
};

static inline std::string AP_SoloValueText(const nlohmann::json &v)
{
	if (v.is_boolean())
		return v.get<bool>() ? "true" : "false";
	if (v.is_number_integer())
		return std::to_string(v.get<long long>());
	if (v.is_string())
		return v.get<std::string>();
	return std::string();
}

// Option help as the apworld writes it is hard-wrapped Markdown. The help page
// wraps by width, so single line breaks become spaces, a blank line or a list
// item starts a new line, and the Markdown marks (** and `) are dropped.
static inline std::string AP_SoloHelpText(const std::string &doc)
{
	std::string out;
	size_t i = 0;
	while (i < doc.size())
	{
		size_t end = doc.find('\n', i);
		if (end == std::string::npos)
			end = doc.size();
		std::string line = doc.substr(i, end - i);
		i = end + 1;
		std::string clean;
		for (size_t k = 0; k < line.size(); k++)
		{
			if (line[k] == '`' || (line[k] == '*' && k + 1 < line.size() && line[k + 1] == '*'))
			{
				if (line[k] == '*')
					k++;
				continue;
			}
			clean += line[k];
		}
		size_t b = clean.find_first_not_of(" \t"), e = clean.find_last_not_of(" \t\r");
		if (b == std::string::npos)
		{
			if (!out.empty() && out.back() != '\n')
				out += '\n';
			continue;
		}
		clean = clean.substr(b, e - b + 1);
		const bool item = clean.size() > 1 && (clean[0] == '-' || clean[0] == '*') && clean[1] == ' ';
		if (!out.empty() && out.back() != '\n')
			out += item ? '\n' : ' ';
		out += clean;
	}
	while (!out.empty() && out.back() == '\n')
		out.pop_back();
	return out;
}

// The checked keys of a set row, in row order.
static inline std::vector<std::string> AP_SoloSetEntries(const APSoloOption &row)
{
	std::vector<std::string> out;
	const nlohmann::json v = nlohmann::json::parse(row.value, nullptr, false);
	if (v.is_array())
		for (const auto &e : v)
			if (e.is_string())
				out.push_back(e.get<std::string>());
	return out;
}

static inline bool AP_SoloSetHas(const APSoloOption &row, const std::string &key)
{
	for (const std::string &e : AP_SoloSetEntries(row))
		if (e == key)
			return true;
	return false;
}

// The set's value from checked keys: a JSON array in row order, so the same
// choice always gives the same text (changed is a plain compare).
static inline std::string AP_SoloSetValue(const APSoloOption &row, const std::vector<std::string> &checked)
{
	nlohmann::json out = nlohmann::json::array();
	for (const std::string &k : row.keys)
		if (std::find(checked.begin(), checked.end(), k) != checked.end())
			out.push_back(k);
	return out.dump();
}

// Cross on checklist row `item`: check or uncheck it.
static inline void AP_SoloSetToggle(APSoloOption &row, int item)
{
	if (row.kind != "set" || !row.forced.empty() || item < 0 || item >= (int)row.keys.size())
		return;
	std::vector<std::string> checked = AP_SoloSetEntries(row);
	auto at = std::find(checked.begin(), checked.end(), row.keys[item]);
	if (at != checked.end())
		checked.erase(at);
	else
		checked.push_back(row.keys[item]);
	row.value = AP_SoloSetValue(row, checked);
}

// "All 7", "None" or "4 of 7": the set row's value on the group page.
static inline std::string AP_SoloSetSummary(const APSoloOption &row)
{
	const size_t on = AP_SoloSetEntries(row).size(), all = row.keys.size();
	if (on == 0)
		return "None";
	if (on == all)
		return "All " + std::to_string(all);
	return std::to_string(on) + " of " + std::to_string(all);
}

// The weights table as a JSON object in row order, so the same choice always
// gives the same text (changed is a plain compare) and --set passes it as is.
static inline std::string AP_SoloWeightsValue(const APSoloOption &row)
{
	nlohmann::ordered_json out = nlohmann::ordered_json::object();
	for (size_t i = 0; i < row.keys.size(); i++)
		out[row.keys[i]] = i < row.keyWeights.size() ? row.keyWeights[i] : 0;
	return out.dump();
}

// A JSON number as a weight: clamp before narrowing, so a huge value lands on
// the maximum instead of wrapping to the wrong end. 0, negatives and NaN are 0.
static inline int AP_SoloClampWeight(double w, int weightMax)
{
	if (!(w > 0))
		return 0;
	if (w >= weightMax)
		return weightMax;
	return (int)w;
}

// Left (-1) / Right (+1) on weight row `item`: wraps between 0 and the
// option's per-trap maximum, like a range row wraps.
static inline void AP_SoloWeightStep(APSoloOption &row, int item, int direction)
{
	if (row.kind != "weights" || !row.forced.empty() || direction == 0 || item < 0 ||
	    item >= (int)row.keyWeights.size())
		return;
	int &w = row.keyWeights[item];
	if (direction > 0)
		w = w >= row.weightMax ? 0 : w + 1;
	else
		w = w <= 0 ? row.weightMax : w - 1;
	row.value = AP_SoloWeightsValue(row);
}

// Cross on weight row `item`: back to the option's default weight.
static inline void AP_SoloWeightReset(APSoloOption &row, int item)
{
	if (row.kind != "weights" || !row.forced.empty() || item < 0 || item >= (int)row.keyWeights.size() ||
	    item >= (int)row.keyDefaultWeights.size())
		return;
	row.keyWeights[item] = row.keyDefaultWeights[item];
	row.value = AP_SoloWeightsValue(row);
}

// "Defaults" or "2 custom": the weights row's value on the group page.
static inline std::string AP_SoloWeightsSummary(const APSoloOption &row)
{
	int custom = 0;
	for (size_t i = 0; i < row.keyWeights.size(); i++)
		if (i >= row.keyDefaultWeights.size() || row.keyWeights[i] != row.keyDefaultWeights[i])
			custom++;
	return custom ? std::to_string(custom) + " custom" : "Defaults";
}

// Build the rows. Options the overlay names but the world lacks are skipped
// (the apworld test fails on that anyway). Toggle, choice and range rows, and
// set rows the overlay gives a checklist.
static inline std::vector<APSoloOption> AP_SoloBuildOptions(const nlohmann::json &describe)
{
	std::vector<APSoloOption> rows;
	if (!describe.is_object())
		return rows;
	std::map<std::string, nlohmann::json> facts;
	auto opts = describe.find("options");
	if (opts != describe.end() && opts->is_array())
		for (const auto &o : *opts)
			if (o.is_object() && o.contains("name") && o["name"].is_string())
				facts[o["name"].get<std::string>()] = o;
	const nlohmann::json overlay = describe.value("overlay", nlohmann::json::object());
	const nlohmann::json values = describe.value("values", nlohmann::json::object());
	std::map<std::string, nlohmann::json> rules;
	if (overlay.contains("rules") && overlay["rules"].is_array())
		for (const auto &r : overlay["rules"])
			if (r.is_object() && r.contains("option") && r["option"].is_string())
				rules[r["option"].get<std::string>()] = r;
	if (!overlay.contains("groups") || !overlay["groups"].is_array())
		return rows;
	for (const auto &g : overlay["groups"])
	{
		if (!g.is_object() || !g.contains("options") || !g["options"].is_array())
			continue;
		const std::string group = g.value("label", std::string());
		for (const auto &n : g["options"])
		{
			if (!n.is_string() || !facts.count(n.get<std::string>()))
				continue;
			const nlohmann::json &f = facts[n.get<std::string>()];
			APSoloOption row;
			row.name = n.get<std::string>();
			if (describe.contains("unresolved") && describe["unresolved"].is_array())
				for (const auto &name : describe["unresolved"])
					row.previewOnly = row.previewOnly || (name.is_string() && name.get<std::string>() == row.name);
			row.label = f.value("display_name", row.name);
			row.group = group;
			row.kind = f.value("kind", std::string());
			row.doc = AP_SoloHelpText(f.value("doc", std::string()));
			if (row.kind == "named_range")
				row.kind = "range";
			if (row.kind == "text_choice")
				row.kind = "choice";
			const nlohmann::json checklists = overlay.value("checklists", nlohmann::json::object());
			if (row.kind == "set" && checklists.is_object() && checklists.contains(row.name) &&
			    checklists[row.name].is_array())
			{
				for (const auto &r : checklists[row.name])
					if (r.is_array() && !r.empty() && r[0].is_string())
					{
						row.keys.push_back(r[0].get<std::string>());
						row.keyLabels.push_back(r.size() > 1 && r[1].is_string() ? r[1].get<std::string>()
						                                                          : r[0].get<std::string>());
					}
			}
			else if (row.kind == "dict" && f.contains("weight_rows") && f["weight_rows"].is_array() &&
			         !f["weight_rows"].empty())
			{
				// A3-22: a dict the screen edits as one number row per key
				// (trap weights). The runtime lists the rows from the world's
				// own registry, so a new trap appears without a change here.
				row.kind = "weights";
				for (const auto &r : f["weight_rows"])
					if (r.is_object() && r.contains("key") && r["key"].is_string())
					{
						row.keys.push_back(r["key"].get<std::string>());
						row.keyLabels.push_back(r.contains("label") && r["label"].is_string()
						                            ? r["label"].get<std::string>()
						                            : row.keys.back());
					}
				row.weightMax = std::max(1, f.value("weight_max", 100));
				if (row.keys.empty())
					continue;
			}
			else if (row.kind != "toggle" && row.kind != "choice" && row.kind != "range")
				continue;
			if (row.kind == "choice" && f.contains("choices") && f["choices"].is_array())
				for (const auto &c : f["choices"])
					if (c.is_object() && c.contains("name") && c["name"].is_string())
						row.choices.push_back(c["name"].get<std::string>());
			if (row.kind == "range")
			{
				row.lo = f.value("range_start", 0);
				row.hi = f.value("range_end", 0);
			}
			nlohmann::json v = values.contains(row.name) ? values[row.name] : f.value("default", nlohmann::json());
			if (row.kind == "weights")
			{
				// Every row shows a number: the YAML's table overlaid on the
				// option's default table (the runtime sends it merged). Values
				// outside 0..max are clamped to what the world accepts.
				const nlohmann::json d = f.value("default", nlohmann::json::object());
				for (const std::string &k : row.keys)
				{
					int def = 0;
					if (d.is_object() && d.contains(k) && d[k].is_number())
						def = AP_SoloClampWeight(d[k].get<double>(), row.weightMax);
					row.keyDefaultWeights.push_back(def);
					int w = def;
					if (v.is_object() && v.contains(k) && v[k].is_number())
						w = AP_SoloClampWeight(v[k].get<double>(), row.weightMax);
					row.keyWeights.push_back(w);
				}
				row.keyInitialWeights = row.keyWeights;
				row.value = AP_SoloWeightsValue(row);
			}
			else if (row.kind == "set")
			{
				// A YAML entry without a row (a custom track in the boss track
				// pool) becomes a row of its own, so it is kept and shown.
				std::vector<std::string> checked;
				if (v.is_array())
					for (const auto &e : v)
						if (e.is_string())
						{
							checked.push_back(e.get<std::string>());
							if (std::find(row.keys.begin(), row.keys.end(), checked.back()) == row.keys.end())
							{
								row.keys.push_back(checked.back());
								row.keyLabels.push_back(checked.back());
							}
						}
				const nlohmann::json d = f.value("default", nlohmann::json::array());
				for (const std::string &k : row.keys)
				{
					bool on = false;
					if (d.is_array())
						for (const auto &e : d)
							on = on || (e.is_string() && e.get<std::string>() == k);
					row.keyDefault.push_back(on);
				}
				row.value = AP_SoloSetValue(row, checked);
			}
			else
				row.value = AP_SoloValueText(v);
			if (row.kind == "toggle" && row.value != "true")
				row.value = "false";
			auto rule = rules.find(row.name);
			if (rule != rules.end())
			{
				const nlohmann::json &r = rule->second;
				if (r.contains("forced"))
				{
					row.forced = AP_SoloValueText(r["forced"]);
					row.forcedReason = r.value("reason", std::string());
					row.value = row.forced;
				}
				if (r.contains("label") && r["label"].is_string() && !r["label"].get<std::string>().empty())
					row.label = r["label"].get<std::string>();
				if (r.contains("step") && r["step"].is_number_integer())
					row.step = std::max(1, r["step"].get<int>());
				if (r.contains("visible_when"))
					row.visibleWhen = r["visible_when"];
				if (r.contains("warn_when"))
				{
					row.warnWhen = r["warn_when"];
					row.warnReason = r.value("reason", std::string());
				}
			}
			row.initial = row.value;
			rows.push_back(std::move(row));
		}
	}
	// Guards: on their own row, and a count guard also on the set it counts.
	if (overlay.contains("guards") && overlay["guards"].is_array())
		for (const auto &g : overlay["guards"])
		{
			if (!g.is_object() || !g.contains("option") || !g["option"].is_string())
				continue;
			for (auto &row : rows)
				if (row.name == g["option"].get<std::string>() ||
				    (g.contains("at_most_count_of") && g["at_most_count_of"].is_string() &&
				     row.name == g["at_most_count_of"].get<std::string>()))
					row.guards.push_back(g);
		}
	return rows;
}

static inline const APSoloOption *AP_SoloFindOption(const std::vector<APSoloOption> &rows, const std::string &name)
{
	for (const auto &r : rows)
		if (r.name == name)
			return &r;
	return nullptr;
}

// {"other": ["a", "b"]}: true when every named option's value is listed.
static inline bool AP_SoloConditionHolds(const std::vector<APSoloOption> &rows, const nlohmann::json &cond)
{
	if (!cond.is_object())
		return true;
	for (auto it = cond.begin(); it != cond.end(); ++it)
	{
		if (!it.value().is_array())
			continue;
		const APSoloOption *other = AP_SoloFindOption(rows, it.key());
		if (!other)
			return false;
		bool listed = false;
		for (const auto &v : it.value())
			listed = listed || AP_SoloValueText(v) == other->value;
		if (!listed)
			return false;
	}
	return true;
}

static inline bool AP_SoloOptionVisible(const std::vector<APSoloOption> &rows, const APSoloOption &row)
{
	if (row.visibleWhen.is_object())
		for (auto it = row.visibleWhen.begin(); it != row.visibleWhen.end(); ++it)
		{
			const APSoloOption *other = AP_SoloFindOption(rows, it.key());
			if (other && other->previewOnly && other->value == other->initial)
				return true; // the roll may enable this row
		}
	return row.visibleWhen.is_null() || AP_SoloConditionHolds(rows, row.visibleWhen);
}

// warn_when: {"<other>": [values], "value": V, "also_zero": "<option>"}.
// Warns when the conditions hold, this row equals value, and also_zero is 0.
static inline bool AP_SoloOptionWarns(const std::vector<APSoloOption> &rows, const APSoloOption &row)
{
	if (!row.warnWhen.is_object())
		return false;
	nlohmann::json cond = nlohmann::json::object();
	for (auto it = row.warnWhen.begin(); it != row.warnWhen.end(); ++it)
		if (it.value().is_array())
			cond[it.key()] = it.value();
	if (!AP_SoloConditionHolds(rows, cond))
		return false;
	if (row.warnWhen.contains("value") && AP_SoloValueText(row.warnWhen["value"]) != row.value)
		return false;
	if (row.warnWhen.contains("also_zero") && row.warnWhen["also_zero"].is_string())
	{
		const APSoloOption *other = AP_SoloFindOption(rows, row.warnWhen["also_zero"].get<std::string>());
		if (!other || other->value != "0")
			return false;
	}
	return true;
}

// An overlay guard (A2-41), with the world's own limits:
//   {"option": O, "when": {"other": [values]}, "has": K, "at_least": N,
//    "needs_all": [keys], "at_most_count_of": S, "reason": "..."}
// Holds (refuses) when every `when` condition holds, set O has K (if named),
// and O has fewer than N entries, misses a needs_all key, or (a range) is
// above the number of entries in set S.
static inline bool AP_SoloGuardActive(const std::vector<APSoloOption> &rows, const nlohmann::json &guard)
{
	if (!guard.is_object() || !guard.contains("option") || !guard["option"].is_string())
		return false;
	// A preview is not a rolled value. Defer only guards that depend on an
	// untouched expression to generation; edited concrete values still guard.
	std::vector<std::string> dependencies = {guard["option"].get<std::string>()};
	if (guard.contains("at_most_count_of") && guard["at_most_count_of"].is_string())
		dependencies.push_back(guard["at_most_count_of"].get<std::string>());
	if (guard.contains("when") && guard["when"].is_object())
		for (auto it = guard["when"].begin(); it != guard["when"].end(); ++it)
			dependencies.push_back(it.key());
	for (const auto &name : dependencies)
	{
		const APSoloOption *dependency = AP_SoloFindOption(rows, name);
		if (dependency && dependency->previewOnly && dependency->value == dependency->initial)
			return false;
	}
	if (guard.contains("when") && !AP_SoloConditionHolds(rows, guard["when"]))
		return false;
	const APSoloOption *row = AP_SoloFindOption(rows, guard["option"].get<std::string>());
	if (!row)
		return false;
	if (guard.contains("has") && guard["has"].is_string() && !AP_SoloSetHas(*row, guard["has"].get<std::string>()))
		return false;
	if (guard.contains("at_most_count_of") && guard["at_most_count_of"].is_string())
	{
		const APSoloOption *set = AP_SoloFindOption(rows, guard["at_most_count_of"].get<std::string>());
		return set && std::strtoll(row->value.c_str(), nullptr, 10) > (long long)AP_SoloSetEntries(*set).size();
	}
	const std::vector<std::string> entries = AP_SoloSetEntries(*row);
	if (guard.contains("at_least") && guard["at_least"].is_number_integer() &&
	    (long long)entries.size() < guard["at_least"].get<long long>())
		return true;
	if (guard.contains("needs_all") && guard["needs_all"].is_array())
		for (const auto &k : guard["needs_all"])
			if (k.is_string() && std::find(entries.begin(), entries.end(), k.get<std::string>()) == entries.end())
				return true;
	return false;
}

// The reason of the first guard on this row that refuses, or "".
static inline std::string AP_SoloGuardReason(const std::vector<APSoloOption> &rows, const APSoloOption &row)
{
	for (const auto &g : row.guards)
		if (AP_SoloGuardActive(rows, g))
			return g.value("reason", std::string("The world refuses this value."));
	return std::string();
}

// The first row a guard refuses (Continue opens it), or -1.
static inline int AP_SoloFirstBlocked(const std::vector<APSoloOption> &rows)
{
	for (size_t i = 0; i < rows.size(); i++)
		if (!AP_SoloGuardReason(rows, rows[i]).empty())
			return (int)i;
	return -1;
}

// Left (-1) / Right (+1) on a row. Forced rows never change. Wraps.
static inline void AP_SoloOptionStep(APSoloOption &row, int direction)
{
	if (!row.forced.empty() || direction == 0)
		return;
	if (row.kind == "toggle")
		row.value = row.value == "true" ? "false" : "true";
	else if (row.kind == "choice" && !row.choices.empty())
	{
		int n = (int)row.choices.size(), at = 0;
		for (int i = 0; i < n; i++)
			if (row.choices[i] == row.value)
				at = i;
		at = ((at + (direction > 0 ? 1 : -1)) % n + n) % n;
		row.value = row.choices[at];
	}
	else if (row.kind == "range" && row.hi >= row.lo)
	{
		// Moves by `step`, stops at the ends of the range and wraps from one
		// end to the other, so a value off the step grid still reaches both.
		long long v = std::strtoll(row.value.c_str(), nullptr, 10);
		const long long step = row.step > 0 ? row.step : 1;
		if (direction > 0)
			v = v >= row.hi ? row.lo : std::min<long long>(v + step, row.hi);
		else
			v = v <= row.lo ? row.hi : std::max<long long>(v - step, row.lo);
		row.value = std::to_string(v);
	}
}

// --set arguments: every visible row that differs from the preset, plus every
// forced row (the preset may not name it).
static inline std::vector<std::string> AP_SoloSetArgs(const std::vector<APSoloOption> &rows)
{
	std::vector<std::string> out;
	for (const auto &r : rows)
	{
		if (!r.forced.empty() || (AP_SoloOptionVisible(rows, r) && r.value != r.initial))
		{
			// Quote choice names so YAML reads "off" as a string, not false. A
			// set is a JSON array, which YAML reads as the list it is.
			const std::string v = r.kind == "choice" ? "\"" + r.value + "\"" : r.value;
			out.push_back(r.name + "=" + v);
		}
	}
	return out;
}
