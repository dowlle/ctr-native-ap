// Host harness for the Discord Rich Presence IPC client (issue #366,
// ap/ap_discord_ipc.cpp): the JSON payloads, and on Unix hosts an end-to-end
// run against a fake Discord IPC server on a Unix socket in a temp directory.
//
//   g++ -m32 -std=c++17 -pthread -Wall -Wextra -I ap -I ap/vendor/json/include -o /tmp/test-discord-ipc tools/test-discord-ipc.cpp ap/ap_discord_ipc.cpp && /tmp/test-discord-ipc
//
// Covers:
//   * handshake and SET_ACTIVITY JSON (fields, optional fields left out, the
//     clear form with a null activity, buffer too small)
//   * enabled without an application ID, or disabled with one: no connection
//     attempt reaches the server
//   * handshake with the client ID, then the activity
//   * rate limit: changes inside the window collapse to the latest one, an
//     unchanged activity is not resent
//   * the server going away and coming back: status drops, the client
//     reconnects on its own and resends the activity
//   * an ERROR reply is counted; a refused handshake reports REJECTED
//   * disabling clears the activity and closes; shutdown does the same

#include "ap_discord_ipc.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef _WIN32
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

using nlohmann::json;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(name, expr)                                    \
	do {                                                     \
		g_checks++;                                          \
		if (!(expr)) {                                       \
			std::printf("FAIL %s (line %d)\n", (name), __LINE__); \
			g_failures++;                                    \
		}                                                    \
	} while (0)

#define GOOD_ID "1234567890123456789"
#define BAD_ID  "99999999999999999"

static ApDiscordActivity Act(const char *details, const char *state, long long start)
{
	ApDiscordActivity a;
	std::memset(&a, 0, sizeof a);
	std::snprintf(a.details, sizeof a.details, "%s", details);
	std::snprintf(a.state, sizeof a.state, "%s", state);
	a.startEpoch = start;
	return a;
}

static void TestJson()
{
	char buf[2048];
	int n = ap_discord_build_handshake(GOOD_ID, buf, sizeof buf);
	CHECK("handshake built", n > 0 && (size_t)n == std::strlen(buf));
	json h = json::parse(buf, nullptr, false);
	CHECK("handshake v", !h.is_discarded() && h.value("v", 0) == 1);
	CHECK("handshake client_id", !h.is_discarded() && h.value("client_id", std::string()) == GOOD_ID);

	ApDiscordActivity a = Act("Crash Cove, Trophy Race", "Checks: 42/312", 1700000000LL);
	n = ap_discord_build_set_activity(&a, 4321, 7, buf, sizeof buf);
	CHECK("activity built", n > 0);
	json j = json::parse(buf, nullptr, false);
	CHECK("activity parses", !j.is_discarded());
	if (!j.is_discarded())
	{
		CHECK("cmd", j.value("cmd", std::string()) == "SET_ACTIVITY");
		CHECK("nonce is a string", j["nonce"].is_string() && j["nonce"] == "7");
		CHECK("pid", j["args"]["pid"] == 4321);
		const json &act = j["args"]["activity"];
		CHECK("details", act.value("details", std::string()) == "Crash Cove, Trophy Race");
		CHECK("state", act.value("state", std::string()) == "Checks: 42/312");
		CHECK("start", act["timestamps"]["start"] == 1700000000LL);
		CHECK("large image key", act["assets"]["large_image"] == AP_DISCORD_LARGE_IMAGE_KEY);
		CHECK("large image text", act["assets"]["large_text"] == AP_DISCORD_LARGE_IMAGE_TEXT);
		CHECK("one button", act["buttons"].is_array() && act["buttons"].size() == 1);
		CHECK("button url", act["buttons"][0]["url"] == "https://github.com/dowlle/ctr-native-ap");
		CHECK("button label fits", std::string(AP_DISCORD_BUTTON_LABEL).size() <= 32);
		CHECK("no other activity fields", act.size() == 5);
	}

	a = Act("In the main menu", "", 0);
	ap_discord_build_set_activity(&a, 1, 8, buf, sizeof buf);
	j = json::parse(buf, nullptr, false);
	CHECK("no state when empty", !j.is_discarded() && !j["args"]["activity"].contains("state"));
	CHECK("no clock when zero", !j.is_discarded() && !j["args"]["activity"].contains("timestamps"));

	a = Act("Quote \" and backslash \\", "", 0);
	ap_discord_build_set_activity(&a, 1, 9, buf, sizeof buf);
	j = json::parse(buf, nullptr, false);
	CHECK("escaping", !j.is_discarded() && j["args"]["activity"]["details"] == "Quote \" and backslash \\");

	n = ap_discord_build_set_activity(nullptr, 55, 10, buf, sizeof buf);
	j = json::parse(buf, nullptr, false);
	CHECK("clear parses", n > 0 && !j.is_discarded());
	CHECK("clear has null activity", !j.is_discarded() && j["args"].contains("activity") && j["args"]["activity"].is_null());
	CHECK("clear has pid", !j.is_discarded() && j["args"]["pid"] == 55);

	char tiny[8];
	CHECK("small buffer refused", ap_discord_build_set_activity(&a, 1, 1, tiny, sizeof tiny) == -1);
}

