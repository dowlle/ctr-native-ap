// Production network admission ordering / lifecycle test (ticket 05).
//
// This harness #includes the REAL ap/ap_net.cpp and links the REAL
// ap/ap_seedcfg.cpp, replacing only the apclientpp TRANSPORT with the
// deterministic mock in tools/harness-support/apclient_mock/apclient.hpp. Every
// decision under test -- parse-before-side-effects, the refusal latch, deferred
// teardown, the packet-tail guards, ap_net_is_connected/location_checked
// admission, held-check preservation -- lives in the production translation
// unit; nothing here reimplements it.
//
// Build + run (from the repo root):
//   g++ -m32 -std=c++17 -DCTR_AP \
//       -Itools/harness-support/apclient_mock -Iap/vendor/json/include \
//       tools/test-net-admission.cpp ap/ap_seedcfg.cpp \
//       -lssl -lcrypto -pthread -o /tmp/test-net-admission && /tmp/test-net-admission
//
// What it pins:
//   1. a REFUSED seed (enabled but block absent/malformed/unknown) sends zero
//      door storage, zero held checks and zero scouts, flips no item queue, and
//      is not reported connected -- even while its APClient is still
//      SLOT_CONNECTED (the Connected+ReceivedItems same-batch case),
//   2. the teardown is deferred past the callback (g_ap alive inside the
//      handler, gone after ap_net_poll) and the visible ERROR + reason survive
//      it (unlike the retry-stop path),
//   3. an ACCEPTED seed performs the normal side effects and activates items,
//   4. a legitimate held check still flushes on a later SAME-seed reconnect,
//      and is discarded on a DIFFERENT seed,
//   5. a successful reconnect after a refusal is not poisoned by the latch.

#include "../ap/ap_net.cpp"

#include <cstdio>
#include <cstring>
#include <fstream>

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
	if (!ok)
	{
		std::printf("FAIL: %s\n", what);
		g_failures++;
	}
}

static APClient *start(const std::string &seed)
{
	ap_net_shutdown();
	ap_mock_seed() = seed;
	ap_mock_slot() = "Harness";
	if (ap_net_init("ctr-native", "Crash Team Racing", "ws://127.0.0.1:1") != 0)
		return nullptr;
	ap_net_connect_slot("Harness", "");
	return g_ap;
}

// A seed the parser must refuse: enabled scalar, block absent.
static nlohmann::json rejected_seed(void)
{
	nlohmann::json d = g_fixture;
	d.erase("hit_character_encounters");
	return d;
}

static APClient::NetworkItem make_item(long long item, long long location, long long index)
{
	APClient::NetworkItem it;
	it.item = item;
	it.location = location;
	it.player = 1;
	it.flags = 0;
	it.index = index;
	return it;
}

// ---------------------------------------------------------------------------

static void test_rejected_zero_side_effects(void)
{
	APClient *m = start("2101");
	expect(m != nullptr, "reject: client up");
	if (!m)
		return;
	// Raw server state says a Hit location is checked; admission must suppress it.
	m->test_set_checked({35025000});
	m->test_set_missing({35025001});
	m->calls.reset();

	m->emit_slot_connected(rejected_seed());

	// The callback returned: no success side effect, but g_ap is STILL alive
	// (deferred teardown) and the refusal is visible.
	expect(g_ap == m, "reject: g_ap not deleted inside callback");
	expect(ap_net_is_connected() == 0, "reject: is_connected includes admission");
	expect(ap_net_status() == AP_NET_STATUS_ERROR, "reject: status is ERROR");
	expect(std::strlen(ap_net_last_error()) > 0, "reject: explanation present");
	expect(ap_net_location_checked(35025000) == 0, "reject: location_checked guarded");
	expect(ap_net_location_exists(35025000) == 0, "reject: location_exists guarded");
	expect(ap_net_location_count() == 0, "reject: location_count guarded");

	// Same-batch ReceivedItems must not enter the queue.
	std::list<APClient::NetworkItem> items;
	items.push_back(make_item(35000001, 35010000, 0));
	m->emit_items_received(items);
	long long out[8];
	expect(ap_net_drain_items(out, 8) == 0, "reject: ReceivedItems not activated");

	// Zero outbound traffic.
	expect(m->calls.data_set == 0, "reject: no door storage Set");
	expect(m->calls.data_get == 0, "reject: no door Get");
	expect(m->calls.set_notify == 0, "reject: no SetNotify");
	expect(m->calls.location_checks == 0, "reject: no held-check send");
	expect(m->calls.location_scouts == 0, "reject: no scout");

	// Deferred teardown happens on the next poll and preserves ERROR + reason.
	ap_net_poll();
	expect(g_ap == nullptr, "reject: teardown after poll");
	expect(ap_net_is_connected() == 0, "reject: still not connected after teardown");
	expect(ap_net_status() == AP_NET_STATUS_ERROR, "reject: ERROR survives teardown");
	expect(std::strlen(ap_net_last_error()) > 0, "reject: reason survives teardown");
}

