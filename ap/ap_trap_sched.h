#ifndef AP_TRAP_SCHED_H
#define AP_TRAP_SCHED_H

// Trap SCHEDULER -- the registry state machine and every millisecond of trap
// timing, with no engine dependency at all. ap_traps.c owns the engine side (what
// counts as a live race frame, what a trap does to physics and the camera, the
// logging); this header owns WHEN a received trap becomes an active effect.
//
// Freestanding by design, the same way ap/ap_box_map.h is: it includes only
// ap_traps.h (forward declarations and the effect enum, no engine headers), so
// tools/test-trap-sched.c compiles the REAL timing rules on any host with no
// disc, no display and no seed.
//
// ── Lifecycle ──
//   EMPTY --receive--> PRIMED --(delay elapses on a fire-safe frame)--> FIRING --> EMPTY
//
// Two delay kinds, and which one a slot gets is decided ONCE, at receipt:
//
//   * received OUTSIDE a live race -> LAP delay. The slot waits for lap 2 or 3 of
//     some later race and then a randomized AP_TRAP_LAP_DELAY_{MIN,MAX}_MS wait.
//     This is the original framework behaviour and is unchanged.
//
//   * received DURING a live race -> MID-RACE delay. A single fixed
//     AP_TRAP_MIDRACE_DELAY_MS wait measured from the receipt, with no lap gate.
//
// The mid-race delay is the 2026-08-13 ruling, and it is deliberately NEITHER of
// the two behaviours this project has shipped before: not the lap-2/3 deferral
// (an itemsanity box handing you a trap that only goes off in a later race reads
// as a bug), and not the same-frame instant fire that replaced it in ed4fe2b8b
// (the trap landed inside the item-drain frame section, which is the regression).
//
// ── Two invariants the caller depends on ──
//  1. A receipt can NEVER fire on the frame it landed on. ap_onframe_body drains
//     received items (which calls the receive path) and ticks the scheduler in the
//     same call, drain first, so a single pathological elapsedMs -- a hitch, a
//     first frame after a load -- would otherwise let the whole mid-race delay
//     expire inside that very frame. armedFrame closes that off structurally
//     instead of relying on the delay being large enough.
//  2. A slot whose delay has run out on a frame that is NOT fire-safe HOLDS. It is
//     never forced through and never silently dropped: the countdown simply does
//     not drain, and the slot fires on the first fire-safe frame after that. The
//     caller gets one `held` event per slot so the hold is visible in the log.

#ifdef CTR_AP

#include "ap_traps.h" // enum AP_TrapEffect / AP_TRAP_COUNT (no engine headers)

// Registry capacity. Unchanged from the original framework.
#define AP_TRAP_SCHED_CAP 16

// Randomized fire-delay window (ms) for a trap that arrives OUTSIDE a race, once
// it reaches the lap 2/3 window.
#define AP_TRAP_LAP_DELAY_MIN_MS 500
#define AP_TRAP_LAP_DELAY_MAX_MS 8000

// Fixed fire delay (ms) for a trap that arrives DURING a live race, measured from
// the receipt frame.
//
// WHY 1000 AND NOT 500 (Stef offered either). 500 ms is the floor of the
// randomized LAP window above, i.e. the smallest wait this framework has ever
// shipped -- a minimum, not a target, and nothing in the code argues for it as the
// mid-race value. A full second buys three things 500 ms does not: it is
// unambiguously clear of the drain frame even across a bad hitch; the receipt
// already paints a feed toast on the drain frame (AP_FeedOnItemReceived), so the
// effect lands just after the player has read what hit them rather than on top of
// it; and it still reads as "that box did this", which is the whole point of not
// deferring to lap 2. One constant, one place to retune.
#define AP_TRAP_MIDRACE_DELAY_MS 1000

enum ApTrapSlotState
{
	AP_TRAP_SLOT_EMPTY = 0,
	AP_TRAP_SLOT_PRIMED, // armed, hidden, waiting for its delay
	AP_TRAP_SLOT_FIRING  // effect active, counting down remainingMs
};

struct ApTrapSlot
{
	int      effect;      // AP_TrapEffect
	int      state;       // enum ApTrapSlotState
	int      midRace;     // 1 = arrived during a live race -> fixed mid-race delay
	int      rolled;      // LAP path: 1 once fireDelayMs has been drawn this window
	int      fireDelayMs; // PRIMED: ms left before firing
	int      remainingMs; // FIRING: ms of effect left
	int      heldLogged;  // 1 once this slot has reported one unsafe-frame hold
	unsigned armedFrame;  // the frame the receipt landed on; it can never fire there
};

