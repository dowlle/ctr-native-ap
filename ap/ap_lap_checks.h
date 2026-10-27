#ifndef AP_LAP_CHECKS_H
#define AP_LAP_CHECKS_H
/* #344 lap completion checks: `<Track>: Complete One Lap`, one per track,
 * sent at the first accepted lap of any lap race on it (docs/LAP_CHECKS.md).
 * Pure C, so tools/test-lap-checks.cpp runs it without the engine.
 *
 * A normal room carries the `lap_checks` block (ap_lap_checks_parser.hpp):
 * the engine LevelIDs 0..17 and Cortex Vortex's 110, and the legacy custom
 * track's generic slot. A content plan carries its lap codes on its pads and
 * cup legs instead (ap_content_plan.h). */

#define AP_LAP_RETAIL_TRACKS 18
#define AP_LAP_RETAIL_CODE_BASE 35028000L
#define AP_LAP_CORTEX_CODE 35026009L
#define AP_LAP_CUSTOM_CODE_BASE 35028100L
#define AP_LAP_CORTEX_KEY 110

typedef struct AP_LapChecks {
    int enabled;
    long tracks[AP_LAP_RETAIL_TRACKS]; /* by engine LevelID, -1 absent */
    long cortex;                       /* Cortex Vortex, -1 absent */
    int custom_slot;                   /* the legacy custom track's slot, 0 none */
    long custom;                       /* its code, -1 absent */
} AP_LapChecks;

/* The frozen retail code of an engine race LevelID 0..17: the canonical
 * retail track order (apworld item_boxes.BOX_TRACKS, the Reach 10 Wumpa and
 * Sapphire Time Trial order) from 35028000. -1 for any other LevelID. */
static inline long AP_LapRetailCode(int levelID)
{
    static const int order[AP_LAP_RETAIL_TRACKS] = {7, 9, 8, 0, 5, 6, 1, 12, 3, 2, 13, 14, 10, 15, 4, 11, 16, 17};
    return levelID >= 0 && levelID < AP_LAP_RETAIL_TRACKS ? AP_LAP_RETAIL_CODE_BASE + order[levelID] : -1;
}

static inline void AP_LapChecksReset(AP_LapChecks *c)
{
    int i;
    c->enabled = 0;
    for (i = 0; i < AP_LAP_RETAIL_TRACKS; ++i) c->tracks[i] = -1;
    c->cortex = -1;
    c->custom_slot = 0;
    c->custom = -1;
}

/* Which race a lap identity names: a retail LevelID 0..17, Cortex Vortex, or
 * the legacy custom track's slot. */
#define AP_LAP_ID_NONE 0
#define AP_LAP_ID_RETAIL 1
#define AP_LAP_ID_CORTEX 2
#define AP_LAP_ID_CUSTOM 3

/* The seed's lap code for one identity, or -1 when the seed has none. */
static inline long AP_LapCodeFor(const AP_LapChecks *c, int kind, int value)
{
    if (!c || !c->enabled) return -1;
    if (kind == AP_LAP_ID_RETAIL) return value >= 0 && value < AP_LAP_RETAIL_TRACKS ? c->tracks[value] : -1;
    if (kind == AP_LAP_ID_CORTEX) return c->cortex;
    if (kind == AP_LAP_ID_CUSTOM) return value > 0 && value == c->custom_slot ? c->custom : -1;
    return -1;
}

/* Does this lap crossing earn the check? The local player's own kart (driver
 * slot 0, not a bot) completing a lap: lapIndex has just advanced to 1 or
 * more. `adventure` is 1 in an Adventure race with laps (Trophy Race, CTR
 * Challenge, Relic Race, Cup leg); boss races, Crystal Challenges, the hub,
 * Arcade and demo mode are 0. */
static inline int AP_LapCrossingCounts(int driverSlot, int isBot, int lapIndex, int adventure)
{
    return driverSlot == 0 && !isBot && lapIndex >= 1 && adventure;
}
#endif
