#ifndef AP_TRAP_SCHED_LOGIC_H
#define AP_TRAP_SCHED_LOGIC_H

// Pure, engine-independent trap scheduler for CTR Native (#280).
//
// Kept in a header with no engine, no network and no config dependency so the
// runtime module (ap/ap_traps.c) and the host harness (tools/test-trap-scheduler.c)
// drive the exact same rules, the way ap_turbogrant_logic.h does for #224.
//
// WHAT REPLACED WHAT. The shipped model was one shared lifecycle for every trap:
// an out-of-race receipt primed silently, waited for a racing lap, rolled a random
// 0.5 to 8 second delay, then ran for a flat 20 seconds. The trap rework notebook
// (Trap rework design notebook, 2026-08-19) rules that timing belongs to the
// effect, so the shared lifecycle is gone. What is left shared is the CONTEXT and
// BOUNDARY behaviour, which is genuinely identical for every trap; everything a
// single effect decides for itself now lives in its descriptor below.
//
// THE MODEL. Every received copy occupies one registry slot and walks
// ARMED, WARNING, ACTIVE, EMPTY. The scheduler never applies an effect; it decides
// which effects are ACTIVE this frame and emits events. The runtime reads
// AP_TrapSchedActive() at the physics, input and camera call sites and drains the
// event ring for its log lines and on-screen presentation.
//
// SEPARATION OF DUTIES. The scheduler owns arming, eligibility, warnings,
// durations, duplicates, conflict families and map boundaries. It owns nothing
// that requires a Driver or a GameTracker: the runtime observes the world once per
// frame into AP_TrapWorld and hands that in.

// ── Effect ids ──
//
// These are NATIVE effect ids, internal to this client. The apworld item id they
// arrive from is a separate space that the apworld owns, and the mapping between
// the two is the explicit table in ap_trap_items.h. Do not reintroduce arithmetic
// between them: the 20 trap identities are not one contiguous item window.
//
// AP_TRAP_USF_NOBRAKE keeps its id and its symbol. The apworld renamed the item
// from "No Brakes" to "Forced USF", which is a display change only; item ids never
// moved, and native matches by id.
//
// AP_TRAP_COUNT is the number of IMPLEMENTED effects, which is also the boundary
// between the wave 1 ids and the wave 2 scaffolds. It is no longer the size of an
// item block, because there is no longer one block.
enum AP_TrapEffectId
{
	AP_TRAP_ICY = 0,     // Icy Road
	AP_TRAP_LOWGRAV,     // Low Gravity
	AP_TRAP_USF_NOBRAKE, // Forced USF, formerly displayed as "No Brakes"
	AP_TRAP_BOOST,       // Forced Boost
	AP_TRAP_FIRSTPERSON, // First Person
	AP_TRAP_COUNT,       // implemented-effect count, and the wave 1 / wave 2 line

	AP_TRAP_WUMPA_WIPEOUT = AP_TRAP_COUNT,
	AP_TRAP_FLATTEN,
	AP_TRAP_ITEM_REROLL,
	AP_TRAP_FORCED_USE,
	AP_TRAP_EMPTY_CRATES,
	AP_TRAP_WEAKENED_KART,
	AP_TRAP_BOOST_BLOCKER,
	AP_TRAP_WIREFRAME,
	AP_TRAP_NITRO,
	AP_TRAP_REVERSE_STEERING,
	AP_TRAP_RED_POTION,
	AP_TRAP_UPSIDE_DOWN,
	AP_TRAP_MIRROR_MODE,
	AP_TRAP_WARPBALL_AMBUSH,
	AP_TRAP_DEMO_CAMERA,
	AP_TRAP_EFFECT_COUNT
};

// AP_TrapSched.enabled is a bitmask over effect ids, so the roster has to fit in
// an unsigned int. Wave 2 growing past 32 effects needs a wider mask.
typedef char ap_trap_roster_fits_in_mask[AP_TRAP_EFFECT_COUNT <= 32 ? 1 : -1];

// ── Timing class ──
// Which shape of schedule the effect follows. The class is descriptive: the
// scheduler branches on the duration and duplicate policies, not on this, so an
// effect cannot be given a class that contradicts how it actually behaves.
enum AP_TrapTimingClass
{
	AP_TRAP_TIMING_INSTANT = 0,       // one activation, engine owns the outcome
	AP_TRAP_TIMING_SHORT_WARNING,     // arm visibly, fire after a brief warning
	AP_TRAP_TIMING_SUSTAINED_MAP,     // runs for the rest of the current map
	AP_TRAP_TIMING_SUSTAINED_TIMED,   // runs for a fixed number of seconds
	AP_TRAP_TIMING_CONDITIONAL_AMBUSH // stays armed until a gameplay condition fires
};

// ── Duration policy ──
enum AP_TrapDurationPolicy
{
	AP_TRAP_DURATION_MAP_LIFETIME = 0, // cleared only at a map boundary
	AP_TRAP_DURATION_FIXED_MS,         // cleared when durationMs elapses
	AP_TRAP_DURATION_ENGINE_NATURAL    // cleared when the runtime reports it done
};

// ── Duplicate policy ──
enum AP_TrapDuplicatePolicy
{
	AP_TRAP_DUP_QUEUE_LATER_MAP = 0, // a second copy waits for a later map
	AP_TRAP_DUP_REFRESH,             // a second copy refills the running timer
	AP_TRAP_DUP_SERIALIZE            // a second copy runs its own full window after
};

