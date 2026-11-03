// Pad Approach Hints (#124): which locations a pad hints and the once-ever
// latch. Runs the production headers ap/ap_pad_hints_logic.h and
// ap/ap_pad_hint_latch.h on the host. The wiring into the pad draw, the glow
// enumeration and the network is pinned by test-pad-approach-hints-production.py.
#include <cstdio>
#include <cstdint>
#include <vector>
#include <map>
#include "../ap/ap_pad_hints_logic.h"
#include "../ap/ap_pad_hint_latch.h"

static int checks, failures;
static void expect(const char *name, long long got, long long want)
{
	checks++;
	if (got != want) { std::printf("FAIL %s: got %lld, want %lld\n", name, got, want); failures++; }
}

static int latched(int64_t code, void *ctx) { return static_cast<APPadHintLatch *>(ctx)->has(code) ? 1 : 0; }
static int inventory(void *ctx, long) { return *static_cast<int *>(ctx); }
static int inventory_by_id(void *ctx, long id) { return (*static_cast<std::map<long, int> *>(ctx))[id]; }

static int pick(APPadHintLatch &l, std::vector<int64_t> open, std::vector<int64_t> &out)
{
	out.assign(open.size() + 1, 0);
	int n = AP_PadHint_Select(open.data(), (int)open.size(), latched, &l, out.data(), (int)out.size());
	out.resize(n);
	return n;
}

