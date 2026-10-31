#ifndef AP_BOSS_ADAPTER_H
#define AP_BOSS_ADAPTER_H

// #169 full weapon suite (J03, 2026-10-28, decisions 5, 6 and 8): the pure
// decisions of the boss adapter. game/PickupBots.c owns the engine side (it
// fires the retail item through VehPickupItem_ShootNow and keeps the bot's
// self-state timers, because bots never run VehPhysProc); everything here is
// freestanding so tools/test-boss-adapter.c runs the same code.
//
// Advanced families are engine item ids carried in the weaponType of a
// composed MetaDataBOSS row. Retail boss tables never carry them, so a retail
// race never reaches this adapter.

enum
{
	AP_BA_TURBO = 0x00,
	AP_BA_SHIELD = 0x06,
	AP_BA_MASK = 0x07,
	AP_BA_CLOCK = 0x08,
	AP_BA_WARP_ORB = 0x09,
	AP_BA_INVISIBILITY = 0x0c,
	AP_BA_SUPER_ENGINE = 0x0d,
};

static inline int AP_BossAdapterIsAdvanced(int weapon)
{
	return weapon == AP_BA_TURBO || weapon == AP_BA_SHIELD || weapon == AP_BA_MASK || weapon == AP_BA_CLOCK ||
	       weapon == AP_BA_WARP_ORB || weapon == AP_BA_INVISIBILITY || weapon == AP_BA_SUPER_ENGINE;
}

// Retail super engine duration (VehPickupItem_ShootNow case 0xd, unjuiced).
#ifndef AP_BOSS_ADAPTER_SUPER_MS
#define AP_BOSS_ADAPTER_SUPER_MS 0x1e00
#ifndef AP_BOSS_ADAPTER_PURE
// Engine glue (ap/ap_hooks_gates.c), CTR_AP builds only.
struct MetaDataBOSS;
// The Purple Gem Cup gauntlet leg running now (adventure Purple Gem Cup with
// purple_cup_boss_gauntlet, on the leg's recorded track and a schedule that
// fitted at race init): the attacker's engine character id, else -1.
int AP_BossGauntletAttacker(void);
// 1 while the running schedule is N. Oxide's composed final record, the only
// one allowed the super engine.
int AP_BossFinalComposedNow(void);
// game/PickupBots.c: retail VehPickState protection (shield pop, mask, post-pop
// invincibility) for the driver the adapter armed; 1 when the hit is absorbed.
struct Driver;
int PickupBots_AdapterProtect(struct Driver *victim);
#endif

#endif

// What the adapter does with an advanced segment this frame.
enum
{
	AP_BA_WAIT = 0, // not eligible yet; keep the segment's one use for later
	AP_BA_FIRE = 1, // activate now and spend the use
	AP_BA_SKIP = 2, // spend the use without firing (warp orb behind, turbo rules)
};

// Facts the engine gathers for one decision.
typedef struct
{
	int weapon;
	int lap, numLaps;        // boss lap index (0 based) and race laps
	int segment, lastSegment; // schedule row of the boss and the last real row
	int bossRank, playerRank; // 0 = first
	int playerClocked;        // player->clockReceive != 0
	int warpOrbInPlay;        // gameMode1 & WARPBALL_HELD
	int shieldHeld;           // boss->instBubbleHold != NULL
	int maskActive;           // the adapter's mask is still alive
	int invisible;            // boss->invisibleTimer != 0
	int superEngine;          // boss->superEngineTimer != 0
	int superAllowed;         // the running schedule is N. Oxide's final record
} AP_BossAdapterFacts;

// Per-race adapter state. Reset at every race init (PickupBots_Init), so a
// restart, an exit to the map or a failed load can never carry a use or a
// timer into the next race.
typedef struct
{
	int armed;        // an advanced segment fired this race: the tick runs
	int keyLap, keySeg; // the (lap, segment) whose one use is tracked
	int used;         // that segment's use is spent
	int turboLap;     // lap the last turbo fired on, -1 none
	int maskOn;       // the adapter activated the boss's mask
	int superMs;      // super engine left, ms
} AP_BossAdapterState;

static inline void AP_BossAdapterReset(AP_BossAdapterState *s)
{
	s->armed = 0;
	s->keyLap = -1;
	s->keySeg = -1;
	s->used = 0;
	s->turboLap = -1;
	s->maskOn = 0;
	s->superMs = 0;
}

// One use per segment entry per lap: entering a new (lap, segment) gives the
// segment a fresh use.
static inline void AP_BossAdapterEnter(AP_BossAdapterState *s, int lap, int segment)
{
	if (lap != s->keyLap || segment != s->keySeg)
	{
		s->keyLap = lap;
		s->keySeg = segment;
		s->used = 0;
	}
}

// The final straight: the last schedule segment of the final lap.
static inline int AP_BossAdapterFinalStraight(int lap, int numLaps, int segment, int lastSegment)
{
	return numLaps > 0 && lap >= numLaps - 1 && segment >= lastSegment;
}

