#ifndef NATIVE_CUSTOM_TRACKS_H
#define NATIVE_CUSTOM_TRACKS_H

#ifdef CTR_CUSTOM_TRACKS

#include <common.h>
#include <platform/native_custom_tracks_policy.h>

// Custom-track loader, engine-facing half (Baby T Park event spike, rung 1).
// The decisions this file's implementation asks are all in
// native_custom_tracks_policy.h; what lives here is the config parse, the
// content verification and the byte serving -- everything that needs a
// filesystem and therefore cannot be unit-tested out of engine.
//
// THE MODEL. One community custom track ships one .lev and one .vrm. The
// feature config names both files, the SHA-256 each must hash to, and the
// arcade levelID the track takes over. On startup the loader hashes both files
// and either arms itself or refuses. Once armed, every BIGFILE read whose
// subfile index falls inside that levelID's 8-subfile group is served from the
// matching source file instead of BIGFILE.BIG, with the single (vrm, lev) pair
// expanded across all four mode slots (see the policy header).
//
// It is byte-for-byte serving: no .lev/.vrm parsing. The custom track's payload
// carries its own well-formed retail pointer map, so the generic fixup
// (LOAD_RunPtrMap) and the VRAM upload downstream of the read need no changes.
//
// REFUSING IS LOUD AND TOTAL. A missing file, an absent or malformed expected
// digest, or a hash mismatch all leave contentVerified at 0. That does two
// things at once: track reads fall back to the retail BIGFILE bytes, AND the
// Purple-destination redirect stays off, so the event destination keeps its
// vanilla four legs rather than racing 7 laps on whatever the retail slot holds.
// Serving the wrong content silently is the one outcome this loader must never
// produce.
//
// With CTR_CUSTOM_TRACKS undefined the whole translation unit expands to
// nothing and every call site above is compiled out, so a guard-off build is
// identical to main.

// Parse the [CustomTracks] section of config.ini and verify the configured
// track's content, once, at startup. Safe to call when config.ini is absent or
// has no [CustomTracks] section: the loader simply stays disarmed and the build
// behaves like retail. Idempotent.
void CustomTrack_Load(void);

// The parsed feature config, never NULL. Callers pass it to the pure decisions
// in native_custom_tracks_policy.h. Calls CustomTrack_Load if it has not run.
const struct CustomTrackFeatureConfig *CustomTrack_Config(void);

// If subfileIndex belongs to the mapped track's group, the loader is armed, AND
// the load in flight is the event race (ctx), return 1 and set *outPath to the
// source file serving that slot (an internal static buffer, valid until the next
// CustomTrack_GetOverride call) and *outSize to its byte size.
//
// The ctx term is what keeps the host slot's RETAIL race retail. The custom
// track takes over an arcade slot, but the ruling replaces one destination, not
// the slot: a race pad to the host track must load BIGFILE bytes in the very
// same session where the event cup loads custom bytes. Callers gather ctx from
// gGT; see the policy header's decision 4 for why those facts are always
// committed before the first subfile read of a level.
//
// Returns 0 for every other index, and for any index in the group whose file has
// changed size since it was verified -- a file swapped under a running game is
// refused for that read rather than served unverified.
int CustomTrack_GetOverride(int subfileIndex, const struct CustomTrackLoadContext *ctx, const char **outPath, u32 *outSize);

// Fill dst with the source file's bytes, zero-padding the tail out to bufBytes
// (the sector-rounded buffer size the CD path allocated). fileBytes is the size
// CustomTrack_GetOverride reported. Returns 1 on success, 0 on any I/O error.
int CustomTrack_ReadFile(const char *path, void *dst, u32 bufBytes, u32 fileBytes);

// --- Purple-destination-as-race seam -------------------------------------
//
// Thin wrappers over the policy header so the three engine call sites
// (AH_WarpPad.c, UI_CupStandings.c, MainFreeze.c) read the same config through
// the same predicate and cannot disagree about whether a cup was redirected.
//
// isAdventureCup is (gGT->gameMode2 & CUP_ANY_KIND) == 0: gGT->cup.cupID is
// shared between adventure gem cups and arcade/VS cups, and only the adventure
// family has a gem to award. See the policy header for why the term is
// load-bearing rather than defensive.

// Does entering cup `cupID` become a single race on the custom track?
int CustomTrack_CupRaceRedirectActive(int cupID, int isAdventureCup);

// The levelID a redirected cup entry loads, or -1 when not redirected.
int CustomTrack_CupRaceLevelID(int cupID, int isAdventureCup);

// The lap count a redirected cup entry races, or 0 when not redirected.
int CustomTrack_CupRaceLaps(int cupID, int isAdventureCup);

// Has the cup finished, given the leg index AFTER UI_CupStandings' increment?
// Answers 4 legs for a vanilla cup and 1 for a redirected one.
int CustomTrack_CupIsComplete(int cupID, int isAdventureCup, int trackIndexAfterIncrement);

// How many legs the HUD's "TRACK n/N" counter should name: 1 for a redirected
// cup, 4 otherwise. Same predicate as CustomTrack_CupIsComplete, so the label
// cannot promise legs the game will never load.
int CustomTrack_CupLegCount(int cupID, int isAdventureCup);

// What the AP-box layer should do about the level being loaded: one of
// CTR_CT_BOX_UNCHANGED / CTR_CT_BOX_ALLOW / CTR_CT_BOX_DENY. Takes the same
// facts as the serve decision, because "is this the event race" is the same
// question in both places.
int CustomTrack_BoxVerdict(int levelID, int adventureCupActive, int cupID);

// Is the event destination active at all, for any cup? Used by the lap-count
// restores, which need to know whether this build could have written a
// non-vanilla numLaps without caring which cup did it.
int CustomTrack_RaceFeatureEnabled(void);

#endif // CTR_CUSTOM_TRACKS

#endif // NATIVE_CUSTOM_TRACKS_H
