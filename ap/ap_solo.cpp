// Solo mode (#89): runtime jobs and the private solo server. See ap_solo.h.
//
// Child processes use SDL3's process API. Their stdout pipe is non-blocking,
// so the game thread polls it every frame; nothing here blocks a frame except
// AP_Solo_Shutdown at quit. A child is only ever ended through its own
// SDL_Process handle, never by name or PID lookup.
#include "ap_solo.h"
#include "ap_solo_logic.hpp"
#include "ap_net.h"
#ifdef CTR_CUSTOM_PACKAGES
#include <platform/native_custom_package.h>
#include "ap_track_registry.h"
#include "ap_content_registry.hpp"
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#define AP_SOLO_GETPID() ((long)_getpid())
#else
#include <fcntl.h>
#include <sys/stat.h>
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
const size_t kMenuMessageMax = 170;       // the menus print "%.170s" of a publication error

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

// A plain file of at most `limit` bytes, or "" (a link, a FIFO or anything
// larger is not read). On POSIX the open does not follow a link and does not
// wait for a FIFO writer, and the opened file itself is checked.
std::string ReadSmallPlainFile(const fs::path &path, size_t limit)
{
#ifdef _WIN32
	std::error_code ec;
	const fs::file_status st = fs::symlink_status(path, ec);
	if (ec || !fs::is_regular_file(st) || fs::file_size(path, ec) > limit || ec)
		return std::string();
	std::ifstream in(path, std::ios::binary);
	std::string data(limit + 1, '\0');
	in.read(&data[0], (std::streamsize)data.size());
	data.resize((size_t)in.gcount());
#else
	const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return std::string();
	struct stat st;
	std::string data;
	if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && (size_t)st.st_size <= limit)
	{
		data.resize(limit + 1);
		ssize_t got = 0, n;
		while ((size_t)got < data.size() && (n = read(fd, &data[(size_t)got], data.size() - (size_t)got)) > 0)
			got += n;
		data.resize(got > 0 ? (size_t)got : 0);
	}
	close(fd);
#endif
	return data.size() > limit ? std::string() : data;
}

