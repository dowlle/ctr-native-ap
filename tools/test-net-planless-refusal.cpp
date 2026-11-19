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
//   3. a broken plan gives matching-client advice and logs its technical reason.

#include "../ap/ap_net.cpp"

#include <cstdio>
#include <cstring>
#include <string>

extern "C" void AP_LogLine(const char *line)
{
	extern std::string captured_log;
	captured_log += line;
}
std::string captured_log;

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
	expect(std::strstr(ap_net_last_error(), "MATCHING CLIENT") != nullptr, "matching-client advice");
	expect(captured_log.find("seed REFUSED (content_plan:") != std::string::npos, "technical reason logged");
	expect(captured_log.find("schema_version") != std::string::npos, "schema logged");
	expect(m->calls.location_checks == 0 && m->calls.location_scouts == 0 && m->calls.data_set == 0,
	       "version advice preserves admission safety");
}

static void test_version_advice(void)
{
	const char *checksums[] = {"06576a4d1654a8c0ecaa9298efc5066c9f6dbab1",
	                          "dfef5220b67ad917f534bac828b5bf882156caad",
	                          "df96b8611492503ee9aeeb1b5fec2ad2b9779ae5", "", "dfef5220-forged"};
	const char *hints[] = {"ALPHA1/ALPHA2", "ALPHA3", "ALPHA4/ALPHA5", "MATCHING CLIENT", "MATCHING CLIENT"};
	for (int i = 0; i < 5; ++i) {
		APClient *m = start();
		m->emit_room_info(checksums[i]);
		nlohmann::json room = {{"schema_version", 18}, {"content_plan", nlohmann::json::object()},
		    {"ctr_options", {{"schema_version", 18}, {"content_plan", true}}}};
		m->emit_slot_connected(room);
		const std::string message = ap_net_last_error();
		expect(message.find(hints[i]) != std::string::npos, "correct family or unknown advice");
		// Two 432px rows at 13px per letter. The first clause is 29 chars;
		// every remaining clause fits the second row even at this worst width.
		const auto split = message.find(". ");
		expect(split != std::string::npos && (split + 1) * 13 <= 432 && (message.size() - split - 2) * 13 <= 432,
		       "advice fits the two connection rows");
		expect(message.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .-/") == std::string::npos,
		       "credits font safe");
	}
	nlohmann::json room = {{"ctr_options", {{"world_version", "0.3.0"}}}, {"build_version", "0.3.0-alpha6"}};
	expect(ap_seed_refusal::message(room, checksums[1]).find("USE CLIENT 0.3.0-ALPHA6") != std::string::npos,
	       "explicit build outranks a table family");
	room["build_version"] = "0.3.0-12345678901234567890";
	expect(ap_seed_refusal::message(room, "").find("MATCHING CLIENT") != std::string::npos,
	       "overlong build label falls back to fitting advice");
	room["build_version"] = "bad~00\n<script>";
	expect(ap_seed_refusal::message(room, "").find("MATCHING CLIENT") != std::string::npos,
	       "unsafe metadata stays out of font");
	for (const char *label : {"0", "0.3", "0.3.0-", "0.3.0-A..B", "0.3.0A", "00.3.0"}) {
		room["build_version"] = label;
		expect(ap_seed_refusal::message(room, checksums[1]).find("CHECK ALPHA3") != std::string::npos,
		       "malformed version shape falls back to tentative family advice");
	}
	room["build_version"] = "0.3.0";
	expect(ap_seed_refusal::message(room, "").find("USE CLIENT 0.3.0.") != std::string::npos,
	       "well-formed explicit stable build is retained");
	APClient *m = start();
	const nlohmann::json saved = {{"schema_version", 18}, {"content_plan", {{"version", 1}, {"room_compatibility", 2}}},
	                            {"ctr_options", {{"schema_version", 18}, {"world_version", "0.3.0"}, {"content_plan", true}}}};
	m->emit_slot_connected(saved);
	const std::string advice = ap_net_last_error();
	const std::string current = CTR_AP_VERSION;
	const std::string build = ap_seed_refusal::version_label(current.substr(current.front() == 'v' ? 1 : 0),
	                                                        CTR_CFG_VERSION_CAP - 1);
	const char *reader_label = build.size() > 18 ? ". THIS BUILD " : ". THIS BUILD IS ";
	expect(ap_net_status() == AP_NET_STATUS_ERROR &&
	       advice == std::string("GENERATED WITH 0.3.0") + reader_label + build + ".",
	       "a refused saved room shows both versions on the connection screen");
	expect(advice.size() - advice.find("THIS BUILD ") <= 33 &&
	       advice.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .-") == std::string::npos,
	       "saved room advice fits the second row and uses credits font glyphs");
	nlohmann::json long_version = saved;
	long_version["ctr_options"]["world_version"] = "0.3.0-content-dev.123";
	long_version["build_version"] = "1.2.3-alpha1";
	expect(ap_seed_refusal::message(long_version, "").find("ROOM BUILD 0.3.0-CONTENT-DEV.123.") == 0,
	       "long room versions fit and the generator field takes precedence");
	nlohmann::json incomplete = saved;
	incomplete["content_plan"] = nlohmann::json::object();
	m = start();
	m->emit_slot_connected(incomplete);
	expect(ap_net_status() == AP_NET_STATUS_ERROR && std::string(ap_net_last_error()) == advice,
	       "a room with missing plan metadata still shows its known generator version");
	room["build_version"] = "bad~00\n<script>";
	room["ctr_options"] = 17;
	expect(ap_seed_refusal::message(room, "").find("MATCHING CLIENT") != std::string::npos,
	       "malformed options safe");
	start();
	expect(g_refusal_store.ctr_checksum.empty(), "new connection clears the previous family");
}

int main(void)
{
	test_refused({{"ctr_options", {{"schema_version", 16}}}}, "schema 16 room");
	test_refused({{"ctr_options", {{"schema_version", 19}}}}, "room with a later schema and no plan");
	test_refused({{"ctr_options", nlohmann::json::object()}}, "room with no schema");
	test_refused(nlohmann::json::object(), "empty room");
	test_broken_plan_keeps_its_refusal();
	test_version_advice();
	ap_net_shutdown();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
