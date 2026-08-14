// Out-of-engine assertions for the AP trap SCHEDULE -- when a received trap turns
// into an active effect. Compiles the REAL timing rules: ap/ap_trap_sched.h is
// freestanding by design and pulls in only ap/ap_traps.h (forward declarations and
// the effect enum), so this harness links nothing from the game and can run on any
// host, with no disc, no display and no seed.
//
//   cc -Wall -Wextra -DCTR_AP -o /tmp/test-trap-sched tools/test-trap-sched.c && /tmp/test-trap-sched
//
// Exit 0 = every assertion held; the failing case is printed otherwise.
//
// What it pins:
//   1. the RULED mid-race behaviour: a trap received during a live race fires after
//      AP_TRAP_MIDRACE_DELAY_MS, with no lap gate -- not on the receipt frame
//      (the ed4fe2b8b regression) and not deferred to lap 2/3 (the behaviour before
//      it). Both wrong answers are asserted against by name,
//   2. the receipt-frame invariant, structurally: even one pathological elapsedMs
//      larger than the whole delay cannot fire a trap on the frame it arrived on,
//      which is the frame the item drain runs in,
//   3. HOLD, NOT FORCE AND NOT DROP: a delay that runs out on an unsafe frame
//      (paused, countdown, mid-load, cutscene, past the finish) waits, reports
//      itself exactly once, and fires on the first safe frame after that,
//   4. boss races are in scope and behave identically -- the schedule is mode-blind
//      by design, so nothing here may special-case a boss race,
//   5. losing the race entirely demotes a waiting mid-race trap to an ordinary
//      primed one, so it can never ambush the next race at lights-out,
//   6. the out-of-race path is UNCHANGED: prime, wait for lap 2/3, then a delay
//      drawn inside [500, 8000),
//   7. the apply gate: `active` (what physics reads) is false on every unsafe
//      frame even while a trap is FIRING, and `firing` (what the camera reads) is
//      not, because the camera must survive a pause without flickering,
//   8. duration and cleanup, including that leaving the track ends a firing trap
//      instead of banking it into the next race,
//   9. a full registry reports the drop rather than losing it silently,
//  10. the connect reset drops every instance AND every flag in one breath.

#include <stdio.h>
#include <string.h>

#include "../ap/ap_trap_sched.h"

static int g_failures = 0;

#define CHECK(cond, ...)                                          \
	do                                                            \
	{                                                             \
		if (!(cond))                                              \
		{                                                         \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);           \
			printf(__VA_ARGS__);                                  \
			printf("\n");                                         \
			g_failures++;                                         \
		}                                                         \
	} while (0)

// A 20 s flat duration table, matching what ap_traps.c binds.
static const int kDuration[AP_TRAP_COUNT] = {20000, 20000, 20000, 20000, 20000};

#define FRAME_MS 33 // ~30 fps, the rate the engine actually ticks at

// One frame of "racing normally". `frame` advances on every step so the
// receipt-frame guard is exercised for real rather than by a constant.
static struct ApTrapSchedFrame RacingFrame(unsigned frame, int lapIndexAtLeastTwo)
{
	struct ApTrapSchedFrame f;
	f.elapsedMs = FRAME_MS;
	f.onTrack = 1;
	f.raceActive = 1;
	f.lapWindow = lapIndexAtLeastTwo;
	f.frame = frame;
	return f;
}

// On a track, but this frame may not be mutated: paused, in the countdown,
// mid-load, in a cutscene, or past the finish line. Same shape for all of them --
// that is the point of a single fire-safe answer.
static struct ApTrapSchedFrame UnsafeOnTrackFrame(unsigned frame)
{
	struct ApTrapSchedFrame f = RacingFrame(frame, 0);
	f.raceActive = 0;
	f.lapWindow = 0;
	return f;
}

// Off the track entirely: hub, menu, a different level.
static struct ApTrapSchedFrame OffTrackFrame(unsigned frame)
{
	struct ApTrapSchedFrame f = UnsafeOnTrackFrame(frame);
	f.onTrack = 0;
	return f;
}

// Run `frames` frames, returning the frame index on which the slot fired, or -1.
static int RunUntilFire(struct ApTrapSched *s, struct ApTrapSchedFrame (*mk)(unsigned, int),
                        int lapWindow, unsigned *frame, int frames, int slot)
{
	struct ApTrapSchedEvents ev;
	int                      i, k;

	for (i = 0; i < frames; i++)
	{
		struct ApTrapSchedFrame f = mk((*frame)++, lapWindow);
		ap_trap_sched_tick(s, &f, &ev);
		for (k = 0; k < ev.firedCount; k++)
			if (ev.fired[k] == slot)
				return i;
	}
	return -1;
}

