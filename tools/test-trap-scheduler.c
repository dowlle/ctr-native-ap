// Host harness for the pure trap scheduler (ap/ap_trap_sched_logic.h, issue #280).
//
// Replaces tools/test-trap-queue.c, whose nine cases described the shared
// lap-2/3 plus random-delay model the rework removed. Same shape as that harness
// and as tools/test-turbo-grant.c: no engine, no build system, one translation
// unit, compile and run.
//
//   cc -std=c99 -Wall -o /tmp/tsched tools/test-trap-scheduler.c && /tmp/tsched
//
// Every case here is a lifecycle rule from the trap rework design notebook
// (2026-08-19) or from the Alpha 2 triage ruling on boost-control serialization.
// The last two cases pin the apworld item identity table instead: the 20 trap ids
// are scattered across three runs, so the mapping is data and data can be wrong.

#include <stdio.h>
#include <string.h>

#include "../ap/ap_trap_items.h"
#include "../ap/ap_trap_observe_logic.h"
#include "../ap/ap_trap_sched_logic.h"

static int failures;
static int checks;

static void expect(const char *name, int got, int want)
{
	checks++;
	if (got != want)
	{
		printf("FAIL %-56s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

// ── World helpers ──
// 100 ms frames keep the arithmetic readable: the ruled one-second warning is ten
// frames and a 15 second effect is a hundred and fifty.
#define FRAME_MS 100

static AP_TrapWorld world_in(int context)
{
	AP_TrapWorld w;
	memset(&w, 0, sizeof w);
	w.context = context;
	w.mapEpoch = 1;
	w.controlUnlocked = 1;
	w.elapsedMs = FRAME_MS;
	return w;
}

static void run(AP_TrapSched *s, const AP_TrapWorld *w, int frames)
{
	int i;
	for (i = 0; i < frames; i++)
		AP_TrapSchedStep(s, w);
}

static void run_ms(AP_TrapSched *s, const AP_TrapWorld *w, int ms)
{
	run(s, w, ms / FRAME_MS);
}

// ── Event helpers ──
static AP_TrapEvent evbuf[AP_TRAP_EVENT_CAP];
static int evn;

static void drain(AP_TrapSched *s)
{
	evn = 0;
	while (evn < AP_TRAP_EVENT_CAP && AP_TrapSchedPopEvent(s, &evbuf[evn]))
		evn++;
}

static int count_ev(int kind, int effect)
{
	int i, n = 0;
	for (i = 0; i < evn; i++)
		if (evbuf[i].kind == kind && evbuf[i].effect == effect)
			n++;
	return n;
}

// Position of the first matching event, or -1. Used to assert ordering between
// effects rather than only that both happened.
static int ev_at(int kind, int effect)
{
	int i;
	for (i = 0; i < evn; i++)
		if (evbuf[i].kind == kind && evbuf[i].effect == effect)
			return i;
	return -1;
}

static int remain_of(const AP_TrapSched *s, int effect)
{
	int i;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
		if (s->slots[i].state == AP_TRAP_SLOT_ACTIVE && s->slots[i].effect == effect)
			return s->slots[i].remainMs;
	return -999;
}

static int slots_used(const AP_TrapSched *s)
{
	int i, n = 0;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
		if (s->slots[i].state != AP_TRAP_SLOT_EMPTY)
			n++;
	return n;
}

// ── Cases ──

// An eligible receipt goes straight to a warning and does not announce itself as
// armed; only a receipt that has to wait does.
static void case_warning_then_fire(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	AP_TrapSchedStep(&s, &w);
	drain(&s);
	expect("eligible receipt warns", count_ev(AP_TRAP_EV_WARN, AP_TRAP_ICY), 1);
	expect("eligible receipt does not announce armed",
	       count_ev(AP_TRAP_EV_ARMED, AP_TRAP_ICY), 0);
	expect("warning does not apply the effect",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 0);

	run_ms(&s, &w, 900);
	expect("effect still held at 900 ms of warning",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 0);
	run_ms(&s, &w, 100);
	drain(&s);
	expect("one second warning then fire", count_ev(AP_TRAP_EV_FIRE, AP_TRAP_ICY), 1);
	expect("effect applied after the warning",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 1);
}

// The settled matrix: Forced Boost is ineligible in Adventure hubs because
// disabled braking is unsafe around hub geometry. It must announce and wait.
static void case_armed_on_ineligible_receipt(void)
{
	AP_TrapSched s;
	AP_TrapWorld hub = world_in(AP_TRAP_CTX_HUB);
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
	run(&s, &hub, 40);
	drain(&s);
	expect("hub receipt announces armed", count_ev(AP_TRAP_EV_ARMED, AP_TRAP_BOOST), 1);
	expect("armed announcement is emitted once, not per frame",
	       count_ev(AP_TRAP_EV_ARMED, AP_TRAP_BOOST), 1);
	expect("ineligible context never fires", count_ev(AP_TRAP_EV_FIRE, AP_TRAP_BOOST), 0);
	expect("copy is retained, not consumed",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_BOOST), 1);

	// Icy Road IS hub-eligible, so the hub is not simply a dead window.
	AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	run_ms(&s, &hub, 1100);
	expect("hub-eligible effect fires in the hub", AP_TrapSchedActive(&s, AP_TRAP_ICY), 1);

	// A hub is a map, so entering a race is a map change: the hub's Icy Road is
	// consumed and the armed Forced Boost finally becomes eligible.
	race.mapEpoch = 2;
	run_ms(&s, &race, 1100);
	drain(&s);
	expect("hub effect cleared at the map boundary",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 0);
	expect("retained copy fires on the first eligible map",
	       AP_TrapSchedActive(&s, AP_TRAP_BOOST), 1);
}

// Demo Camera waits for the runtime's safe-camera predicate instead of spending
// its timed window against a camera it cannot own.
static void case_demo_camera_waits_for_safe_target(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	expect("Demo Camera is enabled by default",
	       AP_TrapSchedIsEnabled(&s, AP_TRAP_DEMO_CAMERA), 1);
	AP_TrapSchedReceive(&s, AP_TRAP_DEMO_CAMERA);
	run(&s, &w, 200);
	drain(&s);
	expect("unsafe camera target leaves the trap armed",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_DEMO_CAMERA), 1);
	expect("unsafe camera target never starts the warning",
	       count_ev(AP_TRAP_EV_WARN, AP_TRAP_DEMO_CAMERA), 0);
	w.conditions = AP_TRAP_COND_DEMO_CAMERA;
	run_ms(&s, &w, 1100);
	expect("safe camera target activates the retained copy",
	       AP_TrapSchedActive(&s, AP_TRAP_DEMO_CAMERA), 1);
}

// Map-lifetime effects are not on a clock; fixed-duration effects are; and an
// engine-natural effect waits for the runtime to say it finished.
static void case_duration_classes(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_LOWGRAV);
	run_ms(&s, &w, 1100);
	run_ms(&s, &w, 60000);
	expect("map-lifetime effect outlives any timer",
	       AP_TrapSchedActive(&s, AP_TRAP_LOWGRAV), 1);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
	run_ms(&s, &w, 1100);
	run_ms(&s, &w, 14900);
	expect("15 s effect still running at 14.9 s",
	       AP_TrapSchedActive(&s, AP_TRAP_BOOST), 1);
	run_ms(&s, &w, 200);
	expect("15 s effect ends on time", AP_TrapSchedActive(&s, AP_TRAP_BOOST), 0);
	expect("finished copy frees its slot", slots_used(&s), 0);

	AP_TrapSchedReset(&s);
	AP_TrapSchedEnable(&s, AP_TRAP_FLATTEN, 1);
	AP_TrapSchedReceive(&s, AP_TRAP_FLATTEN);
	run_ms(&s, &w, 1100);
	expect("engine-natural effect fires", AP_TrapSchedActive(&s, AP_TRAP_FLATTEN), 1);
	run_ms(&s, &w, 30000);
	expect("engine-natural effect ignores the clock",
	       AP_TrapSchedActive(&s, AP_TRAP_FLATTEN), 1);
	AP_TrapSchedEffectDone(&s, AP_TRAP_FLATTEN);
	expect("engine-natural effect ends when the runtime says so",
	       AP_TrapSchedActive(&s, AP_TRAP_FLATTEN), 0);
}

// Pause freezes timed effects and warnings. Nothing ages and nothing fires.
static void case_pause_freeze(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld p;
	int before;

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
	run_ms(&s, &w, 1100);
	run_ms(&s, &w, 5000);
	before = remain_of(&s, AP_TRAP_BOOST);
	expect("timed effect is running before the pause", before > 0, 1);

	p = w;
	p.paused = 1;
	run_ms(&s, &p, 30000);
	expect("pause does not age a timed effect", remain_of(&s, AP_TRAP_BOOST), before);
	expect("pause does not end a timed effect", AP_TrapSchedActive(&s, AP_TRAP_BOOST), 1);

	// A warning is frozen too, and a trap received while paused cannot fire.
	AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	run_ms(&s, &p, 5000);
	drain(&s);
	expect("no warning starts while paused", count_ev(AP_TRAP_EV_WARN, AP_TRAP_ICY), 0);
	expect("nothing fires while paused", count_ev(AP_TRAP_EV_FIRE, AP_TRAP_ICY), 0);

	run_ms(&s, &w, 1100);
	expect("the warning runs once the pause ends",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 1);
}

// A scripted camera or scripted control suspends the effect and its timer, and
// resumes with the remaining time intact.
static void case_scripted_suspend_resume(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld cut;
	int before;

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
	run_ms(&s, &w, 1100);
	run_ms(&s, &w, 4000);
	before = remain_of(&s, AP_TRAP_BOOST);

	cut = w;
	cut.scripted = 1;
	AP_TrapSchedStep(&s, &cut);
	drain(&s);
	expect("scripted control suspends the effect",
	       count_ev(AP_TRAP_EV_SUSPEND, AP_TRAP_BOOST), 1);
	expect("a suspended effect is not applied",
	       AP_TrapSchedActive(&s, AP_TRAP_BOOST), 0);

	run_ms(&s, &cut, 20000);
	expect("suspension does not age the timer", remain_of(&s, AP_TRAP_BOOST), before);

	// A scripted sequence suspends the whole timeline, so an armed copy does not
	// start its warning there either.
	AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	run_ms(&s, &cut, 5000);
	drain(&s);
	expect("no warning starts under scripted control",
	       count_ev(AP_TRAP_EV_WARN, AP_TRAP_ICY), 0);
	expect("the armed copy is retained through the sequence",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_ICY), 1);

	AP_TrapSchedStep(&s, &w);
	drain(&s);
	expect("normal control resumes the effect",
	       count_ev(AP_TRAP_EV_RESUME, AP_TRAP_BOOST), 1);
	expect("resumed effect is applied again", AP_TrapSchedActive(&s, AP_TRAP_BOOST), 1);
	expect("resumed effect keeps its remaining time",
	       remain_of(&s, AP_TRAP_BOOST), before - FRAME_MS);
}

// The podium and the finish ceremony never activate an effect.
static void case_podium_never_activates(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld fin;

	AP_TrapSchedReset(&s);
	fin = w;
	fin.finishOrPodium = 1;
	AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	run_ms(&s, &fin, 10000);
	drain(&s);
	expect("finish ceremony does not fire a trap",
	       count_ev(AP_TRAP_EV_FIRE, AP_TRAP_ICY), 0);
	expect("finish ceremony retains the copy",
	       AP_TrapSchedStateOf(&s, AP_TRAP_ICY) != AP_TRAP_SLOT_EMPTY, 1);

	run_ms(&s, &w, 1100);
	expect("the trap fires once the ceremony is over",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 1);
}

