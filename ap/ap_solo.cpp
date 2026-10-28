// Solo mode (#89): runtime jobs and the private solo server. See ap_solo.h.
//
// Child processes use SDL3's process API. Their stdout pipe is non-blocking,
// so the game thread polls it every frame; nothing here blocks a frame except
// AP_Solo_Shutdown at quit. A child is only ever ended through its own
// SDL_Process handle, never by name or PID lookup.
#include "ap_solo.h"
#include "ap_solo_logic.hpp"
#include "ap_net.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define AP_SOLO_GETPID() ((long)_getpid())
#else
#include <unistd.h>
#define AP_SOLO_GETPID() ((long)getpid())
#endif

extern "C" {
const char *NativeAssets_GetBaseDir(void);
const char *NativeAssets_GetAssetDir(void);
void AP_LogLine(const char *msg);
void AP_Net_Reconnect(const char *uri, const char *slot, const char *password);
void AP_Net_Shutdown(void);
void ap_net_set_check_journal(const char *path, const char *seed);
}

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
const char *const kInterpreter = "solo-runtime/python/python.exe";
#else
const char *const kInterpreter = "solo-runtime/python/bin/python3.12";
#endif
const char *const kScript = "solo-runtime/ctr_solo.py";
const uint64_t kGenerateLimitMs = 120000;
const uint64_t kQuickLimitMs = 60000;   // list, describe, imports
const uint64_t kReadyLimitMs = 30000;
const uint64_t kConnectLimitMs = 20000;
const size_t kDescriptorMax = 64 * 1024; // the wrapper reads no larger descriptor

void Log(const std::string &line)
{
	std::string out = "[AP SOLO] " + line + "\n";
	AP_LogLine(out.c_str());
}

std::string BaseDir()
{
	const char *base = NativeAssets_GetBaseDir();
	std::string dir = base ? base : "";
	if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
		dir += '/';
	return dir;
}

std::string ClientPath(const char *rel) { return BaseDir() + rel; }
std::string SoloPath(const std::string &rel) { return BaseDir() + "solo/" + rel; }

std::string Interpreter()
{
	// Development only: a checkout without the bundled runtime.
	const char *over = SDL_getenv("CTR_SOLO_PYTHON");
	if (over && *over)
		return over;
	return ClientPath(kInterpreter);
}

// The solo folder must be a real folder in the client folder: a link could
// send rooms, saves and the check journal somewhere else.
bool SoloRootIsLink()
{
	std::error_code ec;
	std::string root = BaseDir() + "solo";
	return fs::is_symlink(fs::u8path(root), ec);
}

bool FileExists(const std::string &path)
{
	std::error_code ec;
	return fs::is_regular_file(fs::u8path(path), ec);
}

// Returns false when src did not fit (the copy is cut).
bool CopyText(char *dst, size_t size, const std::string &src)
{
	if (size == 0)
		return false;
	std::snprintf(dst, size, "%s", src.c_str());
	return src.size() < size;
}

// ---------------------------------------------------------------------------
// One child process with a JSON-lines stdout.

struct Child
{
	SDL_Process *proc = nullptr;
	SDL_IOStream *out = nullptr;
	SDL_IOStream *in = nullptr;
	SDL_IOStream *err = nullptr; // stderr redirected to a log file
	std::string buffer;
	uint64_t started = 0;
	bool exited = false;
	int exitCode = 0;

	bool running() const { return proc != nullptr && !exited; }

	bool start(const std::vector<std::string> &args, const std::string &logPath)
	{
		std::vector<const char *> argv;
		argv.push_back(nullptr); // interpreter, set below
		std::string interp = Interpreter();
		argv[0] = interp.c_str();
		static const char *const flags[] = {"-I", "-B"};
		for (const char *f : flags)
			argv.push_back(f);
		std::string script = ClientPath(kScript);
		argv.push_back(script.c_str());
		for (const auto &a : args)
			argv.push_back(a.c_str());
		argv.push_back(nullptr);

		SDL_Environment *env = SDL_CreateEnvironment(true);
		if (env)
		{
			SDL_SetEnvironmentVariable(env, "PYTHONDONTWRITEBYTECODE", "1", true);
			SDL_SetEnvironmentVariable(env, "PYTHONUTF8", "1", true);
			SDL_SetEnvironmentVariable(env, "PYTHONIOENCODING", "utf-8", true);
			SDL_SetEnvironmentVariable(env, "SKIP_REQUIREMENTS_UPDATE", "1", true);
			SDL_UnsetEnvironmentVariable(env, "PYTHONPATH");
			SDL_UnsetEnvironmentVariable(env, "PYTHONHOME");
		}
		std::error_code ec;
		fs::create_directories(fs::u8path(SoloPath("")), ec);
		err = SDL_IOFromFile(logPath.c_str(), "wb");

		SDL_PropertiesID props = SDL_CreateProperties();
		SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)argv.data());
		if (env)
			SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
		SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
		SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
		if (err)
		{
			SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
			SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_POINTER, err);
		}
		else
			SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
