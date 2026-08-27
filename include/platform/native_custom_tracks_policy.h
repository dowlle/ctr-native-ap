#ifndef NATIVE_CUSTOM_TRACKS_POLICY_H
#define NATIVE_CUSTOM_TRACKS_POLICY_H

// Custom-track loader DECISIONS, ruled for the Baby T Park event spike (rung 1).
// Deliberately freestanding, exactly like ap_cup_box_policy.h: the gather and
// the I/O live in engine (platform/native_custom_tracks.c), the decisions live
// here so tools/test-custom-track-policy.c can pin the whole truth table out of
// engine, with no disc, no display, no config file and no seed.
//
// THREE DECISIONS LIVE HERE.
//
// 1. PAIR AUTO-EXPAND. A retail arcade track occupies a contiguous group of 8
//    BIGFILE subfiles at [levelID*8, levelID*8 + 8) because BI_ARCADETRACKS is
//    0. The group is four (vrm, lev) mode-pairs: even slots 0/2/4/6 are VRMs,
//    odd slots 1/3/5/7 are the per-mode LEVs (1P, 2P, 4P, relic). A community
//    custom track ships exactly ONE .lev and ONE .vrm, so the loader expands
//    that single pair across all four mode slots in memory rather than making
//    the packager write eight files. A 1P race reads only slots 0 and 1, so the
//    expansion costs nothing on the path the spike actually exercises; slots
//    2..7 exist so a 2P/4P/relic entry serves plausible bytes instead of the
//    retail track's, which would be a silent content swap mid-group.
//
// 2. CONTENT VERDICT. Serving a track subfile is serving unverified bytes into
//    the engine's load path. Every source file is hashed and compared against
//    the digest the feature config names BEFORE the first byte is served, and
//    any verdict other than OK refuses the whole track. The verdict enum is
//    ordered by how loudly it should read in a log, not by severity.
//
// 3. PURPLE-DESTINATION REDIRECT. Ruled 2026-08-28: when the spike feature is
//    enabled, the seed's Purple Gem Cup destination becomes a full race-track
//    destination running the custom track -- a SINGLE race, 7 laps, AI on, and
//    winning it awards the Purple Gem through the cup's own award path. The
//    redirect is expressed here as two pure answers -- "does this cup entry
//    become a single race" and "is this leg the cup's last" -- because those
//    are the two forks the engine takes, in two different files, and they must
//    not be able to disagree. They are computed from the same inputs, so a
//    cup that redirected at the pad cannot fail to complete at the results
//    screen.
//
// WHY THE LOADER-READY TERM IS LOAD-BEARING. ShouldRedirectCup requires
// contentVerified. A track whose hash did not match, or whose file is missing,
// must not merely fall back to the retail bytes for the LEV -- it must also
// leave the Purple Gem Cup as its vanilla four legs, because a 7-lap single
// race on the RETAIL contents of the mapped slot is exactly the "silent wrong
// content" outcome the hash check exists to prevent. Refusing the load and
// keeping the cup is the only coherent pair of answers.
//
// RUNG-2 SEAM. Every field of CustomTrackFeatureConfig is filled from
// config.ini today (platform/native_custom_tracks.c, CustomTrack_Load). A later
// rung fills the same struct from slot_data instead; nothing in this header or
// in any engine call site changes when it does. That is the whole reason the
// config is a struct passed to pure functions rather than a set of globals read
// at each fork.
//
// Compiled ONLY when CTR_CUSTOM_TRACKS is defined, like the rest of the loader.

#ifdef CTR_CUSTOM_TRACKS

// Arcade tracks are levelID 0..17 (NITRO_COURT == 18 is the first battle
// arena). The loader refuses anything outside that range for two independent
// reasons beyond the BIGFILE grouping: data.ArcadeDifficulty is
// struct Difficulty[18] and is indexed by gGT->levelID with no range check
// (game/BOTS.c, BOTS_Adv_AdjustDifficulty), and data.metaDataLEV[0x41] must
// have a real entry for the slot or the results and HUD paths read garbage.
// A custom track therefore always takes over an existing arcade slot.
#define CTR_CT_MAX_LEVELS  18
#define CTR_CT_GROUP_SIZE  8

// Which half of a mode-pair a BIGFILE subfile slot is.
enum CustomTrackSubfileRole
{
	CTR_CT_ROLE_NONE = 0, // not part of a mapped track's group
	CTR_CT_ROLE_VRM = 1,  // even slot: the track's VRAM/texture payload
	CTR_CT_ROLE_LEV = 2   // odd slot: the track's level payload
};

// Why a track's source file was accepted or refused.
enum CustomTrackVerdict
{
	CTR_CT_VERDICT_OK = 0,             // hashed and matched the expected digest
	CTR_CT_VERDICT_NO_PATH = 1,        // feature config named no file for this role
	CTR_CT_VERDICT_FILE_MISSING = 2,   // named a file that does not exist or is empty
	CTR_CT_VERDICT_NO_EXPECTED = 3,    // named a file but no expected digest for it
	CTR_CT_VERDICT_BAD_EXPECTED = 4,   // expected digest is not 64 hex digits
	CTR_CT_VERDICT_READ_FAILED = 5,    // file existed but could not be read whole
	CTR_CT_VERDICT_HASH_MISMATCH = 6   // read fine, contents are not what was promised
};

// The spike's whole runtime configuration, in one struct. Filled from
// config.ini today and from slot_data in a later rung.
struct CustomTrackFeatureConfig
{
	// --- loader ---
	int mappedLevelID; // arcade slot the custom track takes over, or -1 for none
	int contentVerified; // 1 once BOTH source files hashed OK