// The starting countdown may show the warning, but the effect begins only after
// normal kart control unlocks.
static void case_countdown_warns_but_holds(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld grid = w;
	grid.controlUnlocked = 0;

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	AP_TrapSchedStep(&s, &grid);
	drain(&s);
	expect("countdown allows the warning", count_ev(AP_TRAP_EV_WARN, AP_TRAP_ICY), 1);

	run_ms(&s, &grid, 5000);
	drain(&s);
	expect("countdown holds the effect", count_ev(AP_TRAP_EV_FIRE, AP_TRAP_ICY), 0);
	expect("held warning is not lost",
	       AP_TrapSchedStateOf(&s, AP_TRAP_ICY), AP_TRAP_SLOT_WARNING);

	AP_TrapSchedStep(&s, &w);
	expect("effect begins the frame control unlocks",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 1);
}

// Map-lifetime duplicates queue for a later map instead of stacking.
static void case_duplicate_queue_later_map(void)
{
	AP_TrapSched s;
	AP_TrapWorld a = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld b = world_in(AP_TRAP_CTX_CUP_LEG);
	b.mapEpoch = 2;

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	run_ms(&s, &a, 1100);
	drain(&s);
	expect("first copy fires", count_ev(AP_TRAP_EV_FIRE, AP_TRAP_ICY), 1);
	expect("second copy waits for a later map",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_ICY), 1);
	expect("second copy announces itself armed",
	       count_ev(AP_TRAP_EV_ARMED, AP_TRAP_ICY), 1);

	run_ms(&s, &a, 30000);
	expect("a duplicate never stacks on the same map",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_ICY), 1);

	run_ms(&s, &b, 1100);
	drain(&s);
	expect("the consumed copy is cleared at the boundary",
	       count_ev(AP_TRAP_EV_CLEAR, AP_TRAP_ICY), 1);
	expect("the queued copy activates on the next map",
	       count_ev(AP_TRAP_EV_FIRE, AP_TRAP_ICY), 1);
	expect("only one copy is left", slots_used(&s), 1);
}

// A timed duplicate refills the running window rather than stacking or extending
// past the full duration.
static void case_duplicate_refresh(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
	run_ms(&s, &w, 1100);
	run_ms(&s, &w, 10000);
	drain(&s);

	AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
	AP_TrapSchedStep(&s, &w);
	drain(&s);
	expect("duplicate refreshes the running effect",
	       count_ev(AP_TRAP_EV_REFRESH, AP_TRAP_BOOST), 1);
	expect("refresh does not start a second warning",
	       count_ev(AP_TRAP_EV_WARN, AP_TRAP_BOOST), 0);
	expect("refresh consumes the duplicate slot", slots_used(&s), 1);
	expect("refresh refills to the full duration",
	       remain_of(&s, AP_TRAP_BOOST) > AP_TRAP_BOOST_DURATION_MS - FRAME_MS * 2, 1);

	run_ms(&s, &w, 15000);
	expect("a refreshed effect still ends after one full window",
	       AP_TrapSchedActive(&s, AP_TRAP_BOOST), 0);
}

// A serialized duplicate runs its own full window after the first one ends.
static void case_duplicate_serialize(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	w.conditions = AP_TRAP_COND_EARNED_BOOST;

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_USF_NOBRAKE);
	AP_TrapSchedReceive(&s, AP_TRAP_USF_NOBRAKE);
	AP_TrapSchedStep(&s, &w);
	drain(&s);
	expect("conditional ambush fires on the boost event",
	       count_ev(AP_TRAP_EV_FIRE, AP_TRAP_USF_NOBRAKE), 1);
	expect("the serialized copy waits",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_USF_NOBRAKE), 1);

	run_ms(&s, &w, 10000);
	expect("no overlap while the first window runs",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_USF_NOBRAKE), 1);

	run_ms(&s, &w, 5100);
	drain(&s);
	expect("the second copy gets its own window",
	       count_ev(AP_TRAP_EV_FIRE, AP_TRAP_USF_NOBRAKE), 1);
	expect("the second window is a full one",
	       remain_of(&s, AP_TRAP_USF_NOBRAKE) > AP_TRAP_USF_DURATION_MS - FRAME_MS * 2, 1);
}

// Forced USF stays armed until the player earns a boost. A race starting is not
// the trigger.
static void case_conditional_ambush_waits(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld boosted;

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_USF_NOBRAKE);
	run_ms(&s, &w, 30000);
	drain(&s);
	expect("a race start does not trigger the ambush",
	       count_ev(AP_TRAP_EV_FIRE, AP_TRAP_USF_NOBRAKE), 0);
	expect("the ambush announces itself armed",
	       count_ev(AP_TRAP_EV_ARMED, AP_TRAP_USF_NOBRAKE), 1);

	boosted = w;
	boosted.conditions = AP_TRAP_COND_EARNED_BOOST;
	AP_TrapSchedStep(&s, &boosted);
	expect("the earned boost is escalated on the same frame",
	       AP_TrapSchedActive(&s, AP_TRAP_USF_NOBRAKE), 1);
}

// Boost Blocker, Forced Boost and Forced USF are one mutually exclusive family:
// they never overlap and they run in receipt order.
static void case_boost_family_serializes(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	w.conditions = AP_TRAP_COND_EARNED_BOOST;

	AP_TrapSchedReset(&s);
	AP_TrapSchedEnable(&s, AP_TRAP_BOOST_BLOCKER, 1);

	AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
	AP_TrapSchedReceive(&s, AP_TRAP_USF_NOBRAKE);
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST_BLOCKER);

	run_ms(&s, &w, 1100);
	drain(&s);
	expect("first receipt takes the family", count_ev(AP_TRAP_EV_FIRE, AP_TRAP_BOOST), 1);
	expect("second receipt waits", count_ev(AP_TRAP_EV_FIRE, AP_TRAP_USF_NOBRAKE), 0);
	expect("third receipt waits", count_ev(AP_TRAP_EV_FIRE, AP_TRAP_BOOST_BLOCKER), 0);
	expect("waiting members announce themselves armed",
	       count_ev(AP_TRAP_EV_ARMED, AP_TRAP_USF_NOBRAKE) +
	           count_ev(AP_TRAP_EV_ARMED, AP_TRAP_BOOST_BLOCKER),
	       2);
	expect("only one family member is applied",
	       AP_TrapSchedActive(&s, AP_TRAP_BOOST) + AP_TrapSchedActive(&s, AP_TRAP_USF_NOBRAKE) +
	           AP_TrapSchedActive(&s, AP_TRAP_BOOST_BLOCKER),
	       1);

	// Forced Boost's 15 s ends, and the second receipt takes over with its own
	// full window rather than inheriting what is left of the first.
	run_ms(&s, &w, 15000);
	drain(&s);
	expect("family releases in receipt order",
	       ev_at(AP_TRAP_EV_CLEAR, AP_TRAP_BOOST) < ev_at(AP_TRAP_EV_FIRE, AP_TRAP_USF_NOBRAKE),
	       1);
	expect("second member fires next", count_ev(AP_TRAP_EV_FIRE, AP_TRAP_USF_NOBRAKE), 1);
	expect("third member still waits", count_ev(AP_TRAP_EV_FIRE, AP_TRAP_BOOST_BLOCKER), 0);
	expect("second member gets a full window",
	       remain_of(&s, AP_TRAP_USF_NOBRAKE) > AP_TRAP_USF_DURATION_MS - FRAME_MS * 2, 1);

	// The third member waits out the second window and then takes its own warning
	// rather than inheriting the moment the family frees.
	run_ms(&s, &w, 15000);
	drain(&s);
	expect("third member takes its own warning",
	       count_ev(AP_TRAP_EV_WARN, AP_TRAP_BOOST_BLOCKER), 1);
	expect("third member has not fired during its warning",
	       count_ev(AP_TRAP_EV_FIRE, AP_TRAP_BOOST_BLOCKER), 0);
	run_ms(&s, &w, 1100);
	drain(&s);
	expect("third member fires last", count_ev(AP_TRAP_EV_FIRE, AP_TRAP_BOOST_BLOCKER), 1);
	expect("still no overlap at the end",
	       AP_TrapSchedActive(&s, AP_TRAP_BOOST) + AP_TrapSchedActive(&s, AP_TRAP_USF_NOBRAKE) +
	           AP_TrapSchedActive(&s, AP_TRAP_BOOST_BLOCKER),
	       1);
	expect("every receipt was eventually served", slots_used(&s), 1);
}

// A second camera transform queues behind the active one instead of fighting it
// for cameraDC[0].
static void case_camera_family_queues(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_DEMO_CAMERA);
	AP_TrapSchedReceive(&s, AP_TRAP_FIRSTPERSON);
	w.conditions = AP_TRAP_COND_DEMO_CAMERA;
	run_ms(&s, &w, 1100);
	drain(&s);
	expect("first camera transform fires",
	       count_ev(AP_TRAP_EV_FIRE, AP_TRAP_DEMO_CAMERA), 1);
	expect("the queued camera transform waits",
	       count_ev(AP_TRAP_EV_FIRE, AP_TRAP_FIRSTPERSON), 0);
	expect("the queued transform is still armed",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_FIRSTPERSON), 1);

	run_ms(&s, &w, 15000);
	run_ms(&s, &w, 1100);
	expect("the queued transform takes over when the family frees",
	       AP_TrapSchedActive(&s, AP_TRAP_FIRSTPERSON), 1);
	expect("the first transform is gone",
	       AP_TrapSchedActive(&s, AP_TRAP_DEMO_CAMERA), 0);
}

// The reported First Person regression, as a sequence. A forced camera that is
// only restored on the destination's first frame has already leaked into that
// map's intro camera and blocked the intro-skip input.
static void case_first_person_intro_regression(void)
{
	AP_TrapSched s;
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld next;

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_FIRSTPERSON);
	run_ms(&s, &race, 1100);
	expect("First Person is active on the source map",
	       AP_TrapSchedActive(&s, AP_TRAP_FIRSTPERSON), 1);

	// The runtime calls this at the start of the load, while the source map still
	// stands, so camera ownership is handed back before the destination builds its
	// intro camera state.
	AP_TrapSchedMapChange(&s);
	drain(&s);
	expect("camera released at the start of the load",
	       count_ev(AP_TRAP_EV_CLEAR, AP_TRAP_FIRSTPERSON), 1);
	expect("no camera transform survives the load",
	       AP_TrapSchedActive(&s, AP_TRAP_FIRSTPERSON), 0);

	// Destination map: intro camera plays with control still locked.
	next = world_in(AP_TRAP_CTX_RACE);
	next.mapEpoch = 2;
	next.controlUnlocked = 0;
	next.scripted = 1;
	run_ms(&s, &next, 4000);
	drain(&s);
	expect("nothing is forced during the destination intro",
	       AP_TrapSchedActive(&s, AP_TRAP_FIRSTPERSON), 0);
	expect("no camera transform fires during the intro",
	       count_ev(AP_TRAP_EV_FIRE, AP_TRAP_FIRSTPERSON), 0);
	expect("no copy leaked into the destination map", slots_used(&s), 0);

	// And a copy that was still armed at the boundary is free to fire on the new
	// map, but only once the intro is over.
	AP_TrapSchedReceive(&s, AP_TRAP_FIRSTPERSON);
	run_ms(&s, &next, 4000);
	expect("an armed copy is held through the intro",
	       AP_TrapSchedActive(&s, AP_TRAP_FIRSTPERSON), 0);
	next.controlUnlocked = 1;
	next.scripted = 0;
	run_ms(&s, &next, 1100);
	expect("the armed copy fires after the intro",
	       AP_TrapSchedActive(&s, AP_TRAP_FIRSTPERSON), 1);
}