#ifdef _WIN32
		// No console window for python.exe. On POSIX, background means a
		// double fork whose exit code cannot be read, so it stays off there.
		SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
		proc = SDL_CreateProcessWithProperties(props);
		SDL_DestroyProperties(props);
		if (env)
			SDL_DestroyEnvironment(env);
		if (!proc)
		{
			Log(std::string("could not start the runtime: ") + SDL_GetError());
			if (err)
			{
				SDL_CloseIO(err);
				err = nullptr;
			}
			return false;
		}
		out = SDL_GetProcessOutput(proc);
		in = SDL_GetProcessInput(proc);
		started = SDL_GetTicks();
		exited = false;
		exitCode = 0;
		buffer.clear();
		return true;
	}

	// New complete lines since the last call. Detects exit.
	std::vector<std::string> poll()
	{
		std::vector<std::string> lines;
		if (!proc)
			return lines;
		char chunk[4096];
		for (int guard = 0; out && guard < 64; guard++)
		{
			size_t n = SDL_ReadIO(out, chunk, sizeof chunk);
			if (n == 0)
				break;
			buffer.append(chunk, n);
		}
		if (!exited)
		{
			int code = 0;
			if (SDL_WaitProcess(proc, false, &code))
			{
				exited = true;
				exitCode = code;
				for (int guard = 0; out && guard < 256; guard++)
				{
					size_t n = SDL_ReadIO(out, chunk, sizeof chunk);
					if (n == 0)
						break;
					buffer.append(chunk, n);
				}
				if (!buffer.empty() && buffer.back() != '\n')
					buffer += '\n';
			}
		}
		lines = AP_SoloTakeLines(buffer);
		return lines;
	}

	void write(const char *text)
	{
		if (in && running())
		{
			SDL_WriteIO(in, text, std::strlen(text));
			SDL_FlushIO(in);
		}
	}

	void closeInput()
	{
		if (in)
		{
			// The stdin stream belongs to the process; closing it signals EOF,
			// which the server treats as "the client is gone".
			SDL_CloseIO(in);
			in = nullptr;
		}
	}

	void kill() { if (running()) SDL_KillProcess(proc, true); }

	void release()
	{
		if (proc)
		{
			if (!exited)
			{
				SDL_KillProcess(proc, true);
				SDL_WaitProcess(proc, true, nullptr);
			}
			SDL_DestroyProcess(proc);
		}
		proc = nullptr;
		out = nullptr;
		in = nullptr;
		if (err)
			SDL_CloseIO(err);
		err = nullptr;
		buffer.clear();
		exited = false;
	}
};

// ---------------------------------------------------------------------------
// Jobs

enum JobKind { JOB_NONE, JOB_LIST, JOB_DESCRIBE, JOB_GENERATE, JOB_IMPORT_ROOM, JOB_IMPORT_YAML, JOB_TRACKS };

struct Job
{
	JobKind kind = JOB_NONE;
	Child child;
	int state = AP_SOLO_JOB_IDLE;
	std::string phase;
	std::string errorText;
	std::string errorOption;
	std::string resultRoom;
	nlohmann::json result;
	uint64_t limitMs = kQuickLimitMs;
} g_job;

struct Lists
{
	std::vector<APSoloRoomInfo> rooms;
	std::vector<APSoloStartInfo> starts;
	std::string inbox;
} g_lists;

std::vector<APSoloOption> g_options;
std::vector<APSoloTrackInfo> g_tracks; // the custom-track step (decision 32)

// The folder the client loads custom tracks from (assets/tracks/<key>/original).
std::string AssetsDir()
{
	const char *assets = NativeAssets_GetAssetDir();
	return assets ? assets : "";
}

// ---------------------------------------------------------------------------
// Session

struct Session
{
	Child child;
	int state = AP_SOLO_SESSION_NONE;
	std::string roomId;
	std::string seed;
	std::string error;
	int port = 0;
	uint64_t phaseStart = 0;
	uint64_t stopStart = 0;
	int runningRetries = 0;
	uint64_t retryAt = 0;      // a respawn waiting for an old server to exit
	std::string pendingRoom;   // the next room, once this session has stopped
	bool reconnecting = false; // our own AP_Net_Reconnect call
} g_session;

void FinishJob()
{
	g_job.child.release();
}

void JobFail(const std::string &kind, const std::string &message, const std::string &option)
{
	g_job.state = AP_SOLO_JOB_FAILED;
	g_job.errorText = AP_SoloErrorText(kind, message);
	g_job.errorOption = option;
	Log("job failed (" + kind + "): " + message);
}

