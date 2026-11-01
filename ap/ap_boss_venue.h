#ifndef AP_BOSS_VENUE_H
#define AP_BOSS_VENUE_H

// Boss track shuffle (#169 tier 1, slot_data schema 18).
//
// With the apworld's `boss_track_shuffle` on, each of the four ordinary boss
// garages races its own boss on a track drawn at generation. The seed carries
// the draw in `boss_encounters`; native never redraws it. Everything else
// about the garage stays physical: gGT->bossID, the Boss Race check and Key,
// the loss counter, the boss driving table, music, cutscenes and the goal.
//
// The boss keeps its retail attack schedule. Each segment of that schedule
// starts at a checkpoint (restart point) index on the boss's home track; on a
// drawn venue the index is mapped to the same relative lap position
// (AP_BossVenueCheckpoint). A venue is certified for a garage only when the
// mapped schedule stays strictly increasing inside the venue's checkpoint
// range, so a venue that cannot carry the inherited attack is refused, never
// silently replaced by the retail track (design ruling, 2026-10-13).
//
// Freestanding by design: no engine, no config, no network, so
// tools/test-boss-venue.c pins the same tables and decisions the client uses.
// The tables are shared byte for byte with the apworld through
// tools/fixtures/boss_venues.json.

#define AP_BOSS_VENUE_GARAGES 4
#define AP_BOSS_VENUE_TRACKS 18
#define AP_BOSS_VENUE_SEGMENTS 5
#define AP_BOSS_ENCOUNTERS_VERSION 1
// #169 tiers 2 and 3: a block with the opponent or kit switch on.
#define AP_BOSS_ENCOUNTERS_VERSION_2 2
#define AP_BOSS_ENCOUNTERS_SCHEMA 18
#define AP_BOSS_VENUE_LOCATION_BASE 35011100L

// Restart point count of every retail race track's 1P LEV, by LevelID
// (NTSC-U SCUS_944.26, read with the retail editor's levtool, 2026-10-15).
// All 18 also carry the three AI navigation paths the boss AI drives.
static const short AP_BOSS_VENUE_RESTART_POINTS[AP_BOSS_VENUE_TRACKS] = {
	103, // 0 Dingo Canyon
	84,  // 1 Dragon Mines
	90,  // 2 Blizzard Bluff
	72,  // 3 Crash Cove
	110, // 4 Tiger Temple
	102, // 5 Papu's Pyramid
	78,  // 6 Roo's Tubes
	175, // 7 Hot Air Skyway
	160, // 8 Sewer Speedway
	123, // 9 Mystery Caves
	135, // 10 Cortex Castle
	138, // 11 N. Gin Labs
	167, // 12 Polar Pass
	190, // 13 Oxide Station
	83,  // 14 Coco Park
	194, // 15 Tiny Arena
	121, // 16 Slide Coliseum
	117, // 17 Turbo Track
};

// The four garages in hub order. Index == gGT->bossID == boss_req index.
typedef struct
{
	const char *key;      // wire key, same as boss_garage_req
	short homeLevel;      // retail venue LevelID (R232.bossTracks[hub])
	unsigned char attack[AP_BOSS_VENUE_SEGMENTS]; // retail segment starts
	signed char character; // the garage boss's engine character id
} AP_BossVenueGarage;

static const AP_BossVenueGarage AP_BOSS_VENUE_GARAGE[AP_BOSS_VENUE_GARAGES] = {
	{"ripper_roo", 6, {0x00, 0x10, 0x21, 0x2f, 0x46}, 10}, // data.BossWeaponRoo
	{"papu_papu", 5, {0x00, 0x0f, 0x22, 0x53, 0x5d}, 9},   // data.BossWeaponPapu
	{"komodo_joe", 1, {0x00, 0x10, 0x2a, 0x39, 0x46}, 11}, // data.BossWeaponJoe
	{"pinstripe", 7, {0x00, 0x10, 0x21, 0x2f, 0x46}, 8},   // data.BossWeaponPinstripe
};

// #169 tier 2: the racers a garage may draw, by wire key, with their engine
// character id: the retail roster without N. Oxide (15), who stays outside
// the four ordinary garages. Shared with the apworld through
// tools/fixtures/boss_encounters.json.
#define AP_BOSS_OPPONENTS 15
typedef struct
{
	const char *key;
	signed char character;
} AP_BossOpponent;