// Creates `path` (it must not exist: never through a link left there) and
// writes `text`. 1 written; 0 not created (it exists, or no access): the
// path is someone else's; -1 created by this call but the write failed.
int WriteNewFile(const fs::path &path, const std::string &text)
{
#ifdef _WIN32
	const int fd = _wopen(path.wstring().c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
	if (fd < 0)
		return 0;
	const bool ok = _write(fd, text.data(), (unsigned)text.size()) == (int)text.size();
	return (_close(fd) == 0) && ok ? 1 : -1;
#else
	const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (fd < 0)
		return 0;
	size_t done = 0;
	while (done < text.size())
	{
		const ssize_t n = write(fd, text.data() + done, text.size() - done);
		if (n <= 0)
		{
			close(fd);
			return -1;
		}
		done += (size_t)n;
	}
	return close(fd) == 0 ? 1 : -1;
#endif
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

// Deliberately session-local: empty means an ordinary loopback room without
// a password. The player can set it before starting or resuming a room.
std::string g_sessionPassword;

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

void AddNeedsLapsRows(std::vector<APSoloTrackInfo> &rows); // below, by its writer

// A new track list keeps the picks of the tracks still in it. The first list of a
// start begins as the YAML has each track.
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
			info.timeTrial = t.value("time_trial", false) ? 1 : 0;
			info.pick = AP_SoloTrackStart(t);
			for (const auto &old : g_tracks)
				if (std::strcmp(old.id, info.id) == 0)
					info.pick = old.pick;
			next.push_back(info);
		}
	AddNeedsLapsRows(next);
	for (APSoloTrackInfo &info : next)
		for (const auto &old : g_tracks)
			if (info.needsLaps && std::strcmp(old.id, info.id) == 0 && old.needsLaps)
				info.laps = old.laps;
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

// Store verification runs separately from the SDL child job. Cancellation
// discards its result without joining a worker on the frame thread.
bool TrackPublicationBusy();
bool TickTrackPublication();
bool StartTrackPublication(const std::vector<std::string> &args);
void CancelTrackPublication();
void FinishTrackPublication();

bool JobStart(JobKind kind, const std::vector<std::string> &args, uint64_t limitMs)
{
	if (g_job.state == AP_SOLO_JOB_RUNNING || TrackPublicationBusy() || !AP_Solo_RuntimePresent())
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
	if (TickTrackPublication())
		return;
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
	if (!g_sessionPassword.empty())
	{
		args.push_back("--password");
		args.push_back(g_sessionPassword);
	}
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

extern "C" int AP_Solo_TracksStart(const char *yamlPath)
{
	std::vector<std::string> args = {"tracks", "--assets", AssetsDir()};
	if (yamlPath && *yamlPath)
	{
		args.push_back("--yaml");
		args.push_back(yamlPath);
	}
	return StartTrackPublication(args) ? 1 : 0;
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
	}
	// Picked custom tracks go with a preset's options and with an imported or
	// inbox YAML alike (round 2 decision 5); the picks are cleared whenever a
	// new start is chosen.
	std::vector<std::pair<std::string, int>> picks;
	for (const APSoloTrackInfo &t : g_tracks)
		if (!t.needsLaps) // off rows still tell a YAML's track page to remove them
			picks.emplace_back(t.id, t.pick);
	for (const std::string &arg : AP_SoloTrackArgs(picks))
		args.push_back(arg);
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
		CancelTrackPublication();
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

extern "C" int AP_Solo_TracksViaOptions(int isPreset) { return AP_SoloTracksViaOptions(isPreset != 0) ? 1 : 0; }

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
	if (index >= 0 && index < (int)g_tracks.size() && !g_tracks[index].needsLaps)
	{
		g_tracks[index].pick = g_tracks[index].modes[0] ? AP_SoloTrackNextPick(g_tracks[index].pick) : AP_SOLO_TRACK_OFF;
		if (g_tracks[index].pick != AP_SOLO_TRACK_OFF)
			for (int other = 0; other < (int)g_tracks.size(); ++other)
				if (other != index && AP_SoloTrackSameContent(g_tracks[index].id, g_tracks[other].id))
					g_tracks[other].pick = AP_SOLO_TRACK_OFF;
	}
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
	g_tracks.clear();
}

namespace
{
// Keeps `json` as solo/<folder>/<name>; true once the file holds it. On a
// refusal `why` (when given) gets a short reason a player can act on, and the
// log gets the file name and the system's error.
bool KeepDescriptorFile(const char *folder, const std::string &name, const std::string &json,
                        std::string *why = nullptr)
{
	const std::string where = std::string("solo/") + folder + "/" + name;
	auto refuse = [&](const std::string &reason, const std::string &detail) {
		if (why)
			*why = reason;
		Log("could not keep " + (name.empty() ? std::string("a descriptor") : where) + ": " + detail);
		return false;
	};
	if (name.empty())
		return refuse("its id or version is not a valid file name", "no valid descriptor file name");
	if (json.empty() || json.size() > kDescriptorMax)
		return refuse("its descriptor is empty or too large", "descriptor of " + std::to_string(json.size()) + " bytes");
	if (SoloRootIsLink())
		return refuse("the solo folder is a link; make it a normal folder", "solo is a link");
	std::error_code ec;
	const fs::path dir = fs::u8path(SoloPath(folder));
	if (fs::is_symlink(dir, ec))
		return refuse(std::string("solo/") + folder + " is a link; make it a normal folder",
		              std::string("solo/") + folder + " is a link");
	fs::create_directories(dir, ec);
	if (ec)
		return refuse(std::string("solo/") + folder + " could not be created", ec.message());
	const fs::path target = dir / fs::u8path(name);
	// Only a plain file of descriptor size is read or replaced: a link, a
	// FIFO or a huge file there is left alone (reading one could block or
	// stall the frame).
	const fs::file_status st = fs::symlink_status(target, ec);
	if (fs::exists(st))
	{
		if (!fs::is_regular_file(st) || fs::file_size(target, ec) > kDescriptorMax || ec)
			return refuse(std::string("its file in solo/") + folder + " is not a plain file; delete it", "not a plain descriptor file, left alone");
		if (ReadSmallPlainFile(target, kDescriptorMax) == json)
			return true;
	}
	// A fresh temp name, created exclusively (never through a link left
	// there). A name someone else holds is skipped, never removed.
	static unsigned serial = 0;
	fs::path tmp;
	int wrote = 0, err = 0;
	for (int tries = 0; tries < 16 && wrote == 0; tries++)
	{
		tmp = dir / fs::u8path(name + ".tmp-" + std::to_string(AP_SOLO_GETPID()) + "-" + std::to_string(++serial));
		errno = 0;
		wrote = WriteNewFile(tmp, json);
		err = errno;
	}
	if (wrote != 1)
	{
		if (wrote == -1)
			fs::remove(tmp, ec); // ours: created by this call
		return refuse("could not write in solo/" + std::string(folder) +
		                  " (read-only, full disk or blocked by antivirus)",
		              "temp file " + tmp.filename().u8string() + ": " + (err ? std::strerror(err) : "write failed"));
	}
	// rename replaces the old file in one step (MoveFileExW with
	// MOVEFILE_REPLACE_EXISTING under MinGW libstdc++, rename(2) on Linux).
	// If it fails, the old descriptor stays. Windows refuses to replace a
	// read-only file: our own descriptor gets its write bit back once.
	fs::rename(tmp, target, ec);
	if (ec)
	{
		std::error_code pe;
		const fs::file_status ts = fs::symlink_status(target, pe);
		if (!pe && fs::is_regular_file(ts) && (ts.permissions() & fs::perms::owner_write) == fs::perms::none)
		{
			fs::permissions(target, fs::perms::owner_write, fs::perm_options::add, pe);
			if (!pe)
			{
				Log("made " + where + " writable again");
				fs::rename(tmp, target, ec);
			}
		}
	}
	if (ec)
	{
		const std::string detail = ec.message();
		fs::remove(tmp, ec);
		return refuse(std::string("its file in solo/") + folder + " is read-only or in use (antivirus?)",
		              "replace failed: " + detail);
	}
	Log(std::string("kept the descriptor of ") + name + " in solo/" + folder);
	return true;
}

// Removes solo/needs-laps/<name> when it is a plain file: its track now has a
// complete descriptor in solo/tracks.
void DropNeedsLaps(const std::string &name)
{
	std::error_code ec;
	const fs::path dir = fs::u8path(SoloPath("needs-laps"));
	if (name.empty() || SoloRootIsLink() || fs::is_symlink(dir, ec))
		return;
	const fs::path target = dir / fs::u8path(name);
	if (fs::is_regular_file(fs::symlink_status(target, ec)))
		fs::remove(target, ec);
}

// solo/needs-laps/<track key>__<revision>.json of a listed row
// (id package/<track key>/<revision>), or "".
std::string NeedsLapsFileName(const APSoloTrackInfo &t)
{
	const std::string id = t.id;
	const size_t slash = id.find('/', 8);
	if (id.compare(0, 8, "package/") != 0 || slash == std::string::npos)
		return std::string();
	return AP_SoloDescriptorFileName(id.substr(8, slash - 8), id.substr(slash + 1));
}

// A file in assets/tracks/<key>/original with the pin's suffix and size,
// whatever its name, so a waiting track is listed only while its files are
// installed. Size only: this runs on the game thread. The wrapper checks the
// SHA-256 too (ctr_solo.py _pinned_file) in its own job once the laps are
// kept, and lists the track only then.
bool PinnedFilePresent(const std::string &key, const nlohmann::json &pin)
{
	if (!pin.is_object())
		return false;
	const std::string role = AP_SoloJsonString(pin, "role");
	const auto bytes = pin.find("bytes");
	if ((role != "lev" && role != "vrm") || bytes == pin.end() || !bytes->is_number_unsigned())
		return false;
	const std::uintmax_t size = bytes->get<std::uintmax_t>();
	std::error_code ec;
	const fs::path folder = fs::u8path(AssetsDir()) / "tracks" / fs::u8path(key) / "original";
	for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
	{
		std::string name = it->path().filename().u8string();
		for (char &c : name)
			c = (char)std::tolower((unsigned char)c);
		std::error_code fe;
		if (name.size() > 4 && name.compare(name.size() - 4, 4, "." + role) == 0 &&
		    fs::is_regular_file(it->symlink_status(fe)) && fs::file_size(it->path(), fe) == size && !fe)
			return true;
	}
	return false;
}

// The tracks in solo/needs-laps the wrapper's list does not already have and
// whose files are installed, as rows that wait for their laps (ruling of
// 2026-11-02).
void AddNeedsLapsRows(std::vector<APSoloTrackInfo> &rows)
{
	std::error_code ec;
	const fs::path dir = fs::u8path(SoloPath("needs-laps"));
	if (SoloRootIsLink() || fs::is_symlink(dir, ec) || !fs::is_directory(dir, ec))
		return;
	std::vector<std::string> names;
	for (fs::directory_iterator it(dir, ec), end; !ec && it != end && names.size() < 256; it.increment(ec))
	{
		const std::string name = it->path().filename().u8string();
		if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0)
			names.push_back(name);
	}
	std::sort(names.begin(), names.end());
	for (const std::string &name : names)
	{
		const nlohmann::json d = nlohmann::json::parse(ReadSmallPlainFile(dir / fs::u8path(name), kDescriptorMax),
		                                               nullptr, false);
		if (!AP_SoloNeedsLapsDescriptor(d, name))
			continue;
		APSoloTrackInfo info;
		std::memset(&info, 0, sizeof info);
		if (!CopyText(info.id, sizeof info.id, AP_SoloJsonString(d, "id")) ||
		    !CopyText(info.revision, sizeof info.revision, AP_SoloJsonString(d, "revision")))
			continue;
		CopyText(info.title, sizeof info.title, AP_SoloJsonString(d, "display_name"));
		CopyText(info.author, sizeof info.author, AP_SoloJsonString(d, "author"));
		bool listed = false;
		for (const APSoloTrackInfo &row : rows)
			listed = listed || std::strcmp(row.id, info.id) == 0;
		if (listed)
			continue;
		const auto files = d.find("files");
		bool installed = files != d.end() && files->is_array() && files->size() == 2;
		bool shared = false;
#ifdef CTR_CUSTOM_PACKAGES
		if (installed)
		{
			const auto &lev = (*files)[0], &vrm = (*files)[1];
			const auto levHash = AP_SoloJsonString(lev, "sha256"), vrmHash = AP_SoloJsonString(vrm, "sha256");
			if (lev.is_object() && vrm.is_object() && lev.value("role", "") == "lev" &&
			    vrm.value("role", "") == "vrm" && lev.contains("bytes") && vrm.contains("bytes") &&
			    lev["bytes"].is_number_unsigned() && vrm["bytes"].is_number_unsigned())
			{
				char levPath[4096], vrmPath[4096];
				shared = CustomPackage_FindPinnedFiles(AssetsDir().c_str(), AP_SoloJsonString(d, "revision").c_str(), nullptr,
					levHash.c_str(), lev["bytes"].get<unsigned long>(), vrmHash.c_str(), vrm["bytes"].get<unsigned long>(),
					levPath, sizeof levPath, vrmPath, sizeof vrmPath) != 0;
			}
		}
#endif
		if (installed && !shared)
			for (const auto &pin : *files)
				installed = installed && PinnedFilePresent(AP_SoloJsonString(d, "content_id"), pin);
		if (!installed)
			continue;
		info.needsLaps = 1;
		info.laps = AP_SOLO_LAPS_DEFAULT;
		rows.push_back(info);
	}
}
} // namespace

