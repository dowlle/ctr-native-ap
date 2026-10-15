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
} AP_BossVenueGarage;

static const AP_BossVenueGarage AP_BOSS_VENUE_GARAGE[AP_BOSS_VENUE_GARAGES] = {
	{"ripper_roo", 6, {0x00, 0x10, 0x21, 0x2f, 0x46}}, // data.BossWeaponRoo
	{"papu_papu", 5, {0x00, 0x0f, 0x22, 0x53, 0x5d}},  // data.BossWeaponPapu
	{"komodo_joe", 1, {0x00, 0x10, 0x2a, 0x39, 0x46}}, // data.BossWeaponJoe
	{"pinstripe", 7, {0x00, 0x10, 0x21, 0x2f, 0x46}},  // data.BossWeaponPinstripe
};

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
} AP_BossEncounterRow;

#define AP_BOSS_ENCOUNTER_ROW_FIELDS 7

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

// NULL when garage `g`'s row is one this build can race, else the reason.
static inline const char *AP_BossEncounterRowProblem(int g, const AP_BossEncounterRow *r)
{
	if (g < 0 || g >= AP_BOSS_VENUE_GARAGES || r == 0)
		return "unknown garage";
	if (r->fieldCount != AP_BOSS_ENCOUNTER_ROW_FIELDS)
		return "row has unexpected fields";
	if (r->location != AP_BOSS_VENUE_LOCATION_BASE + g)
		return "wrong Boss Race check code";
	if (r->homeTrack != AP_BOSS_VENUE_GARAGE[g].homeLevel)
		return "wrong home track";
	if (!AP_BossVenueTrackKnown(r->track))
		return "unknown track";
	if (!AP_BossVenueStrEq(r->opponent, AP_BOSS_VENUE_GARAGE[g].key))
		return "opponent swap is not supported by this build";
	if (!AP_BossVenueStrEq(r->aiProfile, "retail"))
		return "AI profile is not supported by this build";
	if (!AP_BossVenueStrEq(r->attack, "inherited"))
		return "attack kit is not supported by this build";
	if (!r->packagesEmpty)
		return "custom content pins are not supported by this build";
	if (!AP_BossVenueCertified(g, r->track))
		return "track cannot carry this boss's attack";
	return 0;
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
	if (e->track[bossID] != level || level == AP_BOSS_VENUE_GARAGE[bossID].homeLevel)
		return -1;
	return bossID;
}

#endif