// A new track list keeps the picks of the tracks still in it.
void ParseTracks(const nlohmann::json &r)
{
	std::vector<APSoloTrackInfo> next;
	if (r.contains("tracks") && r["tracks"].is_array())
		for (const auto &t : r["tracks"])
		{
			if (!t.is_object())
				continue;
			APSoloTrackInfo info;
			std::memset(&info, 0, sizeof info);
			if (!CopyText(info.id, sizeof info.id, AP_SoloJsonString(t, "id")) || info.id[0] == '\0')
				continue;
			CopyText(info.title, sizeof info.title, AP_SoloJsonString(t, "display_name"));
			CopyText(info.revision, sizeof info.revision, AP_SoloJsonString(t, "revision"));
			CopyText(info.author, sizeof info.author, AP_SoloJsonString(t, "author"));
			CopyText(info.modes, sizeof info.modes, AP_SoloTrackModes(t.value("modes", nlohmann::json())));
			for (const auto &old : g_tracks)
				if (std::strcmp(old.id, info.id) == 0)
					info.pick = old.pick;
			next.push_back(info);
		}
	g_tracks = next;
}

void ParseList(const nlohmann::json &r)
{
	g_lists = Lists();
	g_lists.inbox = AP_SoloJsonString(r, "inbox_path");
	if (r.contains("rooms") && r["rooms"].is_array())
		for (const auto &room : r["rooms"])
		{
			if (!room.is_object())
				continue;
			APSoloRoomInfo info;
			std::memset(&info, 0, sizeof info);
			const std::string id = AP_SoloJsonString(room, "room_id");
			if (!AP_SoloRoomIdValid(id))
				continue;
			CopyText(info.roomId, sizeof info.roomId, id);
			CopyText(info.seedName, sizeof info.seedName, AP_SoloJsonString(room, "seed_name"));
			CopyText(info.slot, sizeof info.slot, AP_SoloJsonString(room, "slot"));
			std::string origin = AP_SoloJsonString(room, "origin");
			std::string preset = AP_SoloJsonString(room, "preset");
			CopyText(info.source, sizeof info.source,
			         origin == "imported" ? std::string("Imported room") : (!preset.empty() ? preset : std::string("YAML")));
			CopyText(info.created, sizeof info.created, AP_SoloJsonString(room, "created").substr(0, 10));
			info.checksDone = room.contains("checks_done") && room["checks_done"].is_number_integer()
			                      ? room["checks_done"].get<int>() : -1;
			info.checksTotal = AP_SoloJsonInt(room, "locations_total", 0);
			const std::string rv = AP_SoloJsonString(room, "room_world_version");
			const std::string wv = AP_SoloJsonString(room, "world_version");
			CopyText(info.worldVersion, sizeof info.worldVersion, rv.empty() ? wv : rv);
			info.versionMatches = rv.empty() || rv == wv;
			g_lists.rooms.push_back(info);
		}
	auto addStarts = [](const nlohmann::json &arr, bool preset) {
		if (!arr.is_array())
			return;
		for (const auto &e : arr)
		{
			if (!e.is_object())
				continue;
			APSoloStartInfo s;
			std::memset(&s, 0, sizeof s);
			s.isPreset = preset ? 1 : 0;
			std::string label = preset ? AP_SoloJsonString(e, "label")
			                           : AP_SoloJsonString(e, "file") + (e.contains("name") ? " (" + AP_SoloJsonString(e, "name") + ")" : "");
			if (!preset && AP_SoloJsonString(e, "source") == "inbox")
				label = "Inbox: " + label;
			CopyText(s.label, sizeof s.label, label);
			CopyText(s.error, sizeof s.error, AP_SoloJsonString(e, "error"));
			if (!CopyText(s.path, sizeof s.path, AP_SoloJsonString(e, "path")))
				CopyText(s.error, sizeof s.error, "The path of this file is too long.");
			CopyText(s.presetId, sizeof s.presetId, AP_SoloJsonString(e, "id"));
			g_lists.starts.push_back(s);
		}
	};
	addStarts(r.value("presets", nlohmann::json()), true);
	addStarts(r.value("yamls", nlohmann::json()), false);
	addStarts(r.value("inbox", nlohmann::json()), false);
}

bool JobStart(JobKind kind, const std::vector<std::string> &args, uint64_t limitMs)
{
	if (g_job.state == AP_SOLO_JOB_RUNNING || !AP_Solo_RuntimePresent())
		return false;
	if (SoloRootIsLink())
	{
		g_job.kind = kind;
		g_job.state = AP_SOLO_JOB_RUNNING;
		JobFail("link", "The solo folder is a link. Make it a normal folder inside the client folder.", "");
		return true;
	}
	g_job.child.release();
	g_job.kind = kind;
	g_job.state = AP_SOLO_JOB_RUNNING;
	g_job.phase = "Starting runtime";
	g_job.errorText.clear();
	g_job.errorOption.clear();
	g_job.resultRoom.clear();
	g_job.result = nlohmann::json();
	g_job.limitMs = limitMs;
	if (!g_job.child.start(args, SoloPath("runtime.log")))
	{
		JobFail("start", "", "");
		return true; // started and failed: the menu shows the error
	}
	return true;
}

