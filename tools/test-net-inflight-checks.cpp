// Production test for checks sent into a socket that is already dead (#707).
//
// Build + run (from the repo root):
//   g++ -m32 -std=c++17 -DCTR_AP -DCTR_CUSTOM_TRACKS \
//       -Itools/harness-support/apclient_mock -Iap/vendor/json/include \
//       tools/test-net-inflight-checks.cpp ap/ap_seedcfg.cpp \
//       -lssl -lcrypto -pthread -o /tmp/test-net-inflight-checks && /tmp/test-net-inflight-checks
//
// This harness includes the REAL ap/ap_net.cpp and replaces only the
// apclientpp transport with the mock in tools/harness-support/apclient_mock.
//
// The field case: the player's network drops in a race. The websocket does
// not notice at once (wswrap pings every 25 s and waits 10 s for the pong), so
// for up to about 35 s the client is still "connected". A check earned in that
// window is handed to LocationChecks, which writes it into the dead socket and
// returns. The server never sees it. When the socket finally closes and the
// client reconnects, the check has to be sent again, because the Connected
// packet reports it missing. Before #707 the client only resent checks it had
// tried while already disconnected, so these were lost for good.
//
// What it pins:
//   1. a check sent while connected, then lost with the socket, is sent again
//      on the next same-seed/slot connect when the server reports it missing,
//   2. the same check is not sent again once the server has it,
//   3. a manual Connect (ap_net_shutdown, new client) to the same room resends
//      it too, and a different seed still discards it,
//   4. a check that the server confirmed before the drop is never resent.
#include "../ap/ap_net.cpp"

#include <cstdio>
#include <fstream>
#include <set>
#include <string>

extern "C" void AP_LogLine(const char *line)
{
	std::fputs(line, stdout);
}

static nlohmann::json g_fixture;
static int g_checks = 0;
static int g_failures = 0;

static void expect(bool ok, const char *what)
{
	g_checks++;
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		g_failures++;
}

static std::set<int64_t> plan_ids(void)
{
	std::set<int64_t> ids;
	if (g_fixture.contains("content_plan"))
		for (const auto &check : g_fixture["content_plan"]["checks"])
			ids.insert(check["location"].get<int64_t>());
	return ids;
}

// The server's view, as a Connected packet would report it: every plan check
// is missing except the codes in `checked`.
static void server_state(APClient *m, const std::set<int64_t> &checked)
{
	std::set<int64_t> missing = plan_ids();
	for (int64_t c : checked)
		missing.erase(c);
	m->test_set_checked(checked);
	m->test_set_missing(missing);
}

static APClient *start(const std::string &seed)
{
	ap_net_shutdown();
	ap_mock_seed() = seed;
	ap_mock_slot() = "Harness";
	if (ap_net_init("ctr-native", "Crash Team Racing", "ws://127.0.0.1:1") != 0)
		return nullptr;
	ap_net_connect_slot("Harness", "");
	if (g_ap)
		server_state(g_ap, {});
	return g_ap;
}

static int sends_of(APClient *m, long long code)
{
	int n = 0;
	for (long long c : m->calls.checked_codes)
		n += c == code;
	return n;
}

// Two codes from the plan: a sanity crate stands in for any check class, the
// send path is shared.
static int64_t g_code_a, g_code_b;

static void test_dead_socket_auto_reconnect(void)
{
	APClient *m = start("2101");
	expect(m != nullptr, "auto: client up");
	if (!m)
		return;
	m->emit_slot_connected(g_fixture);
	expect(ap_net_is_connected() == 1, "auto: first connect accepted");

	// The network is already gone, but the socket has not noticed: the send
	// "succeeds" into the dead socket.
	m->calls.reset();
	ap_net_send_location(g_code_a);
	expect(sends_of(m, g_code_a) == 1, "auto: check handed to the dead socket");

	// Ping timeout: the socket closes, apclientpp reconnects the same client.
	m->emit_socket_disconnected();
	expect(ap_net_is_connected() == 0, "auto: disconnect noticed");
	server_state(m, {}); // the server never received the check
	m->calls.reset();
	m->emit_slot_connected(g_fixture);
	expect(sends_of(m, g_code_a) == 1, "auto: lost check resent on reconnect");

	// The resend arrives this time; a later reconnect must not send it again.
	server_state(m, {g_code_a});
	m->emit_socket_disconnected();
	m->calls.reset();
	m->emit_slot_connected(g_fixture);
	expect(sends_of(m, g_code_a) == 0, "auto: confirmed check is not resent");
	ap_net_shutdown();
}

static void test_dead_socket_manual_connect(void)
{
	APClient *m = start("2101");
	expect(m != nullptr, "manual: client up");
	if (!m)
		return;
	m->emit_slot_connected(g_fixture);
	ap_net_send_location(g_code_b); // into the dead socket
	ap_net_shutdown();              // player selects Connect again

	m = start("2101");
	expect(m != nullptr, "manual: new client up");
	if (!m)
		return;
	m->calls.reset();
	m->emit_slot_connected(g_fixture);
	expect(sends_of(m, g_code_b) == 1, "manual: lost check resent to the same room");
	ap_net_shutdown();

	// A check lost for one seed must never reach a different seed.
	m = start("2101");
	if (!m)
		return;
	m->emit_slot_connected(g_fixture);
	ap_net_send_location(g_code_b);
	m = start("9999");
	if (!m)
		return;
	m->calls.reset();
	m->emit_slot_connected(g_fixture);
	expect(sends_of(m, g_code_b) == 0, "manual: different seed does not receive it");
	ap_net_shutdown();
}