// ── Conflict family ──
// Members of one family never overlap. A member that is ready while another
// member holds the family waits, and the wait is resolved in receipt order, so
// each copy still gets its own warning and its own full window. Ruled for
// boost_control in the Alpha 2 triage note (Boost Blocker over Forced Boost would
// otherwise produce a no-boost and no-brakes combination nobody designed), and
// applied identically to camera_transform, where two forced cameras cannot both
// own cameraDC[0].
enum AP_TrapConflictFamily
{
	AP_TRAP_FAMILY_NONE = 0,
	AP_TRAP_FAMILY_CAMERA_TRANSFORM,
	AP_TRAP_FAMILY_BOOST_CONTROL
};

// ── Activation contexts ──
// The settled activation-context matrix, one bit per column. A map that is none of
// these (menus, loading, credits) has context 0, which no trap matches, so every
// armed copy simply stays armed.
enum AP_TrapContext
{
	AP_TRAP_CTX_HUB = 1 << 0,           // Adventure hub driving
	AP_TRAP_CTX_RACE = 1 << 1,          // standard and boss races
	AP_TRAP_CTX_CUP_LEG = 1 << 2,       // one leg of a Cup or Gem Cup
	AP_TRAP_CTX_TIME_TRIAL = 1 << 3,    // Relic Race and Time Trial
	AP_TRAP_CTX_CTR_CHALLENGE = 1 << 4, // CTR Challenge
	AP_TRAP_CTX_BATTLE = 1 << 5,        // battle arenas
	AP_TRAP_CTX_CRYSTAL = 1 << 6        // Crystal Challenge
};

#define AP_TRAP_CTX_ALL                                                              \
	(AP_TRAP_CTX_HUB | AP_TRAP_CTX_RACE | AP_TRAP_CTX_CUP_LEG | AP_TRAP_CTX_TIME_TRIAL | \
	 AP_TRAP_CTX_CTR_CHALLENGE | AP_TRAP_CTX_BATTLE | AP_TRAP_CTX_CRYSTAL)
#define AP_TRAP_CTX_ALL_NO_HUB (AP_TRAP_CTX_ALL & ~AP_TRAP_CTX_HUB)
// Modes with a working weapon inventory. Time Trial and Relic Race have none, so
// the two inventory traps stay armed there.
#define AP_TRAP_CTX_WEAPONS                                                      \
	(AP_TRAP_CTX_RACE | AP_TRAP_CTX_CUP_LEG | AP_TRAP_CTX_CTR_CHALLENGE |           \
	 AP_TRAP_CTX_BATTLE | AP_TRAP_CTX_CRYSTAL)
// Modes with racing AI opponents.
#define AP_TRAP_CTX_AI_RACE \
	(AP_TRAP_CTX_RACE | AP_TRAP_CTX_CUP_LEG | AP_TRAP_CTX_CTR_CHALLENGE)

// ── Conditional predicates ──
// A descriptor carries at most one. The runtime sets the matching bit in
// AP_TrapWorld.conditions on any frame the prerequisite genuinely holds; the
// scheduler treats an unsatisfied predicate exactly like an ineligible context.
enum AP_TrapCondition
{
	AP_TRAP_COND_NONE = 0,
	AP_TRAP_COND_EARNED_BOOST = 1 << 0,    // player just earned a boost from gameplay
	AP_TRAP_COND_TEN_WUMPA = 1 << 1,       // player is holding 10 Wumpa (juiced)
	AP_TRAP_COND_HELD_ITEM = 1 << 2,       // a fully resolved weapon is in the slot
	AP_TRAP_COND_ELIGIBLE_CRATES = 1 << 3, // this map has weapon or Wumpa crates
	AP_TRAP_COND_AI_LEAD = 1 << 4,         // 15 continuous seconds in first, valid AI
	AP_TRAP_COND_SAFE_HAZARD = 1 << 5,     // projected line reaches drivable ground
	AP_TRAP_COND_DEMO_CAMERA = 1 << 6      // retail cinematic camera has a safe owner
};

// One second, the ruled warning for every effect that takes one.
#define AP_TRAP_WARNING_MS 1000

// ── Descriptor ──
// The whole of an effect's schedule. Adding a wave 2 effect is a row here plus its
// application code in ap_traps.c; nothing in the scheduler itself changes.
typedef struct
{
	const char *name;      // player-facing name, post-rename
	unsigned short contexts; // AP_TrapContext bits
	unsigned char timing;  // AP_TrapTimingClass
	unsigned char duration; // AP_TrapDurationPolicy
	unsigned char duplicate; // AP_TrapDuplicatePolicy
	unsigned char family;  // AP_TrapConflictFamily
	unsigned char condition; // one AP_TrapCondition bit, or AP_TRAP_COND_NONE
	unsigned char active;  // 0 = scaffolded for wave 2, arms but never fires
	int durationMs;        // AP_TRAP_DURATION_FIXED_MS only
	int warningMs;         // 0 = fire on the tick the effect becomes eligible
} AP_TrapDescriptor;

// PROVISIONAL, marked where the notebook leaves the value open. See the report in
// issue #280: Forced USF's duration and its duplicate policy are the two the
// notebook explicitly lists as still open for a shipped effect.
#define AP_TRAP_USF_DURATION_MS 15000 // PROVISIONAL, matches Forced Boost
#define AP_TRAP_BOOST_DURATION_MS 15000