static const AP_BossOpponent AP_BOSS_OPPONENT[AP_BOSS_OPPONENTS] = {
	{"crash_bandicoot", 0}, {"coco_bandicoot", 3}, {"polar", 6},       {"pura", 7},
	{"neo_cortex", 1},      {"n_tropy", 12},       {"ripper_roo", 10}, {"papu_papu", 9},
	{"komodo_joe", 11},     {"pinstripe", 8},      {"dingodile", 5},   {"tiny_tiger", 2},
	{"n_gin", 4},           {"fake_crash", 14},    {"penta_penguin", 13},
};

// #169 tier 3: one composed attack segment, the retail MetaDataBOSS fields.
// Weapon: 0x01 bomb, 0x02 missile, 0x03 TNT, 0x04 potion, 0x64 TNT and
// 0x66 potion with the boss juice cycle, 0x0f none. Throw: 2 forward, 3
// dropped or normal. Juice: 0 none, 1 juiced, 2 alternating. These are the
// four projectile families the retail boss path fires through
// VehPickupItem_ShootNow; nothing else is accepted.
typedef struct
{
	unsigned char checkpoint, throwFlag, weapon, pathDisabled;
	unsigned short cooldown, juice;
} AP_BossKitSegment;

// J03: N. Oxide's composed profile keeps the 13 retail Oxide segment
// starts; the Purple Gem Cup gauntlet has four legs.
#define AP_BOSS_FINAL_SEGMENTS 13
#define AP_BOSS_GAUNTLET_LEGS 4
static const unsigned char AP_BOSS_OXIDE_CHECKPOINTS[AP_BOSS_FINAL_SEGMENTS] = {
	0x00, 0x10, 0x21, 0x2f, 0x3b, 0x40, 0x46, 0x56, 0x5d, 0x60, 0x65, 0x66, 0x7f,
};

#define AP_BOSS_KIT_COOLDOWN_MIN 2
#define AP_BOSS_KIT_COOLDOWN_MAX 0xff

// J03 (decisions 5 and 6): the advanced families, engine item ids fired
// through native's boss adapter: 0x00 turbo, 0x06 shield, 0x07 mask, 0x08
// clock, 0x09 warp orb, 0x0c invisibility, 0x0d super engine (N. Oxide's
// final record only).
#define AP_BOSS_KIT_SUPER_ENGINE 0x0d
static inline int AP_BossKitWeaponKnown(int w)
{
	return w == 0x00 || w == 0x01 || w == 0x02 || w == 0x03 || w == 0x04 || w == 0x06 || w == 0x07 ||
	       w == 0x08 || w == 0x09 || w == 0x0c || w == 0x0d || w == 0x0f || w == 0x64 || w == 0x66;
}

// Every (weapon, throw, path, juice) a composed segment may carry: the
// certified family variants and the four retail signature rows. The
// apworld's boss_encounters.CERTIFIED_ROWS is the twin (shared fixture).
#define AP_BOSS_KIT_CERTIFIED_ROWS 17
static const unsigned char AP_BOSS_KIT_CERTIFIED[AP_BOSS_KIT_CERTIFIED_ROWS][4] = {
	{0x00, 3, 1, 0}, {0x01, 3, 1, 0}, {0x02, 3, 1, 0}, {0x03, 3, 0, 0}, {0x04, 2, 1, 1}, {0x06, 3, 1, 0},
	{0x07, 3, 1, 0}, {0x08, 3, 1, 0}, {0x09, 2, 1, 0}, {0x0c, 3, 1, 0}, {0x0c, 3, 1, 1}, {0x0d, 3, 1, 0},
	{0x0f, 2, 0, 2}, {0x0f, 2, 1, 2}, {0x0f, 3, 1, 0}, {0x64, 2, 0, 2}, {0x66, 2, 1, 2},
};

static inline int AP_BossKitRowCertified(int weapon, int throwFlag, int path, int juice)
{
	int i;
	for (i = 0; i < AP_BOSS_KIT_CERTIFIED_ROWS; i++)
		if (AP_BOSS_KIT_CERTIFIED[i][0] == weapon && AP_BOSS_KIT_CERTIFIED[i][1] == throwFlag &&
		    AP_BOSS_KIT_CERTIFIED[i][2] == path && AP_BOSS_KIT_CERTIFIED[i][3] == juice)
			return 1;
	return 0;
}