struct ApTrapSched
{
	struct ApTrapSlot slot[AP_TRAP_SCHED_CAP];
	int               active[AP_TRAP_COUNT];  // FIRING *and* this frame is fire-safe
	int               firing[AP_TRAP_COUNT];  // FIRING, regardless of frame safety
	const int        *durationMs;             // per-effect effect duration table
	unsigned          rng;                    // xorshift32, lazily seeded from a frame
};

// What the scheduler is allowed to know about this frame. Everything engine-shaped
// is resolved by the caller, which is what keeps this header freestanding.
struct ApTrapSchedFrame
{
	int      elapsedMs;  // ms since the previous frame
	int      onTrack;    // the race context still exists (on a live track at all)
	int      raceActive; // FIRE-SAFE: on-track, loaded, lights out, driver born,
	                     // not paused / menu / cutscene / end-of-race
	int      lapWindow;  // raceActive AND lapIndex in [1, numLaps): the lap 2/3 gate
	unsigned frame;      // monotonic frame counter
};

// Transitions this tick, for the caller to log. Slot indices.
struct ApTrapSchedEvents
{
	int firedCount;
	int fired[AP_TRAP_SCHED_CAP];
	int clearedCount;
	int cleared[AP_TRAP_SCHED_CAP];
	int heldCount;
	int held[AP_TRAP_SCHED_CAP]; // delay expired on an unsafe frame -> waiting
};

// ── tiny self-contained RNG (xorshift32) ──
static unsigned ap_trap_sched_rand(struct ApTrapSched *s, unsigned frame)
{
	if (s->rng == 0)
		s->rng = frame | 1u; // lazy seed, never 0
	s->rng ^= s->rng << 13;
	s->rng ^= s->rng >> 17;
	s->rng ^= s->rng << 5;
	return s->rng;
}

static int ap_trap_sched_rand_range(struct ApTrapSched *s, unsigned frame, int lo, int hi)
{
	if (hi <= lo)
		return lo;
	return lo + (int)(ap_trap_sched_rand(s, frame) % (unsigned)(hi - lo));
}

// Clear every slot and every flag. Keeps the duration table.
static void ap_trap_sched_reset(struct ApTrapSched *s)
{
	int i, e;

	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		s->slot[i].effect = 0;
		s->slot[i].state = AP_TRAP_SLOT_EMPTY;
		s->slot[i].midRace = 0;
		s->slot[i].rolled = 0;
		s->slot[i].fireDelayMs = 0;
		s->slot[i].remainingMs = 0;
		s->slot[i].heldLogged = 0;
		s->slot[i].armedFrame = 0;
	}

	// Clear the firing flags in the same breath: the physics call-sites read them
	// directly and run before the next tick recomputes them, so a stale 1 here
	// would keep an effect applied for a frame past the reset.
	for (e = 0; e < AP_TRAP_COUNT; e++)
	{
		s->active[e] = 0;
		s->firing[e] = 0;
	}
}

// Bind the per-effect duration table and clear. Call once before any other use.
static void ap_trap_sched_init(struct ApTrapSched *s, const int *durationMsByEffect)
{
	s->durationMs = durationMsByEffect;
	s->rng = 0;
	ap_trap_sched_reset(s);
}

// How many slots hold something. Used by the connect reset to report what it drops.
static int ap_trap_sched_occupied(const struct ApTrapSched *s)
{
	int i, n = 0;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
		if (s->slot[i].state != AP_TRAP_SLOT_EMPTY)
			n++;
	return n;
}

// Arm one trap. `midRace` is the caller's live-race answer AT RECEIPT TIME -- by
// the time a tick runs, "was a race running when this arrived" is no longer
// knowable. Returns the slot index, or -1 when the registry is full (the caller
// reports the drop; it is never silent).
static int ap_trap_sched_receive(struct ApTrapSched *s, int effect, int midRace, unsigned frame)
{
	int i;

	if (effect < 0 || effect >= AP_TRAP_COUNT)
		return -1;

	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		if (s->slot[i].state != AP_TRAP_SLOT_EMPTY)
			continue;

		s->slot[i].effect = effect;
		s->slot[i].state = AP_TRAP_SLOT_PRIMED;
		s->slot[i].midRace = midRace ? 1 : 0;
		s->slot[i].rolled = 0;
		s->slot[i].fireDelayMs = midRace ? AP_TRAP_MIDRACE_DELAY_MS : 0;
		s->slot[i].remainingMs = 0;
		s->slot[i].heldLogged = 0;
		s->slot[i].armedFrame = frame;
		return i;
	}
	return -1;
}