void TickJob()
{
	if (g_job.state != AP_SOLO_JOB_RUNNING)
		return;
	for (const std::string &line : g_job.child.poll())
	{
		APSoloEvent ev = AP_SoloParseLine(line);
		if (ev.kind == AP_SOLO_EV_PROGRESS)
			g_job.phase = ev.phase;
		else if (ev.kind == AP_SOLO_EV_RESULT)
			g_job.result = ev.body;
		else if (ev.kind == AP_SOLO_EV_ERROR)
		{
			// The wrapper exits right after an error line; end and reap it now
			// so no child outlives a refused job.
			JobFail(ev.errorKind, ev.message, ev.option);
			FinishJob();
			return;
		}
	}
	if (AP_SoloJobTimedOut(SDL_GetTicks() - g_job.child.started, g_job.limitMs))
	{
		JobFail(g_job.kind == JOB_GENERATE ? "timeout" : "internal", "the runtime did not answer in time", "");
		FinishJob();
		return;
	}
	if (g_job.child.exited)
	{
		if (g_job.result.is_object())
		{
			g_job.state = AP_SOLO_JOB_DONE;
			if (g_job.kind == JOB_LIST)
				ParseList(g_job.result);
			else if (g_job.kind == JOB_DESCRIBE)
				g_options = AP_SoloBuildOptions(g_job.result);
			else if (g_job.kind == JOB_TRACKS)
				ParseTracks(g_job.result);
			else if (g_job.kind == JOB_GENERATE || g_job.kind == JOB_IMPORT_ROOM)
				g_job.resultRoom = AP_SoloJsonString(g_job.result, "room_id");
		}
		else
			JobFail("internal", "the runtime exited without an answer", "");
		FinishJob();
	}
}

// ---------------------------------------------------------------------------
// Session lifecycle

void SessionFail(const std::string &message)
{
	g_session.error = message;
	Log("session failed: " + message);
	// The journal stays on this room: checks earned while the server is gone
	// are recorded and sent when the seed is resumed. Any other connection
	// clears it (AP_Solo_BeforeReconnect).
	AP_Net_Shutdown();
	if (g_session.child.running())
	{
		g_session.child.write("/save\n/exit\n");
		g_session.stopStart = SDL_GetTicks();
		g_session.state = AP_SOLO_SESSION_STOPPING;
		g_session.error = message;
		return;
	}
	g_session.child.release();
	g_session.state = AP_SOLO_SESSION_FAILED;
}

bool SessionSpawn()
{
	std::vector<std::string> args = {"serve", "--room", g_session.roomId, "--game-pid",
	                                 std::to_string(AP_SOLO_GETPID())};
	g_session.child.release();
	if (!g_session.child.start(args, SoloPath("rooms/" + g_session.roomId + "/runtime.log")))
		return false;
	g_session.state = AP_SOLO_SESSION_STARTING;
	g_session.phaseStart = SDL_GetTicks();
	return true;
}