static const AP_TrapDescriptor AP_TRAP_DESC[AP_TRAP_EFFECT_COUNT] = {
	// ── Wave 1: implemented ──
	{"Icy Road", AP_TRAP_CTX_ALL, AP_TRAP_TIMING_SUSTAINED_MAP,
	 AP_TRAP_DURATION_MAP_LIFETIME, AP_TRAP_DUP_QUEUE_LATER_MAP, AP_TRAP_FAMILY_NONE,
	 AP_TRAP_COND_NONE, 1, 0, AP_TRAP_WARNING_MS},
	{"Low Gravity", AP_TRAP_CTX_ALL, AP_TRAP_TIMING_SUSTAINED_MAP,
	 AP_TRAP_DURATION_MAP_LIFETIME, AP_TRAP_DUP_QUEUE_LATER_MAP, AP_TRAP_FAMILY_NONE,
	 AP_TRAP_COND_NONE, 1, 0, AP_TRAP_WARNING_MS},
	// Forced USF has no separate warning window: the ruling is that it stays
	// visibly armed and then escalates the boost the player just earned, so the
	// armed announcement is the warning and the boost event is the fire.
	{"Forced USF", AP_TRAP_CTX_ALL_NO_HUB, AP_TRAP_TIMING_CONDITIONAL_AMBUSH,
	 AP_TRAP_DURATION_FIXED_MS, AP_TRAP_DUP_SERIALIZE, AP_TRAP_FAMILY_BOOST_CONTROL,
	 AP_TRAP_COND_EARNED_BOOST, 1, AP_TRAP_USF_DURATION_MS, 0},
	{"Forced Boost", AP_TRAP_CTX_ALL_NO_HUB, AP_TRAP_TIMING_SUSTAINED_TIMED,
	 AP_TRAP_DURATION_FIXED_MS, AP_TRAP_DUP_REFRESH, AP_TRAP_FAMILY_BOOST_CONTROL,
	 AP_TRAP_COND_NONE, 1, AP_TRAP_BOOST_DURATION_MS, AP_TRAP_WARNING_MS},
	{"First Person", AP_TRAP_CTX_ALL, AP_TRAP_TIMING_SUSTAINED_MAP,
	 AP_TRAP_DURATION_MAP_LIFETIME, AP_TRAP_DUP_QUEUE_LATER_MAP,
	 AP_TRAP_FAMILY_CAMERA_TRANSFORM, AP_TRAP_COND_NONE, 1, 0, AP_TRAP_WARNING_MS},

	// ── Wave 2 ──
	// active = 0 means the scheduler arms a received copy, announces it once and
	// then leaves it alone. Nothing is consumed and nothing crashes, so a seed
	// carrying these items is safe on a client that cannot perform them yet.
	// Wave 2 batch 1 turned four of these on; the rest are still scaffolds.
	{"Wumpa Wipeout", AP_TRAP_CTX_ALL_NO_HUB, AP_TRAP_TIMING_INSTANT,
	 AP_TRAP_DURATION_ENGINE_NATURAL, AP_TRAP_DUP_SERIALIZE, AP_TRAP_FAMILY_NONE,
	 AP_TRAP_COND_TEN_WUMPA, 1, 0, AP_TRAP_WARNING_MS},
	// Batch 2 opener. The genuine flattened state is damage type 3, which the
	// decomp calls SQUISH. VehPickState.c handles it with the squish sound, a
	// 0.25 s input lock and a ~3.8 s squishTimer, then falls through to the
	// spinout exit. It is what the Blizzard Bluff and N. Gin Labs hazards
	// inflict, and what a turbo-landing driver inflicts (VehPhysCrash.c:192),
	// so Flatten keeps its frozen name and its ruling: drive the engine's own
	// dispatch and take its natural recovery rather than timing anything here.
	{"Flatten", AP_TRAP_CTX_ALL, AP_TRAP_TIMING_INSTANT,
	 AP_TRAP_DURATION_ENGINE_NATURAL, AP_TRAP_DUP_SERIALIZE, AP_TRAP_FAMILY_NONE,
	 AP_TRAP_COND_NONE, 1, 0, AP_TRAP_WARNING_MS},
	{"Item Reroll", AP_TRAP_CTX_WEAPONS, AP_TRAP_TIMING_INSTANT,
	 AP_TRAP_DURATION_ENGINE_NATURAL, AP_TRAP_DUP_SERIALIZE, AP_TRAP_FAMILY_NONE,
	 AP_TRAP_COND_HELD_ITEM, 1, 0, AP_TRAP_WARNING_MS},
	{"Forced Use", AP_TRAP_CTX_WEAPONS, AP_TRAP_TIMING_INSTANT,
	 AP_TRAP_DURATION_ENGINE_NATURAL, AP_TRAP_DUP_SERIALIZE, AP_TRAP_FAMILY_NONE,
	 AP_TRAP_COND_HELD_ITEM, 1, 0, AP_TRAP_WARNING_MS},
	{"Empty Crates", AP_TRAP_CTX_ALL_NO_HUB, AP_TRAP_TIMING_SUSTAINED_MAP,
	 AP_TRAP_DURATION_MAP_LIFETIME, AP_TRAP_DUP_QUEUE_LATER_MAP, AP_TRAP_FAMILY_NONE,
	 AP_TRAP_COND_ELIGIBLE_CRATES, 1, 0, AP_TRAP_WARNING_MS},
	{"Weakened Kart", AP_TRAP_CTX_ALL, AP_TRAP_TIMING_SUSTAINED_TIMED,
	 AP_TRAP_DURATION_FIXED_MS, AP_TRAP_DUP_REFRESH, AP_TRAP_FAMILY_NONE,
	 AP_TRAP_COND_NONE, 1, 20000, AP_TRAP_WARNING_MS},
	{"Boost Blocker", AP_TRAP_CTX_ALL_NO_HUB, AP_TRAP_TIMING_SUSTAINED_TIMED,
	 AP_TRAP_DURATION_FIXED_MS, AP_TRAP_DUP_REFRESH, AP_TRAP_FAMILY_BOOST_CONTROL,
	 AP_TRAP_COND_NONE, 1, 15000, AP_TRAP_WARNING_MS},
	{"Wireframe", AP_TRAP_CTX_ALL_NO_HUB, AP_TRAP_TIMING_SUSTAINED_MAP,
	 AP_TRAP_DURATION_MAP_LIFETIME, AP_TRAP_DUP_QUEUE_LATER_MAP, AP_TRAP_FAMILY_NONE,
	 AP_TRAP_COND_NONE, 1, 0, AP_TRAP_WARNING_MS},
	{"Nitro", AP_TRAP_CTX_ALL_NO_HUB, AP_TRAP_TIMING_INSTANT,
	 AP_TRAP_DURATION_ENGINE_NATURAL, AP_TRAP_DUP_SERIALIZE, AP_TRAP_FAMILY_NONE,
	 AP_TRAP_COND_SAFE_HAZARD, 1, 0, AP_TRAP_WARNING_MS},
	{"Reverse Steering", AP_TRAP_CTX_ALL, AP_TRAP_TIMING_SHORT_WARNING,
	 AP_TRAP_DURATION_FIXED_MS, AP_TRAP_DUP_REFRESH, AP_TRAP_FAMILY_NONE,
	 AP_TRAP_COND_NONE, 1, 15000, AP_TRAP_WARNING_MS},
	{"Red Potion", AP_TRAP_CTX_ALL_NO_HUB, AP_TRAP_TIMING_INSTANT,
	 AP_TRAP_DURATION_ENGINE_NATURAL, AP_TRAP_DUP_SERIALIZE, AP_TRAP_FAMILY_NONE,
	 AP_TRAP_COND_SAFE_HAZARD, 1, 0, AP_TRAP_WARNING_MS},
	// PROVISIONAL duration: the notebook leaves 15 versus 20 seconds to a live
	// comfort test. 15 is the lower-risk placeholder.
	{"Upside Down", AP_TRAP_CTX_ALL_NO_HUB, AP_TRAP_TIMING_SUSTAINED_TIMED,
	 AP_TRAP_DURATION_FIXED_MS, AP_TRAP_DUP_REFRESH, AP_TRAP_FAMILY_CAMERA_TRANSFORM,
	 AP_TRAP_COND_NONE, 1, 15000, AP_TRAP_WARNING_MS},
	{"Mirror Mode", AP_TRAP_CTX_ALL_NO_HUB, AP_TRAP_TIMING_SUSTAINED_TIMED,
	 AP_TRAP_DURATION_FIXED_MS, AP_TRAP_DUP_REFRESH, AP_TRAP_FAMILY_CAMERA_TRANSFORM,
	 AP_TRAP_COND_NONE, 1, 15000, AP_TRAP_WARNING_MS},
	// The countdown is deliberately hidden and the Warpball itself is the warning,
	// so this one has no warning window either.
	{"Warpball Ambush", AP_TRAP_CTX_AI_RACE, AP_TRAP_TIMING_CONDITIONAL_AMBUSH,
	 AP_TRAP_DURATION_ENGINE_NATURAL, AP_TRAP_DUP_SERIALIZE, AP_TRAP_FAMILY_NONE,
	 AP_TRAP_COND_AI_LEAD, 1, 0, 0},
	{"Demo Camera", AP_TRAP_CTX_AI_RACE | AP_TRAP_CTX_TIME_TRIAL,
	 AP_TRAP_TIMING_SUSTAINED_TIMED, AP_TRAP_DURATION_FIXED_MS, AP_TRAP_DUP_REFRESH,
	 AP_TRAP_FAMILY_CAMERA_TRANSFORM, AP_TRAP_COND_DEMO_CAMERA, 1, 15000,
	 AP_TRAP_WARNING_MS},
};

