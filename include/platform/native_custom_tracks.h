#ifndef NATIVE_CUSTOM_TRACKS_H
#define NATIVE_CUSTOM_TRACKS_H

#ifdef CTR_CUSTOM_TRACKS

#include <common.h>
#include <platform/native_custom_tracks_policy.h>
#include <platform/native_sha256.h> // NATIVE_SHA256_HEX_BYTES: the digest fields

// Custom-track loader, engine-facing half (Baby T Park event spike, rung 1).
// The decisions this file's implementation asks are all in
// native_custom_tracks_policy.h; what lives here is the config parse, the
// content verification and the byte serving -- everything that needs a
// filesystem and therefore cannot be unit-tested out of engine.
//
// THE MODEL. One community custom track ships one .lev and one .vrm. config.ini
// says where those two files are; the SEED says what they must hash to, how many
// laps the race is, which arcade slot the bytes borrow and which cup is replaced.
// When a seed carrying that descriptor connects, the loader hashes both files
// and either arms itself or refuses. No seed means no custom track, whatever
// config.ini holds. Once armed, every BIGFILE read whose
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

// What a seed says about its bound custom track. Everything the loader needs
// except where the two files are: the seed is the authority on content and on
// what the event race is, and config.ini only says where to look.
//
// This is the rung-2 seam the earlier rungs were shaped around. It is filled
// from the slot_data custom_tracks block (ap/ap_hooks.c translates ctr_cfg into
// it); nothing below this struct knows the wire exists.
struct CustomTrackSeedDescriptor
{
	int laps;                // 1..7
	int hostLevelID;         // 0..17, the arcade slot whose bytes are borrowed
	int replacesCupLevelID;  // 100..104
	int boxes;               // 1 = AP boxes allowed on the event race

	char levSha256[NATIVE_SHA256_HEX_BYTES];
	char vrmSha256[NATIVE_SHA256_HEX_BYTES];

	// The describe step's measured capabilities. Only aiNav and spawns gate the
	// race today (see CustomTrackPolicy_FlagsSupportRace); the rest are carried
	// so the check rungs above the Gem have an honest input when they land.
	int flagCrates;
	int flagCtrLetters;
	int flagRelicCrates;
	int flagAiNav;
	int flagMinimap;
	int flagGhosts;
	int flagSpawns;
	int flagCheckpoints;
};

// Read the [CustomTracks] section of config.ini, once, at startup. It carries
// ONLY the two file paths now; the loader stays disarmed until a seed hands over
// a descriptor. Safe to call when config.ini is absent. Idempotent.
void CustomTrack_Load(void);

// Hand the loader a seed's descriptor: validate what this build can actually
// serve, then hash both files against the seed's digests and arm on success.
// Returns 1 when armed. Every failure is loud and total -- no bytes served AND
// the cup left at its vanilla four legs, because serving retail bytes for a race
// the seed thinks is the custom track is the silent wrong-content outcome the
// digests exist to prevent.
//
// Idempotent by content: an unchanged descriptor costs a memcmp, which is what
// lets AP_OnFrame call it every frame instead of needing a connect callback.
int CustomTrack_ApplySeedDescriptor(const struct CustomTrackSeedDescriptor *d);

// No seed, no block, or an unreadable one: the feature goes fully off. Nothing
// is served and every cup is back to its vanilla legs on the next read.
void CustomTrack_ClearSeedDescriptor(void);

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

// Is the load in flight the event race? The same three facts and the same
// decision as the subfile override, without naming a subfile. Asked by
// MainInit_GetPrimMemSize, which has to size the frame's primitive arena for
// whichever track is about to load and cannot wait for a subfile read to say so.
int CustomTrack_ServingLoad(int levelID, int adventureCupActive, int cupID);

// Every line this feature emits goes through here. Writes to stdout, which the
// harnesses assert on, and in an AP build also to AP_LogLine, which is the sink
// that reaches ctr-ap.log.
void CustomTrack_Log(const char *fmt, ...);

// ---------------------------------------------------------------------------
// Per-load render accounting.
//
// Rung 1 printed a line on every new per-load maximum. That was 101 lines in
// one session and it still could not answer the question it was built for: a
// frame that ran the arena dry abandons the rest of level rendering, so it
// spends LESS than a frame that completed and never becomes the maximum. The
// accumulator below keeps the high-water mark, adds the three events that a
// completed frame cannot show, and emits ONE line per level load.
//
// Everything here is a plain counter written from the render path and read once
// per load. No allocation, no formatting, nothing that costs a frame.

// A bucket's primitive reserve was refused, so DrawLevelOvr1P returned and the
// rest of this frame's level geometry was not drawn. `reserveBytes` is what the
// bucket asked for, `freeBytes` what the arena had left. This is the direct
// evidence for the prefix cut; the high-water mark alone is not.
void CustomTrackDiag_NoteReserveRefused(unsigned long reserveBytes, unsigned long freeBytes);

// The rendered-quadblock list reached the end of its array and an append was
// refused. Decision 9. Retail would have written past the array here.
void CustomTrackDiag_NoteRenderedListFull(void);

// RenderLists_PushChild dropped a BSP record because the 51-record scratch
// stack was full, so a whole subtree was never walked. Independent of the
// primitive arena and a competing explanation for missing geometry, which is
// why it is counted next to the arena figures rather than assumed away.
void CustomTrackDiag_NoteBspRecordDropped(void);

// Open a frame's accounting. Called from RenderAllLevelGeometry BEFORE the BSP
// walk, so that everything the frame drops -- BSP records included -- lands in
// the right load's counters. A `levelID` or arena size different from the load
// in progress flushes that load's summary first.
void CustomTrackDiag_BeginFrame(int levelID, unsigned long capacityBytes);

// Close it, with the arena cursor sampled before the terrain, after it and
// after the sky. `primCount` is PrimMem::primitiveCount at the sky sample --
// everything the frame has emitted, not the terrain's share alone.
void CustomTrackDiag_NoteFrameSpend(unsigned long beforeGeomBytes, unsigned long afterGeomBytes, unsigned long afterSkyBytes, int primCount,
                                    int leavesDrawn);

// Emit the summary for the load in progress and forget it. Safe to call when no
// frames have been seen, in which case it does nothing.
void CustomTrackDiag_FlushLevelLoad(void);

#endif // CTR_CUSTOM_TRACKS

#endif // NATIVE_CUSTOM_TRACKS_H
