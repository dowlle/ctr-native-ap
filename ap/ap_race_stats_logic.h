#ifndef AP_RACE_STATS_LOGIC_H
#define AP_RACE_STATS_LOGIC_H

// ---------------------------------------------------------------------------
// Which kart stats a race drives with (ruling of 2026-11-02).
//
// Every Arcade race uses vanilla kart stats for now: retail Arcade in a room,
// the custom Arcade pages, Single Race, Arcade cups and Time Trial. Adventure
// keeps the room's stats. "The room's stats" are the stat source the seed sets
// (progressive or editable), Penta's PAL table and the starting racer's class
// override. Vanilla is what VehBirth_SetConsts writes from the racer's own
// engine class, so in an Arcade race nothing of the room is written over it.
//
// Not stats, so not changed here: Progressive Boost, traps, items and the
// rest of the room's rules.
//
// Freestanding on purpose: no engine or AP headers, so
// tools/test-race-stats.c compiles exactly the code production runs.
// ap_capability.c pins these values to the engine's enum.
// ---------------------------------------------------------------------------

#define AP_RS_TIME_TRIAL 0x20000u
#define AP_RS_ARCADE_MODE 0x400000u

// 1 when the room's kart stats apply to a race in this game mode (gameMode1),
// 0 when the race drives on vanilla stats.
static inline int AP_RaceStats_RoomApplies(unsigned int gameMode1)
{
	return (gameMode1 & (AP_RS_ARCADE_MODE | AP_RS_TIME_TRIAL)) == 0;
}

#endif