// NULL when `seg` is a schedule this build can race on a venue with
// `venuePoints` restart points: `want` segments, the first at checkpoint 0,
// strictly increasing inside the venue, every row certified, at least one
// attack. Super engine only where `allowSuper` (N. Oxide's final record).
static inline const char *AP_BossKitScheduleProblemN(const AP_BossKitSegment *seg, int n, int want,
                                                      int venuePoints, int allowSuper)
{
	int i, prev = -1, attacks = 0;
	if (seg == 0 || n != want)
		return want == AP_BOSS_VENUE_SEGMENTS ? "schedule must have five segments"
		                                      : "schedule has the wrong segment count";
	for (i = 0; i < n; i++)
	{
		const AP_BossKitSegment *s = &seg[i];
		if ((int)s->checkpoint <= prev || (int)s->checkpoint >= venuePoints || (prev < 0 && s->checkpoint != 0))
			return "schedule checkpoints do not fit the venue";
		if (!AP_BossKitRowCertified(s->weapon, s->throwFlag, s->pathDisabled, s->juice) ||
		    s->cooldown < AP_BOSS_KIT_COOLDOWN_MIN || s->cooldown > AP_BOSS_KIT_COOLDOWN_MAX)
			return "schedule has a code this build cannot race";
		if (s->weapon == AP_BOSS_KIT_SUPER_ENGINE && !allowSuper)
			return "super engine is N. Oxide's only";
		if (s->weapon != 0x0f)
			attacks++;
		prev = s->checkpoint;
	}
	if (attacks == 0)
		return "schedule never attacks";
	return 0;
}

// A garage kit or gauntlet leg: five segments, no super engine.
static inline const char *AP_BossKitScheduleProblem(const AP_BossKitSegment *seg, int n, int venuePoints)
{
	return AP_BossKitScheduleProblemN(seg, n, AP_BOSS_VENUE_SEGMENTS, venuePoints, 0);
}

// N. Oxide's composed profile (oxide_attack_profile: all_compatible): the 13
// retail Oxide segment starts exactly, every row certified, super engine
// allowed. The table's own checkpoint scale (0x7f last) is checked, not a
// venue: native maps it at load like the retail Oxide table.
static inline const char *AP_BossFinalScheduleProblem(const AP_BossKitSegment *seg, int n)
{
	int i;
	const char *p = AP_BossKitScheduleProblemN(seg, n, AP_BOSS_FINAL_SEGMENTS, 0x80, 1);
	if (p)
		return p;
	for (i = 0; i < AP_BOSS_FINAL_SEGMENTS; i++)
		if (seg[i].checkpoint != AP_BOSS_OXIDE_CHECKPOINTS[i])
			return "final schedule checkpoints are not the retail Oxide segments";
	return 0;
}

// The engine character id of wire opponent `key`, or -1.
static inline int AP_BossOpponentCharacter(const char *key);
// Decision 7: N. Oxide as a wire opponent, only with boss_oxide_in_pool.
#define AP_BOSS_OXIDE_KEY "nitros_oxide"
#define AP_BOSS_OXIDE_CHARACTER 15
static inline int AP_BossOpponentCharacterEx(const char *key, int allowOxide);

// A retail segment start on the home track, mapped to the same relative lap
// position on a venue whose last checkpoint index is `venueLast`. The apworld
// twin is boss_track_shuffle.translate_checkpoint.
static inline int AP_BossVenueCheckpoint(int checkpoint, int homeLast, int venueLast)
{
	if (homeLast <= 0 || venueLast < 0)
		return -1;
	if (checkpoint <= 0)
		return 0;
	if (checkpoint >= homeLast)
		return venueLast;
	return (checkpoint * venueLast) / homeLast;
}

static inline int AP_BossVenueTrackKnown(int level)
{
	return level >= 0 && level < AP_BOSS_VENUE_TRACKS;
}