namespace
{
// "<revision>|lev:<sha256>:<bytes>|vrm:<sha256>:<bytes>" of a descriptor's
// file pins, or "" when they are not in that shape.
std::string PinSignature(const std::string &revision, const nlohmann::json &files)
{
	if (!files.is_array())
		return std::string();
	std::string out = revision;
	for (const auto &pin : files)
	{
		if (!pin.is_object() || !pin.contains("bytes") || !pin["bytes"].is_number_unsigned())
			return std::string();
		out += "|" + AP_SoloJsonString(pin, "role") + ":" + AP_SoloJsonString(pin, "sha256") + ":" +
		       std::to_string(pin["bytes"].get<std::uintmax_t>());
	}
	return out;
}

// What a descriptor write may replace. A kept descriptor for the same track
// id and version with other file pins is refused unless the caller knows the
// shared store: `live` holds the pin signature of every verified package in
// it. A kept descriptor whose files are no longer installed (in the store or
// in assets/tracks/<key>/original) is stale and is replaced. `fresh` is a
// package the player just installed: it takes over the entry from another
// installed copy. The new descriptor pins exactly the verified files of the
// package it came from, so no pin check is skipped.
struct DescriptorScope
{
	const std::set<std::string> *live = nullptr;
	bool fresh = false;
};

bool MayReplace(const nlohmann::json &kept, const std::string &key, const std::string &revision,
                const DescriptorScope &scope)
{
	if (!scope.live)
		return false;
	if (scope.fresh)
		return true;
	const nlohmann::json files = kept.value("files", nlohmann::json());
	if (scope.live->count(PinSignature(revision, files)))
		return false;
	bool loose = files.is_array() && !files.empty();
	if (loose)
		for (const auto &pin : files)
			loose = loose && PinnedFilePresent(key, pin);
	return !loose;
}

// Removes our own plain descriptor file, clearing a read-only bit once.
bool RemoveDescriptorFile(const fs::path &target, std::string &detail)
{
	std::error_code ec;
	if (!fs::is_regular_file(fs::symlink_status(target, ec)))
		return true;
	if (fs::remove(target, ec) || !fs::exists(fs::symlink_status(target, ec)))
		return true;
	fs::permissions(target, fs::perms::owner_write, fs::perm_options::add, ec);
	if (!ec && fs::remove(target, ec))
		return true;
	detail = ec ? ec.message() : std::string("remove failed");
	return false;
}

// Removes the solo/tracks and solo/needs-laps files of a track key that an
// older import of the same package (same uuid) used, once a newer install of
// that package lists the track under `keeperKey`. A file of a different
// package that happens to use the key is left alone. Laps the player picked
// for the old key move to the new one when it still waits for its laps and
// pins the same files.
bool RetireDescriptor(const std::string &key, const std::string &revision, const std::string &uuid,
                      const std::string &keeperKey, std::string &why)
{
	const std::string name = AP_SoloDescriptorFileName(key, revision);
	const std::string keeperName = AP_SoloDescriptorFileName(keeperKey, revision);
	std::error_code ec;
	if (name.empty() || keeperName.empty() || uuid.empty() || SoloRootIsLink())
		return true;
	for (const char *folder : {"tracks", "needs-laps"})
	{
		const fs::path dir = fs::u8path(SoloPath(folder));
		if (fs::is_symlink(dir, ec))
			continue;
		const fs::path target = dir / fs::u8path(name);
		const auto d = nlohmann::json::parse(ReadSmallPlainFile(target, kDescriptorMax), nullptr, false);
		std::string detail;
		if (!d.is_object() || AP_SoloJsonString(d, "uuid") != uuid)
			continue;
		if (std::strcmp(folder, "tracks") == 0 && d.contains("laps"))
		{
			const auto pending = nlohmann::json::parse(
			    ReadSmallPlainFile(fs::u8path(SoloPath("needs-laps")) / fs::u8path(keeperName), kDescriptorMax), nullptr, false);
			const auto merged = AP_SoloMergeNeedsLaps(d, pending);
			if (!merged.is_null())
			{
				// The old file holds the player's laps until they are safe in the new one.
				if (!KeepDescriptorFile("tracks", keeperName, merged.dump()))
				{
					why = "its laps could not be moved to solo/tracks/" + keeperName;
					Log("kept solo/tracks/" + name + ": " + why);
					return false;
				}
				DropNeedsLaps(keeperName);
			}
		}
		if (RemoveDescriptorFile(target, detail))
			Log(std::string("retired solo/") + folder + "/" + name + ": a newer install of the same package lists the track under another key");
		else
			Log(std::string("could not retire solo/") + folder + "/" + name + ": " + detail);
	}
	return true;
}

// The one writer behind both C entry points and the store publication: kind 1
// is a complete descriptor for solo/tracks, kind 2 one that waits for its laps
// in solo/needs-laps. Every refusal is logged with its file name and gives
// `why` a short reason the player can act on.
bool RecordDescriptor(int kind, const char *contentId, const char *revision, const char *json,
                      const DescriptorScope &scope, std::string &why)
{
	if (!contentId || !revision || !json)
	{
		why = "internal error";
		Log("refused a descriptor without id, revision or body");
		return false;
	}
	const std::string name = AP_SoloDescriptorFileName(contentId, revision);
	auto refuse = [&](const std::string &reason, const std::string &detail) {
		why = reason;
		Log("refused " + (name.empty() ? std::string(contentId) + " " + revision : name) + ": " + detail);
		return false;
	};
	if (name.empty())
		return refuse("its id or version is not a valid file name", "not a valid descriptor file name");
	const nlohmann::json fresh = nlohmann::json::parse(json, nullptr, false);
	if (!fresh.is_object() || !fresh.contains("files"))
		return refuse("its descriptor is not valid", "descriptor is not an object with file pins");
	if (SoloRootIsLink())
		return refuse("the solo folder is a link; make it a normal folder", "solo is a link");
	std::error_code ec;
	const fs::path tracks = fs::u8path(SoloPath("tracks"));
	if (fs::is_symlink(tracks, ec))
		return refuse("solo/tracks is a link; make it a normal folder", "solo/tracks is a link");
	const nlohmann::json kept = nlohmann::json::parse(ReadSmallPlainFile(tracks / fs::u8path(name), kDescriptorMax),
	                                                  nullptr, false);
	const nlohmann::json pending = nlohmann::json::parse(
	    ReadSmallPlainFile(fs::u8path(SoloPath("needs-laps")) / fs::u8path(name), kDescriptorMax), nullptr, false);
	const bool keptOther = kept.is_object() && kept.contains("files") && kept["files"] != fresh["files"];
	const bool pendingOther = pending.is_object() && pending.contains("files") && pending["files"] != fresh["files"];
	for (const auto &[other, d, folder] : {std::make_tuple(keptOther, &kept, "solo/tracks"),
	                                       std::make_tuple(pendingOther, &pending, "solo/needs-laps")})
	{
		if (!other)
			continue;
		if (!MayReplace(*d, contentId, revision, scope))
			return refuse("another installed copy has the same name and version but other files; "
			              "reinstall the copy you want to play",
			              std::string(folder) + " holds other file pins of a copy that is still installed");
		Log(std::string("replacing ") + folder + "/" + name + ": " +
		    (scope.fresh ? "a copy with other files was just installed" : "its files are no longer installed"));
	}
	// A replaced entry for other files goes first, so a file that cannot be
	// removed (in use, antivirus) leaves one state, not both.
	std::string detail;
	if (pendingOther && !RemoveDescriptorFile(fs::u8path(SoloPath("needs-laps")) / fs::u8path(name), detail))
		return refuse("its file in solo/needs-laps is read-only or in use (antivirus?)", "stale entry kept: " + detail);
	if (kind == 2 && keptOther && !RemoveDescriptorFile(tracks / fs::u8path(name), detail))
		return refuse("its file in solo/tracks is read-only or in use (antivirus?)", "stale entry kept: " + detail);
	if (kind != 2)
	{
		if (!KeepDescriptorFile("tracks", name, json, &why))
			return false;
		DropNeedsLaps(name);
		return true;
	}
	// A chosen lap count stays with the same pins. A reinstall may change
	// title, author or capabilities: publish those, keep the player's laps.
	if (kept.is_object() && kept.contains("files") && !keptOther)
	{
		const auto merged = AP_SoloMergeNeedsLaps(kept, fresh);
		if (merged.is_null())
			return refuse("its kept lap count is not 1 to 7; delete its file in solo/tracks",
			              "kept laps are missing or out of range");
		if (!AP_SoloDescriptorUnchanged(kept, merged) && !KeepDescriptorFile("tracks", name, merged.dump(), &why))
			return false;
		DropNeedsLaps(name);
		return true;
	}
	return KeepDescriptorFile("needs-laps", name, json, &why);
}
} // namespace

