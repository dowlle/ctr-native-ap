// A 0.3.0 client refuses a room without a content plan (one generator row 7).
//
// Includes the REAL ap/ap_net.cpp and links the REAL ap/ap_seedcfg.cpp, with the
// mock transport of test-net-admission.cpp, and without its define that lets rooms
// without a plan through.
//
// Build + run (from the repo root):
//   g++ -m32 -std=c++17 -DCTR_AP \
//       -Itools/harness-support/apclient_mock -Iap/vendor/json/include \
//       tools/test-net-planless-refusal.cpp ap/ap_seedcfg.cpp \
//       -lssl -lcrypto -pthread -o /tmp/test-net-planless-refusal && /tmp/test-net-planless-refusal
//
// What it pins:
//   1. a schema 16 room, a later schema, a room with no schema and an empty room
//      are refused with the exact message and no update banner,
//   2. a refused room sends no door storage, no check and no scout, and its items
//      do not enter the queue,
//   3. a room with a broken plan keeps the plan's own refusal.

#include "../ap/ap_net.cpp"

#include <cstdio>
#include <cstring>
#include <string>

extern "C" void AP_LogLine(const char *line)
{
	(void)line;
}

static const char *const kMessage = "This room was made with v0.2.x. Play it on the v0.2.4 client.";
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

static APClient *start(void)
{
	ap_net_shutdown();
	ap_mock_seed() = "0240";
	ap_mock_slot() = "Harness";
	if (ap_net_init("ctr-native", "Crash Team Racing", "ws://127.0.0.1:1") != 0)
		return nullptr;
	ap_net_connect_slot("Harness", "");
	return g_ap;
}

static void test_refused(const nlohmann::json &room, const char *name)
{
	APClient *m = start();
	expect(m != nullptr, "client up");
	if (!m)
		return;
	m->test_set_checked({35011000});
	m->test_set_missing({35011001});
	m->calls.reset();

	m->emit_slot_connected(room);

	std::printf("%s: %s\n", name, ap_net_last_error());
	expect(ap_net_is_connected() == 0, "not connected");
	expect(ap_net_status() == AP_NET_STATUS_ERROR, "status is ERROR");
	expect(std::strcmp(ap_net_last_error(), kMessage) == 0, "the exact message");
	expect(ap_seedcfg_rejected() == 1, "the parser state says refused");
	expect(ctr_cfg.schema_newer == 0, "no update banner");
	expect(ap_net_location_checked(35011000) == 0, "checked state stays hidden");
	expect(m->calls.data_set == 0, "no door storage");
	expect(m->calls.data_get == 0, "no door Get");
	expect(m->calls.set_notify == 0, "no SetNotify");
	expect(m->calls.location_checks == 0, "no check sent");
	expect(m->calls.location_scouts == 0, "no scout");

	APClient::NetworkItem item;
	item.item = 35010014;
	item.location = 35011100;
	item.player = 1;
	item.flags = 0;
	item.index = 0;
	std::list<APClient::NetworkItem> items;
	items.push_back(item);
	m->emit_items_received(items);
	long long out[8];
	expect(ap_net_drain_items(out, 8) == 0, "items stay out of the queue");
}

static void test_broken_plan_keeps_its_refusal(void)
{
	APClient *m = start();
	expect(m != nullptr, "client up");
	if (!m)
		return;
	nlohmann::json room = {{"schema_version", 18},
	                       {"content_plan", nlohmann::json::object()},
	                       {"ctr_options", {{"schema_version", 18}, {"content_plan", true}}}};
	m->emit_slot_connected(room);
	expect(ap_net_status() == AP_NET_STATUS_ERROR, "a broken plan is refused");
	expect(std::strstr(ap_net_last_error(), kMessage) == nullptr, "with its own reason");
}

int main(void)
{
	test_refused({{"ctr_options", {{"schema_version", 16}}}}, "schema 16 room");
	test_refused({{"ctr_options", {{"schema_version", 19}}}}, "room with a later schema and no plan");
	test_refused({{"ctr_options", nlohmann::json::object()}}, "room with no schema");
	test_refused(nlohmann::json::object(), "empty room");
	test_broken_plan_keeps_its_refusal();
	ap_net_shutdown();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