static void ap_trap_sched_fire(struct ApTrapSched *s, int i, struct ApTrapSchedEvents *ev)
{
	s->slot[i].state = AP_TRAP_SLOT_FIRING;
	s->slot[i].midRace = 0;
	s->slot[i].heldLogged = 0;
	s->slot[i].remainingMs = s->durationMs[s->slot[i].effect];
	if (ev != 0)
		ev->fired[ev->firedCount++] = i;
}

// Advance every slot one frame.
static void ap_trap_sched_tick(struct ApTrapSched *s, const struct ApTrapSchedFrame *f,
                               struct ApTrapSchedEvents *ev)
{
	int i, e;
	int elapsedMs = f->elapsedMs;

	if (ev != 0)
	{
		ev->firedCount = 0;
		ev->clearedCount = 0;
		ev->heldCount = 0;
	}

	if (elapsedMs <= 0)
		elapsedMs = 32; // defensive: a paused/odd frame shouldn't stall the timers

	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		struct ApTrapSlot *t = &s->slot[i];

		if (t->state == AP_TRAP_SLOT_PRIMED && t->midRace)
		{
			if (!f->onTrack)
			{
				// The race this trap was earned in is gone (finished, quit, level
				// change). The moment it belonged to no longer exists, so it drops
				// back to an ordinary primed trap rather than ambushing the player
				// the instant the NEXT race's lights go out.
				t->midRace = 0;
				t->fireDelayMs = 0;
				t->rolled = 0;
				t->heldLogged = 0;
			}
			else if (f->frame == t->armedFrame)
			{
				// Invariant 1: never on the receipt frame. See the header note.
			}
			else if (!f->raceActive)
			{
				// Invariant 2: hold, do not force and do not drop. Paused, in the
				// countdown, mid-load, in a cutscene or past the finish line -- all
				// frames where mutating driver/camera state is not safe.
				if (!t->heldLogged)
				{
					t->heldLogged = 1;
					if (ev != 0)
						ev->held[ev->heldCount++] = i;
				}
			}
			else
			{
				t->fireDelayMs -= elapsedMs;
				if (t->fireDelayMs <= 0)
					ap_trap_sched_fire(s, i, ev);
			}
			continue;
		}

		if (t->state == AP_TRAP_SLOT_PRIMED)
		{
			if (f->lapWindow)
			{
				if (!t->rolled)
				{
					t->fireDelayMs = ap_trap_sched_rand_range(s, f->frame,
					                                          AP_TRAP_LAP_DELAY_MIN_MS,
					                                          AP_TRAP_LAP_DELAY_MAX_MS);
					t->rolled = 1;
				}
				else
				{
					t->fireDelayMs -= elapsedMs;
					if (t->fireDelayMs <= 0)
						ap_trap_sched_fire(s, i, ev);
				}
			}
			else
			{
				// Window closed (race ended before firing) -> re-roll next race so a
				// primed trap always eventually fires on some lap 2/3.
				t->rolled = 0;
			}
			continue;
		}

		if (t->state == AP_TRAP_SLOT_FIRING)
		{
			// The duration burns down in every context, exactly as it always has, so
			// a trap can never bank time across a race boundary. Leaving the track
			// ends it outright for the same reason: an effect belongs to the race it
			// fired in (the same rule the reconnect reset enforces across sessions).
			t->remainingMs -= elapsedMs;
			if (t->remainingMs <= 0 || !f->onTrack)
			{
				t->state = AP_TRAP_SLOT_EMPTY;
				if (ev != 0)
					ev->cleared[ev->clearedCount++] = i;
			}
		}
	}

	// Recompute the flags the apply sites read. `firing` is the raw registry answer;
	// `active` additionally requires this frame to be fire-safe, which is what makes
	// "apply only in a safe frame section" a property of the data rather than of
	// every individual call site remembering to ask.
	for (e = 0; e < AP_TRAP_COUNT; e++)
	{
		s->active[e] = 0;
		s->firing[e] = 0;
	}
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		if (s->slot[i].state != AP_TRAP_SLOT_FIRING)
			continue;
		s->firing[s->slot[i].effect] = 1;
		if (f->raceActive)
			s->active[s->slot[i].effect] = 1;
	}
}

#endif // CTR_AP

#endif // AP_TRAP_SCHED_H