void TickSession()
{
	Session &s = g_session;
	if (s.state == AP_SOLO_SESSION_NONE || s.state == AP_SOLO_SESSION_FAILED)
		return;
	if (s.retryAt != 0)
	{
		if (SDL_GetTicks() < s.retryAt)
			return;
		s.retryAt = 0;
		if (!SessionSpawn())
		{
			s.error = AP_SoloErrorText("start", "");
			s.state = AP_SOLO_SESSION_FAILED;
		}
		return;
	}
	std::vector<std::string> lines = s.child.poll();
	for (const std::string &line : lines)
	{
		APSoloEvent ev = AP_SoloParseLine(line);
		if (ev.kind == AP_SOLO_EV_READY && s.state == AP_SOLO_SESSION_STARTING)
		{
			if (ev.roomId != s.roomId)
			{
				SessionFail("The solo runtime opened a different room.");
				return;
			}
			if (!AP_SoloPortAllowed(ev.port))
			{
				SessionFail("The solo runtime chose a port outside its range.");
				return;
			}
			s.seed = ev.seedName;
			s.port = ev.port;
			char uri[64];
			std::snprintf(uri, sizeof uri, "ws://127.0.0.1:%d", ev.port);
			const std::string journal = SoloPath("rooms/" + s.roomId + "/checks.journal");
			// The journal's seed is also the seed ap_net expects in RoomInfo
			// before it sends the slot and password.
			ap_net_set_check_journal(journal.c_str(), s.seed.c_str());
			s.reconnecting = true;
			AP_Net_Reconnect(uri, ev.slot.c_str(), ev.password.c_str());
			s.reconnecting = false;
			s.state = AP_SOLO_SESSION_CONNECTING;
			s.phaseStart = SDL_GetTicks();
			Log("server ready on port " + std::to_string(ev.port) + " for room " + s.roomId);
		}
		else if (ev.kind == AP_SOLO_EV_ERROR && s.state == AP_SOLO_SESSION_STARTING)
		{
			// "running": a server from a moment ago (the game restarted
			// quickly) still holds the lock; its watchdog ends it within a
			// second. Try again a few times.
			if (ev.errorKind == "running" && s.runningRetries < 3)
			{
				s.runningRetries++;
				s.child.release();
				s.retryAt = SDL_GetTicks() + 1200;
				return;
			}
			s.child.release();
			s.error = AP_SoloErrorText(ev.errorKind, ev.message);
			s.state = AP_SOLO_SESSION_FAILED;
			Log("serve refused (" + ev.errorKind + "): " + ev.message);
			return;
		}
		else if (ev.kind == AP_SOLO_EV_STOPPED)
			Log("server stopped (" + AP_SoloJsonString(ev.body, "reason") + ")");
	}

	const uint64_t now = SDL_GetTicks();
	if (s.state == AP_SOLO_SESSION_STOPPING)
	{
		APSoloStopStep step = AP_SoloStopStep(s.child.exited || !s.child.proc, now - s.stopStart);
		if (step == AP_SOLO_STOP_KILL)
		{
			Log("server ignored /exit; ending it");
			s.child.kill();
			s.child.release();
		}
		if (step != AP_SOLO_STOP_WAIT)
		{
			s.child.release();
			s.state = s.error.empty() ? AP_SOLO_SESSION_NONE : AP_SOLO_SESSION_FAILED;
		}
		return;
	}
	if (s.child.exited)
	{
		SessionFail("The solo server stopped. Resume the seed to start it again.");
		return;
	}
	if (s.state == AP_SOLO_SESSION_STARTING && now - s.phaseStart > kReadyLimitMs)
	{
		SessionFail(AP_SoloErrorText("runtime", ""));
		return;
	}
	if (s.state == AP_SOLO_SESSION_CONNECTING || s.state == AP_SOLO_SESSION_LIVE)
	{
		char seed[128] = {0};
		ap_net_seed_name(seed, sizeof seed);
		APSoloIdentity id = AP_SoloIdentity(seed, s.seed);
		if (id == AP_SOLO_ID_FOREIGN)
		{
			SessionFail("Another program is using port " + std::to_string(s.port) + ".");
			return;
		}
		if (s.state == AP_SOLO_SESSION_CONNECTING)
		{
			if (id == AP_SOLO_ID_MATCH && ap_net_is_connected())
			{
				s.state = AP_SOLO_SESSION_LIVE;
				Log("connected to the solo room " + s.roomId);
			}
			else if (ap_net_status() == AP_NET_STATUS_ERROR)
			{
				// The room was refused at slot-connect (for example a slot_data
				// schema this client cannot admit). Show the client's reason.
				const char *why = ap_net_last_error();
				SessionFail(std::string("The client refused this room") + (why && *why ? ": " + std::string(why) : "."));
			}
			else if (now - s.phaseStart > kConnectLimitMs)
				SessionFail("Could not connect to the solo server.");
		}
	}
}

} // namespace

// ---------------------------------------------------------------------------
// C API

extern "C" int AP_Solo_RuntimePresent(void)
{
	return FileExists(Interpreter()) && FileExists(ClientPath(kScript)) ? 1 : 0;
}

extern "C" int AP_Solo_ListStart(void)
{
	return JobStart(JOB_LIST, {"list"}, kQuickLimitMs) ? 1 : 0;
}

extern "C" int AP_Solo_DescribeStart(const char *yamlPath)
{
	std::vector<std::string> args = {"describe"};
	if (yamlPath && *yamlPath)
	{
		args.push_back("--yaml");
		args.push_back(yamlPath);
	}
	return JobStart(JOB_DESCRIBE, args, kQuickLimitMs) ? 1 : 0;
}

extern "C" int AP_Solo_ImportRoomStart(const char *path)
{
	if (!path || !*path)
		return 0;
	return JobStart(JOB_IMPORT_ROOM, {"import-room", path}, kQuickLimitMs) ? 1 : 0;
}

extern "C" int AP_Solo_ImportYamlStart(const char *path)
{
	if (!path || !*path)
		return 0;
	return JobStart(JOB_IMPORT_YAML, {"import-yaml", path}, kQuickLimitMs) ? 1 : 0;
}

extern "C" int AP_Solo_TracksStart(void)
{
	return JobStart(JOB_TRACKS, {"tracks", "--assets", AssetsDir()}, kQuickLimitMs) ? 1 : 0;
}