// ── Registry ──
//
// Capacity bounds the BACKLOG, not the roster: one effect can hold several slots
// at once when its duplicates serialize. 16 was sized when five effects existed.
// The 0.2.0 apworld ships 20 trap identities, several of them conditional and able
// to sit armed indefinitely (every Wumpa Wipeout copy needs its own 10-Wumpa
// trigger, every Warpball Ambush copy its own 15-second lead), so a trap-heavy
// seed can plausibly carry more than 16 unresolved copies. Over capacity is
// refused with a log line, never silently overwritten, and 32 slots cost about
// half a kilobyte.
#define AP_TRAP_SCHED_CAP 32
// The first eligible frame after a batch drain can announce every armed slot at
// once, so the event ring has to outrun the registry rather than match it.
#define AP_TRAP_EVENT_CAP 64

enum AP_TrapSlotState
{
	AP_TRAP_SLOT_EMPTY = 0,
	AP_TRAP_SLOT_ARMED,   // received, waiting for an eligible window
	AP_TRAP_SLOT_WARNING, // warning shown, effect not applied yet
	AP_TRAP_SLOT_ACTIVE   // effect applied
};

enum AP_TrapEventKind
{
	AP_TRAP_EV_ARMED = 0, // announce visibly: received outside an eligible window
	AP_TRAP_EV_INACTIVE,  // wave 2 scaffold received, retained armed, log only
	AP_TRAP_EV_WARN,      // warning started
	AP_TRAP_EV_FIRE,      // effect became active
	AP_TRAP_EV_CLEAR,     // effect ended
	AP_TRAP_EV_REARM,     // an unfired warning was returned to armed
	AP_TRAP_EV_SUSPEND,   // scripted camera or control took over
	AP_TRAP_EV_RESUME,    // control returned, remaining time intact
	AP_TRAP_EV_REFRESH,   // duplicate refilled a running timer
	AP_TRAP_EV_DROPPED    // registry full
};

