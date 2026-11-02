#ifndef AP_RACE_STATS_LOGIC_H
#define AP_RACE_STATS_LOGIC_H

// ---------------------------------------------------------------------------
// Which kart stats a race drives with (ruling of 2026-11-02).
//
// Only Adventure keeps the room's kart stats: hub races, cups, bosses, Relic
// Races, Crystal Challenges, everything on the AP map. Every other mode drives
// vanilla stats, in and out of a room: Arcade (retail, the custom pages,
// Single Race, Arcade cups), Time Trial, Versus and Battle (ruling of 2026-11-02).
// "The room's stats" are the stat source the seed sets (progressive or
// editable), Penta's PAL table and the starting racer's class override.
// Vanilla is what VehBirth_SetConsts writes from the racer's own engine class,
// so outside Adventure nothing of the room is written over it.
//
// Not stats, so not changed here: Progressive Boost, traps, items and the
// rest of the room's rules.
//
// Freestanding on purpose: no engine or AP headers, so
// tools/test-race-stats.c compiles exactly the code production runs.
// ap_capability.c pins these values to the engine's enum.
// ---------------------------------------------------------------------------

#define AP_RS_ADVENTURE_MODE 0x80000u

// 1 when the room's kart stats apply to a race in this game mode (gameMode1),
// 0 when the race drives on vanilla stats. Versus has no mode bit of its own,
// so the rule names the one mode that keeps the room's stats, not the others.
static inline int AP_RaceStats_RoomApplies(unsigned int gameMode1)
{
	return (gameMode1 & AP_RS_ADVENTURE_MODE) != 0;
}

#endif