extern "C" int AP_Solo_GenerateStart(const char *yamlPath, const char *slotName, const char *presetId, int useOptions)
{
	if (!yamlPath || !*yamlPath)
		return 0;
	std::vector<std::string> args = {"generate", "--yaml", yamlPath, "--assets", AssetsDir()};
	if (slotName && *slotName)
	{
		args.push_back("--name");
		args.push_back(slotName);
	}
	if (presetId && *presetId)
	{
		args.push_back("--preset");
		args.push_back(presetId);
		// A YAML this client builds from a preset says which versions made it.
		args.push_back("--stamp-requires");
	}
	if (useOptions)
	{
		for (const std::string &set : AP_SoloSetArgs(g_options))
		{
			args.push_back("--set");
			args.push_back(set);
		}
		for (const APSoloTrackInfo &t : g_tracks)
		{
			if (t.pick == AP_SOLO_TRACK_OFF)
				continue;
			args.push_back("--track");
			args.push_back(t.id);
			if (t.pick == AP_SOLO_TRACK_GUARANTEED)
			{
				args.push_back("--guarantee");
				args.push_back(t.id);
			}
		}
	}
	return JobStart(JOB_GENERATE, args, kGenerateLimitMs) ? 1 : 0;
}

extern "C" int AP_Solo_JobState(void) { return g_job.state; }
extern "C" const char *AP_Solo_JobPhase(void) { return g_job.phase.c_str(); }
extern "C" const char *AP_Solo_JobErrorText(void) { return g_job.errorText.c_str(); }
extern "C" const char *AP_Solo_JobErrorOption(void) { return g_job.errorOption.c_str(); }
extern "C" const char *AP_Solo_JobResultRoom(void) { return g_job.resultRoom.c_str(); }

extern "C" void AP_Solo_JobCancel(void)
{
	// Generation writes into solo/tmp and moves the room only after success,
	// so ending the child loses nothing.
	if (g_job.state == AP_SOLO_JOB_RUNNING)
	{
		g_job.child.kill();
		g_job.child.release();
		g_job.state = AP_SOLO_JOB_IDLE;
		Log("job cancelled");
	}
}

extern "C" void AP_Solo_JobClear(void)
{
	if (g_job.state != AP_SOLO_JOB_RUNNING)
		g_job.state = AP_SOLO_JOB_IDLE;
}

extern "C" int AP_Solo_TrackCount(void) { return (int)g_tracks.size(); }

extern "C" int AP_Solo_TrackGet(int index, struct APSoloTrackInfo *out)
{
	if (index < 0 || index >= (int)g_tracks.size() || !out)
		return 0;
	*out = g_tracks[index];
	return 1;
}

extern "C" void AP_Solo_TrackCycle(int index)
{
	if (index >= 0 && index < (int)g_tracks.size())
		g_tracks[index].pick = AP_SoloTrackNextPick(g_tracks[index].pick);
}

extern "C" int AP_Solo_TracksPicked(void)
{
	int n = 0;
	for (const APSoloTrackInfo &t : g_tracks)
		n += t.pick != AP_SOLO_TRACK_OFF;
	return n;
}

extern "C" void AP_Solo_TracksClear(void)
{
	for (APSoloTrackInfo &t : g_tracks)
		t.pick = AP_SOLO_TRACK_OFF;
}

extern "C" void AP_Solo_RecordTrackDescriptor(const char *contentId, const char *revision, const char *json)
{
	if (!contentId || !revision || !json || !*json || std::strlen(json) > kDescriptorMax)
		return;
	const std::string name = AP_SoloDescriptorFileName(contentId, revision);
	if (name.empty() || SoloRootIsLink())
		return;
	std::error_code ec;
	const fs::path dir = fs::u8path(SoloPath("tracks"));
	if (fs::is_symlink(dir, ec))
		return;
	fs::create_directories(dir, ec);
	const fs::path target = dir / fs::u8path(name);
	// Only a plain file of descriptor size is read or replaced: a link, a
	// FIFO or a huge file there is left alone (reading one could block or
	// stall the frame).
	const fs::file_status st = fs::symlink_status(target, ec);
	if (fs::exists(st))
	{
		if (!fs::is_regular_file(st) || fs::file_size(target, ec) > kDescriptorMax || ec)
		{
			Log("left " + name + " alone: not a plain descriptor file");
			return;
		}
		std::ifstream in(target, std::ios::binary);
		std::stringstream have;
		have << in.rdbuf();
		if (have.str() == json)
			return;
	}
	// A temp name no other writer uses, created fresh (never an existing
	// path, so never through a link someone left there).
	static unsigned serial = 0;
	fs::path tmp;
	for (int tries = 0; tries < 16; tries++)
	{
		tmp = dir / fs::u8path(name + ".tmp-" + std::to_string(AP_SOLO_GETPID()) + "-" + std::to_string(++serial));
		if (!fs::exists(fs::symlink_status(tmp, ec)))
			break;
		tmp.clear();
	}
	if (tmp.empty())
		return;
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << json;
		if (!out.flush())
		{
			out.close();
			fs::remove(tmp, ec);
			Log("could not keep the descriptor of " + name);
			return;
		}
	}
	// rename replaces the old file in one step on Linux and, where the
	// runtime supports it, on Windows. Only if that fails is the old file
	// removed first; a crash in between loses a cached copy, which the next
	// verification of the package writes again.
	fs::rename(tmp, target, ec);
	if (ec)
	{
		std::error_code ec2;
		fs::remove(target, ec2);
		fs::rename(tmp, target, ec);
	}
	if (ec)
	{
		fs::remove(tmp, ec);
		Log("could not keep the descriptor of " + name);
		return;
	}
	Log("kept the descriptor of " + name + " for the custom-track step");
}