// Does `venueRestartPoints` carry garage `g`'s inherited attack? Every mapped
// segment must start inside the venue and strictly after the previous one.
static inline int AP_BossVenueScheduleFits(int g, int venueRestartPoints)
{
	int i, prev = -1;
	int homeLast;

	if (g < 0 || g >= AP_BOSS_VENUE_GARAGES || venueRestartPoints <= 0)
		return 0;
	homeLast = AP_BOSS_VENUE_RESTART_POINTS[AP_BOSS_VENUE_GARAGE[g].homeLevel] - 1;
	for (i = 0; i < AP_BOSS_VENUE_SEGMENTS; i++)
	{
		int cp = AP_BossVenueCheckpoint(AP_BOSS_VENUE_GARAGE[g].attack[i], homeLast,
		                                venueRestartPoints - 1);
		if (cp <= prev || cp >= venueRestartPoints)
			return 0;
		prev = cp;
	}
	return 1;
}

static inline int AP_BossVenueCertified(int g, int level)
{
	return AP_BossVenueTrackKnown(level) &&
	       AP_BossVenueScheduleFits(g, AP_BOSS_VENUE_RESTART_POINTS[level]);
}

// ── the parsed seed record ─────────────────────────────────────────────────

enum
{
	AP_BOSS_ENC_RETAIL = 0,  // no draw: every garage races its retail track
	AP_BOSS_ENC_VALID = 1,   // track[] holds the seed's four venues
	AP_BOSS_ENC_REFUSED = 2, // the seed asked for a draw this build cannot race
};

typedef struct
{
	int state;
	signed char track[AP_BOSS_VENUE_GARAGES];
	char reason[96]; // first refusal, for the log and the garage panel
	// #169 tiers 2 and 3 (block version 2). opponent[g] is the engine
	// character id the garage races (its own boss unless the opponent switch
	// drew another), composed[g] 1 when schedule[g] replaces the garage's
	// retail attack. lossEasing 0 drops the retail per-loss cooldown bonus.
	int version;
	signed char opponent[AP_BOSS_VENUE_GARAGES];
	unsigned char composed[AP_BOSS_VENUE_GARAGES];
	AP_BossKitSegment schedule[AP_BOSS_VENUE_GARAGES][AP_BOSS_VENUE_SEGMENTS];
	unsigned char kit;
	unsigned char lossEasing;
	// #133 PR 6b: a content-plan garage racing custom bytes (a custom package
	// or Cortex Vortex) served on its home LevelID. track[g] is then the home
	// level, custom[g] 1 and points[g] the certified restart point count.
	unsigned char custom[AP_BOSS_VENUE_GARAGES];
	short points[AP_BOSS_VENUE_GARAGES];
	// J03 2026-10-28 (decisions 5 to 9): the three new scalars, N. Oxide's
	// garage record (`final`, both Oxide races) and the Purple Gem Cup
	// gauntlet. finalOpponent is the engine id racing the Oxide garage (15
	// without a final record); finalComposed 1 when finalSchedule replaces
	// Oxide's retail table. gauntletTrack[i] is leg i's retail LevelID or -1
	// (no attacks), gauntletAttacker[i] the engine id that fires it.
	unsigned char trackOn;
	unsigned char opponentOn;
	unsigned char oxideInPool;
	unsigned char gauntletOn;
	unsigned char oxideProfileAll;
	unsigned char finalPresent;
	signed char finalOpponent;
	unsigned char finalComposed;
	AP_BossKitSegment finalSchedule[AP_BOSS_FINAL_SEGMENTS];
	signed char gauntletTrack[AP_BOSS_GAUNTLET_LEGS];
	signed char gauntletAttacker[AP_BOSS_GAUNTLET_LEGS];
	unsigned char gauntletComposed[AP_BOSS_GAUNTLET_LEGS];
	AP_BossKitSegment gauntletSchedule[AP_BOSS_GAUNTLET_LEGS][AP_BOSS_VENUE_SEGMENTS];
} AP_BossEncounters;

// One garage row's fields, already type-checked by the JSON reader.
typedef struct
{
	long location;
	int homeTrack;
	int track;
	const char *opponent;
	const char *aiProfile;
	const char *attack;
	int packagesEmpty;
	int fieldCount; // exact number of keys in the row object
	// Version 2 rows (#169 tiers 2 and 3): the block's version, the two
	// switches, and the row's schedule (scheduleCount -1 = not a valid list).
	int version;
	int trackOn;
	int opponentOn;
	int kitOn;
	int oxideInPool; // J03: nitros_oxide is a valid opponent
	int scheduleCount;
	AP_BossKitSegment schedule[AP_BOSS_VENUE_SEGMENTS];
} AP_BossEncounterRow;

#define AP_BOSS_ENCOUNTER_ROW_FIELDS 7
#define AP_BOSS_ENCOUNTER_ROW_FIELDS_2 8