typedef struct
{
	unsigned char kind;
	unsigned char effect;
	unsigned char slot;
} AP_TrapEvent;

typedef struct
{
	unsigned char state;     // AP_TrapSlotState
	unsigned char effect;    // AP_TrapEffectId
	unsigned char suspended; // ACTIVE and frozen by a scripted sequence
	unsigned char announced; // the armed announcement has been emitted once
	int warnMs;              // WARNING: milliseconds of warning left
	int remainMs;            // ACTIVE: milliseconds left, or -1 for engine-natural
	unsigned seq;            // receipt order, drives family serialization
} AP_TrapSlot;

typedef struct
{
	AP_TrapSlot slots[AP_TRAP_SCHED_CAP];
	unsigned nextSeq;
	unsigned enabled; // bit per effect id, seeded from the descriptors
	int epoch;        // last observed map epoch
	int epochSeen;    // 0 until the first step, so the first map is not a change
	AP_TrapEvent events[AP_TRAP_EVENT_CAP];
	int eventHead;
	int eventCount;
	int eventsLost;
	// -1 = descriptor's legacy policy, 0 = full race, >0 = selected timed window.
	// Runtime sets this from config.ini; the sentinel keeps standalone callers
	// source-compatible and makes descriptor tests explicit.
	int durationOverrideMs;
} AP_TrapSched;

// One frame of observed world state. Everything the scheduler is allowed to know.
typedef struct
{
	int context;         // one AP_TrapContext bit, or 0 for menus, loading, credits
	int mapEpoch;        // bumped by the runtime on every load, restart and Cup leg
	int controlUnlocked; // the player genuinely drives: countdown done, not loading
	int paused;
	int scripted;       // scripted camera or control owns the kart
	int finishOrPodium; // finish ceremony or podium
	unsigned conditions; // satisfied AP_TrapCondition bits
	int elapsedMs;
} AP_TrapWorld;

// ── Event ring ──
static inline void AP_TrapSchedEmit(AP_TrapSched *s, int kind, int effect, int slot)
{
	int at;
	if (s->eventCount >= AP_TRAP_EVENT_CAP)
	{
		s->eventHead = (s->eventHead + 1) % AP_TRAP_EVENT_CAP;
		s->eventCount--;
		s->eventsLost++;
	}
	at = (s->eventHead + s->eventCount) % AP_TRAP_EVENT_CAP;
	s->events[at].kind = (unsigned char)kind;
	s->events[at].effect = (unsigned char)effect;
	s->events[at].slot = (unsigned char)slot;
	s->eventCount++;
}

// Drain one event. Returns 0 when the ring is empty.
static inline int AP_TrapSchedPopEvent(AP_TrapSched *s, AP_TrapEvent *out)
{
	if (s->eventCount <= 0)
		return 0;
	*out = s->events[s->eventHead];
	s->eventHead = (s->eventHead + 1) % AP_TRAP_EVENT_CAP;
	s->eventCount--;
	return 1;
}

// ── Construction and identity resets ──
//
// This is the "new slot, seed or connection" rule: drop every armed and every
// active trap. It is also the constructor, so a scheduler is never read before it
// has been reset.
static inline void AP_TrapSchedReset(AP_TrapSched *s)
{
	int i;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		s->slots[i].state = AP_TRAP_SLOT_EMPTY;
		s->slots[i].effect = 0;
		s->slots[i].suspended = 0;
		s->slots[i].announced = 0;
		s->slots[i].warnMs = 0;
		s->slots[i].remainMs = 0;
		s->slots[i].seq = 0;
	}
	s->nextSeq = 1;
	s->enabled = 0;
	for (i = 0; i < AP_TRAP_EFFECT_COUNT; i++)
		if (AP_TRAP_DESC[i].active)
			s->enabled |= 1u << i;
	s->epoch = 0;
	s->epochSeen = 0;
	s->eventHead = 0;
	s->eventCount = 0;
	s->eventsLost = 0;
	s->durationOverrideMs = -1;
}

static inline void AP_TrapSchedSetDuration(AP_TrapSched *s, int durationMs)
{
	s->durationOverrideMs = durationMs < 0 ? -1 : durationMs;
}

// Turn one effect on or off for this scheduler. Wave 2 activates its effects one
// at a time through this rather than by editing the descriptor table, and the
// harness uses it to exercise a family rule across effects that are not applied
// yet. Disabling an effect only stops it being promoted; a copy already running is
// left to finish under its own rules.
static inline void AP_TrapSchedEnable(AP_TrapSched *s, int effect, int on)
{
	if (effect < 0 || effect >= AP_TRAP_EFFECT_COUNT)
		return;
	if (on)
		s->enabled |= 1u << effect;
	else
		s->enabled &= ~(1u << effect);
}