// Restarting the current map clears map-lifetime and timed effects alike.
static void case_restart_clears(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld again;

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
	run_ms(&s, &w, 1100);
	expect("both effects run before the restart",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY) + AP_TrapSchedActive(&s, AP_TRAP_BOOST), 2);

	// A restart keeps the level but is still a load, so the runtime bumps the
	// epoch for it.
	again = w;
	again.mapEpoch = 2;
	AP_TrapSchedStep(&s, &again);
	expect("restart clears the map-lifetime effect",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 0);
	expect("restart clears the timed effect", AP_TrapSchedActive(&s, AP_TRAP_BOOST), 0);
	expect("restart consumes both copies", slots_used(&s), 0);
}

// A warning interrupted by a load is re-armed rather than consumed: it never
// fired, so nothing was delivered.
static void case_unfired_warning_survives_the_load(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld next;

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	run_ms(&s, &w, 500);
	drain(&s);
	expect("the warning is running", AP_TrapSchedStateOf(&s, AP_TRAP_ICY),
	       AP_TRAP_SLOT_WARNING);

	next = w;
	next.mapEpoch = 2;
	AP_TrapSchedStep(&s, &next);
	drain(&s);
	expect("the unfired warning is re-armed", count_ev(AP_TRAP_EV_REARM, AP_TRAP_ICY), 1);
	expect("the copy is not consumed",
	       AP_TrapSchedStateOf(&s, AP_TRAP_ICY) != AP_TRAP_SLOT_EMPTY, 1);
	expect("its warning restarts from full on the new map",
	       count_ev(AP_TRAP_EV_WARN, AP_TRAP_ICY), 1);

	run_ms(&s, &next, 800);
	expect("the restarted warning is not short-changed",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 0);
	run_ms(&s, &next, 300);
	expect("it fires on the new map", AP_TrapSchedActive(&s, AP_TRAP_ICY), 1);
}

// A new slot, seed or connection drops everything armed and everything active.
static void case_identity_reset(void)
{
	AP_TrapSched s;
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld hub = world_in(AP_TRAP_CTX_HUB);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
	AP_TrapSchedReceive(&s, AP_TRAP_USF_NOBRAKE);
	run_ms(&s, &race, 1100);
	expect("state exists before the reset", slots_used(&s) > 0, 1);
	expect("an effect is applied before the reset",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 1);

	AP_TrapSchedReset(&s);
	expect("reset drops every copy", slots_used(&s), 0);
	expect("reset drops every applied effect",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 0);

	// And the reset restores the shipped roster.
	expect("reset restores the default roster",
	       AP_TrapSchedIsEnabled(&s, AP_TRAP_DEMO_CAMERA), 1);

	// Nothing lingers to fire on the next connection.
	run_ms(&s, &hub, 5000);
	drain(&s);
	expect("nothing fires after a reset", evn, 0);
}

// Conditional predicates gate on their own prerequisite, independently of the
// context column.
static void case_conditional_predicates(void)
{
	AP_TrapSched s;
	AP_TrapWorld tt = world_in(AP_TRAP_CTX_TIME_TRIAL);
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedEnable(&s, AP_TRAP_ITEM_REROLL, 1);
	AP_TrapSchedReceive(&s, AP_TRAP_ITEM_REROLL);

	// Time Trial has no weapon inventory at all: wrong context, not just an unmet
	// condition.
	tt.conditions = AP_TRAP_COND_HELD_ITEM;
	run_ms(&s, &tt, 5000);
	expect("a weaponless mode is an ineligible context",
	       AP_TrapSchedActive(&s, AP_TRAP_ITEM_REROLL), 0);

	// Right context, no held item yet.
	race.mapEpoch = 2;
	run_ms(&s, &race, 5000);
	expect("the right context alone does not fire it",
	       AP_TrapSchedActive(&s, AP_TRAP_ITEM_REROLL), 0);

	race.conditions = AP_TRAP_COND_HELD_ITEM;
	run_ms(&s, &race, 1100);
	expect("it fires when the prerequisite holds",
	       AP_TrapSchedActive(&s, AP_TRAP_ITEM_REROLL), 1);
}

// The registry has a finite capacity and says so rather than overwriting.
static void case_registry_capacity(void)
{
	AP_TrapSched s;
	int i, last;

	AP_TrapSchedReset(&s);
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
		expect("slot accepted", AP_TrapSchedReceive(&s, AP_TRAP_ICY) >= 0, 1);
	last = AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	drain(&s);
	expect("an over-capacity receipt is refused", last, -1);
	expect("and it is reported", count_ev(AP_TRAP_EV_DROPPED, AP_TRAP_ICY), 1);
}

// ── Apworld item identity ──
//
// The expected ids are written out here rather than derived from the table under
// test, so this case genuinely pins the apworld's layout instead of agreeing with
// whatever ap_trap_items.h happens to say.
typedef struct
{
	long long id;
	int effect;
	int buildable;
} TrapIdRow;

static const TrapIdRow TRAP_IDS[] = {
	{35010016, AP_TRAP_ICY, 1},
	{35010017, AP_TRAP_LOWGRAV, 1},
	{35010018, AP_TRAP_USF_NOBRAKE, 1}, // renamed to Forced USF, id unchanged
	{35010019, AP_TRAP_BOOST, 1},
	{35010020, AP_TRAP_FIRSTPERSON, 1},
	// Wave 2 batch 1 made four of these buildable. Their ids did not move: an
	// effect becoming performable is a native fact, not an apworld one.
	{35010106, AP_TRAP_WUMPA_WIPEOUT, 1},
	{35010107, AP_TRAP_FLATTEN, 1}, // batch 2 opener
	{35010108, AP_TRAP_ITEM_REROLL, 1},
	{35010109, AP_TRAP_FORCED_USE, 1},
	{35010110, AP_TRAP_EMPTY_CRATES, 1},
	{35010111, AP_TRAP_WEAKENED_KART, 1},
	{35010112, AP_TRAP_BOOST_BLOCKER, 1},
	{35010113, AP_TRAP_WIREFRAME, 1},
	{35010114, AP_TRAP_NITRO, 1}, // renamed to Nitro Drop, id unchanged
	{35010115, AP_TRAP_REVERSE_STEERING, 1},
	{35010116, AP_TRAP_RED_POTION, 1},
	{35010190, AP_TRAP_UPSIDE_DOWN, 1},
	{35010191, AP_TRAP_MIRROR_MODE, 1},
	{35010192, AP_TRAP_WARPBALL_AMBUSH, 1},
	{35010193, AP_TRAP_DEMO_CAMERA, 1},
};
#define TRAP_ID_COUNT ((int)(sizeof TRAP_IDS / sizeof TRAP_IDS[0]))

// Ids from every other item family in the 0.2.0 table, plus the boundaries. None
// of them may enter the trap path.
static const long long NON_TRAP_IDS[] = {
	35010000,  // Trophy
	35010009,  // Red Gem
	35010014,  // Key
	35010015,  // Wumpa Fruit, the item the old contiguous block sat behind
	35010021,  // first comfort item, immediately after the old trap window
	35010025,  // last comfort item
	35010027,  // Progressive Boost, the capability ladder
	35010095,  // first itemsanity weapon
	35010105,  // last itemsanity weapon, immediately before the expansion block
	35010117,  // immediately after the expansion block
	35010120,  // Wumpa small bundle
	35010122,  // Wumpa progressive
	35010123,  // first character unlock
	35010139,  // first letter
	35010187,  // Gas Pedal
	35010188,  // Tizi Helper
	35010189,  // Turbo Grant, immediately before the camera transforms
	35010194,  // one past the end of the table
	35010500,  // far past the end
	35009999,  // one below the item base
	35000000,  // far below the item base
	0,
	-1,
};
#define NON_TRAP_ID_COUNT ((int)(sizeof NON_TRAP_IDS / sizeof NON_TRAP_IDS[0]))

static void case_item_id_map(void)
{
	int i, idx;

	expect("the table holds all 20 trap identities", AP_TRAP_ITEM_MAP_COUNT, 20);

	for (i = 0; i < TRAP_ID_COUNT; i++)
	{
		const TrapIdRow *r = &TRAP_IDS[i];
		char name[96];

		snprintf(name, sizeof name, "id %lld resolves to %s", r->id,
		         AP_TRAP_DESC[r->effect].name);
		expect(name, AP_TrapEffectForItemId(r->id), r->effect);

		// The index-level entry point, which is what the receive path actually
		// calls, has to agree with the id-level one.
		snprintf(name, sizeof name, "index %d agrees with id %lld",
		         (int)(r->id - AP_TRAP_ITEM_ID_BASE), r->id);
		expect(name, AP_TrapEffectForItemIndex((int)(r->id - AP_TRAP_ITEM_ID_BASE)),
		       r->effect);

		snprintf(name, sizeof name, "%s buildable flag", AP_TRAP_DESC[r->effect].name);
		expect(name, AP_TrapItemIsBuildable(r->effect), r->buildable);
	}

	// The five wave 1 effects, wave 2 batch 1's four, and Flatten as the batch 2
	// opener plus this batch's Boost Blocker and Wireframe have native effects.
	// The other six identities are reserved.
	{
		int buildable = 0;
		for (i = 0; i < TRAP_ID_COUNT; i++)
			buildable += AP_TrapItemIsBuildable(TRAP_IDS[i].effect);
		expect("all twenty identities are buildable", buildable, 20);
	}

	// No two identities share an effect, which a copy-paste slip in the table would
	// otherwise hide behind a passing per-row check.
	{
		int dupes = 0, j;
		for (i = 0; i < TRAP_ID_COUNT; i++)
			for (j = i + 1; j < TRAP_ID_COUNT; j++)
				if (TRAP_IDS[i].effect == TRAP_IDS[j].effect)
					dupes++;
		expect("no effect is claimed by two identities", dupes, 0);
	}

	for (i = 0; i < NON_TRAP_ID_COUNT; i++)
	{
		char name[96];
		snprintf(name, sizeof name, "id %lld is not a trap", NON_TRAP_IDS[i]);
		expect(name, AP_TrapEffectForItemId(NON_TRAP_IDS[i]), -1);
	}

	// Exhaustive sweep of the whole item table: every index that is not one of the
	// 20 must be refused, so a family dropped into one of the gaps between the runs
	// cannot quietly become a trap.
	{
		int wrong = 0, traps = 0;
		for (idx = 0; idx <= AP_TRAP_ITEM_INDEX_MAX; idx++)
		{
			int expectedTrap = 0;
			for (i = 0; i < TRAP_ID_COUNT; i++)
				if (TRAP_IDS[i].id - AP_TRAP_ITEM_ID_BASE == idx)
					expectedTrap = 1;
			if (AP_TrapItemIndexIsTrap(idx) != expectedTrap)
				wrong++;
			traps += AP_TrapItemIndexIsTrap(idx);
		}
		expect("no index in 0..193 is misclassified", wrong, 0);
		expect("exactly 20 indices are traps", traps, 20);
	}
}

