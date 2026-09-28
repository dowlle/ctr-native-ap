// Discord Rich Presence IPC client (issue #366). See ap_discord_ipc.h for the
// contract and ap_discord_logic.h for the design.
//
// Protocol (Discord's local RPC over IPC, the same one the discord-rpc library
// uses): connect to the Discord client's named pipe \\.\pipe\discord-ipc-N
// (Windows) or Unix socket discord-ipc-N (Linux, under the runtime or temp
// directory; Flatpak and Snap Discord put it in a subdirectory), N = 0..9.
// Send HANDSHAKE {"v":1,"client_id":...}, wait for the READY dispatch, then
// send FRAME {"cmd":"SET_ACTIVITY","args":{"pid":...,"activity":...},"nonce":...}.
// Discord answers every command; the answers are read and dropped, except an
// ERROR (counted once per connection, for the log) and PING (answered with
// PONG). A CLOSE frame or a read/write failure ends the connection; the
// scheduler then retries.

#include "ap_discord_ipc.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

using nlohmann::json;

namespace
{

uint32_t now_ms()
{
	using namespace std::chrono;
	return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

long self_pid()
{
#ifdef _WIN32
	return (long)GetCurrentProcessId();
#else
	return (long)getpid();
#endif
}

// ── Transport ──────────────────────────────────────────────────────────────
class Pipe
{
public:
	~Pipe() { close(); }

	bool is_open() const
	{
#ifdef _WIN32
		return h_ != INVALID_HANDLE_VALUE;
#else
		return fd_ >= 0;
#endif
	}

	void close()
	{
#ifdef _WIN32
		if (h_ != INVALID_HANDLE_VALUE)
			CloseHandle(h_);
		h_ = INVALID_HANDLE_VALUE;
#else
		if (fd_ >= 0)
			::close(fd_);
		fd_ = -1;
#endif
	}

	bool open(const std::string &dirOverride)
	{
		close();
#ifdef _WIN32
		(void)dirOverride;
		for (int i = 0; i < 10; i++)
		{
			char name[64];
			std::snprintf(name, sizeof name, "\\\\.\\pipe\\discord-ipc-%d", i);
			HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
			                       OPEN_EXISTING, 0, nullptr);
			if (h != INVALID_HANDLE_VALUE)
			{
				h_ = h;
				return true;
			}
		}
		return false;
#else
		std::vector<std::string> dirs;
		if (!dirOverride.empty())
			dirs.push_back(dirOverride);
		else
		{
			static const char *const envs[] = {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"};
			std::vector<std::string> bases;
			for (const char *e : envs)
			{
				const char *v = std::getenv(e);
				if (v != nullptr && *v != '\0')
					bases.push_back(v);
			}
			bases.push_back("/tmp");
			// Plain install, Flatpak (stable and Canary), Snap, Vesktop Flatpak.
			static const char *const subs[] = {
				"", "app/com.discordapp.Discord", "app/com.discordapp.DiscordCanary",
				"snap.discord", ".flatpak/dev.vencord.Vesktop/xdg-run",
			};
			for (const std::string &b : bases)
				for (const char *sub : subs)
					dirs.push_back(*sub ? b + "/" + sub : b);
		}
		for (const std::string &d : dirs)
			for (int i = 0; i < 10; i++)
			{
				std::string path = d + "/discord-ipc-" + std::to_string(i);
				if (try_connect(path))
					return true;
			}
		return false;
#endif
	}

	bool write_all(const void *data, size_t len)
	{
		const char *p = (const char *)data;
#ifdef _WIN32
		while (len > 0)
		{
			DWORD wrote = 0;
			if (!WriteFile(h_, p, (DWORD)len, &wrote, nullptr) || wrote == 0)
				return false;
			p += wrote;
			len -= wrote;
		}
		return true;
#else
		while (len > 0)
		{
#ifdef MSG_NOSIGNAL
			ssize_t n = ::send(fd_, p, len, MSG_NOSIGNAL);
#else
			ssize_t n = ::send(fd_, p, len, 0);
#endif
			if (n < 0 && errno == EINTR)
				continue;
			if (n <= 0)
				return false;
			p += n;
			len -= (size_t)n;
		}
		return true;
#endif
	}

	// 1 = got all bytes, 0 = timed out, -1 = closed or failed.
	int read_exact(void *data, size_t len, unsigned timeoutMs)
	{
		char *p = (char *)data;
		const uint32_t start = now_ms();
		while (len > 0)
		{
			const uint32_t spent = now_ms() - start;
			const unsigned left = spent >= timeoutMs ? 0 : timeoutMs - spent;
#ifdef _WIN32
			DWORD avail = 0;
			if (!PeekNamedPipe(h_, nullptr, 0, nullptr, &avail, nullptr))
				return -1;
			if (avail == 0)
			{
				if (left == 0)
					return 0;
				Sleep(5);
				continue;
			}
			DWORD want = avail < len ? avail : (DWORD)len, got = 0;
			if (!ReadFile(h_, p, want, &got, nullptr) || got == 0)
				return -1;
			p += got;
			len -= got;
#else
			struct pollfd pfd = {fd_, POLLIN, 0};
			int r = ::poll(&pfd, 1, (int)left);
			if (r < 0 && errno == EINTR)
				continue;
			if (r < 0)
				return -1;
			if (r == 0)
				return 0;
			ssize_t n = ::recv(fd_, p, len, 0);
			if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
				continue;
			if (n <= 0)
				return -1;
			p += n;
			len -= (size_t)n;
#endif
		}
		return 1;
	}

	// 1 = data (or an end-of-stream) waiting, 0 = nothing, -1 = failed.
	int readable()
	{
#ifdef _WIN32
		DWORD avail = 0;
		if (!PeekNamedPipe(h_, nullptr, 0, nullptr, &avail, nullptr))
			return -1;
		return avail > 0 ? 1 : 0;
#else
		struct pollfd pfd = {fd_, POLLIN, 0};
		int r = ::poll(&pfd, 1, 0);
		if (r < 0)
			return errno == EINTR ? 0 : -1;
		if (r == 0)
			return 0;
		if (pfd.revents & (POLLERR | POLLNVAL))
			return -1;
		return 1; // POLLIN or POLLHUP: the read tells which
#endif
	}

private:
#ifdef _WIN32
	HANDLE h_ = INVALID_HANDLE_VALUE;
#else
	int fd_ = -1;

	bool try_connect(const std::string &path)
	{
		struct sockaddr_un addr;
		if (path.size() >= sizeof addr.sun_path)
			return false;
		int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0)
			return false;
		::fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
		int one = 1;
		::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
		std::memset(&addr, 0, sizeof addr);
		addr.sun_family = AF_UNIX;
		std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
		if (::connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0)
		{
			::close(fd);
			return false;
		}
		// A write never waits long for a stuck Discord.
		struct timeval tv = {2, 0};
		::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
		fd_ = fd;
		return true;
	}
#endif
};

bool write_frame(Pipe &p, uint32_t op, const std::string &payload)
{
	std::string buf(AP_DISCORD_FRAME_HEADER, '\0');
	AP_Discord_PutHeader((unsigned char *)&buf[0], op, (uint32_t)payload.size());
	buf += payload;
	return p.write_all(buf.data(), buf.size());
}

// 1 = frame read, 0 = nothing within timeoutMs, -1 = closed, failed or bad frame.
int read_frame(Pipe &p, unsigned timeoutMs, uint32_t *op, std::string *payload)
{
	unsigned char hdr[AP_DISCORD_FRAME_HEADER];
	int r = p.read_exact(hdr, sizeof hdr, timeoutMs);
	if (r <= 0)
		return r;
	uint32_t len = 0;
	if (!AP_Discord_GetHeader(hdr, op, &len))
		return -1;
	payload->assign(len, '\0');
	if (len > 0 && p.read_exact(&(*payload)[0], len, timeoutMs > 2000 ? timeoutMs : 2000) != 1)
		return -1;
	return 1;
}

std::string handshake_json(const std::string &appId)
{
	json j = {{"v", 1}, {"client_id", appId}};
	return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string set_activity_json(const ApDiscordActivity *a, long pid, unsigned nonce)
{
	json args;
	args["pid"] = pid;
	if (a != nullptr)
	{
		json act = json::object();
		if (std::strlen(a->details) >= 2)
			act["details"] = a->details;
		if (std::strlen(a->state) >= 2)
			act["state"] = a->state;
		if (a->startEpoch > 0)
			act["timestamps"] = {{"start", a->startEpoch}};
		act["assets"] = {{"large_image", AP_DISCORD_LARGE_IMAGE_KEY},
		                 {"large_text", AP_DISCORD_LARGE_IMAGE_TEXT}};
		act["buttons"] = json::array({{{"label", AP_DISCORD_BUTTON_LABEL},
		                               {"url", AP_DISCORD_BUTTON_URL}}});
		args["activity"] = act;
	}
	else
		args["activity"] = nullptr;
	json j = {{"cmd", "SET_ACTIVITY"}, {"args", args}, {"nonce", std::to_string(nonce)}};
	return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

// A string member of a JSON object, or "" when absent, null or not a string.
// (json::value() throws on a present null, and Discord sends "evt": null.)
std::string str_field(const json &j, const char *key)
{
	if (!j.is_object())
		return std::string();
	auto it = j.find(key);
	if (it == j.end() || !it->is_string())
		return std::string();
	return it->get<std::string>();
}

int copy_out(const std::string &s, char *buf, int n)
{
	if (buf == nullptr || n <= 0 || (size_t)n <= s.size())
		return -1;
	std::memcpy(buf, s.c_str(), s.size() + 1);
	return (int)s.size();
}

// ── Shared state ───────────────────────────────────────────────────────────
struct Shared
{
	std::mutex m;
	std::condition_variable wake;
	std::condition_variable done;
	bool enabled = false;
	std::string appId;
	ApDiscordActivity act{};
	uint32_t actGen = 0;
	bool shutdown = false;
	bool finished = false;
	bool started = false;
	std::atomic<int> status{AP_DISCORD_STATUS_OFF};
	unsigned errCount = 0;
	std::string errMsg;
	uint32_t firstRetryMs = 15000, retryMs = 30000, minSendMs = 15000, pollMs = 1000;
	std::string socketDir;
};

// Created once and never destroyed: the worker is detached and may still be
// running while the process runs its exit-time destructors.
std::atomic<Shared *> g_shared{nullptr};
std::mutex g_sharedInit;

Shared *shared_state()
{
	std::lock_guard<std::mutex> lk(g_sharedInit);
	Shared *s = g_shared.load();
	if (s == nullptr)
	{
		s = new Shared();
		g_shared.store(s);
	}
	return s;
}

class Worker
{
public:
	explicit Worker(Shared *s) : s_(s) {}

	void run()
	{
		std::unique_lock<std::mutex> lk(s_->m);
		AP_DiscordSched_Init(&sched_, s_->firstRetryMs, s_->retryMs, s_->minSendMs, s_->pollMs);
		const std::string dir = s_->socketDir;
		while (!s_->shutdown)
		{
			const bool wanted = s_->enabled && !s_->appId.empty();
			const bool idChanged = sched_.connected && s_->appId != connectedId_;
			uint32_t waitMs = 0;
			const int act = AP_DiscordSched_Next(&sched_, now_ms(), wanted && !idChanged,
			                                     s_->actGen, &waitMs);
			if (act == AP_DISCORD_DO_CONNECT)
			{
				const std::string id = s_->appId;
				lk.unlock();
				bool rejected = false;
				const bool ok = connect(dir, id, &rejected);
				lk.lock();
				AP_DiscordSched_OnConnect(&sched_, now_ms(), ok);
				if (ok)
				{
					connectedId_ = id;
					errorSeen_ = false;
				}
				s_->status = ok ? AP_DISCORD_STATUS_CONNECTED
				                : (rejected ? AP_DISCORD_STATUS_REJECTED : AP_DISCORD_STATUS_SEARCHING);
			}
			else if (act == AP_DISCORD_DO_SEND)
			{
				const ApDiscordActivity a = s_->act;
				const uint32_t gen = s_->actGen;
				lk.unlock();
				const bool ok = write_frame(pipe_, AP_DISCORD_OP_FRAME,
				                            set_activity_json(&a, self_pid(), ++nonce_));
				lk.lock();
				if (ok)
					AP_DiscordSched_OnSent(&sched_, now_ms(), gen);
				else
					drop();
			}
			else if (act == AP_DISCORD_DO_CLOSE)
			{
				lk.unlock();
				clear_and_close();
				lk.lock();
				AP_DiscordSched_OnClosed(&sched_);
				s_->status = AP_DISCORD_STATUS_OFF;
			}
			else
			{
				if (!wanted && !sched_.connected)
					s_->status = AP_DISCORD_STATUS_OFF;
				if (sched_.connected)
				{
					lk.unlock();
					const int r = drain();
					lk.lock();
					if (r < 0)
					{
						drop();
						continue;
					}
				}
				if (s_->shutdown)
					break;
				if (waitMs == AP_DISCORD_WAIT_FOREVER)
					s_->wake.wait(lk);
				else
					s_->wake.wait_for(lk, std::chrono::milliseconds(waitMs));
			}
		}
		lk.unlock();
		clear_and_close();
		lk.lock();
		s_->status = AP_DISCORD_STATUS_OFF;
		s_->finished = true;
		s_->done.notify_all();
	}

private:
	Shared *s_;
	Pipe pipe_;
	ApDiscordSched sched_{};
	std::string connectedId_;
	unsigned nonce_ = 0;
	bool errorSeen_ = false;

	// Lock held.
	void drop()
	{
		pipe_.close();
		AP_DiscordSched_OnDisconnect(&sched_, now_ms());
		s_->status = AP_DISCORD_STATUS_SEARCHING;
	}

	bool connect(const std::string &dir, const std::string &id, bool *rejected)
	{
		if (!pipe_.open(dir))
			return false;
		if (!write_frame(pipe_, AP_DISCORD_OP_HANDSHAKE, handshake_json(id)))
		{
			pipe_.close();
			return false;
		}
		const uint32_t start = now_ms();
		for (;;)
		{
			const uint32_t spent = now_ms() - start;
			if (spent >= 3000)
				break;
			uint32_t op = 0;
			std::string payload;
			const int r = read_frame(pipe_, 3000 - spent, &op, &payload);
			if (r <= 0)
				break;
			if (op == AP_DISCORD_OP_CLOSE)
			{
				*rejected = true;
				break;
			}
			if (op == AP_DISCORD_OP_PING)
			{
				write_frame(pipe_, AP_DISCORD_OP_PONG, payload);
				continue;
			}
			if (op == AP_DISCORD_OP_FRAME)
			{
				json j = json::parse(payload, nullptr, false);
				if (!j.is_discarded() && str_field(j, "evt") == "READY")
					return true;
			}
		}
		pipe_.close();
		return false;
	}

	// Read and handle whatever Discord sent. -1 = the connection is gone.
	int drain()
	{
		for (int frames = 0; frames < 16; frames++)
		{
			const int ready = pipe_.readable();
			if (ready < 0)
				return -1;
			if (ready == 0)
				return 0;
			uint32_t op = 0;
			std::string payload;
			const int r = read_frame(pipe_, 1000, &op, &payload);
			if (r < 0)
				return -1;
			if (r == 0)
				return 0;
			if (op == AP_DISCORD_OP_CLOSE)
				return -1;
			if (op == AP_DISCORD_OP_PING)
			{
				if (!write_frame(pipe_, AP_DISCORD_OP_PONG, payload))
					return -1;
				continue;
			}
			if (op != AP_DISCORD_OP_FRAME)
				continue;
			json j = json::parse(payload, nullptr, false);
			if (j.is_discarded() || str_field(j, "evt") != "ERROR")
				continue;
			std::string msg;
			auto data = j.find("data");
			if (data != j.end())
				msg = str_field(*data, "message");
			std::lock_guard<std::mutex> lk(s_->m);
			s_->errCount++;
			if (!errorSeen_)
				s_->errMsg = msg;
			errorSeen_ = true;
		}
		return 0;
	}

	void clear_and_close()
	{
		if (!pipe_.is_open())
			return;
		write_frame(pipe_, AP_DISCORD_OP_FRAME, set_activity_json(nullptr, self_pid(), ++nonce_));
		// Give Discord a moment to take the clear before the pipe closes (closing
		// alone also clears it, so this is best effort).
		uint32_t op = 0;
		std::string payload;
		read_frame(pipe_, 300, &op, &payload);
		pipe_.close();
	}
};

void worker_main(Shared *s)
{
	// Nothing here is expected to throw, but an exception escaping a thread
	// would end the game, so presence just stops instead.
	try
	{
		Worker w(s);
		w.run();
	}
	catch (...)
	{
		std::lock_guard<std::mutex> lk(s->m);
		s->status = AP_DISCORD_STATUS_OFF;
		s->finished = true;
		s->done.notify_all();
	}
}

} // namespace

extern "C" void ap_discord_configure(int enabled, const char *app_id)
{
	const std::string id = app_id != nullptr ? app_id : "";
	const bool want = enabled && !id.empty();
	if (!want && g_shared.load() == nullptr)
		return; // never enabled: stay fully inert
	Shared *s = shared_state();
	bool start = false;
	{
		std::lock_guard<std::mutex> lk(s->m);
		if (s->enabled == (enabled != 0) && s->appId == id)
			return;
		s->enabled = enabled != 0;
		s->appId = id;
		if (want && !s->started && !s->shutdown)
		{
			s->started = true;
			start = true;
		}
	}
	if (start)
	{
		try
		{
			std::thread(worker_main, s).detach();
		}
		catch (...)
		{
			std::lock_guard<std::mutex> lk(s->m);
			s->started = false; // no thread: presence simply stays off
		}
	}
	s->wake.notify_all();
}

extern "C" void ap_discord_publish(const ApDiscordActivity *activity)
{
	Shared *s = g_shared.load();
	if (activity == nullptr || s == nullptr)
		return;
	{
		std::lock_guard<std::mutex> lk(s->m);
		if (s->actGen != 0 && AP_Discord_Activity_Equal(&s->act, activity))
			return;
		s->act = *activity;
		if (++s->actGen == 0)
			s->actGen = 1;
	}
	s->wake.notify_all();
}

extern "C" int ap_discord_status(void)
{
	Shared *s = g_shared.load();
	return s != nullptr ? s->status.load() : AP_DISCORD_STATUS_OFF;
}

extern "C" unsigned ap_discord_error_count(char *buf, int n)
{
	if (buf != nullptr && n > 0)
		buf[0] = '\0';
	Shared *s = g_shared.load();
	if (s == nullptr)
		return 0;
	std::lock_guard<std::mutex> lk(s->m);
	if (buf != nullptr && n > 0)
		std::snprintf(buf, (size_t)n, "%s", s->errMsg.c_str());
	return s->errCount;
}

extern "C" void ap_discord_shutdown(unsigned timeout_ms)
{
	Shared *s = g_shared.load();
	if (s == nullptr)
		return;
	std::unique_lock<std::mutex> lk(s->m);
	const bool running = s->started;
	s->shutdown = true;
	s->wake.notify_all();
	if (running)
		s->done.wait_for(lk, std::chrono::milliseconds(timeout_ms), [s] { return s->finished; });
}

extern "C" int ap_discord_build_handshake(const char *app_id, char *buf, int n)
{
	return copy_out(handshake_json(app_id != nullptr ? app_id : ""), buf, n);
}

extern "C" int ap_discord_build_set_activity(const ApDiscordActivity *activity, long pid,
                                             unsigned nonce, char *buf, int n)
{
	return copy_out(set_activity_json(activity, pid, nonce), buf, n);
}

extern "C" void ap_discord_test_set_timing(unsigned first_retry_ms, unsigned retry_ms,
                                           unsigned min_send_ms, unsigned poll_ms)
{
	Shared *s = shared_state();
	std::lock_guard<std::mutex> lk(s->m);
	s->firstRetryMs = first_retry_ms;
	s->retryMs = retry_ms;
	s->minSendMs = min_send_ms;
	s->pollMs = poll_ms;
}

extern "C" void ap_discord_test_set_socket_dir(const char *dir)
{
	Shared *s = shared_state();
	std::lock_guard<std::mutex> lk(s->m);
	s->socketDir = dir != nullptr ? dir : "";
}