#ifndef _WIN32

// Fake Discord: one listening socket, one client at a time. Records what the
// client sends as short strings.
class FakeDiscord
{
public:
	explicit FakeDiscord(const std::string &dir) : path_(dir + "/discord-ipc-0") {}
	~FakeDiscord() { stop(); }

	void start()
	{
		stop();
		::unlink(path_.c_str());
		lfd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
		struct sockaddr_un addr;
		std::memset(&addr, 0, sizeof addr);
		addr.sun_family = AF_UNIX;
		std::snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path_.c_str());
		if (::bind(lfd_, (struct sockaddr *)&addr, sizeof addr) != 0 || ::listen(lfd_, 4) != 0)
		{
			std::printf("FAIL fake server could not listen on %s\n", path_.c_str());
			std::exit(1);
		}
		running_ = true;
		th_ = std::thread([this] { loop(); });
	}

	void stop()
	{
		running_ = false;
		if (th_.joinable())
			th_.join();
		if (lfd_ >= 0)
			::close(lfd_);
		lfd_ = -1;
		::unlink(path_.c_str());
	}

	std::vector<std::string> events()
	{
		std::lock_guard<std::mutex> lk(m_);
		return ev_;
	}
	void clear_events()
	{
		std::lock_guard<std::mutex> lk(m_);
		ev_.clear();
	}
	int accepts() const { return accepts_.load(); }
	void reject_id(const std::string &id)
	{
		std::lock_guard<std::mutex> lk(m_);
		reject_.insert(id);
	}
	void error_next() { errorNext_ = true; }