static inline int AP_BossVenueStrEq(const char *a, const char *b)
{
	if (a == 0 || b == 0)
		return 0;
	while (*a && *a == *b)
	{
		a++;
		b++;
	}
	return *a == *b;
}

static inline int AP_BossOpponentCharacter(const char *key)
{
	int i;
	for (i = 0; i < AP_BOSS_OPPONENTS; i++)
		if (AP_BossVenueStrEq(key, AP_BOSS_OPPONENT[i].key))
			return AP_BOSS_OPPONENT[i].character;
	return -1;
}

static inline int AP_BossOpponentCharacterEx(const char *key, int allowOxide)
{
	if (allowOxide && AP_BossVenueStrEq(key, AP_BOSS_OXIDE_KEY))
		return AP_BOSS_OXIDE_CHARACTER;
	return AP_BossOpponentCharacter(key);
}

// NULL when garage `g`'s row is one this build can race, else the reason.
static inline const char *AP_BossEncounterRowProblem(int g, const AP_BossEncounterRow *r)
{
	if (g < 0 || g >= AP_BOSS_VENUE_GARAGES || r == 0)
		return "unknown garage";
	int v2 = r->version == AP_BOSS_ENCOUNTERS_VERSION_2;
	if (r->fieldCount != (v2 ? AP_BOSS_ENCOUNTER_ROW_FIELDS_2 : AP_BOSS_ENCOUNTER_ROW_FIELDS))
		return "row has unexpected fields";
	if (r->location != AP_BOSS_VENUE_LOCATION_BASE + g)
		return "wrong Boss Race check code";
	if (r->homeTrack != AP_BOSS_VENUE_GARAGE[g].homeLevel)
		return "wrong home track";
	if (!AP_BossVenueTrackKnown(r->track))
		return "unknown track";
	if (v2 && !r->trackOn && r->track != r->homeTrack)
		return "venue moved without the track switch";
	if (v2 && r->opponentOn)
	{
		if (AP_BossOpponentCharacterEx(r->opponent, r->oxideInPool) < 0)
			return "unknown opponent";
	}
	else if (!AP_BossVenueStrEq(r->opponent, AP_BOSS_VENUE_GARAGE[g].key))
		return "opponent swap without the opponent switch";
	// The physical garage's boss-grade driving table (BossDifficulty[bossID]).
	if (!AP_BossVenueStrEq(r->aiProfile, "retail"))
		return "AI profile is not supported by this build";
	if (v2 && r->kitOn)
	{
		if (!AP_BossVenueStrEq(r->attack, "composed"))
			return "kit switch on but the attack is not composed";
		if (r->scheduleCount < 0)
			return "schedule is not a list of six integers per segment";
		{
			const char *p = AP_BossKitScheduleProblem(r->schedule, r->scheduleCount,
			                                          AP_BOSS_VENUE_RESTART_POINTS[r->track]);
			int i, homeLast = AP_BOSS_VENUE_RESTART_POINTS[AP_BOSS_VENUE_GARAGE[g].homeLevel] - 1;
			if (p)
				return p;
			// The segments start where the garage's retail segments map to
			// on this venue, as generation composes them.
			for (i = 0; i < AP_BOSS_VENUE_SEGMENTS; i++)
				if (r->schedule[i].checkpoint !=
				    AP_BossVenueCheckpoint(AP_BOSS_VENUE_GARAGE[g].attack[i], homeLast,
				                           AP_BOSS_VENUE_RESTART_POINTS[r->track] - 1))
					return "schedule checkpoints are not the garage's mapped segments";
		}
	}
	else
	{
		if (!AP_BossVenueStrEq(r->attack, "inherited"))
			return "attack kit without the kit switch";
		if (v2 && r->scheduleCount != 0)
			return "inherited attack with a schedule";
	}
	if (!r->packagesEmpty)
		return "custom content pins are not supported by this build";
	if (!AP_BossVenueCertified(g, r->track))
		return "track cannot carry this boss's attack";
	return 0;
}

// J03: N. Oxide's garage record (`final`, both Oxide races), already
// type-checked by the JSON reader. scheduleCount -1 = not a valid list.
typedef struct
{
	int fieldCount;
	int locationsOk; // exactly [35011104, 35011105]
	const char *opponent;
	const char *attack;
	int scheduleCount;
	AP_BossKitSegment schedule[AP_BOSS_FINAL_SEGMENTS];
} AP_BossFinalRow;