// ── 1/2. the ruled mid-race delay, and the receipt-frame invariant ──
static void TestMidRaceDelay(void)
{
	struct ApTrapSched       s;
	struct ApTrapSchedEvents ev;
	struct ApTrapSchedFrame  f;
	unsigned                 frame = 100;
	int                      slot, firedOn, expected;

	CHECK(AP_TRAP_MIDRACE_DELAY_MS == 1000,
	      "the mid-race delay is the ruling; it is %d ms, expected 1000",
	      AP_TRAP_MIDRACE_DELAY_MS);
	CHECK(AP_TRAP_MIDRACE_DELAY_MS >= AP_TRAP_LAP_DELAY_MIN_MS,
	      "the mid-race delay (%d) is below the smallest wait this framework has "
	      "ever shipped (%d)",
	      AP_TRAP_MIDRACE_DELAY_MS, AP_TRAP_LAP_DELAY_MIN_MS);

	ap_trap_sched_init(&s, kDuration);

	// The receipt lands on frame 100, mid-race, on LAP 1 (lapWindow false
	// throughout -- if the schedule ever needs lap 2 again this test fails).
	slot = ap_trap_sched_receive(&s, AP_TRAP_LOWGRAV, 1, frame);
	CHECK(slot == 0, "first receipt should take slot 0, got %d", slot);
	CHECK(s.slot[0].state == AP_TRAP_SLOT_PRIMED, "a mid-race receipt is PRIMED, not FIRING");
	CHECK(s.firing[AP_TRAP_LOWGRAV] == 0, "nothing may be firing before the first tick");

	// The tick that runs in the SAME ap_onframe_body call as the drain. This is the
	// regression frame: ed4fe2b8b fired here.
	f = RacingFrame(frame, 0);
	ap_trap_sched_tick(&s, &f, &ev);
	CHECK(ev.firedCount == 0, "a mid-race trap must NOT fire on its receipt frame");
	CHECK(s.active[AP_TRAP_LOWGRAV] == 0, "no effect may be applied on the receipt frame");

	// Same again, with an elapsedMs larger than the whole delay -- a hitch, or the
	// first frame after a load. The guard must be structural, not "the delay is big
	// enough".
	f = RacingFrame(frame, 0);
	f.elapsedMs = AP_TRAP_MIDRACE_DELAY_MS * 5;
	ap_trap_sched_tick(&s, &f, &ev);
	CHECK(ev.firedCount == 0,
	      "a 5x-delay hitch on the receipt frame must still not fire the trap");

	// From here the delay drains at the real frame rate.
	frame++;
	expected = (AP_TRAP_MIDRACE_DELAY_MS + FRAME_MS - 1) / FRAME_MS; // ceil, 0-based below
	firedOn = RunUntilFire(&s, RacingFrame, 0, &frame, 200, 0);
	CHECK(firedOn >= 0, "the mid-race trap never fired");
	CHECK(firedOn == expected - 1,
	      "expected the fire on frame %d after the receipt frame, got %d",
	      expected - 1, firedOn);
	CHECK(s.slot[0].state == AP_TRAP_SLOT_FIRING, "the slot should be FIRING");
	CHECK(s.slot[0].remainingMs == kDuration[AP_TRAP_LOWGRAV],
	      "a fresh fire carries the full duration, got %d", s.slot[0].remainingMs);

	// The whole wait must be roughly one second, not one lap and not one frame.
	CHECK(firedOn * FRAME_MS >= 900 && firedOn * FRAME_MS <= 1100,
	      "the wait was %d ms, which is neither the ruled ~1000 ms nor close to it",
	      firedOn * FRAME_MS);
}