// Every shipped identity drives the real receive path: id, then map lookup,
// then AP_TrapSchedReceive. Conditional effects may remain armed in this world.
static void case_shipped_identity_receipt(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	int i;
	int armedKept = 0;
	int fired = 0;

	AP_TrapSchedReset(&s);
	for (i = 0; i < TRAP_ID_COUNT; i++)
	{
		int effect = AP_TrapEffectForItemId(TRAP_IDS[i].id);
		expect("every identity is accepted by the registry",
		       AP_TrapSchedReceive(&s, effect) >= 0, 1);
	}
	expect("all 20 fit in the registry", slots_used(&s), 20);

	run_ms(&s, &w, 2000);
	drain(&s);

	for (i = 0; i < TRAP_ID_COUNT; i++)
	{
		int effect = TRAP_IDS[i].effect;
		if (TRAP_IDS[i].buildable)
			continue;
		armedKept += AP_TrapSchedArmedCount(&s, effect);
		// One line each, and it is the "not implemented" line, not the armed
		// presentation a player would read as a live threat.
		expect("reserved identity logs one inactive line",
		       count_ev(AP_TRAP_EV_INACTIVE, effect), 1);
		expect("reserved identity never fires", count_ev(AP_TRAP_EV_FIRE, effect), 0);
	}
	expect("no shipped identity remains reserved", armedKept, 0);

	// The effects behaved normally in the same batch. This counts what actually
	// ran rather than assuming all twenty, because ruled behaviours
	// deliberately hold some of them back in this world: only one member of the
	// boost_control family may be active at once, and a conditional effect whose
	// prerequisite is absent stays armed. With no conditions set, Forced USF
	// waits for a boost event and the three inventory and fruit traps wait for a
	// held item or ten Wumpa, leaving Icy Road, Low Gravity, Forced Boost, First
	// Person, Reverse Steering and Flatten to run.
	for (i = 0; i < TRAP_ID_COUNT; i++)
		if (TRAP_IDS[i].buildable)
			fired += count_ev(AP_TRAP_EV_FIRE, TRAP_IDS[i].effect);
	expect("buildable identities still run in the same batch", fired, 8);
	expect("Icy Road from its real item id is applied",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 1);
	expect("Forced USF stays armed without its boost event",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_USF_NOBRAKE), 1);
	expect("nothing was dropped", count_ev(AP_TRAP_EV_DROPPED, AP_TRAP_ICY), 0);
}

// Review-wave additions, 2026-08-19: three ruling gaps the first harness
// missed, found adversarially, plus the engine-natural wiring contract.

// Notebook, Item Reroll / Forced Use / Wumpa Wipeout: a prerequisite lost
// during the warning re-arms the copy instead of consuming it.
static void case_condition_lost_during_warning(void)
{
	AP_TrapSched s;
	AP_TrapWorld held = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld empty = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedEnable(&s, AP_TRAP_ITEM_REROLL, 1);
	AP_TrapSchedReceive(&s, AP_TRAP_ITEM_REROLL);

	held.conditions = AP_TRAP_COND_HELD_ITEM;
	AP_TrapSchedStep(&s, &held);
	expect("a held item starts the reroll warning",
	       AP_TrapSchedStateOf(&s, AP_TRAP_ITEM_REROLL), AP_TRAP_SLOT_WARNING);

	empty.conditions = 0;
	run_ms(&s, &empty, 2000);
	expect("the reroll does not fire after the item was used",
	       AP_TrapSchedActive(&s, AP_TRAP_ITEM_REROLL), 0);
	expect("the copy returns to armed for a later item",
	       AP_TrapSchedStateOf(&s, AP_TRAP_ITEM_REROLL), AP_TRAP_SLOT_ARMED);

	held.conditions = AP_TRAP_COND_HELD_ITEM;
	run_ms(&s, &held, 2000);
	expect("a later held item still fires the retained copy",
	       AP_TrapSchedActive(&s, AP_TRAP_ITEM_REROLL), 1);
}

// Hazard projection is a start gate, not a frame-perfect warning gate. Track
// seams and jumps may briefly lose the ground query after the player has already
// seen the warning; the warning must not loop, and the engine retries the actual
// spawn quietly once the scheduler fires it.
static void case_hazard_warning_commits_once(void)
{
	AP_TrapSched s;
	AP_TrapWorld safe = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld airborne = world_in(AP_TRAP_CTX_RACE);
	int effects[2] = {AP_TRAP_NITRO, AP_TRAP_RED_POTION};
	int i;

	safe.conditions = AP_TRAP_COND_SAFE_HAZARD;
	for (i = 0; i < 2; i++)
	{
		AP_TrapSchedReset(&s);
		AP_TrapSchedReceive(&s, effects[i]);
		AP_TrapSchedStep(&s, &safe);
		drain(&s);
		expect("hazard starts exactly one warning",
		       count_ev(AP_TRAP_EV_WARN, effects[i]), 1);

		run_ms(&s, &airborne, 1100);
		drain(&s);
		expect("lost ground does not re-arm the warning",
		       count_ev(AP_TRAP_EV_REARM, effects[i]), 0);
		expect("committed hazard reaches its quiet spawn retry",
		       AP_TrapSchedActive(&s, effects[i]), 1);
	}
}

// An armed trap is never a silent wait: receipts during the results screen and
// during pause still announce, even though nothing may fire there.
static void case_receipt_during_podium_announces(void)
{
	AP_TrapSched s;
	AP_TrapWorld podium = world_in(AP_TRAP_CTX_RACE);
	AP_TrapEvent ev;
	int armed = 0;

	AP_TrapSchedReset(&s);
	podium.finishOrPodium = 1;
	podium.controlUnlocked = 0;
	AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	run_ms(&s, &podium, 3000);
	while (AP_TrapSchedPopEvent(&s, &ev))
		if (ev.kind == AP_TRAP_EV_ARMED)
			armed++;
	expect("an armed trap announces itself during the results screen", armed, 1);
	expect("and nothing fired there",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 0);
}

static void case_receipt_during_pause_announces(void)
{
	AP_TrapSched s;
	AP_TrapWorld paused = world_in(AP_TRAP_CTX_HUB);
	AP_TrapEvent ev;
	int armed = 0;

	AP_TrapSchedReset(&s);
	paused.paused = 1;
	AP_TrapSchedReceive(&s, AP_TRAP_USF_NOBRAKE);
	run_ms(&s, &paused, 3000);
	while (AP_TrapSchedPopEvent(&s, &ev))
		if (ev.kind == AP_TRAP_EV_ARMED)
			armed++;
	expect("an armed trap announces itself while the game is paused", armed, 1);
	paused.paused = 0;
	run_ms(&s, &paused, 100);
	armed = 0;
	while (AP_TrapSchedPopEvent(&s, &ev))
		if (ev.kind == AP_TRAP_EV_ARMED)
			armed++;
	expect("the announcement is not repeated after unpausing", armed, 0);
}

// Wave-2 wiring contract, pinned deliberately: an engine-natural effect stays
// ACTIVE until its application code calls AP_TrapSchedEffectDone, and its
// serialized duplicate waits behind it. Activating any engine-natural effect
// WITHOUT wiring that call would ship exactly this shape live.
static void case_engine_natural_requires_a_done_reporter(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedEnable(&s, AP_TRAP_FLATTEN, 1);
	AP_TrapSchedReceive(&s, AP_TRAP_FLATTEN);
	AP_TrapSchedReceive(&s, AP_TRAP_FLATTEN);
	run_ms(&s, &w, 60000);
	expect("an engine-natural effect waits for its done report",
	       AP_TrapSchedActive(&s, AP_TRAP_FLATTEN), 1);
	expect("its serialized duplicate waits behind it",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_FLATTEN), 1);
	AP_TrapSchedEffectDone(&s, AP_TRAP_FLATTEN);
	run_ms(&s, &w, 2000);
	expect("the done report releases the next copy",
	       AP_TrapSchedActive(&s, AP_TRAP_FLATTEN), 1);
	expect("and the queue drained",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_FLATTEN), 0);
}

// ── Wave 2, batch 1 ──
//
// Four effects were activated: Wumpa Wipeout, Item Reroll, Forced Use and
// Reverse Steering. The cases below are per-effect rather than shared, because
// the whole point of the rework is that timing belongs to the effect.

// The roster this batch shipped, pinned explicitly. Activating an effect is a
// one-character edit in the descriptor table, so the set of effects a build will
// actually perform is exactly the kind of thing that drifts silently. The
// excluded half is listed too: each of those is waiting on something real
// (a missing engine state, an acceptance gate, a pending ruling), and flipping
// one on by accident is the failure this case exists to catch.
static void case_complete_roster(void)
{
	AP_TrapSched s;
	static const int active[] = {
		AP_TRAP_ICY, AP_TRAP_LOWGRAV, AP_TRAP_USF_NOBRAKE, AP_TRAP_BOOST,
		AP_TRAP_FIRSTPERSON, AP_TRAP_WUMPA_WIPEOUT, AP_TRAP_ITEM_REROLL,
		AP_TRAP_FORCED_USE, AP_TRAP_REVERSE_STEERING,
		// Batch 2 opener, once the squish damage state was correctly identified.
		AP_TRAP_FLATTEN,
		AP_TRAP_EMPTY_CRATES,
		AP_TRAP_WEAKENED_KART, AP_TRAP_BOOST_BLOCKER, AP_TRAP_WIREFRAME,
		AP_TRAP_UPSIDE_DOWN, AP_TRAP_MIRROR_MODE,
		AP_TRAP_WARPBALL_AMBUSH,
		AP_TRAP_NITRO, AP_TRAP_RED_POTION,
		AP_TRAP_DEMO_CAMERA,
	};
	int i;

	AP_TrapSchedReset(&s);
	for (i = 0; i < (int)(sizeof active / sizeof active[0]); i++)
		expect("implemented effect is active", AP_TrapSchedIsEnabled(&s, active[i]), 1);
	expect("the roster is twenty active effects",
	       (int)(sizeof active / sizeof active[0]), AP_TRAP_EFFECT_COUNT);
}

// None of the four joins a conflict family, and that is a deliberate reading of
// the notebook rather than an oversight: an inventory or fruit effect shares no
// engine resource with a camera or a boost tier. Pinned so a later batch that
// adds a family has to come back here and say why.
static void case_wave2_batch1_families(void)
{
	expect("Wumpa Wipeout joins no conflict family",
	       AP_TRAP_DESC[AP_TRAP_WUMPA_WIPEOUT].family, AP_TRAP_FAMILY_NONE);
	expect("Item Reroll joins no conflict family",
	       AP_TRAP_DESC[AP_TRAP_ITEM_REROLL].family, AP_TRAP_FAMILY_NONE);
	expect("Forced Use joins no conflict family",
	       AP_TRAP_DESC[AP_TRAP_FORCED_USE].family, AP_TRAP_FAMILY_NONE);
	expect("Reverse Steering joins no conflict family",
	       AP_TRAP_DESC[AP_TRAP_REVERSE_STEERING].family, AP_TRAP_FAMILY_NONE);
	// Flatten owns a damage animation, not a camera or a boost tier, so it shares
	// no resource with either existing family. It serializes against ITSELF
	// through the ordinary duplicate policy, which is what its ruling asks for.
	expect("Flatten joins no conflict family",
	       AP_TRAP_DESC[AP_TRAP_FLATTEN].family, AP_TRAP_FAMILY_NONE);
	expect("Flatten serializes its own duplicates",
	       AP_TRAP_DESC[AP_TRAP_FLATTEN].duplicate, AP_TRAP_DUP_SERIALIZE);
	expect("Flatten takes the engine's recovery, not a timer",
	       AP_TRAP_DESC[AP_TRAP_FLATTEN].duration, AP_TRAP_DURATION_ENGINE_NATURAL);
	expect("Flatten is eligible everywhere, hubs included",
	       AP_TRAP_DESC[AP_TRAP_FLATTEN].contexts, AP_TRAP_CTX_ALL);
}