static void test_confirmed_before_drop(void)
{
	APClient *m = start("2101");
	expect(m != nullptr, "confirmed: client up");
	if (!m)
		return;
	m->emit_slot_connected(g_fixture);
	ap_net_send_location(g_code_a);
	m->emit_room_update_checked({g_code_a}); // the server echo arrived
	m->emit_socket_disconnected();
	server_state(m, {g_code_a});
	m->calls.reset();
	m->emit_slot_connected(g_fixture);
	expect(sends_of(m, g_code_a) == 0, "confirmed: settled check is not resent");
	ap_net_shutdown();
}

static void test_coop_preference_replies(void)
{
	APClient *m = start("2101");
	expect(m != nullptr, "co-op: client up");
	if (!m)
		return;
	m->emit_slot_connected(g_fixture);
	m->calls.reset();
	const auto key = ap_coop_key();
	int latest = -1;
	ap_net_coop_subscribe();
	expect(!ap_net_coop_set(4) && m->calls.data_set == 0, "co-op: writes wait for Get");
	m->emit_set_reply(key, 5);
	expect(!ap_net_coop_known(&latest), "co-op: notification cannot settle the initial read");
	m->emit_retrieved({{key, nullptr}});
	expect(ap_net_coop_known(&latest) && latest == 64, "co-op: empty Get supplies the default");

	expect(ap_net_coop_set(32 | 10) && ap_net_coop_set(11), "co-op: two edits queued");
	const unsigned revision = ap_net_coop_revision();
	m->emit_set_reply(key, 32 | 10, m->calls.set_extras.at(0));
	expect(ap_net_coop_known(&latest) && latest == 11 && ap_net_coop_revision() == revision,
	       "co-op: first echo cannot restore the old racer or rules");
	m->emit_set_reply(key, 2);
	expect(ap_net_coop_known(&latest) && latest == 11 && ap_net_coop_revision() == revision,
	       "co-op: another client cannot change the input to the next toggle");
	expect(ap_net_coop_set(latest | 32), "co-op: next toggle keeps the newest racer");
	m->emit_set_reply(key, 11, m->calls.set_extras.at(1));
	expect(ap_net_coop_known(&latest) && latest == (32 | 11), "co-op: second echo cannot eat the toggle");
	m->emit_set_reply(key, 32 | 11, m->calls.set_extras.at(2));
	expect(!ap_net_coop_save_pending(), "co-op: latest matching echo confirms the save");
	m->emit_set_reply(key, 2);
	expect(ap_net_coop_known(&latest) && latest == 2, "co-op: subsequent external edit updates the cache");

	// A queued write into a lost socket has no SetReply. The same room's
	// next Get can confirm the value or expose the failed save.
	expect(ap_net_coop_set(32 | 2) && ap_net_coop_save_pending(), "co-op: queued save is visibly pending");
	m->emit_socket_disconnected();
	m->emit_slot_connected(g_fixture);
	ap_net_coop_subscribe();
	m->emit_retrieved({{key, 2}});
	expect(ap_net_coop_known(&latest) && latest == 2 && ap_net_coop_save_lost() &&
	       !ap_net_coop_save_pending(), "co-op: dead-socket write is reported unsaved on reconnect");
	expect(ap_net_coop_set(32 | 2) && !ap_net_coop_save_lost(), "co-op: player can retry the save");
	m->emit_socket_disconnected();
	m->emit_slot_connected(g_fixture);
	ap_net_coop_subscribe();
	m->emit_retrieved({{key, 32 | 2}});
	expect(ap_net_coop_known(&latest) && latest == (32 | 2) &&
	       !ap_net_coop_save_pending() && !ap_net_coop_save_lost(), "co-op: reconnect confirms a save whose echo was lost");
	ap_net_shutdown();
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "tools/fixtures/content-plan/global-checks.json";
	std::ifstream in(path);
	if (!in)
	{
		std::fprintf(stderr, "cannot open fixture: %s\n", path);
		return 2;
	}
	try
	{
		in >> g_fixture;
	}
	catch (const std::exception &e)
	{
		std::fprintf(stderr, "fixture is not valid JSON: %s\n", e.what());
		return 2;
	}
	const std::set<int64_t> ids = plan_ids();
	if (ids.size() < 2)
	{
		std::fprintf(stderr, "fixture has fewer than two plan checks\n");
		return 2;
	}
	g_code_a = *ids.begin();
	g_code_b = *std::next(ids.begin());

	test_dead_socket_auto_reconnect();
	test_dead_socket_manual_connect();
	test_confirmed_before_drop();
	test_coop_preference_replies();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