extern "C" int AP_Solo_RecordTrackDescriptor(const char *contentId, const char *revision, const char *json)
{
	std::string why;
	return RecordDescriptor(1, contentId, revision, json, DescriptorScope(), why) ? 1 : 0;
}

extern "C" int AP_Solo_RecordTrackNeedsLaps(const char *contentId, const char *revision, const char *json)
{
	std::string why;
	return RecordDescriptor(2, contentId, revision, json, DescriptorScope(), why) ? 1 : 0;
}

#ifdef CTR_CUSTOM_PACKAGES
namespace {
// PinSignature of a verified store package's LEV and VRM, or "".
std::string StoreSignature(const CustomPackageManifest &m)
{
	const CustomPackageFile *lev = nullptr, *vrm = nullptr;
	for (unsigned int i = 0; i < m.count; i++)
	{
		if (!std::strcmp(m.files[i].role, "lev")) lev = &m.files[i];
		if (!std::strcmp(m.files[i].role, "vrm")) vrm = &m.files[i];
	}
	if (!lev || !vrm)
		return std::string();
	return std::string(m.version) + "|lev:" + lev->sha256 + ":" + std::to_string((std::uintmax_t)lev->bytes) +
	       "|vrm:" + vrm->sha256 + ":" + std::to_string((std::uintmax_t)vrm->bytes);
}

// A short name for a store package in a menu message: its title, else its pin.
std::string PackageLabel(const CustomPackageStoreEntry &entry)
{
	std::string title = entry.manifest.title[0] ? entry.manifest.title : "package " + std::string(entry.pin).substr(0, 12);
	if (title.size() > 40)
	{
		size_t cut = 37; // never inside a UTF-8 sequence
		while (cut > 0 && ((unsigned char)title[cut] & 0xc0) == 0x80)
			cut--;
		title = title.substr(0, cut) + "...";
	}
	return title;
}

struct StorePublication
{
	// kind 0 retires `key` (json holds the package uuid) once the copy `keeper`
	// (pin, under keeperKey) is published.
	struct Descriptor
	{
		int kind;
		std::string key, revision, json, title, pin, keeper, keeperKey;
		Descriptor(int k, std::string key_, std::string revision_, std::string json_, std::string title_, std::string pin_,
		           std::string keeper_ = std::string(), std::string keeperKey_ = std::string())
		    : kind(k), key(std::move(key_)), revision(std::move(revision_)), json(std::move(json_)), title(std::move(title_)),
		      pin(std::move(pin_)), keeper(std::move(keeper_)), keeperKey(std::move(keeperKey_)) {}
	};
	std::vector<Descriptor> descriptors;
	std::set<std::string> live; // StoreSignature of every verified package
	mutable std::set<std::string> listed; // pins whose descriptor was written
	std::vector<std::string> notes; // log lines from the worker, written on the game thread
	int failed = 0;
	int published = 0;
	std::string assets;
	std::string pin;
	std::string error;
	const std::atomic<bool> *cancel = nullptr;
};
void PublishStoreEntry(const CustomPackageStoreEntry *entry, void *opaque)
{
	auto &result = *static_cast<StorePublication *>(opaque);
	if (result.cancel && result.cancel->load())
		throw std::runtime_error("Track verification cancelled");
	if (entry->verified)
		result.live.insert(StoreSignature(entry->manifest));
	if (!result.pin.empty() && result.pin != entry->pin) return;
	result.published++;
	if (!entry->verified)
	{
		result.failed++;
		const std::string named = PackageLabel(*entry) + ": " + entry->error + "; reinstall or remove this track";
		result.notes.push_back("package " + std::string(entry->pin) + " not published: " + named);
		if (result.error.empty()) result.error = named;
		return;
	}
	char key[65], revision[65], json[16384], error[256];
	std::string knownKey;
	if (entry->local.trackID > 0)
	{
		const CustomPackageFile *lev = nullptr, *vrm = nullptr;
		for (unsigned int i = 0; i < entry->manifest.count; i++)
		{
			if (!std::strcmp(entry->manifest.files[i].role, "lev")) lev = &entry->manifest.files[i];
			if (!std::strcmp(entry->manifest.files[i].role, "vrm")) vrm = &entry->manifest.files[i];
		}
		ApTrackRegistryMatch match = {};
		if (lev && vrm && ap_track_registry_lookup(lev->sha256, lev->bytes, vrm->sha256, vrm->bytes, &match) == AP_TRACK_REGISTRY_MATCH)
			knownKey = match.track_key;
		if (knownKey.empty())
		{
			static const auto named = nlohmann::json::parse(kContentRegistryJSON).at("named_blocks");
			for (const auto &row : named)
				if (row.value("name", "") == entry->manifest.title)
				{
					knownKey = row.value("key", "");
					break;
				}
		}
	}
	const int kind = CustomPackage_StoreDescriptor(result.assets.c_str(), entry->pin,
		knownKey.empty() ? nullptr : knownKey.c_str(),
		key, sizeof key, revision, sizeof revision, json, sizeof json, error, sizeof error);
	if (!kind)
	{
		result.failed++;
		const std::string named = PackageLabel(*entry) + ": " + error;
		result.notes.push_back("package " + std::string(entry->pin) + " not published: " + named);
		if (result.error.empty()) result.error = named;
	}
	else
		result.descriptors.push_back({kind, key, revision, json, entry->manifest.title, entry->pin});
}

// Two installed packages can resolve to one Play Solo entry (same track key
// and version) with different files: a track reinstalled after its files
// changed without a new version, or two copies whose titles fold to the same
// key. Only one can own solo/tracks/<key>__<version>.json: the copy that owns
// it now while it is still installed, otherwise the newest install. The others
// stay installed, so seeds that pin them still load; Play Solo leaves them out
// and the log says which and why.
void KeepOneCopyPerEntry(StorePublication &result)
{
	using Descriptor = StorePublication::Descriptor;
	std::vector<Descriptor> &all = result.descriptors;
	std::vector<bool> skip(all.size(), false);
	auto pins = [](const std::string &json) {
		const auto d = nlohmann::json::parse(json, nullptr, false);
		return d.is_object() ? d.value("files", nlohmann::json()) : nlohmann::json();
	};
	auto files = [&](const Descriptor &d) { return pins(d.json); };
	auto installed = [&](const Descriptor &d) {
		std::error_code ec;
		const auto t = fs::last_write_time(fs::u8path(result.assets) / "tracks/packages" / d.pin / "manifest.json", ec);
		return ec ? fs::file_time_type::min() : t;
	};
	// One track imported again under another name has one package uuid and
	// revision but two packages and, when the name sets the key, two keys. Only
	// the newest install keeps a row; the key an older copy used is retired so
	// its descriptor file stops listing the same files a second time (A3-29).
	std::vector<Descriptor> retired;
	{
		std::vector<APSoloStoreCopy> copies;
		for (const Descriptor &d : all)
		{
			const auto parsed = nlohmann::json::parse(d.json, nullptr, false);
			const auto stamp = installed(d).time_since_epoch().count();
			copies.push_back({d.key, d.revision, parsed.is_object() ? AP_SoloJsonString(parsed, "uuid") : std::string(),
			                  d.pin, static_cast<long long>(stamp)});
		}
		const std::vector<size_t> into = AP_SoloFoldStoreCopies(copies);
		for (size_t i = 0; i < all.size(); i++)
		{
			if (into[i] == i)
				continue;
			skip[i] = true;
			result.notes.push_back("left " + all[i].title + " (package " + all[i].pin.substr(0, 12) + ") out of Play Solo: " +
			    all[into[i]].title + " (package " + all[into[i]].pin.substr(0, 12) + ") is the newest install of the same track");
			if (all[i].key != all[into[i]].key)
				retired.push_back({0, all[i].key, all[i].revision, copies[i].uuid, all[i].title, all[i].pin,
				                   all[into[i]].pin, all[into[i]].key});
		}
	}
	for (size_t i = 0; i < all.size(); i++)
	{
		if (skip[i])
			continue;
		const std::string name = AP_SoloDescriptorFileName(all[i].key, all[i].revision);
		std::vector<size_t> same;
		bool differ = false;
		for (size_t j = i; j < all.size(); j++)
			if (!skip[j] && AP_SoloDescriptorFileName(all[j].key, all[j].revision) == name)
			{
				same.push_back(j);
				differ = differ || files(all[j]) != files(all[i]);
			}
		if (!differ || name.empty())
			continue;
		nlohmann::json owner;
		for (const char *folder : {"tracks", "needs-laps"})
			if (owner.is_null())
				owner = pins(ReadSmallPlainFile(fs::u8path(SoloPath(folder)) / fs::u8path(name), kDescriptorMax));
		size_t keep = same[0];
		bool current = false;
		for (size_t j : same)
			if (!current && !owner.is_null() && files(all[j]) == owner)
			{
				keep = j;
				current = true;
			}
		if (!current)
			for (size_t j : same)
				if (installed(all[j]) > installed(all[keep]) ||
				    (installed(all[j]) == installed(all[keep]) && all[j].pin > all[keep].pin))
					keep = j;
		const nlohmann::json kept = files(all[keep]);
		for (size_t j : same)
			if (files(all[j]) != kept)
			{
				skip[j] = true;
				result.notes.push_back("left " + all[j].title + " (package " + all[j].pin.substr(0, 12) + ") out of Play Solo: " + name +
				    " belongs to " + all[keep].title + " (package " + all[keep].pin.substr(0, 12) + "), " +
				    (current ? "which owns it now" : "the newest install") + "; same id and version, other files");
			}
	}
	std::vector<Descriptor> out;
	for (size_t i = 0; i < all.size(); i++)
		if (!skip[i])
			out.push_back(std::move(all[i]));
	for (Descriptor &d : retired)
		out.push_back(std::move(d));
	all = std::move(out);
}
StorePublication CollectInstalledStore(const std::string &assets, const std::string &pin,
                                      const std::atomic<bool> *cancel = nullptr)
{
	StorePublication result;
	result.assets = assets;
	result.pin = pin;
	result.cancel = cancel;
	{
		const fs::path store = fs::u8path(assets) / "tracks/packages";
		std::error_code ec;
		if (!fs::exists(store, ec) && result.pin.empty()) return result;
		char scanError[256] = {};
		if (!CustomPackage_ScanStore(store.u8string().c_str(), PublishStoreEntry, &result,
		                             scanError, sizeof scanError))
		{
			result.failed++;
			if (result.error.empty()) result.error = scanError;
		}
	}
	if (!result.pin.empty() && !result.published)
	{
		result.failed++;
		result.error = "Installed package was not found in the shared store";
	}
	if (result.pin.empty())
		KeepOneCopyPerEntry(result);
	return result;
}

// Writes one collected descriptor. On a refusal `message` names the track and
// says what to do (the menus show at most 170 characters of it).
bool ApplyStoreDescriptor(const StorePublication::Descriptor &d, const StorePublication &from, std::string &message)
{
	if (d.kind == 0)
	{
		// Only once the copy that replaces it is listed: a failed write must not leave no row at all.
		std::string why;
		if (from.listed.count(d.keeper) && !RetireDescriptor(d.key, d.revision, d.json, d.keeperKey, why))
		{
			message = (d.title.empty() ? d.key : d.title) + " " + d.revision + ": " + why;
			return false;
		}
		return true;
	}
	DescriptorScope scope;
	scope.live = &from.live;
	scope.fresh = !from.pin.empty();
	std::string why;
	if (RecordDescriptor(d.kind, d.key.c_str(), d.revision.c_str(), d.json.c_str(), scope, why))
	{
		from.listed.insert(d.pin);
		return true;
	}
	std::string title = d.title.empty() ? d.key : d.title;
	if (title.size() > 40)
	{
		size_t cut = 37; // never inside a UTF-8 sequence
		while (cut > 0 && ((unsigned char)title[cut] & 0xc0) == 0x80)
			cut--;
		title = title.substr(0, cut) + "...";
	}
	Log("package descriptor not published: " + title + " " + d.revision + ": " + why);
	message = title + " " + (d.revision.size() > 16 ? d.revision.substr(0, 13) + "..." : d.revision) + ": " + why;
	if (message.size() > kMenuMessageMax)
	{
		size_t cut = kMenuMessageMax; // never inside a UTF-8 sequence
		while (cut > 0 && ((unsigned char)message[cut] & 0xc0) == 0x80)
			cut--;
		message.resize(cut);
	}
	return false;
}

std::future<StorePublication> g_trackPublication;
std::atomic<bool> g_trackPublicationCancel{false};
StorePublication g_trackPublicationResult;
std::vector<std::string> g_trackPublicationArgs;
std::string g_trackPublicationFailure; // the first refused track, for the menu
size_t g_trackPublicationIndex = 0;
bool g_trackPublishing = false;
} // namespace
#endif