// Wumpa Wipeout: hub-ineligible, gated on the juiced threshold, one second of
// warning, and one successful reset per copy.
static void case_wumpa_wipeout_lifecycle(void)
{
	AP_TrapSched s;
	AP_TrapWorld hub = world_in(AP_TRAP_CTX_HUB);
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);

	hub.conditions = AP_TRAP_COND_TEN_WUMPA;

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_WUMPA_WIPEOUT);

	// Adventure hubs are ruled out of the Wumpa economy, so ten fruit there is
	// still not an activation window.
	run_ms(&s, &hub, 5000);
	drain(&s);
	expect("a juiced hub does not fire Wumpa Wipeout",
	       count_ev(AP_TRAP_EV_FIRE, AP_TRAP_WUMPA_WIPEOUT), 0);
	expect("the hub receipt announces itself armed",
	       count_ev(AP_TRAP_EV_ARMED, AP_TRAP_WUMPA_WIPEOUT), 1);

	// A race without ten fruit is the right context and the wrong condition.
	race.mapEpoch = 2;
	run_ms(&s, &race, 5000);
	expect("a race below ten Wumpa does not fire it",
	       AP_TrapSchedActive(&s, AP_TRAP_WUMPA_WIPEOUT), 0);
	expect("and the copy is retained",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_WUMPA_WIPEOUT), 1);

	// Reaching ten starts the ruled one-second warning.
	race.conditions = AP_TRAP_COND_TEN_WUMPA;
	AP_TrapSchedStep(&s, &race);
	drain(&s);
	expect("ten Wumpa starts the warning",
	       count_ev(AP_TRAP_EV_WARN, AP_TRAP_WUMPA_WIPEOUT), 1);
	run_ms(&s, &race, 900);
	expect("the warning holds the reset for its full second",
	       AP_TrapSchedActive(&s, AP_TRAP_WUMPA_WIPEOUT), 0);
	run_ms(&s, &race, 100);
	expect("then it fires", AP_TrapSchedActive(&s, AP_TRAP_WUMPA_WIPEOUT), 1);

	// Engine-natural: the runtime reports the reset landed. A fruit reset has no
	// aftermath, so the runtime reports on the firing tick, but the scheduler is
	// what must not release the slot before that report arrives.
	run_ms(&s, &race, 30000);
	expect("it stays active until the runtime reports it done",
	       AP_TrapSchedActive(&s, AP_TRAP_WUMPA_WIPEOUT), 1);
	AP_TrapSchedEffectDone(&s, AP_TRAP_WUMPA_WIPEOUT);
	expect("the done report clears it",
	       AP_TrapSchedActive(&s, AP_TRAP_WUMPA_WIPEOUT), 0);
	expect("and consumes exactly the one copy", slots_used(&s), 0);
}

// Losing the fruit during the warning re-arms the copy instead of consuming it,
// and each duplicate needs its own later ten-Wumpa trigger.
static void case_wumpa_wipeout_duplicates_serialize(void)
{
	AP_TrapSched s;
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_WUMPA_WIPEOUT);
	AP_TrapSchedReceive(&s, AP_TRAP_WUMPA_WIPEOUT);

	race.conditions = AP_TRAP_COND_TEN_WUMPA;
	run_ms(&s, &race, 500); // mid-warning
	race.conditions = 0;    // the player spends or loses fruit
	AP_TrapSchedStep(&s, &race);
	drain(&s);
	expect("fruit lost during the warning re-arms the copy",
	       count_ev(AP_TRAP_EV_REARM, AP_TRAP_WUMPA_WIPEOUT), 1);
	expect("and does not consume it",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_WUMPA_WIPEOUT), 2);

	// Back to ten: the first copy fires, the second waits its turn.
	race.conditions = AP_TRAP_COND_TEN_WUMPA;
	run_ms(&s, &race, 1100);
	expect("the first copy fires on the next ten-Wumpa state",
	       AP_TrapSchedActive(&s, AP_TRAP_WUMPA_WIPEOUT), 1);
	expect("the duplicate waits behind it",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_WUMPA_WIPEOUT), 1);

	AP_TrapSchedEffectDone(&s, AP_TRAP_WUMPA_WIPEOUT);
	run_ms(&s, &race, 1100);
	expect("the second copy needs its own trigger and gets it",
	       AP_TrapSchedActive(&s, AP_TRAP_WUMPA_WIPEOUT), 1);
	AP_TrapSchedEffectDone(&s, AP_TRAP_WUMPA_WIPEOUT);
	expect("both copies are spent", slots_used(&s), 0);
}

// Item Reroll and Forced Use share a shape: a resolved held item is the
// prerequisite, weaponless modes are the wrong context entirely, and the copy is
// only released when the runtime reports the outcome landed.
static void case_inventory_traps_wait_for_their_outcome(void)
{
	AP_TrapSched s;
	AP_TrapWorld tt = world_in(AP_TRAP_CTX_TIME_TRIAL);
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);

	tt.conditions = AP_TRAP_COND_HELD_ITEM;
	race.conditions = AP_TRAP_COND_HELD_ITEM;

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_FORCED_USE);

	// A Time Trial has no weapon inventory, so a held item there is not even a
	// coherent state: the context column, not the predicate, is what refuses it.
	run_ms(&s, &tt, 5000);
	expect("Forced Use stays armed in a weaponless mode",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_FORCED_USE), 1);

	race.mapEpoch = 2;
	run_ms(&s, &race, 1100);
	expect("it fires once a weapon-enabled race holds an item",
	       AP_TrapSchedActive(&s, AP_TRAP_FORCED_USE), 1);

	// The engine refuses a fire request in an incompatible driver state, so the
	// scheduler must hold the copy open rather than assume the use landed.
	run_ms(&s, &race, 60000);
	expect("Forced Use is not consumed until the use is proven",
	       AP_TrapSchedActive(&s, AP_TRAP_FORCED_USE), 1);
	AP_TrapSchedEffectDone(&s, AP_TRAP_FORCED_USE);
	expect("the done report consumes it",
	       AP_TrapSchedActive(&s, AP_TRAP_FORCED_USE), 0);

	// Item Reroll behaves the same way while its roulette spins.
	AP_TrapSchedReceive(&s, AP_TRAP_ITEM_REROLL);
	AP_TrapSchedReceive(&s, AP_TRAP_ITEM_REROLL);
	run_ms(&s, &race, 1100);
	expect("Item Reroll fires on a resolved held item",
	       AP_TrapSchedActive(&s, AP_TRAP_ITEM_REROLL), 1);
	run_ms(&s, &race, 10000);
	expect("it holds the slot while the roulette spins",
	       AP_TrapSchedActive(&s, AP_TRAP_ITEM_REROLL), 1);
	expect("its duplicate waits for a later item",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_ITEM_REROLL), 1);
	AP_TrapSchedEffectDone(&s, AP_TRAP_ITEM_REROLL);
	run_ms(&s, &race, 1100);
	expect("and then takes its own turn",
	       AP_TrapSchedActive(&s, AP_TRAP_ITEM_REROLL), 1);
}

// Losing the held item during the warning re-arms both inventory traps: the
// use-it-or-lose-it window is real counterplay, not a way to discard the trap.
static void case_inventory_traps_rearm_on_lost_item(void)
{
	AP_TrapSched s;
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_ITEM_REROLL);
	AP_TrapSchedReceive(&s, AP_TRAP_FORCED_USE);

	race.conditions = AP_TRAP_COND_HELD_ITEM;
	run_ms(&s, &race, 500);
	race.conditions = 0; // the player fires the weapon during the warning
	AP_TrapSchedStep(&s, &race);
	drain(&s);
	expect("Item Reroll re-arms when the item goes",
	       count_ev(AP_TRAP_EV_REARM, AP_TRAP_ITEM_REROLL), 1);
	expect("Forced Use re-arms when the item goes",
	       count_ev(AP_TRAP_EV_REARM, AP_TRAP_FORCED_USE), 1);
	expect("neither is consumed", slots_used(&s), 2);

	// The next resolved item pays both, each with a fresh full warning.
	race.conditions = AP_TRAP_COND_HELD_ITEM;
	run_ms(&s, &race, 1100);
	expect("both fire on the next held item",
	       AP_TrapSchedActive(&s, AP_TRAP_ITEM_REROLL) +
	           AP_TrapSchedActive(&s, AP_TRAP_FORCED_USE),
	       2);
}

// Reverse Steering: hub-eligible, one second of warning, exactly fifteen
// seconds, and a duplicate that refreshes rather than stacking a second
// inversion that would cancel the first.
static void case_reverse_steering_lifecycle(void)
{
	AP_TrapSched s;
	AP_TrapWorld hub = world_in(AP_TRAP_CTX_HUB);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_REVERSE_STEERING);

	// The matrix rules it eligible everywhere, hubs included: it disrupts
	// steering without touching the doors-and-geometry safety the boost-control
	// traps are held out of hubs for. The first step promotes the copy into its
	// warning without ageing it, so the second's countdown starts here.
	AP_TrapSchedStep(&s, &hub);
	run_ms(&s, &hub, 900);
	expect("the warning holds the inversion",
	       AP_TrapSchedActive(&s, AP_TRAP_REVERSE_STEERING), 0);
	run_ms(&s, &hub, 100);
	expect("Reverse Steering activates in a hub",
	       AP_TrapSchedActive(&s, AP_TRAP_REVERSE_STEERING), 1);
	expect("with the ruled fifteen second window",
	       remain_of(&s, AP_TRAP_REVERSE_STEERING), 15000);

	run_ms(&s, &hub, 10000);
	expect("it is still running ten seconds in",
	       AP_TrapSchedActive(&s, AP_TRAP_REVERSE_STEERING), 1);

	// A duplicate refills the window on the spot instead of queueing a second
	// inversion, which would cancel the first.
	AP_TrapSchedReceive(&s, AP_TRAP_REVERSE_STEERING);
	AP_TrapSchedStep(&s, &hub);
	drain(&s);
	expect("a duplicate refreshes the running effect",
	       count_ev(AP_TRAP_EV_REFRESH, AP_TRAP_REVERSE_STEERING), 1);
	expect("it does not queue a second inversion",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_REVERSE_STEERING), 0);
	expect("and the window is full again",
	       remain_of(&s, AP_TRAP_REVERSE_STEERING), 15000);

	run_ms(&s, &hub, 15000);
	drain(&s);
	expect("the timer restores normal steering",
	       AP_TrapSchedActive(&s, AP_TRAP_REVERSE_STEERING), 0);
	expect("exactly once", count_ev(AP_TRAP_EV_CLEAR, AP_TRAP_REVERSE_STEERING), 1);
	expect("and nothing is left behind", slots_used(&s), 0);
}