// ── 3. hold, not force and not drop ──
static void TestUnsafeFrameHolds(void)
{
	struct ApTrapSched       s;
	struct ApTrapSchedEvents ev;
	struct ApTrapSchedFrame  f;
	unsigned                 frame = 500;
	int                      i, fired = 0, held = 0;

	ap_trap_sched_init(&s, kDuration);
	ap_trap_sched_receive(&s, AP_TRAP_FIRSTPERSON, 1, frame);
	frame++;

	// Two full delays' worth of UNSAFE on-track frames: paused, in a cutscene, past
	// the finish line. The trap may neither fire nor disappear.
	for (i = 0; i < 100; i++)
	{
		f = UnsafeOnTrackFrame(frame++);
		ap_trap_sched_tick(&s, &f, &ev);
		fired += ev.firedCount;
		held += ev.heldCount;
	}
	CHECK(fired == 0, "a trap fired on an unsafe frame (%d times)", fired);
	CHECK(s.slot[0].state == AP_TRAP_SLOT_PRIMED, "the held trap was dropped instead of waiting");
	CHECK(held == 1, "the hold must be reported exactly once, got %d reports", held);
	CHECK(s.slot[0].fireDelayMs == AP_TRAP_MIDRACE_DELAY_MS,
	      "an unsafe frame must not drain the delay; %d ms left of %d",
	      s.slot[0].fireDelayMs, AP_TRAP_MIDRACE_DELAY_MS);

	// The first safe frames after that spend the delay and fire it.
	CHECK(RunUntilFire(&s, RacingFrame, 0, &frame, 200, 0) >= 0,
	      "a held trap never fired once the frames became safe again");
}

// ── 4. boss races are in scope, and the schedule is mode-blind ──
static void TestBossRaceIdentical(void)
{
	struct ApTrapSched s1, s2;
	unsigned           f1 = 700, f2 = 700;
	int                a, b;

	// There is no boss flag in ApTrapSchedFrame, and that IS the assertion: a boss
	// race reaches the scheduler as an ordinary live race, so the two runs below
	// cannot diverge. If anyone adds a boss special case, the frame struct has to
	// grow first and this comment stops being true.
	ap_trap_sched_init(&s1, kDuration);
	ap_trap_sched_init(&s2, kDuration);
	ap_trap_sched_receive(&s1, AP_TRAP_FIRSTPERSON, 1, f1);
	ap_trap_sched_receive(&s2, AP_TRAP_FIRSTPERSON, 1, f2);
	f1++;
	f2++;

	a = RunUntilFire(&s1, RacingFrame, 0, &f1, 200, 0);
	b = RunUntilFire(&s2, RacingFrame, 0, &f2, 200, 0);
	CHECK(a >= 0 && a == b, "a boss race must fire on the same frame as any other race (%d vs %d)", a, b);
	CHECK(memcmp(&s1.slot[0], &s2.slot[0], sizeof s1.slot[0]) == 0,
	      "a boss race left the slot in a different state than an ordinary race");
	CHECK(s1.firing[AP_TRAP_FIRSTPERSON] == 1 && s2.firing[AP_TRAP_FIRSTPERSON] == 1,
	      "the first-person trap must fire in a boss race, not be excluded from it");
}

// ── 5. losing the race demotes rather than banks ──
static void TestLosingTheRaceDemotes(void)
{
	struct ApTrapSched       s;
	struct ApTrapSchedEvents ev;
	struct ApTrapSchedFrame  f;
	unsigned                 frame = 900;
	int                      i, firedOn;

	ap_trap_sched_init(&s, kDuration);
	ap_trap_sched_receive(&s, AP_TRAP_ICY, 1, frame);
	frame++;

	f = OffTrackFrame(frame++);
	ap_trap_sched_tick(&s, &f, &ev);
	CHECK(ev.firedCount == 0, "leaving the track must not fire the waiting trap");
	CHECK(s.slot[0].midRace == 0, "the waiting trap should have demoted to an ordinary primed trap");
	CHECK(s.slot[0].state == AP_TRAP_SLOT_PRIMED, "it must still be primed, not dropped");

	// Next race: lights out on lap 1. A demoted trap must NOT go off here.
	for (i = 0; i < 60; i++)
	{
		f = RacingFrame(frame++, 0);
		ap_trap_sched_tick(&s, &f, &ev);
		CHECK(ev.firedCount == 0, "a demoted trap ambushed lap 1 of the next race");
	}

	// It fires on lap 2/3, after a rolled delay.
	firedOn = RunUntilFire(&s, RacingFrame, 1, &frame, 1000, 0);
	CHECK(firedOn > 0, "a demoted trap never fired in the lap 2/3 window");
}