namespace {
bool TrackPublicationBusy()
{
#ifdef CTR_CUSTOM_PACKAGES
	return g_trackPublication.valid() || g_trackPublishing;
#else
	return false;
#endif
}

bool StartTrackPublication(const std::vector<std::string> &args)
{
#ifdef CTR_CUSTOM_PACKAGES
	if (g_job.state == AP_SOLO_JOB_RUNNING || TrackPublicationBusy() || !AP_Solo_RuntimePresent())
		return false;
	g_job.child.release();
	g_job.kind = JOB_TRACKS;
	g_job.state = AP_SOLO_JOB_RUNNING;
	g_job.phase = "Verifying installed tracks";
	g_job.errorText.clear(); g_job.errorOption.clear(); g_job.resultRoom.clear();
	g_job.result = nlohmann::json();
	if (SoloRootIsLink())
	{
		JobFail("link", "The solo folder is a link. Make it a normal folder inside the client folder.", "");
		return true;
	}
	g_trackPublicationArgs = args;
	g_trackPublicationCancel.store(false);
	const std::string assets = AssetsDir();
	try
	{
		g_trackPublication = std::async(std::launch::async, [assets] {
			return CollectInstalledStore(assets, "", &g_trackPublicationCancel);
		});
	}
	catch (const std::exception &e) { JobFail("publish", e.what(), ""); }
	return true;
#else
	return JobStart(JOB_TRACKS, args, kQuickLimitMs);
#endif
}

bool TickTrackPublication()
{
#ifdef CTR_CUSTOM_PACKAGES
	if (g_trackPublication.valid())
	{
		if (g_trackPublication.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			return true;
		try { g_trackPublicationResult = g_trackPublication.get(); }
		catch (const std::exception &e)
		{
			if (!g_trackPublicationCancel.load()) JobFail("publish", e.what(), "");
			return true;
		}
		if (g_trackPublicationCancel.load())
		{
			g_trackPublicationResult = StorePublication();
			return true;
		}
		for (const std::string &note : g_trackPublicationResult.notes)
			Log(note);
		// A refused package does not stop the others; the first is named at the end.
		g_trackPublicationIndex = 0;
		g_trackPublicationFailure = g_trackPublicationResult.failed ? g_trackPublicationResult.error : std::string();
		g_trackPublishing = true;
		g_job.phase = "Publishing track entries";
	}
	if (g_trackPublishing)
	{
		if (g_trackPublicationIndex < g_trackPublicationResult.descriptors.size())
		{
			// One small descriptor per frame; package verification never runs here.
			// A refused track does not stop the others.
			const auto &d = g_trackPublicationResult.descriptors[g_trackPublicationIndex++];
			std::string message;
			if (!ApplyStoreDescriptor(d, g_trackPublicationResult, message) && g_trackPublicationFailure.empty())
				g_trackPublicationFailure = message;
			return true;
		}
		g_trackPublishing = false;
		g_trackPublicationResult = StorePublication();
		if (!g_trackPublicationFailure.empty())
		{
			JobFail("publish", g_trackPublicationFailure, "");
			return true;
		}
		g_job.state = AP_SOLO_JOB_IDLE;
		if (!JobStart(JOB_TRACKS, g_trackPublicationArgs, kQuickLimitMs))
		{
			// The menu is already busy on this job; an idle state would read as success.
			g_job.kind = JOB_TRACKS;
			JobFail("start", "the runtime is missing", "");
		}
		return true;
	}
#endif
	return false;
}

void CancelTrackPublication()
{
#ifdef CTR_CUSTOM_PACKAGES
	g_trackPublicationCancel.store(true);
	g_trackPublishing = false;
	g_trackPublicationResult = StorePublication();
#endif
}

void FinishTrackPublication()
{
#ifdef CTR_CUSTOM_PACKAGES
	CancelTrackPublication();
	if (g_trackPublication.valid())
	{
		try { g_trackPublication.get(); } catch (const std::exception &) {}
	}
#endif
}
} // namespace

extern "C" int AP_Solo_PublishInstalledStore(const char *pin, char *error, size_t errorSize)
{
	if (error && errorSize) error[0] = 0;
#ifdef CTR_CUSTOM_PACKAGES
	auto result = CollectInstalledStore(AssetsDir(), pin ? pin : "");
	for (const std::string &note : result.notes)
		Log(note);
	for (const auto &d : result.descriptors)
	{
		std::string message;
		if (!ApplyStoreDescriptor(d, result, message) && !result.failed++)
			result.error = message;
	}
	if (result.failed)
	{
		if (error && errorSize) std::snprintf(error, errorSize, "%s", result.error.c_str());
		Log("package descriptor publication failed: " + result.error);
		return 0;
	}
#else
	(void)pin;
#endif
	return 1;
}

extern "C" void AP_Solo_TrackStepLaps(int index, int direction)
{
	if (index >= 0 && index < (int)g_tracks.size() && g_tracks[index].needsLaps)
		g_tracks[index].laps = AP_SoloLapsStep(g_tracks[index].laps, direction);
}

extern "C" int AP_Solo_TrackKeepLaps(int index)
{
	if (index < 0 || index >= (int)g_tracks.size() || !g_tracks[index].needsLaps)
		return 0;
	const std::string name = NeedsLapsFileName(g_tracks[index]);
	std::error_code ec;
	const fs::path dir = fs::u8path(SoloPath("needs-laps"));
	if (name.empty() || SoloRootIsLink() || fs::is_symlink(dir, ec))
		return 0;
	const nlohmann::json d =
	    nlohmann::json::parse(ReadSmallPlainFile(dir / fs::u8path(name), kDescriptorMax), nullptr, false);
	if (!AP_SoloNeedsLapsDescriptor(d, name))
		return 0;
	const std::string json = AP_SoloDescriptorWithLaps(d, g_tracks[index].laps);
	if (json.empty() || !KeepDescriptorFile("tracks", name, json))
		return 0;
	DropNeedsLaps(name);
	return 1;
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
	CopyText(out->value, sizeof out->value, o.kind == "set" ? AP_SoloSetSummary(o) : o.value);
	out->kind = o.kind == "toggle"   ? AP_SOLO_OPT_TOGGLE
	            : o.kind == "choice" ? AP_SOLO_OPT_CHOICE
	            : o.kind == "set"    ? AP_SOLO_OPT_SET
	                                 : AP_SOLO_OPT_RANGE;
	out->visible = AP_SoloOptionVisible(g_options, o) ? 1 : 0;
	out->forced = o.forced.empty() ? 0 : 1;
	out->changed = o.value != o.initial ? 1 : 0;
	const std::string guard = AP_SoloGuardReason(g_options, o);
	out->blocked = guard.empty() ? 0 : 1;
	if (!o.forced.empty())
		CopyText(out->note, sizeof out->note, o.forcedReason);
	else if (!guard.empty())
		CopyText(out->note, sizeof out->note, guard);
	else if (AP_SoloOptionWarns(g_options, o))
		CopyText(out->note, sizeof out->note, o.warnReason);
	return 1;
}

extern "C" int AP_Solo_OptionItemCount(int index)
{
	if (index < 0 || index >= (int)g_options.size() || g_options[index].kind != "set")
		return 0;
	return (int)g_options[index].keys.size();
}

extern "C" int AP_Solo_OptionItemGet(int index, int item, struct APSoloItemInfo *out)
{
	if (!out || item < 0 || item >= AP_Solo_OptionItemCount(index))
		return 0;
	const APSoloOption &o = g_options[index];
	std::memset(out, 0, sizeof *out);
	CopyText(out->label, sizeof out->label, o.keyLabels[item]);
	out->on = AP_SoloSetHas(o, o.keys[item]) ? 1 : 0;
	out->isDefault = item < (int)o.keyDefault.size() && o.keyDefault[item] ? 1 : 0;
	return 1;
}

extern "C" void AP_Solo_OptionItemToggle(int index, int item)
{
	if (index >= 0 && index < (int)g_options.size())
		AP_SoloSetToggle(g_options[index], item);
}

extern "C" int AP_Solo_OptionsBlocked(void) { return AP_SoloFirstBlocked(g_options); }

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

extern "C" const char *AP_Solo_Password(void) { return g_sessionPassword.c_str(); }

extern "C" int AP_Solo_SetPassword(const char *password)
{
	if (!password || std::strlen(password) >= 64 || AP_Solo_SessionActive())
		return 0;
	g_sessionPassword = password;
	return 1;
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

extern "C" int AP_Solo_SessionOwnsConnection(void)
{
	// Stopping has already closed the solo connection, so any connection then is
	// the next room's own.
	const int s = g_session.state;
	return s == AP_SOLO_SESSION_STARTING || s == AP_SOLO_SESSION_CONNECTING || s == AP_SOLO_SESSION_LIVE;
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
	ap_net_set_solo_session_query(AP_Solo_SessionOwnsConnection);
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
	FinishTrackPublication(); // Quit only: never join a verifier from a frame.
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