// Every batch 1 effect ends at a map boundary, and an unfired warning survives
// it. Reverse Steering must not carry inverted controls into the next track, and
// an inventory trap that was still waiting for its outcome must not be spent by
// a load the player did not choose.
static void case_wave2_batch1_map_change(void)
{
	AP_TrapSched s;
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld next = world_in(AP_TRAP_CTX_RACE);

	race.conditions = AP_TRAP_COND_HELD_ITEM | AP_TRAP_COND_TEN_WUMPA;
	next.conditions = AP_TRAP_COND_HELD_ITEM | AP_TRAP_COND_TEN_WUMPA;
	next.mapEpoch = 2;

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_REVERSE_STEERING);
	AP_TrapSchedReceive(&s, AP_TRAP_ITEM_REROLL);
	run_ms(&s, &race, 1100);
	expect("both are running before the load",
	       AP_TrapSchedActive(&s, AP_TRAP_REVERSE_STEERING) +
	           AP_TrapSchedActive(&s, AP_TRAP_ITEM_REROLL),
	       2);

	AP_TrapSchedStep(&s, &next);
	drain(&s);
	expect("inverted steering does not cross the map boundary",
	       AP_TrapSchedActive(&s, AP_TRAP_REVERSE_STEERING), 0);
	expect("nor does the reroll's open slot",
	       AP_TrapSchedActive(&s, AP_TRAP_ITEM_REROLL), 0);
	expect("both are reported cleared",
	       count_ev(AP_TRAP_EV_CLEAR, AP_TRAP_REVERSE_STEERING) +
	           count_ev(AP_TRAP_EV_CLEAR, AP_TRAP_ITEM_REROLL),
	       2);
	expect("and the registry is empty", slots_used(&s), 0);

	// A copy still in its warning is returned to armed rather than consumed, so
	// a load during the warning costs the player nothing.
	AP_TrapSchedReceive(&s, AP_TRAP_WUMPA_WIPEOUT);
	run_ms(&s, &next, 500);
	expect("the copy is mid-warning",
	       AP_TrapSchedStateOf(&s, AP_TRAP_WUMPA_WIPEOUT), AP_TRAP_SLOT_WARNING);
	next.mapEpoch = 3;
	AP_TrapSchedStep(&s, &next);
	drain(&s);
	expect("an unfired warning is returned to armed by the load",
	       count_ev(AP_TRAP_EV_REARM, AP_TRAP_WUMPA_WIPEOUT), 1);
	expect("the copy survives the load", slots_used(&s), 1);
	expect("and was never consumed by it",
	       count_ev(AP_TRAP_EV_FIRE, AP_TRAP_WUMPA_WIPEOUT), 0);
	// The destination map is eligible too, so the same step that re-armed the
	// copy also starts its warning again from the full second. That is the point
	// of re-arming rather than consuming: the trap is intact and simply owes the
	// player its warning a second time.
	expect("and starts a fresh warning on the destination map",
	       AP_TrapSchedStateOf(&s, AP_TRAP_WUMPA_WIPEOUT), AP_TRAP_SLOT_WARNING);
}

// ── Runtime observation predicates (wave 2 batch 1 review) ──
//
// These drive ap/ap_trap_observe_logic.h directly. The review found that every
// runtime function in ap/ap_traps.c had zero coverage, and that two real defects
// lived in exactly the unpinned observation code. The predicates that reduce to
// integers were lifted into that header so they can be pinned here; the parts
// that must walk engine state (the LEV crate census, choosing a valid AI
// shooter) are NOT pinned, because faking an InstDef arena or a drivers array
// would only test the mock.

// Slot classification, which both inventory traps read as their prerequisite.
static void case_held_item_slot_states(void)
{
	expect("an empty slot is empty",
	       AP_TrapSlotContentsOf(AP_TRAP_ITEM_NONE, 0, 0), AP_TRAP_ITEM_STATE_EMPTY);
	expect("the roulette placeholder is rolling",
	       AP_TrapSlotContentsOf(AP_TRAP_ITEM_ROLLING, 90, 0), AP_TRAP_ITEM_STATE_ROLLING);
	expect("a live roll timer is rolling even without the placeholder",
	       AP_TrapSlotContentsOf(0x2, 40, 0), AP_TRAP_ITEM_STATE_ROLLING);
	expect("a weapon inside the post-use lockout is not usable yet",
	       AP_TrapSlotContentsOf(0x2, 0, 5), AP_TRAP_ITEM_STATE_LOCKED);
	expect("a settled weapon with clear timers is resolved",
	       AP_TrapSlotContentsOf(0x2, 0, 0), AP_TRAP_ITEM_STATE_RESOLVED);

	// The condition bit the descriptors gate on is exactly "resolved".
	expect("held-item condition holds only for a resolved slot",
	       AP_TrapHeldItemIsResolved(0x2, 0, 0), 1);
	expect("held-item condition rejects the lockout window",
	       AP_TrapHeldItemIsResolved(0x2, 0, 5), 0);
	expect("held-item condition rejects an empty slot",
	       AP_TrapHeldItemIsResolved(AP_TRAP_ITEM_NONE, 0, 0), 0);
	expect("held-item condition rejects a spinning roulette",
	       AP_TrapHeldItemIsResolved(AP_TRAP_ITEM_ROLLING, 90, 0), 0);
}

// REVIEW DEFECT 1. Item Reroll hung ACTIVE forever when the race ended mid-spin,
// because PlayLevel.c empties the slot without ever reaching the roll resolve.
static void case_reroll_survives_a_destroyed_roulette(void)
{
	expect("a spinning roulette is still worth waiting for",
	       AP_TrapRerollOutcome(AP_TRAP_ITEM_ROLLING, 90, 0), AP_TRAP_OUTCOME_WAIT);
	expect("so is one whose placeholder cleared but whose timer has not",
	       AP_TrapRerollOutcome(0x2, 40, 0), AP_TRAP_OUTCOME_WAIT);
	expect("an ordinary resolve completes the reroll",
	       AP_TrapRerollOutcome(0x2, 0, 0), AP_TRAP_OUTCOME_DONE);
	// The defect: the finish line confiscates the slot mid-spin, so the resolve
	// can never arrive. Waiting for it held the slot for the rest of the map and
	// swallowed a serialized duplicate.
	expect("a slot confiscated mid-spin completes rather than hanging",
	       AP_TrapRerollOutcome(AP_TRAP_ITEM_NONE, 90, 0), AP_TRAP_OUTCOME_DONE);
	expect("and so does one confiscated with the timer already clear",
	       AP_TrapRerollOutcome(AP_TRAP_ITEM_NONE, 0, 0), AP_TRAP_OUTCOME_DONE);
}

// REVIEW DEFECT 2. Forced Use read the finish line's emptied slot as proof its
// press had landed, and was consumed without ever firing the weapon.
static void case_forced_use_distinguishes_use_from_confiscation(void)
{
	// Armed holding one Missile (id 2, count 0).
	expect("holding the same weapon is not yet proof of a use",
	       AP_TrapForcedUseOutcome(0x2, 0, 0, 0x2, 0), AP_TRAP_OUTCOME_WAIT);
	expect("the post-use lockout starting is proof",
	       AP_TrapForcedUseOutcome(0x2, 0, 0x1e, 0x2, 0), AP_TRAP_OUTCOME_DONE);
	expect("a different weapon in the slot is proof",
	       AP_TrapForcedUseOutcome(0x3, 0, 0, 0x2, 0), AP_TRAP_OUTCOME_DONE);

	// The defect: an empty slot with a clear lockout and an undiminished count
	// is the finish-line confiscation, and the ruling says wait.
	expect("an emptied slot with no lockout is a confiscation, not a use",
	       AP_TrapForcedUseOutcome(AP_TRAP_ITEM_NONE, 0, 0, 0x2, 0), AP_TRAP_OUTCOME_WAIT);
	// An empty slot WITH a lockout is the tail of a genuine use, so it still counts.
	expect("an emptied slot with a live lockout is still a use",
	       AP_TrapForcedUseOutcome(AP_TRAP_ITEM_NONE, 0, 1, 0x2, 0), AP_TRAP_OUTCOME_DONE);

	// Triple ammunition: armed holding Missile x3 (id 0xb) with three rounds.
	expect("a full triple set is not yet a use",
	       AP_TrapForcedUseOutcome(0xb, 3, 0, 0xb, 3), AP_TRAP_OUTCOME_WAIT);
	expect("losing one round of a triple set is a use",
	       AP_TrapForcedUseOutcome(0xb, 2, 0, 0xb, 3), AP_TRAP_OUTCOME_DONE);
	expect("a triple set confiscated whole is not a use",
	       AP_TrapForcedUseOutcome(AP_TRAP_ITEM_NONE, 3, 0, 0xb, 3), AP_TRAP_OUTCOME_WAIT);
}

// REVIEW DEFECT 3. The lead timer reset on every state the ruling says to
// EXCLUDE from elapsed time, so one pause discarded a nearly complete countdown.
static void case_lead_timer_freezes_and_resets_correctly(void)
{
	// Ordinary accumulation while leading a race that has AI in it.
	expect("leading accumulates", AP_TrapLeadAccumulate(0, 0, 1, 1, 100), 100);
	expect("and keeps accumulating", AP_TrapLeadAccumulate(14900, 0, 1, 1, 100), 15000);

	// The ruled reset, and the ONLY reset: the player was genuinely overtaken.
	expect("losing first place resets completely",
	       AP_TrapLeadAccumulate(14900, 1, 1, 1, 100), 0);
	expect("being well down the field also resets",
	       AP_TrapLeadAccumulate(14900, 5, 1, 1, 100), 0);

	// The defect: excluded states must freeze, not clear.
	expect("pause and other excluded states freeze rather than reset",
	       AP_TrapLeadAccumulate(14900, 0, 1, 0, 100), 14900);
	expect("an unranked sort frame freezes rather than reading as overtaken",
	       AP_TrapLeadAccumulate(14900, -1, 1, 1, 100), 14900);
	expect("no valid AI freezes rather than reset",
	       AP_TrapLeadAccumulate(14900, 0, 0, 1, 100), 14900);

	// A frozen countdown resumes with its remaining time, which is the whole
	// point of freezing: a pause must not cost the player their lead.
	{
		int ms = 14900;
		ms = AP_TrapLeadAccumulate(ms, 0, 1, 0, 100); // paused
		ms = AP_TrapLeadAccumulate(ms, -1, 1, 1, 100); // ranks re-sorting
		ms = AP_TrapLeadAccumulate(ms, 0, 1, 1, 100); // control back
		expect("a frozen countdown resumes and completes", ms >= 15000, 1);
	}

	// A negative or zero frame time cannot run the countdown backwards.
	expect("a zero frame adds nothing", AP_TrapLeadAccumulate(500, 0, 1, 1, 0), 500);
	expect("a negative frame adds nothing", AP_TrapLeadAccumulate(500, 0, 1, 1, -50), 500);
}

// Review blocker: the lead can complete while retail's singleton is occupied.
// The player may then lose first before that Warpball disappears, but the copy
// has already earned its fire and must not demand another fifteen-second lead.
static void case_warpball_earned_wait_is_latched(void)
{
	AP_TrapLeadState lead = {0, 0};

	lead = AP_TrapLeadUpdate(lead, 0, 1, 1, 15000, 15000);
	expect("Warpball lead becomes earned at fifteen seconds", lead.earned, 1);
	expect("earned Warpball retains its completed timer", lead.elapsedMs, 15000);

	// A pre-existing Warpball keeps the condition hidden in production. The
	// observer still runs, and this overtaken frame is the exact former defect.
	lead = AP_TrapLeadUpdate(lead, 2, 1, 1, 100, 15000);
	expect("losing first during singleton wait preserves earned Warpball",
	       lead.earned, 1);
	expect("singleton wait does not restart the completed timer", lead.elapsedMs, 15000);

	// Successful birth owns the reset. A serialized duplicate starts from this
	// cleared state and therefore cannot inherit the previous copy's earned lead.
	lead.elapsedMs = 0;
	lead.earned = 0;
	lead = AP_TrapLeadUpdate(lead, 0, 1, 1, 100, 15000);
	expect("serialized duplicate requires a fresh lead", lead.earned, 0);
	expect("serialized duplicate starts a new timer", lead.elapsedMs, 100);
}