private:
	std::string path_;
	int lfd_ = -1;
	std::thread th_;
	std::atomic<bool> running_{false};
	std::atomic<int> accepts_{0};
	std::atomic<bool> errorNext_{false};
	std::mutex m_;
	std::vector<std::string> ev_;
	std::set<std::string> reject_;

	void push(const std::string &e)
	{
		std::lock_guard<std::mutex> lk(m_);
		ev_.push_back(e);
	}

	static bool read_all(int fd, void *p, size_t n)
	{
		char *c = (char *)p;
		while (n > 0)
		{
			ssize_t r = ::recv(fd, c, n, 0);
			if (r <= 0)
				return false;
			c += r;
			n -= (size_t)r;
		}
		return true;
	}

	static void send_frame(int fd, uint32_t op, const std::string &body)
	{
		unsigned char h[AP_DISCORD_FRAME_HEADER];
		AP_Discord_PutHeader(h, op, (uint32_t)body.size());
		std::string buf((const char *)h, sizeof h);
		buf += body;
		::send(fd, buf.data(), buf.size(), MSG_NOSIGNAL);
	}

	void serve(int fd)
	{
		while (running_)
		{
			struct pollfd p = {fd, POLLIN, 0};
			if (::poll(&p, 1, 20) <= 0)
				continue;
			unsigned char h[AP_DISCORD_FRAME_HEADER];
			if (!read_all(fd, h, sizeof h))
			{
				push("EOF");
				return;
			}
			uint32_t op = 0, len = 0;
			if (!AP_Discord_GetHeader(h, &op, &len))
			{
				push("BADFRAME");
				return;
			}
			std::string body(len, '\0');
			if (len > 0 && !read_all(fd, &body[0], len))
			{
				push("EOF");
				return;
			}
			json j = json::parse(body, nullptr, false);
			if (j.is_discarded())
			{
				push("BADJSON");
				continue;
			}
			if (op == AP_DISCORD_OP_HANDSHAKE)
			{
				const std::string id = j.value("client_id", std::string());
				push("HANDSHAKE " + id);
				bool reject;
				{
					std::lock_guard<std::mutex> lk(m_);
					reject = reject_.count(id) != 0;
				}
				if (reject)
				{
					send_frame(fd, AP_DISCORD_OP_CLOSE, "{\"code\":4000,\"message\":\"Invalid Client ID\"}");
					continue;
				}
				// A ping first, to check the client answers it.
				send_frame(fd, AP_DISCORD_OP_PING, "{\"p\":1}");
				send_frame(fd, AP_DISCORD_OP_FRAME, "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\",\"data\":{\"v\":1}}");
			}
			else if (op == AP_DISCORD_OP_PONG)
				push("PONG");
			else if (op == AP_DISCORD_OP_FRAME && j.value("cmd", std::string()) == "SET_ACTIVITY")
			{
				const json &a = j["args"]["activity"];
				if (a.is_null())
					push("CLEAR");
				else
					push("ACT " + a.value("details", std::string()) + "|" + a.value("state", std::string()));
				json reply = {{"cmd", "SET_ACTIVITY"}, {"nonce", j["nonce"]}};
				if (errorNext_.exchange(false))
				{
					reply["evt"] = "ERROR";
					reply["data"] = {{"code", 4000}, {"message", "child \"activity\" fails"}};
				}
				else
					reply["evt"] = nullptr;
				send_frame(fd, AP_DISCORD_OP_FRAME, reply.dump());
			}
			else
				push("OTHER");
		}
	}

	void loop()
	{
		while (running_)
		{
			struct pollfd p = {lfd_, POLLIN, 0};
			if (::poll(&p, 1, 20) <= 0)
				continue;
			int fd = ::accept(lfd_, nullptr, nullptr);
			if (fd < 0)
				continue;
			accepts_++;
			serve(fd);
			::close(fd);
		}
	}
};

