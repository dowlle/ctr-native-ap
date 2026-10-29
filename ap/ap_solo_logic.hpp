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

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

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

// The --track and --guarantee arguments for the picks (id, pick 0 off, 1 in
// the pool, 2 own pad). They go with a preset's options and with an imported
// or inbox YAML alike (round 2 decision 5); a YAML gets no --set arguments.
static inline std::vector<std::string> AP_SoloTrackArgs(const std::vector<std::pair<std::string, int>> &picks)
{
	std::vector<std::string> out;
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
// "options as set" line follow the start: a preset came by way of its
// options screen, a YAML straight from the start list.
static inline bool AP_SoloTracksViaOptions(bool isPreset)
{
	return isPreset;
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
	std::string value;                // shown and passed as --set name=value
	std::string initial;              // the preset's value
	std::string forced;               // non-empty: fixed to this value
	std::string forcedReason;
	nlohmann::json visibleWhen;       // {"other": ["v1", "v2"]}
	nlohmann::json warnWhen;
	std::string warnReason;
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

// Build the rows. Options the overlay names but the world lacks are skipped
// (the apworld test fails on that anyway). Only toggle, choice and range rows.
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
			row.label = f.value("display_name", row.name);
			row.group = group;
			row.kind = f.value("kind", std::string());
			row.doc = f.value("doc", std::string());
			if (row.kind == "named_range")
				row.kind = "range";
			if (row.kind == "text_choice")
				row.kind = "choice";
			if (row.kind != "toggle" && row.kind != "choice" && row.kind != "range")
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
		long long v = std::strtoll(row.value.c_str(), nullptr, 10) + (direction > 0 ? 1 : -1);
		if (v > row.hi)
			v = row.lo;
		if (v < row.lo)
			v = row.hi;
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
			// Quote choice names so YAML reads "off" as a string, not false.
			const std::string v = r.kind == "choice" ? "\"" + r.value + "\"" : r.value;
			out.push_back(r.name + "=" + v);
		}
	}
	return out;
}