	// --- Purple-destination-as-race seam ---
	int raceEnabled;  // runtime flag: is the event destination active at all
	int raceCupID;    // cup whose destination is replaced (4 == Purple Gem Cup)
	int raceLaps;     // lap count for the single race (7, per the ruling)
};

// A subfile index's role within a mapped track's group, or CTR_CT_ROLE_NONE if
// the index does not belong to the mapped track. This is the pair auto-expand:
// slot parity alone decides which of the two source files answers, so one .lev
// and one .vrm cover all four mode-pairs.
static int CustomTrackPolicy_SubfileRole(int subfileIndex, int mappedLevelID)
{
	int base;
	int slot;

	if (subfileIndex < 0)
		return CTR_CT_ROLE_NONE;

	if (mappedLevelID < 0 || mappedLevelID >= CTR_CT_MAX_LEVELS)
		return CTR_CT_ROLE_NONE;

	base = mappedLevelID * CTR_CT_GROUP_SIZE;
	slot = subfileIndex - base;

	if (slot < 0 || slot >= CTR_CT_GROUP_SIZE)
		return CTR_CT_ROLE_NONE;

	return (slot & 1) ? CTR_CT_ROLE_LEV : CTR_CT_ROLE_VRM;
}

// Is a levelID one the loader is allowed to take over? See the range note above
// CTR_CT_MAX_LEVELS: this is not a stylistic bound, it is the point past which
// data.ArcadeDifficulty and data.metaDataLEV stop having an entry.
static int CustomTrackPolicy_LevelIDIsMappable(int levelID)
{
	return (levelID >= 0 && levelID < CTR_CT_MAX_LEVELS) ? 1 : 0;
}

// Does entering cup `cupID` become a single race on the custom track?
//
// Every term is independently load-bearing:
//   raceEnabled     -- the runtime flag; off means vanilla, in every build.
//   contentVerified -- see "WHY THE LOADER-READY TERM IS LOAD-BEARING" above.
//   mappedLevelID   -- a redirect with no mapped slot has nowhere to send the
//                      player; refusing here is what keeps the two engine forks
//                      from disagreeing when config is half-filled.
//   isAdventureCup  -- gGT->cup.cupID is reused by BOTH cup families: adventure
//                      gem cups 0..4 and arcade/VS cups 0..3 (data.ArcadeCups is
//                      [4]). Only the adventure family has a gem to award, and
//                      raceCupID is configurable, so without this term a config
//                      naming cup 2 would also cut Arcade Cup 2 down to one
//                      race. The caller passes (gGT->gameMode2 & CUP_ANY_KIND)
//                      == 0.
//   cupID match     -- only the configured cup's destination is replaced.
static int CustomTrackPolicy_ShouldRedirectCup(const struct CustomTrackFeatureConfig *cfg, int cupID, int isAdventureCup)
{
	if (cfg == NULL)
		return 0;

	if (!cfg->raceEnabled)
		return 0;

	if (!cfg->contentVerified)
		return 0;

	if (!isAdventureCup)
		return 0;

	if (!CustomTrackPolicy_LevelIDIsMappable(cfg->mappedLevelID))
		return 0;

	return (cupID == cfg->raceCupID) ? 1 : 0;
}

// The levelID a redirected cup entry loads. Callers must have asked
// ShouldRedirectCup first; this returns -1 rather than a plausible-looking
// levelID when they did not, so a missed guard fails loudly at the load rather
// than quietly racing slot 0.
static int CustomTrackPolicy_RaceLevelID(const struct CustomTrackFeatureConfig *cfg, int cupID, int isAdventureCup)
{
	if (!CustomTrackPolicy_ShouldRedirectCup(cfg, cupID, isAdventureCup))
		return -1;

	return cfg->mappedLevelID;
}

// The lap count a redirected cup entry races. Returns 0 when the redirect is
// not active, so the caller writing gGT->numLaps can treat 0 as "do not write".
// gGT->numLaps is a char, and the retail lap ladder (D230.lapRowVal) tops out
// at 7, so anything outside 1..7 is refused rather than truncated into the
// field.
static int CustomTrackPolicy_RaceLaps(const struct CustomTrackFeatureConfig *cfg, int cupID, int isAdventureCup)
{
	if (!CustomTrackPolicy_ShouldRedirectCup(cfg, cupID, isAdventureCup))
		return 0;

	if (cfg->raceLaps < 1 || cfg->raceLaps > 7)
		return 0;

	return cfg->raceLaps;
}

// Has the cup finished, given the leg index AFTER UI_CupStandings' increment?
//
// Vanilla answers "trackIndex >= 4": four legs, then the gem. A redirected cup
// ran exactly one race, so it is finished at trackIndex 1. Expressing both
// answers in one function is what guarantees the results screen cannot decide
// to load leg 1 of a cup the warp pad turned into a single race -- the fork at
// game/UI/UI_CupStandings.c and the fork at game/232/AH_WarpPad.c read the same
// config through the same predicate.
static int CustomTrackPolicy_CupIsComplete(const struct CustomTrackFeatureConfig *cfg, int cupID, int isAdventureCup, int trackIndexAfterIncrement)
{
	if (CustomTrackPolicy_ShouldRedirectCup(cfg, cupID, isAdventureCup))
		return (trackIndexAfterIncrement >= 1) ? 1 : 0;

	return (trackIndexAfterIncrement >= 4) ? 1 : 0;
}

#endif // CTR_CUSTOM_TRACKS

#endif // NATIVE_CUSTOM_TRACKS_POLICY_H
