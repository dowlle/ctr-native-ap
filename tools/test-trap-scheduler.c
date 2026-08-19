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
// The last two cases pin the apworld item identity table instead: the 19 trap ids
// are scattered across three runs, so the mapping is data and data can be wrong.

#include <stdio.h>
#include <string.h>

#include "../ap/ap_trap_items.h"
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

// A wave 2 scaffold is armed and reported once, never fired, never consumed.
static void case_inactive_scaffold_retained(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);

	AP_TrapSchedReset(&s);
	expect("scaffold is disabled by default",
	       AP_TrapSchedIsEnabled(&s, AP_TRAP_WIREFRAME), 0);
	AP_TrapSchedReceive(&s, AP_TRAP_WIREFRAME);
	run(&s, &w, 200);
	drain(&s);
	expect("scaffold logs exactly one inactive line",
	       count_ev(AP_TRAP_EV_INACTIVE, AP_TRAP_WIREFRAME), 1);
	expect("scaffold does not use the armed presentation",
	       count_ev(AP_TRAP_EV_ARMED, AP_TRAP_WIREFRAME), 0);
	expect("scaffold never fires", count_ev(AP_TRAP_EV_FIRE, AP_TRAP_WIREFRAME), 0);
	expect("scaffold stays armed", AP_TrapSchedArmedCount(&s, AP_TRAP_WIREFRAME), 1);

	// Wave 2 flips one effect on without touching the descriptor table.
	AP_TrapSchedEnable(&s, AP_TRAP_WIREFRAME, 1);
	run_ms(&s, &w, 1100);
	expect("enabling the effect activates the retained copy",
	       AP_TrapSchedActive(&s, AP_TRAP_WIREFRAME), 1);
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
	AP_TrapSchedEnable(&s, AP_TRAP_DEMO_CAMERA, 1);

	AP_TrapSchedReceive(&s, AP_TRAP_DEMO_CAMERA);
	AP_TrapSchedReceive(&s, AP_TRAP_FIRSTPERSON);
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

	// And the reset restores the shipped roster, so a scheduler that had wave 2
	// effects switched on does not carry them into the next session.
	expect("reset restores the default roster",
	       AP_TrapSchedIsEnabled(&s, AP_TRAP_BOOST_BLOCKER), 0);

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
	{35010106, AP_TRAP_WUMPA_WIPEOUT, 0},
	{35010107, AP_TRAP_FLATTEN, 0},
	{35010108, AP_TRAP_ITEM_REROLL, 0},
	{35010109, AP_TRAP_FORCED_USE, 0},
	{35010110, AP_TRAP_EMPTY_CRATES, 0},
	{35010111, AP_TRAP_WEAKENED_KART, 0},
	{35010112, AP_TRAP_BOOST_BLOCKER, 0},
	{35010113, AP_TRAP_WIREFRAME, 0},
	{35010114, AP_TRAP_NITRO, 0},
	{35010115, AP_TRAP_REVERSE_STEERING, 0},
	{35010116, AP_TRAP_RED_POTION, 0},
	{35010190, AP_TRAP_UPSIDE_DOWN, 0},
	{35010191, AP_TRAP_MIRROR_MODE, 0},
	{35010192, AP_TRAP_WARPBALL_AMBUSH, 0},
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
	35010193,  // one past the end of the table
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

	expect("the table holds all 19 trap identities", AP_TRAP_ITEM_MAP_COUNT, 19);

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

	// Exactly the five wave 1 effects have a native effect today.
	{
		int buildable = 0;
		for (i = 0; i < TRAP_ID_COUNT; i++)
			buildable += AP_TrapItemIsBuildable(TRAP_IDS[i].effect);
		expect("exactly five identities are buildable", buildable, 5);
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
	// 19 must be refused, so a family dropped into one of the gaps between the runs
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
		expect("no index in 0..192 is misclassified", wrong, 0);
		expect("exactly 19 indices are traps", traps, 19);
	}
}

// A received identity this build has no effect for is armed, reported once and
// kept. A buildable one runs its schedule as usual. Both drive the real receive
// path: id, then map lookup, then AP_TrapSchedReceive.
static void case_reserved_identity_receipt(void)
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
	expect("all 19 fit in the registry", slots_used(&s), 19);

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
	expect("every reserved identity is retained armed", armedKept, 14);

	// The five buildable ones behaved normally in the same batch. Only one member
	// of the boost_control family can be active at once, which is the ruled
	// behaviour, so this counts what actually ran rather than assuming five.
	for (i = 0; i < TRAP_ID_COUNT; i++)
		if (TRAP_IDS[i].buildable)
			fired += count_ev(AP_TRAP_EV_FIRE, TRAP_IDS[i].effect);
	expect("buildable identities still run in the same batch", fired, 4);
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

int main(void)
{
	case_warning_then_fire();
	case_armed_on_ineligible_receipt();
	case_inactive_scaffold_retained();
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
	case_reserved_identity_receipt();
	case_condition_lost_during_warning();
	case_receipt_during_podium_announces();
	case_receipt_during_pause_announces();
	case_engine_natural_requires_a_done_reporter();

	printf("%s: %d checks\n", failures ? "FAIL" : "PASS", checks);
	return failures != 0;
}
