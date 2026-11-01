// A Play Solo session never sends or receives a DeathLink (#89), whatever the room
// or the OPTIONS rows say. Imported rooms keep their own slot data, so the client
// has to hold this itself; the wrapper forces the option off only on seeds it makes.
//
// Includes the REAL ap/ap_net.cpp with the mock transport and the REAL resolve in
// ap/ap_race_attempt_logic.h.
//
// Build + run (from the repo root):
//   g++ -m32 -std=c++17 -DCTR_AP \
//       -Itools/harness-support/apclient_mock -Iap/vendor/json/include \
//       tools/test-solo-deathlink.cpp ap/ap_seedcfg.cpp \
//       -lssl -lcrypto -pthread -o /tmp/test-solo-deathlink && /tmp/test-solo-deathlink
//
// What it pins:
//   1. every seed mode, send list and OPTIONS row resolves to nothing in a session,
//      with the same inputs live outside one,
//   2. in a session the DeathLink tag is not declared, a death is not sent and a
//      bounced death does not reach the game, and all three work outside one.

#define AP_NET_TEST_PLANLESS_ROOMS
#include "../ap/ap_net.cpp"

extern "C"
{
#include "../ap/ap_race_attempt_logic.h"
}

#include <cstdio>
#include <cstring>

extern "C" void AP_LogLine(const char *line)
{
	(void)line;
}

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

static void test_resolve(void)
{
	int live = 0;
	for (int seedRecv = 0; seedRecv <= 4; seedRecv++)
		for (int seedSend = -1; seedSend <= 7; seedSend++)
			for (int row = -1; row <= 4; row++)
				for (int fall = -1; fall <= 1; fall++)
					for (int hit = -1; hit <= 1; hit++)
						for (int loss = -1; loss <= 1; loss++)
						{
							const APDeathLinkState on =
							    AP_DeathLinkResolveSession(1, seedRecv, seedSend, row, fall, hit, loss);
							const APDeathLinkState out =
							    AP_DeathLinkResolveSession(0, seedRecv, seedSend, row, fall, hit, loss);
							const APDeathLinkState plain =
							    AP_DeathLinkResolve(seedRecv, seedSend, row, fall, hit, loss);
							expect(on.recv == 0 && on.send == 0, "a session resolves to off");
							expect(!AP_DeathLinkTagWanted(on.recv, on.send), "a session wants no tag");
							for (int trigger : {AP_DL_TRIG_FALL, AP_DL_TRIG_HIT, AP_DL_TRIG_LOSS})
								expect(!AP_DeathLinkTriggerSends(on.send, trigger, 0), "a session sends nothing");
							expect(out.recv == plain.recv && out.send == plain.send, "outside a session nothing changes");
							live += AP_DeathLinkTagWanted(out.recv, out.send);
						}
	expect(live > 0, "control: the same inputs are live outside a session");
}

static APClient *start(void)
{
	ap_net_shutdown();
	ap_mock_seed() = "0240";
	ap_mock_slot() = "Solo";
	if (ap_net_init("ctr-native", "Crash Team Racing", "ws://127.0.0.1:1") != 0)
		return nullptr;
	ap_net_connect_slot("Solo", "");
	return g_ap;
}

static void test_wire(int solo)
{
	APClient *m = start();
	expect(m != nullptr, "client up");
	if (!m)
		return;
	m->emit_socket_connected();
	m->emit_room_info();
	m->emit_slot_connected(nlohmann::json{{"ctr_options", {{"death_link", 3}, {"death_link_send", 7}}}});
	ap_net_set_solo_session(solo);
	m->calls.reset();

	ap_net_deathlink_enable();
	expect(m->calls.connect_update == (solo ? 0 : 1), "the tag follows the session");
	ap_net_deathlink_send("lost the race");
	expect(m->calls.bounce == (solo ? 0 : 1), "a death goes out only outside a session");

	m->emit_bounced(nlohmann::json{{"tags", {"DeathLink"}},
	                               {"data", {{"source", "Elsewhere"}, {"cause", "Elsewhere fell"}}}});
	char cause[128] = {0};
	char source[64] = {0};
	const int took = ap_net_deathlink_take(cause, sizeof cause, source, sizeof source);
	expect((took != 0) == (solo == 0), "a death comes in only outside a session");
	ap_net_set_solo_session(0);
}

int main(void)
{
	test_resolve();
	test_wire(0);
	test_wire(1);
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