static inline int AP_TrapSchedIsEnabled(const AP_TrapSched *s, int effect)
{
	if (effect < 0 || effect >= AP_TRAP_EFFECT_COUNT)
		return 0;
	return (s->enabled & (1u << effect)) != 0;
}

// ── Receipt ──
// Returns the slot index, or -1 when the registry is full. Nothing is decided
// here beyond receipt order: whether the copy announces itself as armed depends on
// the world, which the next step supplies.
// Announce-only pass for states where the step returns before its promotion
// loop (pause, scripted camera, finish ceremony). The design principle is that
// an armed trap is never a silent wait, and those are exactly the screens the
// player is looking at. Eligibility is deliberately not consulted: this pass
// exists for frames where nothing may fire anyway.
static inline void AP_TrapSchedAnnounceArmed(AP_TrapSched *s)
{
	int i;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		if (s->slots[i].state != AP_TRAP_SLOT_ARMED || s->slots[i].announced)
			continue;
		s->slots[i].announced = 1;
		AP_TrapSchedEmit(s,
		                 AP_TrapSchedIsEnabled(s, s->slots[i].effect)
		                     ? AP_TRAP_EV_ARMED : AP_TRAP_EV_INACTIVE,
		                 s->slots[i].effect, i);
	}
}

static inline int AP_TrapSchedReceive(AP_TrapSched *s, int effect)
{
	int i;
	if (effect < 0 || effect >= AP_TRAP_EFFECT_COUNT)
		return -1;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		if (s->slots[i].state != AP_TRAP_SLOT_EMPTY)
			continue;
		s->slots[i].state = AP_TRAP_SLOT_ARMED;
		s->slots[i].effect = (unsigned char)effect;
		s->slots[i].suspended = 0;
		s->slots[i].announced = 0;
		s->slots[i].warnMs = 0;
		s->slots[i].remainMs = 0;
		s->slots[i].seq = s->nextSeq++;
		return i;
	}
	AP_TrapSchedEmit(s, AP_TRAP_EV_DROPPED, effect, 0);
	return -1;
}

// ── Queries the runtime uses at the effect call sites ──
static inline int AP_TrapSchedActive(const AP_TrapSched *s, int effect)
{
	int i;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
		if (s->slots[i].state == AP_TRAP_SLOT_ACTIVE && s->slots[i].effect == effect &&
		    !s->slots[i].suspended)
			return 1;
	return 0;
}

static inline int AP_TrapSchedArmedCount(const AP_TrapSched *s, int effect)
{
	int i, n = 0;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
		if (s->slots[i].state == AP_TRAP_SLOT_ARMED && s->slots[i].effect == effect)
			n++;
	return n;
}

static inline int AP_TrapSchedStateOf(const AP_TrapSched *s, int effect)
{
	int i, best = AP_TRAP_SLOT_EMPTY;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
		if (s->slots[i].effect == effect && s->slots[i].state > best)
			best = s->slots[i].state;
	return best;
}

// Any copy of this effect occupying the effect: warning shown or effect applied.
static inline int AP_TrapSchedEffectBusy(const AP_TrapSched *s, int effect)
{
	int i;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
		if (s->slots[i].effect == effect &&
		    (s->slots[i].state == AP_TRAP_SLOT_WARNING ||
		     s->slots[i].state == AP_TRAP_SLOT_ACTIVE))
			return 1;
	return 0;
}

// Any OTHER effect of the same family occupying the family.
static inline int AP_TrapSchedFamilyBusy(const AP_TrapSched *s, int family, int effect)
{
	int i;
	if (family == AP_TRAP_FAMILY_NONE)
		return 0;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		if (s->slots[i].state != AP_TRAP_SLOT_WARNING &&
		    s->slots[i].state != AP_TRAP_SLOT_ACTIVE)
			continue;
		if (s->slots[i].effect == effect)
			continue;
		if (AP_TRAP_DESC[s->slots[i].effect].family == family)
			return 1;
	}
	return 0;
}

// ── Map boundary ──
//
// Clear active effects before destination initialization. This is the one rule the
// runtime must be able to invoke by itself, at the moment the load begins and
// while the OLD map is still standing, because of the First Person regression: a
// forced camera that is only restored on the destination's first frame has already
// leaked into that map's intro camera state and eaten the intro-skip input.
//
// An ACTIVE copy is consumed and cleared. A WARNING copy is returned to ARMED
// instead: it never fired, so nothing was delivered, and consuming it would lose a
// trap to a load the player did not choose.
static inline void AP_TrapSchedMapChange(AP_TrapSched *s)
{
	int i;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		if (s->slots[i].state == AP_TRAP_SLOT_ACTIVE)
		{
			AP_TrapSchedEmit(s, AP_TRAP_EV_CLEAR, s->slots[i].effect, i);
			s->slots[i].state = AP_TRAP_SLOT_EMPTY;
			s->slots[i].suspended = 0;
			s->slots[i].remainMs = 0;
		}
		else if (s->slots[i].state == AP_TRAP_SLOT_WARNING)
		{
			AP_TrapSchedEmit(s, AP_TRAP_EV_REARM, s->slots[i].effect, i);
			s->slots[i].state = AP_TRAP_SLOT_ARMED;
			s->slots[i].warnMs = 0;
			s->slots[i].announced = 0;
		}
	}
}

