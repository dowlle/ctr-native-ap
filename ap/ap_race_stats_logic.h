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
// The same Adventure/event boundary also isolates applied boost, terrain
// items and received-item processing outside Adventure (alpha2).
//
// Freestanding on purpose: no engine or AP headers, so
// tools/test-race-stats.c compiles exactly the code production runs.
// ap_capability.c pins these values to the engine's enum.
// ---------------------------------------------------------------------------

#define AP_RS_BATTLE_MODE 0x20u
#define AP_RS_TIME_TRIAL 0x20000u
#define AP_RS_ADVENTURE_MODE 0x80000u
#define AP_RS_ADVENTURE_ARENA 0x100000u
#define AP_RS_ARCADE_MODE 0x400000u
#define AP_RS_RELIC_RACE 0x4000000u
#define AP_RS_CRYSTAL_CHALLENGE 0x8000000u
#define AP_RS_ADVENTURE_CUP 0x10000000u
#define AP_RS_ADVENTURE_BOSS 0x80000000u

// The modes that are never Adventure, and the bits only Adventure sets. A
// Relic Race on the AP map can run without ADVENTURE_MODE (ap_boxes.c,
// AP_BoxesRaceCarriesBoxes, live test of 2026-08-12), so the other Adventure
// bits count too, but only in a 1P race: quitting an event to the title can
// leave its bit set (MM_MenuFlow.c does not clear RELIC_RACE), and Versus,
// which has no mode bit of its own, is always 2 to 4 players. An explicit
// Arcade, Time Trial or Battle bit wins over any stale Adventure bit.
#define AP_RS_NOT_ADVENTURE (AP_RS_ARCADE_MODE | AP_RS_TIME_TRIAL | AP_RS_BATTLE_MODE)
#define AP_RS_ADVENTURE_EVENT_BITS                                                                       \
	(AP_RS_ADVENTURE_ARENA | AP_RS_RELIC_RACE | AP_RS_CRYSTAL_CHALLENGE | AP_RS_ADVENTURE_CUP |       \
	 AP_RS_ADVENTURE_BOSS)

// 1 when the room's kart stats apply to a race in this game mode (gameMode1)
// with this many human players (numPlyrCurrGame), 0 when the race drives on
// vanilla stats.
static inline int AP_RaceStats_RoomApplies(unsigned int gameMode1, int players)
{
	if ((gameMode1 & AP_RS_NOT_ADVENTURE) != 0)
		return 0;
	if ((gameMode1 & AP_RS_ADVENTURE_MODE) != 0)
		return 1;
	return players <= 1 && (gameMode1 & AP_RS_ADVENTURE_EVENT_BITS) != 0;
}

#endif
