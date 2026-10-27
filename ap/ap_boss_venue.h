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

#define AP_BOSS_KIT_COOLDOWN_MIN 2
#define AP_BOSS_KIT_COOLDOWN_MAX 0xff

static inline int AP_BossKitWeaponKnown(int w)
{
	return w == 0x01 || w == 0x02 || w == 0x03 || w == 0x04 || w == 0x0f || w == 0x64 || w == 0x66;
}

// Every (weapon, throw, path, juice) a composed segment may carry: the
// certified family variants and the four retail signature rows. The
// apworld's boss_encounters.CERTIFIED_ROWS is the twin (shared fixture).
#define AP_BOSS_KIT_CERTIFIED_ROWS 9
static const unsigned char AP_BOSS_KIT_CERTIFIED[AP_BOSS_KIT_CERTIFIED_ROWS][4] = {
	{0x01, 3, 1, 0}, {0x02, 3, 1, 0}, {0x03, 3, 0, 0}, {0x04, 2, 1, 1}, {0x0f, 2, 0, 2},
	{0x0f, 2, 1, 2}, {0x0f, 3, 1, 0}, {0x64, 2, 0, 2}, {0x66, 2, 1, 2},
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
// `venuePoints` restart points: five segments, the first at checkpoint 0,
// strictly increasing inside the venue, every field a known code.
static inline const char *AP_BossKitScheduleProblem(const AP_BossKitSegment *seg, int n, int venuePoints)
{
	int i, prev = -1, attacks = 0;
	if (seg == 0 || n != AP_BOSS_VENUE_SEGMENTS)
		return "schedule must have five segments";
	for (i = 0; i < n; i++)
	{
		const AP_BossKitSegment *s = &seg[i];
		if ((int)s->checkpoint <= prev || (int)s->checkpoint >= venuePoints || (prev < 0 && s->checkpoint != 0))
			return "schedule checkpoints do not fit the venue";
		if (!AP_BossKitRowCertified(s->weapon, s->throwFlag, s->pathDisabled, s->juice) ||
		    s->cooldown < AP_BOSS_KIT_COOLDOWN_MIN || s->cooldown > AP_BOSS_KIT_COOLDOWN_MAX)
			return "schedule has a code this build cannot race";
		if (s->weapon != 0x0f)
			attacks++;
		prev = s->checkpoint;
	}
	if (attacks == 0)
		return "schedule never attacks";
	return 0;
}

// The engine character id of wire opponent `key`, or -1.
static inline int AP_BossOpponentCharacter(const char *key);

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
		if (AP_BossOpponentCharacter(r->opponent) < 0)
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
// Unlike the inherited-attack adapter this applies on the home track too.
static inline int AP_BossEncounterKitGarage(const AP_BossEncounters *e, int advBoss, int advCup,
                                            int bossID, int level)
{
	if (!advBoss || advCup || e == 0 || e->state != AP_BOSS_ENC_VALID ||
	    e->version != AP_BOSS_ENCOUNTERS_VERSION_2)
		return -1;
	if (bossID < 0 || bossID >= AP_BOSS_VENUE_GARAGES || !e->composed[bossID])
		return -1;
	if (e->track[bossID] != level || e->custom[bossID])
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