// ── Flatten (batch 2 opener) ──

// The state gate that decides whether the engine may be handed a fresh squish.
// The ruling suppresses Flatten during a mask rescue and other incompatible
// damage animations, and DELIBERATELY allows it airborne, so nothing here tests
// ground contact: there is no ground term to get wrong.
static void case_flatten_ready_gate(void)
{
	// Ordinary driving, drifting and the anti-vshift window all accept a squish.
	// These three are the engine's own controllable set (VehPhysProc.c:489).
	expect("normal driving accepts a squish",
	       AP_TrapFlattenReady(AP_TRAP_KS_NORMAL, 0, 0, 0, 0, 0), 1);
	expect("drifting accepts a squish",
	       AP_TrapFlattenReady(AP_TRAP_KS_DRIFTING, 0, 0, 0, 0, 0), 1);
	expect("the anti-vshift window accepts a squish",
	       AP_TrapFlattenReady(AP_TRAP_KS_ANTIVSHIFT, 0, 0, 0, 0, 0), 1);

	// Incompatible damage animations and the mask rescue all wait.
	expect("a mask rescue waits",
	       AP_TrapFlattenReady(AP_TRAP_KS_MASK_GRABBED, 0, 0, 0, 0, 0), 0);
	expect("a spinout waits", AP_TrapFlattenReady(AP_TRAP_KS_SPINNING, 0, 0, 0, 0, 0), 0);
	expect("a blast waits", AP_TrapFlattenReady(AP_TRAP_KS_BLASTED, 0, 0, 0, 0, 0), 0);
	expect("a crash waits", AP_TrapFlattenReady(AP_TRAP_KS_CRASHING, 0, 0, 0, 0, 0), 0);

	// REVIEW DEFECT A. Scripted movement is not a damage animation, so a gate that
	// lists damage states to refuse lets all of it through. Each of these installs
	// its own driver func table, and the squish routes DRIVER_FUNC_INIT at
	// VehPhysProc_SpinFirst_Init, which overwrites the whole table. In a hub warp
	// pad that erases VehStuckProc_Warp_PhysAngular, the only stage the pad leaves
	// running, and the warp can never finish: the player is stranded in the hub.
	expect("a hub warp pad waits, and is not torn out from under the player",
	       AP_TrapFlattenReady(AP_TRAP_KS_WARP_PAD, 0, 0, 0, 0, 0), 0);
	expect("the end-of-event freeze waits",
	       AP_TrapFlattenReady(AP_TRAP_KS_FREEZE, 0, 0, 0, 0, 0), 0);
	expect("the starting countdown rev waits",
	       AP_TrapFlattenReady(AP_TRAP_KS_ENGINE_REVVING, 0, 0, 0, 0, 0), 0);

	// Already flattened: do not re-squish. damageType 3 has no such guard of its
	// own, unlike the blasted branch, so this one is ours.
	expect("an already flattened kart is not re-squished",
	       AP_TrapFlattenReady(AP_TRAP_KS_NORMAL, 0xf00, 0, 0, 0, 0), 0);

	// REVIEW DEFECT B. VehPickState_NewState zeroes pendingDamageType at entry,
	// so dispatching on top of a queued collision hit deletes it outright: the
	// player never takes it, its attacker is never credited, and its DeathLink
	// never sends. The gap between VehPhysCrash_Attack queuing it and
	// VehPickupItem_ShootOnCirclePress consuming it spans a whole frame, with
	// AP_TrapTick running inside it, so this is structural rather than a race.
	expect("a queued collision hit is waited for, not swallowed",
	       AP_TrapFlattenReady(AP_TRAP_KS_NORMAL, 0, 0, 0, 0, 2), 0);
	expect("a queued turbo-land squish is waited for too",
	       AP_TrapFlattenReady(AP_TRAP_KS_NORMAL, 0, 0, 0, 0, 3), 0);

	// Engine protection. Checked BEFORE the dispatch specifically so a bubble
	// shield is not popped by an attempt that then refuses to flatten anyway.
	expect("invincibility waits",
	       AP_TrapFlattenReady(AP_TRAP_KS_NORMAL, 0, 0x2a0, 0, 0, 0), 0);
	expect("an active mask waits",
	       AP_TrapFlattenReady(AP_TRAP_KS_NORMAL, 0, 0, 1, 0, 0), 0);
	expect("a bubble shield waits rather than being spent",
	       AP_TrapFlattenReady(AP_TRAP_KS_NORMAL, 0, 0, 0, 1, 0), 0);
}

// Completion is the engine's own recovery, not a duration this trap invented.
static void case_flatten_recovery_gate(void)
{
	expect("a running squish timer is not recovered",
	       AP_TrapFlattenRecovered(AP_TRAP_KS_NORMAL, 0xf00), 0);
	// damageType 3 falls through to the spinout init, so the follow-up spin is
	// part of the recovery and must not be mistaken for the end of it.
	expect("the follow-up spin is not recovered yet",
	       AP_TrapFlattenRecovered(AP_TRAP_KS_SPINNING, 0), 0);
	expect("a blast during recovery is not recovered",
	       AP_TrapFlattenRecovered(AP_TRAP_KS_BLASTED, 0), 0);
	// A scripted state is not the end of a recovery either, so an expired squish
	// timer inside a warp pad or an end-of-event freeze still holds the slot.
	expect("a warp pad is not a finished recovery",
	       AP_TrapFlattenRecovered(AP_TRAP_KS_WARP_PAD, 0), 0);
	expect("the end-of-event freeze is not a finished recovery",
	       AP_TrapFlattenRecovered(AP_TRAP_KS_FREEZE, 0), 0);
	expect("back to normal control is recovered",
	       AP_TrapFlattenRecovered(AP_TRAP_KS_NORMAL, 0), 1);
	expect("back to drifting is recovered too",
	       AP_TrapFlattenRecovered(AP_TRAP_KS_DRIFTING, 0), 1);
}

// Scheduler-side lifecycle: one second of warning, hub eligible, engine-natural
// completion, and duplicates that serialize instead of stacking.
static void case_flatten_lifecycle(void)
{
	AP_TrapSched s;
	AP_TrapWorld hub = world_in(AP_TRAP_CTX_HUB);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_FLATTEN);

	AP_TrapSchedStep(&s, &hub);
	drain(&s);
	expect("an eligible receipt goes straight to its warning",
	       count_ev(AP_TRAP_EV_WARN, AP_TRAP_FLATTEN), 1);
	run_ms(&s, &hub, 900);
	expect("the warning holds the squish for its full second",
	       AP_TrapSchedActive(&s, AP_TRAP_FLATTEN), 0);
	run_ms(&s, &hub, 100);
	expect("Flatten activates in a hub", AP_TrapSchedActive(&s, AP_TRAP_FLATTEN), 1);

	// Engine-natural: the runtime holds the slot through the squish, the spin and
	// the grace interval, and only then reports done.
	run_ms(&s, &hub, 60000);
	expect("it holds the slot until the runtime reports recovery",
	       AP_TrapSchedActive(&s, AP_TRAP_FLATTEN), 1);
	AP_TrapSchedEffectDone(&s, AP_TRAP_FLATTEN);
	expect("the done report consumes it",
	       AP_TrapSchedActive(&s, AP_TRAP_FLATTEN), 0);
	expect("and nothing is left behind", slots_used(&s), 0);
}

// Duplicates serialize: the second copy waits for the first to finish recovering
// rather than squishing a kart that is already flattened.
static void case_flatten_duplicates_serialize(void)
{
	AP_TrapSched s;
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_FLATTEN);
	AP_TrapSchedReceive(&s, AP_TRAP_FLATTEN);

	run_ms(&s, &race, 1100);
	expect("the first copy fires", AP_TrapSchedActive(&s, AP_TRAP_FLATTEN), 1);
	run_ms(&s, &race, 30000);
	expect("the duplicate does not stack on top of it",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_FLATTEN), 1);

	AP_TrapSchedEffectDone(&s, AP_TRAP_FLATTEN);
	run_ms(&s, &race, 1100);
	expect("the duplicate takes its own turn with its own warning",
	       AP_TrapSchedActive(&s, AP_TRAP_FLATTEN), 1);
	AP_TrapSchedEffectDone(&s, AP_TRAP_FLATTEN);
	expect("both copies are spent", slots_used(&s), 0);
}

// Flatten and a running timed effect are independent: no shared family, so a
// flatten neither waits for Reverse Steering nor cancels it.
static void case_flatten_and_timed_effects_coexist(void)
{
	AP_TrapSched s;
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_REVERSE_STEERING);
	run_ms(&s, &race, 1100);
	expect("Reverse Steering is running",
	       AP_TrapSchedActive(&s, AP_TRAP_REVERSE_STEERING), 1);

	AP_TrapSchedReceive(&s, AP_TRAP_FLATTEN);
	run_ms(&s, &race, 1100);
	expect("Flatten fires without waiting for it",
	       AP_TrapSchedActive(&s, AP_TRAP_FLATTEN), 1);
	expect("and Reverse Steering keeps running underneath",
	       AP_TrapSchedActive(&s, AP_TRAP_REVERSE_STEERING), 1);

	// The inverted-steering window still ends on its own clock, unaffected. It has
	// 13.9 s left by now: the fifteen started when it fired, and the eleven frames
	// that carried Flatten through its own warning aged it by 1.1 s.
	run_ms(&s, &race, 14000);
	expect("the timed effect still ends on its own clock",
	       AP_TrapSchedActive(&s, AP_TRAP_REVERSE_STEERING), 0);
	expect("while Flatten is still waiting on the engine",
	       AP_TrapSchedActive(&s, AP_TRAP_FLATTEN), 1);
}

static void case_client_trap_duration_policy(void)
{
	AP_TrapSched s;
	AP_TrapWorld hub = world_in(AP_TRAP_CTX_HUB);
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld locked = race;
	AP_TrapWorld next = race;
	static const int choices[] = {10000, 15000, 20000, 25000, 30000,
	                              45000, 60000, 90000};
	int i;

	// Every public timed choice replaces both former map-lifetime and fixed
	// descriptor durations. Engine-natural effects remain outside this policy.
	for (i = 0; i < (int)(sizeof(choices) / sizeof(choices[0])); i++)
	{
		AP_TrapSchedReset(&s);
		AP_TrapSchedSetDuration(&s, choices[i]);
		expect("timed choice governs former map-lifetime effects",
		       AP_TrapSchedEffectiveDuration(&s, &AP_TRAP_DESC[AP_TRAP_ICY]), choices[i]);
		expect("timed choice governs existing fixed effects",
		       AP_TrapSchedEffectiveDuration(&s, &AP_TRAP_DESC[AP_TRAP_BOOST]), choices[i]);
		expect("timed choice excludes engine-natural effects",
		       AP_TrapSchedEffectiveDuration(&s, &AP_TRAP_DESC[AP_TRAP_FLATTEN]), -1);
	}

	AP_TrapSchedReset(&s);
	AP_TrapSchedSetDuration(&s, 10000);
	AP_TrapSchedReceive(&s, AP_TRAP_ICY);
	run_ms(&s, &hub, 2000);
	expect("duration trap received in hub stays armed",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_ICY), 1);
	run_ms(&s, &race, 1100);
	expect("duration trap fires in the next playable event",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 1);
	run_ms(&s, &race, 9900);
	expect("ten-second choice is still active before its last frame",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 1);
	locked.controlUnlocked = 0;
	run_ms(&s, &locked, 5000);
	expect("control-locked frames do not consume duration",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 1);
	run_ms(&s, &race, 100);
	expect("timed choice expires on the final playable frame",
	       AP_TrapSchedActive(&s, AP_TRAP_ICY), 0);

	AP_TrapSchedReset(&s);
	AP_TrapSchedSetDuration(&s, 0);
	AP_TrapSchedReceive(&s, AP_TRAP_FIRSTPERSON);
	run_ms(&s, &race, 1100);
	run_ms(&s, &race, 120000);
	expect("Full race ignores elapsed playable time",
	       AP_TrapSchedActive(&s, AP_TRAP_FIRSTPERSON), 1);
	next.mapEpoch++;
	AP_TrapSchedStep(&s, &next);
	expect("Full race clears at the race or map boundary",
	       AP_TrapSchedActive(&s, AP_TRAP_FIRSTPERSON), 0);
}