extern "C" int AP_Solo_RoomCount(void) { return (int)g_lists.rooms.size(); }

extern "C" int AP_Solo_RoomGet(int index, struct APSoloRoomInfo *out)
{
	if (index < 0 || index >= (int)g_lists.rooms.size() || !out)
		return 0;
	*out = g_lists.rooms[index];
	return 1;
}

extern "C" int AP_Solo_StartCount(void) { return (int)g_lists.starts.size(); }

extern "C" int AP_Solo_StartGet(int index, struct APSoloStartInfo *out)
{
	if (index < 0 || index >= (int)g_lists.starts.size() || !out)
		return 0;
	*out = g_lists.starts[index];
	return 1;
}

extern "C" const char *AP_Solo_InboxPath(void) { return g_lists.inbox.c_str(); }

extern "C" int AP_Solo_RoomDelete(const char *roomId)
{
	if (!roomId || !AP_SoloRoomIdValid(roomId))
		return 0;
	if (AP_Solo_SessionActive() && g_session.roomId == roomId)
		return 0;
	std::error_code ec;
	fs::path folder = fs::u8path(SoloPath(std::string("rooms/") + roomId));
	if (!fs::is_directory(folder, ec))
		return 0;
	fs::remove_all(folder, ec);
	Log(std::string("deleted solo room ") + roomId + (ec ? " (incomplete: " + ec.message() + ")" : ""));
	for (size_t i = 0; i < g_lists.rooms.size(); i++)
		if (std::strcmp(g_lists.rooms[i].roomId, roomId) == 0)
		{
			g_lists.rooms.erase(g_lists.rooms.begin() + (long)i);
			break;
		}
	return ec ? 0 : 1;
}

extern "C" int AP_Solo_OptionCount(void) { return (int)g_options.size(); }

extern "C" int AP_Solo_OptionGet(int index, struct APSoloOptionInfo *out)
{
	if (index < 0 || index >= (int)g_options.size() || !out)
		return 0;
	const APSoloOption &o = g_options[index];
	std::memset(out, 0, sizeof *out);
	CopyText(out->name, sizeof out->name, o.name);
	CopyText(out->label, sizeof out->label, o.label);
	CopyText(out->group, sizeof out->group, o.group);
	CopyText(out->value, sizeof out->value, o.value);
	out->kind = o.kind == "toggle" ? AP_SOLO_OPT_TOGGLE : o.kind == "choice" ? AP_SOLO_OPT_CHOICE : AP_SOLO_OPT_RANGE;
	out->visible = AP_SoloOptionVisible(g_options, o) ? 1 : 0;
	out->forced = o.forced.empty() ? 0 : 1;
	if (!o.forced.empty())
		CopyText(out->note, sizeof out->note, o.forcedReason);
	else if (AP_SoloOptionWarns(g_options, o))
		CopyText(out->note, sizeof out->note, o.warnReason);
	return 1;
}

extern "C" void AP_Solo_OptionStep(int index, int direction)
{
	if (index >= 0 && index < (int)g_options.size())
		AP_SoloOptionStep(g_options[index], direction);
}

extern "C" const char *AP_Solo_OptionHelp(int index)
{
	if (index < 0 || index >= (int)g_options.size())
		return "";
	return g_options[index].doc.c_str();
}

extern "C" int AP_Solo_OptionIndex(const char *name)
{
	for (size_t i = 0; name && i < g_options.size(); i++)
		if (g_options[i].name == name)
			return (int)i;
	return -1;
}

extern "C" void AP_Solo_OptionsReset(void)
{
	for (auto &o : g_options)
		o.value = o.initial;
}

namespace {
std::mutex g_pickLock;
int g_pickState = 0; // 0 idle, 1 open, 2 picked, 3 cancelled
std::string g_pickPath;

void SDLCALL PickCallback(void *, const char *const *files, int)
{
	std::lock_guard<std::mutex> lock(g_pickLock);
	if (files && files[0])
	{
		g_pickPath = files[0];
		g_pickState = 2;
	}
	else
		g_pickState = 3;
}
} // namespace