// ── 6. the out-of-race path is unchanged ──
static void TestOutOfRacePrimes(void)
{
	struct ApTrapSched       s;
	struct ApTrapSchedEvents ev;
	struct ApTrapSchedFrame  f;
	unsigned                 frame = 1200;
	int                      i, firedOn, waitMs;

	ap_trap_sched_init(&s, kDuration);
	ap_trap_sched_receive(&s, AP_TRAP_BOOST, 0, frame); // received in the hub
	frame++;

	// Lap 1 of a race: silent.
	for (i = 0; i < 300; i++)
	{
		f = RacingFrame(frame++, 0);
		ap_trap_sched_tick(&s, &f, &ev);
		CHECK(ev.firedCount == 0, "a hub-received trap fired before the lap 2/3 window");
	}

	firedOn = RunUntilFire(&s, RacingFrame, 1, &frame, 2000, 0);
	CHECK(firedOn > 0, "a hub-received trap never fired in the lap window");

	// The first lap-window frame only rolls the delay; the wait starts after it.
	waitMs = firedOn * FRAME_MS;
	CHECK(waitMs >= AP_TRAP_LAP_DELAY_MIN_MS - FRAME_MS && waitMs <= AP_TRAP_LAP_DELAY_MAX_MS + FRAME_MS,
	      "the rolled delay landed at %d ms, outside [%d, %d]",
	      waitMs, AP_TRAP_LAP_DELAY_MIN_MS, AP_TRAP_LAP_DELAY_MAX_MS);
}

// ── 7. the apply gate: physics stops on unsafe frames, the camera does not ──
static void TestApplyGate(void)
{
	struct ApTrapSched       s;
	struct ApTrapSchedEvents ev;
	struct ApTrapSchedFrame  f;
	unsigned                 frame = 1600;

	ap_trap_sched_init(&s, kDuration);
	ap_trap_sched_receive(&s, AP_TRAP_LOWGRAV, 1, frame);
	frame++;
	CHECK(RunUntilFire(&s, RacingFrame, 0, &frame, 200, 0) >= 0, "setup: trap never fired");
	CHECK(s.active[AP_TRAP_LOWGRAV] == 1, "a firing trap on a safe frame must apply");
	CHECK(s.firing[AP_TRAP_LOWGRAV] == 1, "a firing trap must read as firing");

	// Pause / cutscene / past the finish line: physics must stop mutating.
	f = UnsafeOnTrackFrame(frame++);
	ap_trap_sched_tick(&s, &f, &ev);
	CHECK(s.active[AP_TRAP_LOWGRAV] == 0,
	      "physics must not be mutated on an unsafe frame, even mid-effect");
	CHECK(s.firing[AP_TRAP_LOWGRAV] == 1,
	      "the raw firing flag must survive an unsafe frame, or the camera flickers");

	// Back to racing: it resumes.
	f = RacingFrame(frame++, 0);
	ap_trap_sched_tick(&s, &f, &ev);
	CHECK(s.active[AP_TRAP_LOWGRAV] == 1, "the effect must resume on the next safe frame");
}

// ── 8. duration, cleanup, and no banking across a race boundary ──
static void TestDurationAndCleanup(void)
{
	struct ApTrapSched       s;
	struct ApTrapSchedEvents ev;
	struct ApTrapSchedFrame  f;
	unsigned                 frame = 2000;
	int                      i, cleared = 0;

	ap_trap_sched_init(&s, kDuration);
	ap_trap_sched_receive(&s, AP_TRAP_USF_NOBRAKE, 1, frame);
	frame++;
	CHECK(RunUntilFire(&s, RacingFrame, 0, &frame, 200, 0) >= 0, "setup: trap never fired");

	for (i = 0; i < 2000 && cleared == 0; i++)
	{
		f = RacingFrame(frame++, 0);
		ap_trap_sched_tick(&s, &f, &ev);
		cleared += ev.clearedCount;
	}
	CHECK(cleared == 1, "the trap never cleared");
	CHECK(i * FRAME_MS >= kDuration[AP_TRAP_USF_NOBRAKE] &&
	          i * FRAME_MS <= kDuration[AP_TRAP_USF_NOBRAKE] + 2 * FRAME_MS,
	      "the effect ran %d ms, expected about %d", i * FRAME_MS,
	      kDuration[AP_TRAP_USF_NOBRAKE]);
	CHECK(s.active[AP_TRAP_USF_NOBRAKE] == 0 && s.firing[AP_TRAP_USF_NOBRAKE] == 0,
	      "a cleared trap must leave no flag standing");

	// A firing trap does not survive leaving the track.
	ap_trap_sched_init(&s, kDuration);
	ap_trap_sched_receive(&s, AP_TRAP_ICY, 1, frame);
	frame++;
	CHECK(RunUntilFire(&s, RacingFrame, 0, &frame, 200, 0) >= 0, "setup: trap never fired");
	f = OffTrackFrame(frame++);
	ap_trap_sched_tick(&s, &f, &ev);
	CHECK(ev.clearedCount == 1, "leaving the track must end a firing trap");
	CHECK(s.firing[AP_TRAP_ICY] == 0, "no effect may be banked into the next race");
}