static void case_empty_crates_reward_policy(void)
{
	expect("inactive Empty Crates preserves the local reward",
	       AP_TrapCrateRewardSuppressed(0, 1, 0), 0);
	expect("active Empty Crates suppresses the local human reward",
	       AP_TrapCrateRewardSuppressed(1, 1, 0), 1);
	expect("active Empty Crates preserves a remote human reward",
	       AP_TrapCrateRewardSuppressed(1, 0, 0), 0);
	expect("active Empty Crates preserves an AI reward",
	       AP_TrapCrateRewardSuppressed(1, 0, 1), 0);
	expect("a defensive local-bot combination never suppresses AI",
	       AP_TrapCrateRewardSuppressed(1, 1, 1), 0);
}

static void case_empty_crates_lifecycle(void)
{
	AP_TrapSched s;
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);
	AP_TrapWorld empty = race;

	AP_TrapSchedReset(&s);
	empty.conditions = 0;
	AP_TrapSchedReceive(&s, AP_TRAP_EMPTY_CRATES);
	run_ms(&s, &empty, 2000);
	expect("Empty Crates waits on a map without eligible crates",
	       AP_TrapSchedActive(&s, AP_TRAP_EMPTY_CRATES), 0);
	expect("the waiting Empty Crates copy remains armed",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_EMPTY_CRATES), 1);

	race.conditions = AP_TRAP_COND_ELIGIBLE_CRATES;
	run_ms(&s, &race, 1100);
	expect("Empty Crates activates after its warning on an eligible map",
	       AP_TrapSchedActive(&s, AP_TRAP_EMPTY_CRATES), 1);

	AP_TrapSchedReceive(&s, AP_TRAP_EMPTY_CRATES);
	run_ms(&s, &race, 2000);
	expect("an Empty Crates duplicate does not stack on the current map",
	       AP_TrapSchedActive(&s, AP_TRAP_EMPTY_CRATES), 1);
	expect("an Empty Crates duplicate queues for a later map",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_EMPTY_CRATES), 1);

	race.mapEpoch++;
	AP_TrapSchedStep(&s, &race);
	expect("the active Empty Crates copy clears on map change",
	       AP_TrapSchedActive(&s, AP_TRAP_EMPTY_CRATES), 0);
	run_ms(&s, &race, 1100);
	expect("the queued Empty Crates copy activates on the next eligible map",
	       AP_TrapSchedActive(&s, AP_TRAP_EMPTY_CRATES), 1);
}

static void case_boost_blocker_policy(void)
{
	expect("Boost Blocker rejects a local boost grant",
	       AP_TrapBoostGrantAllowed(1, 1), 0);
	expect("Boost Blocker leaves AI and non-local boost grants alone",
	       AP_TrapBoostGrantAllowed(1, 0), 1);
	expect("boost grants pass when Boost Blocker is inactive",
	       AP_TrapBoostGrantAllowed(0, 1), 1);
}

static void case_weakened_boost_policy(void)
{
	expect("inactive Weakened Kart preserves a pack-off tier",
	       AP_TrapWeakenedBoostTier(0, -1, 2), -1);
	expect("pack-off vanilla USF capability weakens to ordinary boost",
	       AP_TrapWeakenedBoostTier(1, -1, 2), 1);
	expect("Blue Fire weakens to USF",
	       AP_TrapWeakenedBoostTier(1, 3, 2), 2);
	expect("USF weakens to ordinary boost",
	       AP_TrapWeakenedBoostTier(1, 2, 2), 1);
	expect("ordinary boost weakens to no boost",
	       AP_TrapWeakenedBoostTier(1, 1, 2), 0);
	expect("no boost stays at its floor",
	       AP_TrapWeakenedBoostTier(1, 0, 2), 0);
}

static void case_hazard_projection_distance(void)
{
	expect("a stopped kart still gets a visible minimum hazard lead",
	       AP_TrapHazardDistance(0, 1750), 160);
	expect("reverse speed projects by magnitude",
	       AP_TrapHazardDistance(-0x3000, 1750),
	       AP_TrapHazardDistance(0x3000, 1750));
	expect("ordinary race speed projects between the safety clamps",
	       AP_TrapHazardDistance(0x3000, 1750), 328);
	expect("extreme speed cannot project beyond the terrain probe budget",
	       AP_TrapHazardDistance(0x7ffff, 1750), 420);
}

static void case_remaining_roster_contracts(void)
{
	AP_TrapSched s;
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);

	expect("Weakened Kart runs for twenty seconds",
	       AP_TRAP_DESC[AP_TRAP_WEAKENED_KART].durationMs, 20000);
	expect("Upside Down uses the provisional fifteen-second comfort window",
	       AP_TRAP_DESC[AP_TRAP_UPSIDE_DOWN].durationMs, 15000);
	expect("Mirror Mode runs for fifteen seconds",
	       AP_TRAP_DESC[AP_TRAP_MIRROR_MODE].durationMs, 15000);
	expect("Upside Down is a camera transform",
	       AP_TRAP_DESC[AP_TRAP_UPSIDE_DOWN].family, AP_TRAP_FAMILY_CAMERA_TRANSFORM);
	expect("Mirror Mode is a camera transform",
	       AP_TRAP_DESC[AP_TRAP_MIRROR_MODE].family, AP_TRAP_FAMILY_CAMERA_TRANSFORM);
	expect("Nitro waits for a safe projected target",
	       AP_TRAP_DESC[AP_TRAP_NITRO].condition, AP_TRAP_COND_SAFE_HAZARD);
	expect("Red Potion waits for a safe projected target",
	       AP_TRAP_DESC[AP_TRAP_RED_POTION].condition, AP_TRAP_COND_SAFE_HAZARD);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_NITRO);
	run_ms(&s, &race, 2000);
	expect("Nitro remains armed without safe ground",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_NITRO), 1);
	race.conditions = AP_TRAP_COND_SAFE_HAZARD;
	run_ms(&s, &race, 1100);
	expect("Nitro fires after a safe-ground warning",
	       AP_TrapSchedActive(&s, AP_TRAP_NITRO), 1);
	AP_TrapSchedEffectDone(&s, AP_TRAP_NITRO);
	expect("a successful Nitro birth consumes exactly one copy",
	       slots_used(&s), 0);
}

static void case_boost_blocker_lifecycle(void)
{
	AP_TrapSched s;
	AP_TrapWorld hub = world_in(AP_TRAP_CTX_HUB);
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST_BLOCKER);
	run_ms(&s, &hub, 2000);
	expect("Boost Blocker stays armed in a hub",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_BOOST_BLOCKER), 1);

	run_ms(&s, &race, 1100);
	expect("Boost Blocker activates after its race warning",
	       AP_TrapSchedActive(&s, AP_TRAP_BOOST_BLOCKER), 1);
	run_ms(&s, &race, 10000);
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST_BLOCKER);
	run_ms(&s, &race, 100);
	expect("a duplicate refreshes Boost Blocker without stacking",
	       slots_used(&s), 1);
	run_ms(&s, &race, 10000);
	expect("refreshed Boost Blocker still owns its full new window",
	       AP_TrapSchedActive(&s, AP_TRAP_BOOST_BLOCKER), 1);
	run_ms(&s, &race, 5100);
	expect("Boost Blocker clears after fifteen refreshed seconds",
	       AP_TrapSchedActive(&s, AP_TRAP_BOOST_BLOCKER), 0);
}

static void case_wireframe_lifecycle(void)
{
	AP_TrapSched s;
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_WIREFRAME);
	run_ms(&s, &race, 1100);
	expect("Wireframe activates after its warning",
	       AP_TrapSchedActive(&s, AP_TRAP_WIREFRAME), 1);

	AP_TrapSchedReceive(&s, AP_TRAP_WIREFRAME);
	run_ms(&s, &race, 2000);
	expect("a Wireframe duplicate queues for a later map",
	       AP_TrapSchedArmedCount(&s, AP_TRAP_WIREFRAME), 1);
	race.mapEpoch++;
	AP_TrapSchedStep(&s, &race);
	expect("active Wireframe clears at the loading boundary",
	       AP_TrapSchedActive(&s, AP_TRAP_WIREFRAME), 0);
	run_ms(&s, &race, 1100);
	expect("queued Wireframe activates on the next map",
	       AP_TrapSchedActive(&s, AP_TRAP_WIREFRAME), 1);
}

int main(void)
{
	case_warning_then_fire();
	case_armed_on_ineligible_receipt();
	case_demo_camera_waits_for_safe_target();
	case_duration_classes();
	case_pause_freeze();
	case_scripted_suspend_resume();
	case_podium_never_activates();
	case_countdown_warns_but_holds();
	case_duplicate_queue_later_map();
	case_duplicate_refresh();
	case_duplicate_serialize();
	case_conditional_ambush_waits();
	case_boost_family_serializes();
	case_camera_family_queues();
	case_first_person_intro_regression();
	case_restart_clears();
	case_unfired_warning_survives_the_load();
	case_identity_reset();
	case_conditional_predicates();
	case_registry_capacity();
	case_item_id_map();
	case_shipped_identity_receipt();
	case_condition_lost_during_warning();
	case_hazard_warning_commits_once();
	case_receipt_during_podium_announces();
	case_receipt_during_pause_announces();
	case_engine_natural_requires_a_done_reporter();
	case_complete_roster();
	case_wave2_batch1_families();
	case_wumpa_wipeout_lifecycle();
	case_wumpa_wipeout_duplicates_serialize();
	case_inventory_traps_wait_for_their_outcome();
	case_inventory_traps_rearm_on_lost_item();
	case_reverse_steering_lifecycle();
	case_wave2_batch1_map_change();
	case_held_item_slot_states();
	case_reroll_survives_a_destroyed_roulette();
	case_forced_use_distinguishes_use_from_confiscation();
	case_lead_timer_freezes_and_resets_correctly();
	case_warpball_earned_wait_is_latched();
	case_flatten_ready_gate();
	case_flatten_recovery_gate();
	case_flatten_lifecycle();
	case_flatten_duplicates_serialize();
	case_flatten_and_timed_effects_coexist();
	case_client_trap_duration_policy();
	case_empty_crates_reward_policy();
	case_empty_crates_lifecycle();
	case_boost_blocker_policy();
	case_weakened_boost_policy();
	case_hazard_projection_distance();
	case_remaining_roster_contracts();
	case_boost_blocker_lifecycle();
	case_wireframe_lifecycle();

	printf("%s: %d checks\n", failures ? "FAIL" : "PASS", checks);
	return failures != 0;
}