static void test_accepted_side_effects(void)
{
	APClient *m = start("2101");
	expect(m != nullptr, "accept: client up");
	if (!m)
		return;
	m->test_set_checked({35025000});
	m->test_set_missing({35025001});
	m->calls.reset();

	m->emit_slot_connected(g_fixture);

	expect(g_ap == m, "accept: client retained");
	expect(ap_net_is_connected() == 1, "accept: connected");
	expect(ap_net_status() == AP_NET_STATUS_CONNECTED, "accept: status CONNECTED");
	expect(m->calls.data_get >= 1, "accept: door Get sent");
	expect(m->calls.set_notify >= 1, "accept: door SetNotify sent");
	expect(m->calls.location_scouts >= 1, "accept: scout sent");
	expect(ap_net_location_checked(35025000) == 1, "accept: location_checked live");

	std::list<APClient::NetworkItem> items;
	items.push_back(make_item(35000042, 35010000, 0));
	m->emit_items_received(items);
	long long out[8];
	int n = ap_net_drain_items(out, 8);
	expect(n == 1 && out[0] == 35000042, "accept: ReceivedItems activated");

	ap_net_shutdown();
}

static void test_same_seed_held_flush(void)
{
	// Establish the held-check identity for seed 2101 / slot Harness.
	APClient *m = start("2101");
	expect(m != nullptr, "flush: first client up");
	if (!m)
		return;
	m->emit_slot_connected(g_fixture);
	expect(ap_net_is_connected() == 1, "flush: first connect accepted");
	ap_net_shutdown();

	// Earn a check while offline: it is retained, not lost.
	ap_net_send_location(35025007);

	// Reconnect to the SAME seed/slot: the retained check must flush.
	m = start("2101");
	expect(m != nullptr, "flush: reconnect client up");
	if (!m)
		return;
	m->calls.reset();
	m->emit_slot_connected(g_fixture);
	expect(m->calls.location_checks == 1, "flush: held check sent once");
	expect(!m->calls.checked_codes.empty() && m->calls.checked_codes[0] == 35025007,
	       "flush: correct held code sent");
	ap_net_shutdown();
}

static void test_different_seed_discard(void)
{
	APClient *m = start("2101");
	expect(m != nullptr, "discard: first client up");
	if (!m)
		return;
	m->emit_slot_connected(g_fixture);
	ap_net_shutdown();

	ap_net_send_location(35025008);

	m = start("9999");
	expect(m != nullptr, "discard: second client up");
	if (!m)
		return;
	m->calls.reset();
	m->emit_slot_connected(g_fixture); // valid block, but a different seed
	expect(m->calls.location_checks == 0, "discard: held check not cross-delivered");
	ap_net_shutdown();
}

static void test_reconnect_after_refusal(void)
{
	APClient *m = start("2101");
	expect(m != nullptr, "recover: client up");
	if (!m)
		return;
	m->emit_slot_connected(rejected_seed());
	ap_net_poll();
	expect(g_ap == nullptr, "recover: refused client torn down");
	expect(ap_net_status() == AP_NET_STATUS_ERROR, "recover: refusal visible");

	// A manual new connect to a valid seed must be admitted normally.
	m = start("2101");
	expect(m != nullptr, "recover: new client up");
	if (!m)
		return;
	m->test_set_checked({35025000});
	m->calls.reset();
	m->emit_slot_connected(g_fixture);
	expect(ap_net_is_connected() == 1, "recover: accepted after refusal");
	expect(m->calls.location_scouts >= 1, "recover: side effects resumed");
	ap_net_shutdown();
}