// ── 9/10. capacity, and the connect reset ──
static void TestCapacityAndReset(void)
{
	struct ApTrapSched s;
	int                i, slot;

	ap_trap_sched_init(&s, kDuration);
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		slot = ap_trap_sched_receive(&s, AP_TRAP_ICY, 0, (unsigned)i);
		CHECK(slot == i, "receipt %d should take slot %d, got %d", i, i, slot);
	}
	CHECK(ap_trap_sched_occupied(&s) == AP_TRAP_SCHED_CAP, "the registry should be full");
	CHECK(ap_trap_sched_receive(&s, AP_TRAP_ICY, 0, 0) == -1,
	      "an over-capacity receipt must report the drop, not overwrite a slot");
	CHECK(ap_trap_sched_receive(&s, -1, 0, 0) == -1, "an out-of-range effect must be refused");
	CHECK(ap_trap_sched_receive(&s, AP_TRAP_COUNT, 0, 0) == -1,
	      "an out-of-range effect must be refused");

	// A FIRING instance plus a PRIMED one, then a fresh connect.
	{
		struct ApTrapSchedEvents ev;
		struct ApTrapSchedFrame  f;
		unsigned                 frame = 3000;

		ap_trap_sched_init(&s, kDuration);
		ap_trap_sched_receive(&s, AP_TRAP_BOOST, 1, frame);
		ap_trap_sched_receive(&s, AP_TRAP_ICY, 0, frame);
		frame++;
		CHECK(RunUntilFire(&s, RacingFrame, 0, &frame, 200, 0) >= 0, "setup: trap never fired");
		CHECK(s.firing[AP_TRAP_BOOST] == 1, "setup: the boost trap should be firing");
		CHECK(ap_trap_sched_occupied(&s) == 2, "setup: two instances expected");

		ap_trap_sched_reset(&s);
		CHECK(ap_trap_sched_occupied(&s) == 0, "the connect reset must drop every instance");
		for (i = 0; i < AP_TRAP_COUNT; i++)
			CHECK(s.active[i] == 0 && s.firing[i] == 0,
			      "the connect reset must drop every flag too (effect %d)", i);

		// The reset must not lose the duration table -- the next fire needs it.
		ap_trap_sched_receive(&s, AP_TRAP_BOOST, 1, frame);
		frame++;
		CHECK(RunUntilFire(&s, RacingFrame, 0, &frame, 200, 0) >= 0,
		      "a trap received after a connect reset never fired");
		CHECK(s.slot[0].remainingMs == kDuration[AP_TRAP_BOOST],
		      "the reset lost the duration table (remaining %d)", s.slot[0].remainingMs);
		(void)ev;
		(void)f;
	}
}

// ── determinism: the same seed rolls the same lap delays ──
static void TestDeterministicRolls(void)
{
	struct ApTrapSched s1, s2;
	unsigned           f1 = 4242, f2 = 4242;

	ap_trap_sched_init(&s1, kDuration);
	ap_trap_sched_init(&s2, kDuration);
	ap_trap_sched_receive(&s1, AP_TRAP_ICY, 0, f1);
	ap_trap_sched_receive(&s2, AP_TRAP_ICY, 0, f2);
	f1++;
	f2++;
	CHECK(RunUntilFire(&s1, RacingFrame, 1, &f1, 2000, 0) ==
	          RunUntilFire(&s2, RacingFrame, 1, &f2, 2000, 0),
	      "two schedulers seeded from the same frame rolled different delays");
}

int main(void)
{
	TestMidRaceDelay();
	TestUnsafeFrameHolds();
	TestBossRaceIdentical();
	TestLosingTheRaceDemotes();
	TestOutOfRacePrimes();
	TestApplyGate();
	TestDurationAndCleanup();
	TestCapacityAndReset();
	TestDeterministicRolls();

	if (g_failures != 0)
	{
		printf("\n%d assertion(s) failed\n", g_failures);
		return 1;
	}
	printf("test-trap-sched: all assertions held\n");
	return 0;
}