#define AP_BOSS_FINAL_FIELDS 4

// NULL when the final record matches the switches: with boss_oxide_in_pool
// a drawn racer other than N. Oxide, else N. Oxide; composed exactly with
// oxide_attack_profile all_compatible.
static inline const char *AP_BossFinalRowProblem(const AP_BossFinalRow *r, int oxideInPool, int profileAll)
{
	int c;
	if (r == 0 || r->fieldCount != AP_BOSS_FINAL_FIELDS)
		return "final record has unexpected fields";
	if (!r->locationsOk)
		return "final record locations are not N. Oxide's two races";
	c = AP_BossOpponentCharacterEx(r->opponent, 1);
	if (c < 0)
		return "final record has an unknown opponent";
	if (oxideInPool ? c == AP_BOSS_OXIDE_CHARACTER : c != AP_BOSS_OXIDE_CHARACTER)
		return oxideInPool ? "final race must draw a racer with N. Oxide in the pool"
		                   : "final racer swapped without boss_oxide_in_pool";
	if (profileAll)
	{
		if (!AP_BossVenueStrEq(r->attack, "composed"))
			return "oxide_attack_profile all_compatible but the final attack is not composed";
		if (r->scheduleCount < 0)
			return "final schedule is not a list of six integers per segment";
		return AP_BossFinalScheduleProblem(r->schedule, r->scheduleCount);
	}
	if (!AP_BossVenueStrEq(r->attack, "inherited"))
		return "final attack composed without oxide_attack_profile all_compatible";
	if (r->scheduleCount != 0)
		return "inherited final attack with a schedule";
	return 0;
}

// J03: one Purple Gem Cup gauntlet leg, type-checked by the JSON reader.
typedef struct
{
	int fieldCount;
	int track; // -1 or a retail LevelID; -2 when not an integer
	const char *attacker;
	int scheduleCount;
	AP_BossKitSegment schedule[AP_BOSS_VENUE_SEGMENTS];
} AP_BossGauntletLeg;

#define AP_BOSS_GAUNTLET_LEG_FIELDS 3

// NULL when leg `i` is one this build can race: attacked by garage i's
// resolved racer (`attacker`, engine id), on a retail leg track with garage
// i's retail segment starts mapped onto it, or track -1 with no schedule.
static inline const char *AP_BossGauntletLegProblem(int i, const AP_BossGauntletLeg *l, int attacker,
                                                    int oxideInPool)
{
	int k, homeLast;
	if (i < 0 || i >= AP_BOSS_GAUNTLET_LEGS || l == 0 || l->fieldCount != AP_BOSS_GAUNTLET_LEG_FIELDS)
		return "gauntlet leg has unexpected fields";
	if (AP_BossOpponentCharacterEx(l->attacker, oxideInPool) != attacker)
		return "gauntlet attacker is not the garage's racer";
	if (l->track == -1)
		return l->scheduleCount == 0 ? 0 : "gauntlet leg without a track has a schedule";
	if (!AP_BossVenueCertified(i, l->track))
		return "gauntlet leg track cannot carry the attack";
	if (l->scheduleCount < 0)
		return "gauntlet schedule is not a list of six integers per segment";
	{
		const char *p = AP_BossKitScheduleProblem(l->schedule, l->scheduleCount,
		                                          AP_BOSS_VENUE_RESTART_POINTS[l->track]);
		if (p)
			return p;
	}
	homeLast = AP_BOSS_VENUE_RESTART_POINTS[AP_BOSS_VENUE_GARAGE[i].homeLevel] - 1;
	for (k = 0; k < AP_BOSS_VENUE_SEGMENTS; k++)
		if (l->schedule[k].checkpoint !=
		    AP_BossVenueCheckpoint(AP_BOSS_VENUE_GARAGE[i].attack[k], homeLast,
		                           AP_BOSS_VENUE_RESTART_POINTS[l->track] - 1))
			return "gauntlet checkpoints are not the garage's mapped segments";
	return 0;
}