extern "C" int AP_Solo_PickStart(int room)
{
	static const SDL_DialogFileFilter roomFilters[] = {{"Archipelago room", "zip;archipelago"}};
	static const SDL_DialogFileFilter yamlFilters[] = {{"Player YAML", "yaml;yml"}};
	{
		std::lock_guard<std::mutex> lock(g_pickLock);
		if (g_pickState == 1)
			return 0;
		g_pickState = 1;
	}
	SDL_ShowOpenFileDialog(PickCallback, nullptr, nullptr, room ? roomFilters : yamlFilters, 1, nullptr, false);
	return 1;
}

extern "C" int AP_Solo_PickTake(char *out, int size)
{
	std::lock_guard<std::mutex> lock(g_pickLock);
	if (g_pickState < 2)
		return 0;
	int result = g_pickState == 2 ? 1 : -1;
	if (result == 1 && (!out || size <= 0 || g_pickPath.size() >= (size_t)size))
		result = -2; // the path does not fit: refuse it rather than pass a cut path
	if (result == 1)
		std::snprintf(out, (size_t)size, "%s", g_pickPath.c_str());
	g_pickState = 0;
	g_pickPath.clear();
	return result;
}

extern "C" int AP_Solo_SessionStart(const char *roomId)
{
	if (!roomId || !AP_SoloRoomIdValid(roomId) || !AP_Solo_RuntimePresent())
		return 0;
	if (AP_Solo_SessionActive())
	{
		if (g_session.roomId == roomId && g_session.state != AP_SOLO_SESSION_STOPPING)
			return 1; // already running for this room: one server per room
		// Switch rooms without blocking the frame: stop this one, start the
		// next from AP_Solo_Tick once the old server has exited.
		std::string next = roomId;
		AP_Solo_SessionStop();
		g_session.pendingRoom = next;
		return 1;
	}
	g_session.child.release();
	g_session = Session();
	g_session.roomId = roomId;
	if (SoloRootIsLink())
	{
		g_session.error = "The solo folder is a link. Make it a normal folder inside the client folder.";
		g_session.state = AP_SOLO_SESSION_FAILED;
		return 1;
	}
	if (!SessionSpawn())
	{
		g_session.error = AP_SoloErrorText("start", "");
		g_session.state = AP_SOLO_SESSION_FAILED;
	}
	return 1;
}

extern "C" int AP_Solo_SessionState(void) { return g_session.state; }
extern "C" const char *AP_Solo_SessionError(void) { return g_session.error.c_str(); }
extern "C" const char *AP_Solo_SessionRoom(void) { return g_session.roomId.c_str(); }

extern "C" int AP_Solo_SessionActive(void)
{
	const int s = g_session.state;
	return s == AP_SOLO_SESSION_STARTING || s == AP_SOLO_SESSION_CONNECTING || s == AP_SOLO_SESSION_LIVE ||
	       s == AP_SOLO_SESSION_STOPPING;
}

extern "C" void AP_Solo_SessionStop(void)
{
	Session &s = g_session;
	s.pendingRoom.clear();
	if (s.state == AP_SOLO_SESSION_NONE || s.state == AP_SOLO_SESSION_FAILED || s.state == AP_SOLO_SESSION_STOPPING)
		return;
	// The journal stays on this room until another connection is made, so a
	// check earned offline after leaving is still resent on the next resume.
	AP_Net_Shutdown();
	s.error.clear();
	if (s.child.running())
	{
		s.child.write("/save\n/exit\n");
		s.stopStart = SDL_GetTicks();
		s.state = AP_SOLO_SESSION_STOPPING;
	}
	else
	{
		s.child.release();
		s.state = AP_SOLO_SESSION_NONE;
	}
}

extern "C" void AP_Solo_BeforeReconnect(void)
{
	if (g_session.reconnecting)
		return;
	// Another connection (Connect, a room link): no queued room may start
	// later, the running session ends, and checks stop going to the journal.
	g_session.pendingRoom.clear();
	if (AP_Solo_SessionActive())
	{
		Log("another connection replaces the solo session");
		AP_Solo_SessionStop();
	}
	ap_net_set_check_journal(nullptr, nullptr);
}

extern "C" void AP_Solo_Tick(void)
{
	TickJob();
	TickSession();
	if (!g_session.pendingRoom.empty() && !AP_Solo_SessionActive())
	{
		std::string next = g_session.pendingRoom;
		g_session.pendingRoom.clear();
		AP_Solo_SessionStart(next.c_str());
	}
}

extern "C" void AP_Solo_Shutdown(void)
{
	g_session.pendingRoom.clear();
	AP_Solo_JobCancel();
	AP_Solo_SessionStop();
	// Bounded wait for the server to save and exit.
	while (g_session.state == AP_SOLO_SESSION_STOPPING)
	{
		TickSession();
		if (g_session.state == AP_SOLO_SESSION_STOPPING)
			SDL_Delay(20);
	}
	if (g_session.state == AP_SOLO_SESSION_FAILED)
		g_session.state = AP_SOLO_SESSION_NONE;
}