static void test_malformed_block_refused(void)
{
	APClient *m = start("2101");
	expect(m != nullptr, "malformed: client up");
	if (!m)
		return;
	nlohmann::json d = g_fixture;
	d["hit_character_encounters"]["schema"] = 2; // unknown block schema
	m->calls.reset();
	m->emit_slot_connected(d);
	expect(ap_net_is_connected() == 0, "malformed: not admitted");
	expect(ap_net_status() == AP_NET_STATUS_ERROR, "malformed: ERROR");
	expect(m->calls.location_scouts == 0, "malformed: no scout");
	ap_net_poll();
	expect(g_ap == nullptr, "malformed: torn down");
}

// Admission needs BOTH the accepted latch and the raw transport state. Raw
// socket state alone is not enough.
static void test_raw_socket_not_admission(void)
{
	APClient *m = start("2101");
	expect(m != nullptr, "raw: client up");
	if (!m)
		return;
	m->emit_slot_connected(g_fixture);
	expect(ap_net_is_connected() == 1, "raw: accepted seed is connected");
	g_connected = false; // latch drops without any socket change
	expect(ap_net_is_connected() == 0, "raw: socket alone is not admission");
	g_connected = true;
	ap_net_shutdown();
}

// Trailing packet-tail events after a refusal must not overwrite the visible
// incompatibility reason/status, before or after the deferred teardown.
static void test_rejected_trailing_events(void)
{
	APClient *m = start("2101");
	expect(m != nullptr, "trailing: client up");
	if (!m)
		return;
	m->emit_slot_connected(rejected_seed());
	expect(ap_net_status() == AP_NET_STATUS_ERROR, "trailing: ERROR set");
	const std::string reason = ap_net_last_error();
	expect(!reason.empty(), "trailing: reason set");

	m->emit_socket_error("Connection refused");
	m->emit_slot_refused({"Invalid slot"});
	m->emit_socket_disconnected();
	expect(ap_net_status() == AP_NET_STATUS_ERROR, "trailing: status preserved");
	expect(ap_net_last_error() == reason, "trailing: reason preserved");

	ap_net_poll(); // deletion may fire handlers; the refusal must survive it
	expect(g_ap == nullptr, "trailing: torn down");
	expect(ap_net_status() == AP_NET_STATUS_ERROR, "trailing: ERROR survives teardown");
	expect(ap_net_last_error() == reason, "trailing: reason survives teardown");
}

// A refused room must not accept NEW held-check mutations (the queue still
// belongs to the last accepted seed/slot); existing held checks are preserved,
// and normal offline retention resumes once the latch clears.
static void test_rejected_held_guard(void)
{
	APClient *m = start("2101");
	expect(m != nullptr, "held: client up");
	if (!m)
		return;
	m->emit_slot_connected(rejected_seed());
	const int before = g_held_checks.size();
	ap_net_send_location(35025011);
	expect(g_held_checks.size() == before, "held: rejected room cannot add held checks");
	ap_net_poll();
	ap_net_shutdown();

	ap_net_send_location(35025012); // latch cleared -> normal offline retention
	expect(g_held_checks.size() == before + 1, "held: offline retention resumes");
	ap_net_shutdown();
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "tools/fixtures/ctr_hit_character_seed2101.json";
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

	test_rejected_zero_side_effects();
	test_accepted_side_effects();
	test_same_seed_held_flush();
	test_different_seed_discard();
	test_reconnect_after_refusal();
	test_malformed_block_refused();
	test_raw_socket_not_admission();
	test_rejected_trailing_events();
	test_rejected_held_guard();

	std::printf("%s: %d checks, %d failures\n",
	            g_failures ? "FAIL" : "PASS", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