// The engine id racing N. Oxide's garage when the seed drew one, or -1 for
// N. Oxide himself (no final record, profile-only record, or refused).
static inline int AP_BossEncounterFinalOpponent(const AP_BossEncounters *e)
{
	if (e == 0 || e->state != AP_BOSS_ENC_VALID || e->version != AP_BOSS_ENCOUNTERS_VERSION_2 ||
	    !e->finalPresent || e->finalOpponent == AP_BOSS_OXIDE_CHARACTER || e->finalOpponent < 0)
		return -1;
	return e->finalOpponent;
}

// The engine character id garage `g` races, or -1 for the retail opponent
// (no record, a tier 1 record, or a refused one).
static inline int AP_BossEncounterOpponent(const AP_BossEncounters *e, int g)
{
	if (e == 0 || g < 0 || g >= AP_BOSS_VENUE_GARAGES || e->state != AP_BOSS_ENC_VALID ||
	    e->version != AP_BOSS_ENCOUNTERS_VERSION_2)
		return -1;
	return e->opponent[g];
}

// The garage whose composed kit is running now, or -1: a garage boss race on
// that garage's recorded venue, never Oxide, a cup leg or an ordinary race.
// Unlike the inherited-attack adapter this applies on the home track too, and
// (J11) on a content-plan venue on custom bytes (a custom package or Cortex
// Vortex served on the home LevelID): its kit was composed for the venue's
// certified restart point count (points[g]), which
// AP_BossEncounterCertifiedPoints hands the decision 8 fit.
static inline int AP_BossEncounterKitGarage(const AP_BossEncounters *e, int advBoss, int advCup,
                                            int bossID, int level)
{
	if (!advBoss || advCup || e == 0 || e->state != AP_BOSS_ENC_VALID ||
	    e->version != AP_BOSS_ENCOUNTERS_VERSION_2)
		return -1;
	if (bossID < 0 || bossID >= AP_BOSS_VENUE_GARAGES || !e->composed[bossID])
		return -1;
	if (e->track[bossID] != level)
		return -1;
	return bossID;
}

// The models a garage boss race loads (LOAD_DriverMPK, boss branch): the
// time-trial pack of the garage's own boss, which carries everything the
// retail race needs, stays; a drawn opponent other than that boss and other
// than the player's racer is one extra hi model. Returns the pack character
// and writes the extra's character id (or -1) to *extra.
static inline int AP_BossOpponentModels(int garageBoss, int opponent, int player, int *extra)
{
	*extra = -1;
	if (opponent >= 0 && opponent != garageBoss && opponent != player)
		*extra = opponent;
	return garageBoss;
}

// The restart point count garage `g`'s venue was certified on: a custom
// venue's stated (and load-measured) count, else the retail table's.
static inline int AP_BossEncounterCertifiedPoints(const AP_BossEncounters *e, int g, int level)
{
	if (e != 0 && g >= 0 && g < AP_BOSS_VENUE_GARAGES && e->custom[g])
		return e->points[g];
	return AP_BossVenueTrackKnown(level) ? AP_BOSS_VENUE_RESTART_POINTS[level] : -1;
}

// The LevelID garage `g` loads, or -1 when the seed's record is refused.
static inline int AP_BossEncounterVenue(const AP_BossEncounters *e, int g)
{
	if (g < 0 || g >= AP_BOSS_VENUE_GARAGES)
		return -1;
	if (e == 0 || e->state == AP_BOSS_ENC_RETAIL)
		return AP_BOSS_VENUE_GARAGE[g].homeLevel;
	if (e->state != AP_BOSS_ENC_VALID)
		return -1;
	return e->track[g];
}

// The garage whose drawn venue is running now, or -1 for every other race
// (retail venue, Oxide, a cup leg, an ordinary race). Only a race on a venue
// other than the home track needs the inherited-attack adapter: on the home
// track the retail path is already exact.
static inline int AP_BossEncounterShuffledGarage(const AP_BossEncounters *e, int advBoss,
                                                 int advCup, int bossID, int level)
{
	if (!advBoss || advCup || e == 0 || e->state != AP_BOSS_ENC_VALID)
		return -1;
	if (bossID < 0 || bossID >= AP_BOSS_VENUE_GARAGES)
		return -1;
	if (e->track[bossID] != level)
		return -1;
	// Custom bytes on the home LevelID (#133 PR 6b) need the adapter too:
	// the geometry is not the home track's.
	if (level == AP_BOSS_VENUE_GARAGE[bossID].homeLevel && !e->custom[bossID])
		return -1;
	return bossID;
}

#endif