// Decision 6, per family. Called only for an advanced weapon whose segment
// use is not yet spent.
static inline int AP_BossAdapterDecide(const AP_BossAdapterState *s, const AP_BossAdapterFacts *f)
{
	switch (f->weapon)
	{
	case AP_BA_CLOCK:
		// Retail effect, hits the player wherever they are; never stacked.
		return f->playerClocked ? AP_BA_WAIT : AP_BA_FIRE;
	case AP_BA_WARP_ORB:
		// Only when the player leads the boss; otherwise the use is skipped.
		if (f->playerRank >= f->bossRank)
			return AP_BA_SKIP;
		return f->warpOrbInPlay ? AP_BA_WAIT : AP_BA_FIRE;
	case AP_BA_SHIELD:
		return f->shieldHeld ? AP_BA_WAIT : AP_BA_FIRE;
	case AP_BA_MASK:
		return f->maskActive ? AP_BA_WAIT : AP_BA_FIRE;
	case AP_BA_TURBO:
		// At most one per lap, never on the final straight.
		if (s->turboLap == f->lap || AP_BossAdapterFinalStraight(f->lap, f->numLaps, f->segment, f->lastSegment))
			return AP_BA_SKIP;
		return AP_BA_FIRE;
	case AP_BA_INVISIBILITY:
		return f->invisible ? AP_BA_WAIT : AP_BA_FIRE;
	case AP_BA_SUPER_ENGINE:
		// N. Oxide's all_compatible profile only (decision 6).
		if (!f->superAllowed)
			return AP_BA_SKIP;
		return f->superEngine ? AP_BA_WAIT : AP_BA_FIRE;
	}
	return AP_BA_SKIP;
}

// Record a decision on the state (the engine fires on AP_BA_FIRE).
static inline void AP_BossAdapterCommit(AP_BossAdapterState *s, const AP_BossAdapterFacts *f, int decision)
{
	if (decision == AP_BA_WAIT)
		return;
	s->used = 1;
	if (decision != AP_BA_FIRE)
		return;
	s->armed = 1;
	if (f->weapon == AP_BA_TURBO)
		s->turboLap = f->lap;
	if (f->weapon == AP_BA_MASK)
		s->maskOn = 1;
	if (f->weapon == AP_BA_SUPER_ENGINE)
		s->superMs = AP_BOSS_ADAPTER_SUPER_MS;
}

// A countdown in ms. Returns 1 on the frame it reaches zero.
static inline int AP_BossAdapterCountdown(int *timer, int elapsedMs)
{
	if (*timer <= 0)
		return 0;
	*timer -= elapsedMs;
	if (*timer <= 0)
	{
		*timer = 0;
		return 1;
	}
	return 0;
}

// Must every self-state end now? On finish, on boss damage for invisibility
// (Wayfinder D2), and whenever the race is over.
enum
{
	AP_BA_END_NONE = 0,
	AP_BA_END_INVISIBILITY = 1,
	AP_BA_END_ALL = 2,
};
static inline int AP_BossAdapterEndRule(int bossFinished, int raceOver, int bossDamaged)
{
	if (bossFinished || raceOver)
		return AP_BA_END_ALL;
	if (bossDamaged)
		return AP_BA_END_INVISIBILITY;
	return AP_BA_END_NONE;
}

// Decision 8: a schedule recorded for a venue with `recordedPoints` restart
// points, raced on a loaded track with `loadedPoints`. Remaps each checkpoint
// to the same relative lap position (the tier 1 rule) into `out`. Returns 1
// when the remapped schedule starts at 0 and rises strictly inside the loaded
// track, else 0 (the caller then throws nothing, as before).
static inline int AP_BossAdapterRemap(const unsigned char *cps, int n, int recordedPoints, int loadedPoints,
                                      unsigned char *out)
{
	int i, prev = -1;
	if (n <= 0 || recordedPoints < 2 || loadedPoints < 2 || loadedPoints > 256)
		return 0;
	for (i = 0; i < n; i++)
	{
		int cp = cps[i];
		int m;
		if (cp <= 0)
			m = 0;
		else if (cp >= recordedPoints - 1)
			m = loadedPoints - 1;
		else
			m = (cp * (loadedPoints - 1)) / (recordedPoints - 1);
		if (m <= prev || m >= loadedPoints || (prev < 0 && m != 0))
			return 0;
		out[i] = (unsigned char)m;
		prev = m;
	}
	return 1;
}

#ifndef AP_BOSS_ADAPTER_PURE
// Engine glue (ap/ap_hooks_gates.c), CTR_AP builds only.
struct MetaDataBOSS;
// The Purple Gem Cup gauntlet leg running now (adventure Purple Gem Cup with
// purple_cup_boss_gauntlet, on the leg's recorded track and a schedule that
// fitted at race init): the attacker's engine character id, else -1.
int AP_BossGauntletAttacker(void);
// 1 while the running schedule is N. Oxide's composed final record, the only
// one allowed the super engine.
int AP_BossFinalComposedNow(void);
// game/PickupBots.c: retail VehPickState protection (shield pop, mask, post-pop
// invincibility) for the driver the adapter armed; 1 when the hit is absorbed.
struct Driver;
int PickupBots_AdapterProtect(struct Driver *victim);
#endif

#endif
