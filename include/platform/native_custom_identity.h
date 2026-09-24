#ifndef NATIVE_CUSTOM_IDENTITY_H
#define NATIVE_CUSTOM_IDENTITY_H

// Custom-track identity for the offline Arcade custom race (box authoring
// build, CTR_CUSTOM_PACKAGES). Freestanding: C99 only, so
// tools/test-custom-identity.c checks exactly what the engine uses.
//
// THE RULE. A custom track has its own identity and never borrows a retail
// track's. The engine still reads the package's LEV and VRM through one arcade
// BIGFILE subfile group (the host slot, see MM_CustomTrackSelect.c), because
// the BIGFILE index space cannot grow and every race-mode rule in the engine is
// a range test on levelID. That host slot is a loading mechanism only. Every
// identity-keyed read the race reaches goes through this header instead of
// through gGT->levelID:
//
//   level id for logs, state and range gates   CTR_CUSTOM_LEVEL_ID (0x80)
//   race-start banner                           the package title
//   AI difficulty                               CTR_CUSTOM_DIFFICULTY_* below
//   music (song) and level sound-effect bank    CTR_CUSTOM_SONG_BANK / _FX_BANK
//   reverb                                      the engine default for a level
//                                               with no reverb row (0x80 >= 30)
//   hardcoded track ambience, Roo bubbles       none (0x80 matches no track)
//   high-score pointer                          a private empty table
//   AI recordings                               blocked (no recording identity)
//   box placements                              package UUID + LEV/VRM SHA-256
//
// The audit of every levelID-indexed read and write a custom race reaches, and
// how each is handled, is in the pull request that added this header
// (dowlle/ctr-native-ap #405). tools/test-custom-identity.c pins the values.

#define CTR_CUSTOM_LEVEL_ID 0x80 // above all 65 retail LevelIDs (0..64), as LinksLab uses 128+

// Retail thresholds the custom id must clear, mirrored so this header stays
// engine-free; tools/test-custom-identity.c checks them against the enum.
#define CTR_CUSTOM_RETAIL_LEVEL_COUNT 65  // enum LevelID: DINGO_CANYON (0) .. SCRAPBOOK (64)
#define CTR_CUSTOM_RETAIL_TRACK_COUNT 18  // arcade tracks with a BIGFILE group, difficulty row, high scores
#define CTR_CUSTOM_AMBIENT_LEVEL_LIMIT 0x19 // Level_AmbientSound returns for levelID >= 0x19
#define CTR_CUSTOM_REVERB_LEVEL_LIMIT 30    // INTRO_RACE_TODAY: reverbMode[] only below this

// The identity-facing level id: the custom id during a custom race, otherwise
// the engine's own level id.
static inline int CustomIdentity_LevelID(int engineLevelID, int customRace)
{
	return customRace ? CTR_CUSTOM_LEVEL_ID : engineLevelID;
}

// Music and level sound effects. Explicit defaults, the same for every custom
// track and independent of the host slot: Crash Cove's song (bank index 3) and
// Crash Cove's level SFX bank (index 4). A package does not carry music yet;
// when it does, this is where it plugs in.
#define CTR_CUSTOM_SONG_BANK 0x03
#define CTR_CUSTOM_FX_BANK 0x04

// AI difficulty. Explicit, independent of the host slot: for each of the 14
// parameters of params1 and params2, the median (lower middle) of that
// parameter over the 18 retail arcade rows in data.ArcadeDifficulty
// (game/zGlobal_DATA.c). It is the typical retail tuning, not any one track's;
// the menu's arcade difficulty then blends it exactly as for a retail track.
#define CTR_CUSTOM_DIFFICULTY_PARAMS1 \
	{640, 768, 3840, 5888, 7936, 9984, 12032, 16128, 1100, -1100, 0, -4000, -1500, -400}
#define CTR_CUSTOM_DIFFICULTY_PARAMS2 \
	{-2048, -1024, -768, 1920, 3840, 6388, 8936, 12032, 3700, 1200, -500, -6000, -2000, -1000}

// Lap count for an offline custom race. A pinned race_settings value wins when
// the package has one; a package without race_settings races the default; an
// invalid sidecar, zero laps, or more laps than the engine can time refuses.
// Returns 1 with *laps set, or 0 with *laps = 0.
#define CTR_CUSTOM_DEFAULT_LAPS 3
#define CTR_CUSTOM_MAX_LAPS 7 // GameTracker.lapTime[7]
static inline int CustomIdentity_RaceLaps(int hasSettings, int settingsValid, unsigned int settingsLaps,
                                          unsigned int *laps)
{
	unsigned int chosen;

	if (laps == 0)
		return 0;
	*laps = 0;
	if (!hasSettings)
		chosen = CTR_CUSTOM_DEFAULT_LAPS;
	else if (!settingsValid)
		return 0;
	else
		chosen = settingsLaps;
	if (chosen < 1 || chosen > CTR_CUSTOM_MAX_LAPS)
		return 0;
	*laps = chosen;
	return 1;
}

// The package title as the race-start banner can draw it: the big font has
// printable ASCII upper case only, so lower case is raised, anything else
// (including each whole UTF-8 sequence) becomes '?', and the result is cut to
// cap-1 bytes. An empty result reads "CUSTOM TRACK".
static inline void CustomIdentity_BannerName(char *dst, unsigned long cap, const char *title)
{
	unsigned long n = 0;
	const char *fallback = "CUSTOM TRACK";

	if (dst == 0 || cap == 0)
		return;
	for (; title != 0 && *title != '\0' && n + 1 < cap; title++)
	{
		unsigned char c = (unsigned char)*title;
		if ((c & 0xC0) == 0x80)
			continue;
		if (c >= 'a' && c <= 'z')
			c = (unsigned char)(c - 'a' + 'A');
		dst[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
	}
	dst[n] = '\0';
	if (n == 0)
	{
		for (; fallback[n] != '\0' && n + 1 < cap; n++)
			dst[n] = fallback[n];
		dst[n] = '\0';
	}
}

#endif // NATIVE_CUSTOM_IDENTITY_H
