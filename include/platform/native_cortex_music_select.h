#ifndef NATIVE_CORTEX_MUSIC_SELECT_H
#define NATIVE_CORTEX_MUSIC_SELECT_H

// Which races play Cortex Vortex's bundled music, freestanding.
//
// The .sca level bank goes on the SPU first (Music_AsyncParseBanks stage 0),
// so its admission check must know every bank stages 1 and 2 load after it.
// The answer is the layout of those later banks, copied term for term from
// stages 1 and 2 of game/HOWL/HOWL_Music.c:
//   BANK54      the 8-driver bank 54, then a character bank for each player
//               bank 54 does not carry: an Adventure race that is not a boss
//               race, arena, Crystal Challenge or Relic Race and not the
//               undisplaced Purple Gem Cup, and every Arcade race
//   PER_PLAYER  a character bank for every player: any other driving race
//               outside the undisplaced Purple Gem Cup (a Relic Race)
//   ROSTER5     the undisplaced Purple Gem Cup: five character banks, one per
//               roster slot
// OFF means Oxide Station's bank and song play as before: the race is not the
// Cortex Vortex race served on host level 13 (CustomTrack_CortexMusicServing).
// A boss race never asks: stage 0 gives it its boss bank before any track bank.

// Retail gameMode1 bits (namespace_Main.h; the harness checks they agree).
#define CORTEX_MUSIC_ADVENTURE_MODE 0x80000u
#define CORTEX_MUSIC_ARCADE_MODE    0x400000u
#define CORTEX_MUSIC_ADVENTURE_CUP  0x10000000u
// Boss race, Adventure Arena, Crystal Challenge, Relic Race: stage 1's mask.
#define CORTEX_MUSIC_NOT_8_DRIVERS  0x8c100000u
#define CORTEX_MUSIC_PURPLE_CUP_ID  4

enum CortexMusicBanks
{
	CORTEX_MUSIC_OFF = 0,
	CORTEX_MUSIC_BANK54,
	CORTEX_MUSIC_PER_PLAYER,
	CORTEX_MUSIC_ROSTER5,
};

// cupDisplaced: CustomTrack_ServingLoad for this level and cup, the term that
// lets a displaced Purple Gem Cup load bank 54. A served Cortex Vortex race has
// it set, so ROSTER5 is here for parity with stage 2 only.
static inline enum CortexMusicBanks CortexMusic_Banks(int cortexServing, unsigned int gameMode1, int cupID,
                                                      int cupDisplaced)
{
	int adventure = (gameMode1 & CORTEX_MUSIC_ADVENTURE_MODE) != 0;
	int purpleRoster = adventure && (gameMode1 & CORTEX_MUSIC_ADVENTURE_CUP) != 0 &&
	                   cupID == CORTEX_MUSIC_PURPLE_CUP_ID && !cupDisplaced;

	if (!cortexServing)
		return CORTEX_MUSIC_OFF;
	if ((adventure && (gameMode1 & CORTEX_MUSIC_NOT_8_DRIVERS) == 0 && !purpleRoster) ||
	    (gameMode1 & CORTEX_MUSIC_ARCADE_MODE) != 0)
		return CORTEX_MUSIC_BANK54;
	return purpleRoster ? CORTEX_MUSIC_ROSTER5 : CORTEX_MUSIC_PER_PLAYER;
}

#endif // NATIVE_CORTEX_MUSIC_SELECT_H