int main()
{
	// Eligibility: every condition is needed. A closed pad (requirement or
	// racer lock), an empty pad, a custom track not installed, option off, a
	// room that is not a plan, or no latch read yet: nothing.
	for (int m = 0; m < 64; ++m) {
		int want = m == 63;
		expect("eligible mask", AP_PadHint_Eligible(m & 1, (m >> 1) & 1, (m >> 2) & 1, (m >> 3) & 1, (m >> 4) & 1, (m >> 5) & 1), want);
	}

	// Pad admission alone does not satisfy a location's inventory predicate.
	// Drive the exported evaluator with an open region and a missing item.
	{
		AP_WinLogic *logic = new AP_WinLogic{};
		AP_WinLogicEnv env{};
		int held = 0;
		logic->state = AP_WL_VALID;
		logic->node_count = 2;
		logic->nodes[0] = {AP_WL_T_TRUE, 0, 0, 0};
		logic->nodes[1] = {AP_WL_T_ITEMS_SUM, 1, 0, 1};
		logic->id_count = 1;
		logic->ids[0] = 123;
		logic->region_count = 1;
		logic->region_root[0] = 0;
		logic->check_count = 1;
		logic->checks[0] = {101, AP_WL_KIND_CTR, 0, 1};
		env.user = &held;
		env.item_count = inventory;
		expect("open pad missing location item does not hint", AP_PadHint_InLogic(logic, &env, 101), 0);
		held = 1;
		expect("location item arrives", AP_PadHint_InLogic(logic, &env, 101), 1);
		logic->nodes[0].tag = AP_WL_T_FALSE;
		expect("closed exported region does not hint", AP_PadHint_InLogic(logic, &env, 101), 0);
		logic->nodes[0].tag = AP_WL_T_TRUE;
		expect("unexported location does not hint", AP_PadHint_InLogic(logic, &env, 102), 0);
		env.unreliable = 1;
		expect("unreliable inventory does not hint", AP_PadHint_InLogic(logic, &env, 101), 0);
		env.unreliable = 0;
		logic->state = AP_WL_ABSENT;
		expect("absent logic does not guess", AP_PadHint_InLogic(logic, &env, 101), 0);
		delete logic;
	}

	{
		AP_WinLogic *logic = new AP_WinLogic{};
		AP_WinLogicEnv env{};
		std::map<long, int> held;
		env.user = &held; env.item_count = inventory_by_id;
		env.character_unlocks = 1; env.starting_character = 0; env.boost_mode = 2;
		held[AP_WL_BOOST_ITEM[0]] = 2;
		for (int i = 1; i <= 3; i++) held[AP_WL_BOOST_ITEM[1] + i] = 1;
		held[AP_WL_UNLOCK_ITEM[1]] = 1;
		expect("boost and stats on different racers do not combine", AP_WinLogicStatCap(logic, &env, 2, -1, 2), 0);
		for (int i = 1; i <= 3; i++) held[AP_WL_BOOST_ITEM[0] + i] = 1;
		expect("same starting racer satisfies boost and all stats", AP_WinLogicStatCap(logic, &env, 2, -1, 2), 1);
		held[AP_WL_BOOST_ITEM[1]] = 2; held[AP_WL_UNLOCK_ITEM[1]] = 0;
		expect("bound locked racer fails despite all chains", AP_WinLogicStatCap(logic, &env, 2, 1, 2), 0);
		logic->start_count = 1; logic->start_id[0] = AP_WL_UNLOCK_ITEM[1]; logic->start_n[0] = 1;
		expect("precollected unlock counts", AP_WinLogicStatCap(logic, &env, 2, 1, 2), 1);
		for (int i = 28; i <= 30; i++) held[35010000L + i] = 1;
		expect("shared stats with bound boost", AP_WinLogicStatCap(logic, &env, 2, 1, 1), 1);
		held[35010030L] = 0;
		expect("all three shared stats required", AP_WinLogicStatCap(logic, &env, 2, 1, 1), 0);
		delete logic;
	}

	APPadHintLatch l;
	l.connect("seed1", 0, 3);
	expect("key scoped", l.key == "ctr_pad_hints_v1:5:seed1:0:3", 1);
	expect("not ready before the read", l.ready(), 0);
	l.retrieved(nlohmann::json());
	expect("null read is an empty latch", l.ready() && l.codes.empty(), 1);

	// Which codes: positive, unlatched, each once, in the glow's order.
	std::vector<int64_t> out;
	expect("first approach count", pick(l, {101, 102, 0, -5, 102, 103}, out), 3);
	expect("first approach order", out[0] == 101 && out[1] == 102 && out[2] == 103, 1);
	nlohmann::json update = l.mark(out.data(), (int)out.size());
	expect("update object", update.size(), 3);
	expect("update key", update.contains("102") && update["102"] == 1, 1);
	// Standing on the pad: every later frame sends nothing.
	expect("second frame", pick(l, {101, 102, 103}, out), 0);
	// The pad's second stage opens: only the new checks.
	expect("stage two", pick(l, {101, 102, 103, 201, 202}, out), 2);
	expect("stage two codes", out[0] == 201 && out[1] == 202, 1);
	// Capped output never overruns.
	{
		int64_t open[4] = {1, 2, 3, 4}, two[2] = {0, 0};
		APPadHintLatch e; e.connect("s", 0, 1); e.retrieved(nlohmann::json());
		expect("cap", AP_PadHint_Select(open, 4, latched, &e, two, 2), 2);
		expect("null out", AP_PadHint_Select(open, 4, latched, &e, nullptr, 2), 0);
	}

	// Once ever: a reconnect reads the room's value and sends nothing again.
	APPadHintLatch again;
	again.connect("seed1", 0, 3);
	again.retrieved(update);
	expect("reconnect ready", again.ready(), 1);
	expect("reconnect sends nothing", pick(again, {101, 102, 103}, out), 0);
	expect("reconnect still hints new", pick(again, {101, 104}, out), 1);
	// Another client of the slot hinted more (SetNotify): merged.
	again.reply(nlohmann::json::object({{"104", 1}}));
	expect("notify merged", pick(again, {104}, out), 0);
	// Disconnect: not ready until the next read.
	again.disconnected();
	expect("disconnected", again.ready(), 0);

	// An unreadable value stops hints for the connection instead of risking
	// a second send.
	const char *bad[] = {"[1,2]", "5", "\"x\"", "{\"abc\":1}", "{\"\":1}", "{\"0\":1}", "{\"1234567890123456789\":1}"};
	for (const char *b : bad) {
		APPadHintLatch x; x.connect("s", 0, 1);
		x.retrieved(nlohmann::json::parse(b));
		expect(b, x.ready(), 0);
	}
	// A bad notify does not wipe what is known.
	again.connect("seed1", 0, 3); again.retrieved(update);
	again.reply(nlohmann::json::parse("[1]"));
	expect("bad notify keeps latch", again.has(101), 1);
	// A different seed or slot is a different key and starts empty.
	APPadHintLatch other; other.connect("seed2", 0, 3);
	expect("other seed key", other.key != again.key, 1);
	expect("other seed empty", other.codes.empty(), 1);

	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
