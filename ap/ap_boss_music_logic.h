#ifndef AP_BOSS_MUSIC_LOGIC_H
#define AP_BOSS_MUSIC_LOGIC_H

// A4-61: the Boss Music client option (config.ini [Archipelago] boss_music),
// asked for by Dex on Discord (2026-11-11): in boss cups, an in-game OPTIONS
// row to hear the regular track music instead of the boss theme.
//
// Freestanding: no engine types, no allocation, no IO. The consumer is
// Music_AsyncParseBanks (game/HOWL/HOWL_Music.c), which decides per level load
// whether a boss race takes the boss banks; tools/test-boss-music.c pins the
// decision on the host.
//
// THE OPTION
// ----------
// A local client option, never slot_data: two players in one room can choose
// differently and the seed's logic is untouched. 0 = BOSS THEME (the default,
// the behaviour every build had before), 1 = TRACK. A hand-edited value outside
// that pair falls back to the default, like the other enum rows.
//
// THE DECISION
// ------------
// A boss race's music is loaded by Music_AsyncParseBanks stages 0 (the level
// FX bank) and 3 (the song). Retail gives every boss race the boss's FX bank
// (sdata->songBankBossID) and the shared boss song (HOWL_SONG_BOSS_RACE). That
// branch tests IS_BOSS_RACE, the ADVENTURE_BOSS bit of gameMode1, which a
// single boss race and every leg of a boss cup (#133 PR 6b) both carry, so one
// decision covers both. With TRACK, the load takes the ordinary track branch
// instead: the venue's own retail bank and song, or the served custom track's
// .sca bank and song (HOWL_CustomMusic.c), which that branch already owns.
// The venue is gGT->levelID: the boss's home track, or the seed's shuffled
// venue for a #169 garage.

enum
{
	AP_BOSS_MUSIC_BOSS_THEME = 0,
	AP_BOSS_MUSIC_TRACK = 1
};

// 1 when this load takes the boss banks: a boss race whose player left Boss
// Music on BOSS THEME. isBossRace is IS_BOSS_RACE(gameMode1) at the call site;
// a non-boss race never takes the boss banks, whatever the option says. Any
// value that is not TRACK is the default, so an out-of-range config edit keeps
// the behaviour the player had before.
static inline int AP_BossMusic_BossBanks(int isBossRace, int option)
{
	return isBossRace && option != AP_BOSS_MUSIC_TRACK;
}

#endif