template <class F>
static bool WaitFor(F pred, int ms)
{
	for (int i = 0; i < ms / 10; i++)
	{
		if (pred())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return pred();
}

static int Count(const std::vector<std::string> &ev, const std::string &prefix)
{
	int n = 0;
	for (const std::string &e : ev)
		if (e.compare(0, prefix.size(), prefix) == 0)
			n++;
	return n;
}

static bool Has(FakeDiscord &srv, const std::string &e)
{
	for (const std::string &x : srv.events())
		if (x == e)
			return true;
	return false;
}

static void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static void TestEndToEnd()
{
	char tmpl[] = "/tmp/ctr-discord-XXXXXX";
	char *dir = ::mkdtemp(tmpl);
	if (dir == nullptr)
	{
		std::printf("FAIL mkdtemp\n");
		g_failures++;
		return;
	}
	FakeDiscord srv(dir);
	srv.start();

	// Before anything is enabled: no worker, status off.
	CHECK("status off before use", ap_discord_status() == AP_DISCORD_STATUS_OFF);
	ap_discord_configure(0, GOOD_ID);
	ap_discord_configure(1, nullptr);
	ap_discord_configure(1, "");
	Sleep(300);
	CHECK("disabled or no ID: no connection attempt", srv.accepts() == 0);
	CHECK("disabled or no ID: status off", ap_discord_status() == AP_DISCORD_STATUS_OFF);

	ap_discord_test_set_timing(200, 300, 400, 20);
	ap_discord_test_set_socket_dir(dir);

	// Enable: handshake, then the first activity at once.
	ap_discord_configure(1, GOOD_ID);
	CHECK("connects", WaitFor([] { return ap_discord_status() == AP_DISCORD_STATUS_CONNECTED; }, 3000));
	CHECK("handshake carries the client ID", Has(srv, "HANDSHAKE " GOOD_ID));
	CHECK("answers ping", WaitFor([&] { return Has(srv, "PONG"); }, 1000));
	ApDiscordActivity a = Act("Crash Cove, Trophy Race", "Checks: 1/10", 100);
	ap_discord_publish(&a);
	CHECK("first activity arrives", WaitFor([&] { return Has(srv, "ACT Crash Cove, Trophy Race|Checks: 1/10"); }, 1000));
	const auto tFirst = std::chrono::steady_clock::now();

	// Two quick changes inside the 400 ms window: only the latest goes out.
	ApDiscordActivity b = Act("Crash Cove, Trophy Race", "Checks: 2/10", 100);
	ApDiscordActivity c = Act("Crash Cove, Trophy Race", "Checks: 3/10", 100);
	ap_discord_publish(&b);
	ap_discord_publish(&c);
	CHECK("latest change arrives", WaitFor([&] { return Has(srv, "ACT Crash Cove, Trophy Race|Checks: 3/10"); }, 2000));
	const long long gapMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - tFirst).count();
	CHECK("rate limited", gapMs >= 350);
	CHECK("intermediate change collapsed", !Has(srv, "ACT Crash Cove, Trophy Race|Checks: 2/10"));

	// Unchanged activity is not resent.
	const int before = Count(srv.events(), "ACT ");
	ap_discord_publish(&c);
	Sleep(800);
	CHECK("unchanged not resent", Count(srv.events(), "ACT ") == before);

	// Discord restarts: status drops, client reconnects and resends.
	srv.stop();
	CHECK("drop noticed", WaitFor([] { return ap_discord_status() == AP_DISCORD_STATUS_SEARCHING; }, 3000));
	srv.clear_events();
	srv.start();
	CHECK("reconnects after restart", WaitFor([] { return ap_discord_status() == AP_DISCORD_STATUS_CONNECTED; }, 3000));
	CHECK("activity resent after reconnect",
	      WaitFor([&] { return Has(srv, "ACT Crash Cove, Trophy Race|Checks: 3/10"); }, 2000));

	// An ERROR reply is counted.
	char msg[128];
	const unsigned errs = ap_discord_error_count(msg, sizeof msg);
	srv.error_next();
	ApDiscordActivity d = Act("Crash Cove, Trophy Race", "Checks: 4/10", 100);
	ap_discord_publish(&d);
	CHECK("error counted", WaitFor([&] { return ap_discord_error_count(msg, sizeof msg) == errs + 1; }, 2000));
	CHECK("error message kept", std::strstr(msg, "activity") != nullptr);

	// A refused application ID.
	srv.reject_id(BAD_ID);
	ap_discord_configure(1, BAD_ID);
	CHECK("refused handshake reported", WaitFor([] { return ap_discord_status() == AP_DISCORD_STATUS_REJECTED; }, 3000));
	CHECK("old activity cleared before switching IDs", Has(srv, "CLEAR"));
	ap_discord_configure(1, GOOD_ID);
	CHECK("good ID connects again", WaitFor([] { return ap_discord_status() == AP_DISCORD_STATUS_CONNECTED; }, 3000));

	// Disable: clear, then close.
	srv.clear_events();
	ap_discord_configure(0, GOOD_ID);
	CHECK("disable clears", WaitFor([&] { return Has(srv, "CLEAR"); }, 2000));
	CHECK("disable closes", WaitFor([&] { return Has(srv, "EOF"); }, 2000));
	CHECK("disable: status off", WaitFor([] { return ap_discord_status() == AP_DISCORD_STATUS_OFF; }, 1000));
	const int accepts = srv.accepts();
	Sleep(500);
	CHECK("disabled: no reconnect", srv.accepts() == accepts);

	// Enable again, then shut down: clear and close within the timeout.
	ap_discord_configure(1, GOOD_ID);
	CHECK("re-enable connects", WaitFor([] { return ap_discord_status() == AP_DISCORD_STATUS_CONNECTED; }, 3000));
	CHECK("re-enable resends", WaitFor([&] { return Count(srv.events(), "ACT ") > 0; }, 2000));
	srv.clear_events();
	const auto t0 = std::chrono::steady_clock::now();
	ap_discord_shutdown(2000);
	const long long shutMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - t0).count();
	CHECK("shutdown clears", WaitFor([&] { return Has(srv, "CLEAR"); }, 1000));
	CHECK("shutdown closes", WaitFor([&] { return Has(srv, "EOF"); }, 1000));
	CHECK("shutdown bounded", shutMs < 2000);
	CHECK("status off after shutdown", ap_discord_status() == AP_DISCORD_STATUS_OFF);

	srv.stop();
	::rmdir(dir);
}
#endif

int main()
{
	TestJson();
#ifndef _WIN32
	TestEndToEnd();
#endif
	if (g_failures == 0)
		std::printf("discord ipc: all %d checks passed\n", g_checks);
	return g_failures == 0 ? 0 : 1;
}