// An engine-natural effect reports its own completion: the flatten animation has
// recovered, the reroll landed, the Nitro was spawned. Clears the oldest running
// copy so the next serialized copy can take its turn.
static inline void AP_TrapSchedEffectDone(AP_TrapSched *s, int effect)
{
	int i, best = -1;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		if (s->slots[i].state != AP_TRAP_SLOT_ACTIVE || s->slots[i].effect != effect)
			continue;
		if (best < 0 || s->slots[i].seq < s->slots[best].seq)
			best = i;
	}
	if (best < 0)
		return;
	AP_TrapSchedEmit(s, AP_TRAP_EV_CLEAR, effect, best);
	s->slots[best].state = AP_TRAP_SLOT_EMPTY;
	s->slots[best].suspended = 0;
	s->slots[best].remainMs = 0;
}

// ── Internals used by the step ──
static inline int AP_TrapSchedUsesClientDuration(const AP_TrapDescriptor *d)
{
	return d->duration == AP_TRAP_DURATION_FIXED_MS ||
	       d->duration == AP_TRAP_DURATION_MAP_LIFETIME;
}

static inline int AP_TrapSchedEffectiveDuration(const AP_TrapSched *s,
	                                             const AP_TrapDescriptor *d)
{
	if (s->durationOverrideMs >= 0 && AP_TrapSchedUsesClientDuration(d))
		return s->durationOverrideMs == 0 ? -1 : s->durationOverrideMs;
	return d->duration == AP_TRAP_DURATION_FIXED_MS ? d->durationMs : -1;
}

static inline void AP_TrapSchedFire(AP_TrapSched *s, int i)
{
	const AP_TrapDescriptor *d = &AP_TRAP_DESC[s->slots[i].effect];
	s->slots[i].state = AP_TRAP_SLOT_ACTIVE;
	s->slots[i].suspended = 0;
	s->slots[i].warnMs = 0;
	s->slots[i].remainMs = AP_TrapSchedEffectiveDuration(s, d);
	AP_TrapSchedEmit(s, AP_TRAP_EV_FIRE, s->slots[i].effect, i);
}

// May an armed copy of this effect start its warning right now? Context, condition
// and roster membership only; duplicates and families are checked separately so
// the caller can tell "wrong map" from "waiting its turn".
static inline int AP_TrapSchedEligible(const AP_TrapSched *s, const AP_TrapWorld *w,
                                       int effect)
{
	const AP_TrapDescriptor *d = &AP_TRAP_DESC[effect];
	if (!AP_TrapSchedIsEnabled(s, effect))
		return 0;
	// A configured duration turns the governed effects into playable-event traps:
	// receipts in hubs, menus and loading remain armed for the next event.
	if (s->durationOverrideMs >= 0 && AP_TrapSchedUsesClientDuration(d) &&
	    (w->context == 0 || w->context == AP_TRAP_CTX_HUB))
		return 0;
	if ((w->context & d->contexts) == 0)
		return 0;
	if (d->condition != AP_TRAP_COND_NONE && (w->conditions & d->condition) == 0)
		return 0;
	return 1;
}

// May an effect that has finished its warning actually begin? The starting
// countdown may show a warning, but nothing begins before the player has control,
// and podium, finish ceremonies and scripted sequences never start an effect.
static inline int AP_TrapSchedMayFire(const AP_TrapWorld *w)
{
	return w->controlUnlocked && !w->paused && !w->scripted && !w->finishOrPodium;
}

// ── Per-frame step ──
//
// Rule order matters and is fixed:
//   1. map boundary clears before anything else can observe the new map
//   2. pause freezes timed effects and warnings outright
//   3. scripted control suspends running effects and their timers
//   4. running effects age
//   5. warnings age and fire
//   6. armed copies are promoted, oldest receipt first
//
// Promotion runs last so a copy cannot be armed and fired in one frame unless its
// descriptor asks for a zero-length warning, which is the conditional-ambush case.
static inline void AP_TrapSchedStep(AP_TrapSched *s, const AP_TrapWorld *w)
{
	unsigned char considered[AP_TRAP_SCHED_CAP];
	int i, pass;
	int dt = w->elapsedMs > 0 ? w->elapsedMs : 0;
	int freeze;

	if (!s->epochSeen)
	{
		s->epoch = w->mapEpoch;
		s->epochSeen = 1;
	}
	else if (w->mapEpoch != s->epoch)
	{
		s->epoch = w->mapEpoch;
		AP_TrapSchedMapChange(s);
	}

	if (w->paused)
	{
		AP_TrapSchedAnnounceArmed(s);
		return;
	}

	// Scripted camera or control, and the finish ceremony, suspend the effect and
	// its timer. Ordinary respawns are not in this set: the lifecycle continues
	// through them unless an individual effect rules otherwise.
	freeze = w->scripted || w->finishOrPodium;
	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		if (s->slots[i].state != AP_TRAP_SLOT_ACTIVE)
			continue;
		if (freeze && !s->slots[i].suspended)
		{
			s->slots[i].suspended = 1;
			AP_TrapSchedEmit(s, AP_TRAP_EV_SUSPEND, s->slots[i].effect, i);
		}
		else if (!freeze && s->slots[i].suspended)
		{
			s->slots[i].suspended = 0;
			AP_TrapSchedEmit(s, AP_TRAP_EV_RESUME, s->slots[i].effect, i);
		}
	}

	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		if (s->slots[i].state != AP_TRAP_SLOT_ACTIVE || s->slots[i].suspended)
			continue;
		if (s->slots[i].remainMs < 0)
			continue; // map-lifetime or engine-natural, not on a clock
		// Only genuinely playable event frames consume a configured comfort
		// window. The -1 legacy sentinel retains descriptor behaviour for pure
		// callers that deliberately do not opt into the client policy.
		if (s->durationOverrideMs >= 0 &&
		    (!w->controlUnlocked || w->context == 0 || w->context == AP_TRAP_CTX_HUB))
			continue;
		s->slots[i].remainMs -= dt;
		if (s->slots[i].remainMs <= 0)
		{
			AP_TrapSchedEmit(s, AP_TRAP_EV_CLEAR, s->slots[i].effect, i);
			s->slots[i].state = AP_TRAP_SLOT_EMPTY;
			s->slots[i].remainMs = 0;
		}
	}

	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
	{
		if (s->slots[i].state != AP_TRAP_SLOT_WARNING)
			continue;
		if (freeze)
			continue;
		// A prerequisite lost during the warning re-arms the copy instead of
		// consuming it: Item Reroll, Forced Use and Wumpa Wipeout all rule
		// that losing the held item or the fruit during the warning keeps the
		// trap waiting for the next eligible moment.
		if (!AP_TrapSchedEligible(s, w, s->slots[i].effect))
		{
			s->slots[i].state = AP_TRAP_SLOT_ARMED;
			s->slots[i].warnMs = 0;
			AP_TrapSchedEmit(s, AP_TRAP_EV_REARM, s->slots[i].effect, i);
			continue;
		}
		if (s->slots[i].warnMs > 0)
			s->slots[i].warnMs -= dt;
		// A warning that runs out during the countdown holds at zero and fires the
		// moment control unlocks, rather than being restarted or lost.
		if (s->slots[i].warnMs <= 0)
		{
			s->slots[i].warnMs = 0;
			if (AP_TrapSchedMayFire(w))
				AP_TrapSchedFire(s, i);
		}
	}

	// A scripted sequence or a finish ceremony suspends the whole timeline, so an
	// armed copy does not start its warning there either. The starting countdown is
	// deliberately NOT in this set: it is the one state that may show a warning
	// before the effect is allowed to begin. Armed receipts still announce, so a
	// trap arriving on the results screen is never a silent wait.
	if (freeze)
	{
		AP_TrapSchedAnnounceArmed(s);
		return;
	}

	for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
		considered[i] = 0;

	for (pass = 0; pass < AP_TRAP_SCHED_CAP; pass++)
	{
		const AP_TrapDescriptor *d;
		int best = -1;
		int effect;

		for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
		{
			if (s->slots[i].state != AP_TRAP_SLOT_ARMED || considered[i])
				continue;
			if (best < 0 || s->slots[i].seq < s->slots[best].seq)
				best = i;
		}
		if (best < 0)
			break;
		considered[best] = 1;

		effect = s->slots[best].effect;
		d = &AP_TRAP_DESC[effect];

		if (!AP_TrapSchedEligible(s, w, effect))
		{
			// Ineligible map or unmet condition: keep the trap armed, and say so
			// once so an armed trap is never a silent wait.
			if (!s->slots[best].announced)
			{
				s->slots[best].announced = 1;
				AP_TrapSchedEmit(s,
				                 AP_TrapSchedIsEnabled(s, effect) ? AP_TRAP_EV_ARMED
				                                                  : AP_TRAP_EV_INACTIVE,
				                 effect, best);
			}
			continue;
		}

		// A refreshing duplicate never queues: it refills the copy that is already
		// running and is consumed on the spot. A copy still in its warning is not
		// refreshable, because there is no window to refill yet.
		if (d->duplicate == AP_TRAP_DUP_REFRESH)
		{
			int running = -1;
			for (i = 0; i < AP_TRAP_SCHED_CAP; i++)
				if (s->slots[i].state == AP_TRAP_SLOT_ACTIVE && s->slots[i].effect == effect)
					running = i;
			if (running >= 0)
			{
				if (AP_TrapSchedUsesClientDuration(d))
					s->slots[running].remainMs = AP_TrapSchedEffectiveDuration(s, d);
				AP_TrapSchedEmit(s, AP_TRAP_EV_REFRESH, effect, running);
				s->slots[best].state = AP_TRAP_SLOT_EMPTY;
				continue;
			}
		}

		if (AP_TrapSchedEffectBusy(s, effect) ||
		    AP_TrapSchedFamilyBusy(s, d->family, effect))
		{
			// Waiting its turn: a map-lifetime duplicate for a later map, a
			// serialized duplicate for the copy ahead of it, or a different member
			// of the same conflict family. All three keep receipt order, because
			// this loop walks armed copies oldest first.
			if (!s->slots[best].announced)
			{
				s->slots[best].announced = 1;
				AP_TrapSchedEmit(s, AP_TRAP_EV_ARMED, effect, best);
			}
			continue;
		}

		s->slots[best].state = AP_TRAP_SLOT_WARNING;
		s->slots[best].warnMs = d->warningMs;
		AP_TrapSchedEmit(s, AP_TRAP_EV_WARN, effect, best);
		// A zero-length warning is the conditional ambush: the armed presentation
		// already happened and the gameplay event itself is the cue, so the effect
		// starts on this frame rather than the next one.
		if (s->slots[best].warnMs <= 0 && AP_TrapSchedMayFire(w))
			AP_TrapSchedFire(s, best);
	}
}

#endif // AP_TRAP_SCHED_LOGIC_H
