#ifndef NATIVE_CUSTOM_TRACK_MANAGER_H
#define NATIVE_CUSTOM_TRACK_MANAGER_H

#ifdef CTR_CUSTOM_TRACKS

#include <stddef.h>

#include <platform/native_sha256.h>

#define CTR_CT_MANAGER_PATH_MAX 1024
#define CTR_CT_MANAGER_DETAIL_MAX 256
#define CTR_CT_MANAGER_UUID_BYTES 37

// ── measuring `wumpa_collectible` out of a verified LEV ────────────────────
//
// The descriptor's per-track Reach 10 Wumpa capability is MEASURED, not
// declared: can the local player actually reach ten Wumpa Fruit on this track?
// It is deliberately not inferred from the broad `crates` flag, because a track
// can carry weapon boxes, TNT and relic time crates and still offer no route to
// ten fruit at all.
//
// The measurement walks the level's own instance table and counts the two
// instance kinds that pay fruit:
//
//   * PU_FRUIT_CRATE   one fruit crate. RB_Crate.c pays `MixRNG % 4 + 5`, so the
//                      GUARANTEED floor per crate is five, not the average.
//   * PU_WUMPA_FRUIT   one loose fruit on the track, worth exactly one.
//
// Ten or more guaranteed fruit in a single lap is `true`. Using the floor rather
// than the mean is what makes the answer honest for the worst roll a player can
// get; counting one lap rather than the descriptor's lap count is the same
// conservatism one step further out.
//
// The two model ids are mirrored from `enum MODEL_ID`
// (include/namespace_Instance.h) rather than included, because this file is
// platform code that has to compile without the engine headers -- the same
// reason ap_reward_policy.h mirrors its model ids. ap_hooks.c sees both
// definitions and static-asserts them equal, so neither mirror can drift.
#define CTR_CT_MODEL_WUMPA_FRUIT 0x02
#define CTR_CT_MODEL_FRUIT_CRATE 0x07
#define CTR_CT_WUMPA_PER_CRATE_MIN 5
#define CTR_CT_WUMPA_TARGET 10

// What the walk found. Reported in the log and in the status detail on a
// mismatch, so a package that measures unexpectedly is diagnosable from a
// support bundle rather than only from a rebuild.
struct CustomTrackWumpaMeasurement
{
	int fruitCrates;     // PU_FRUIT_CRATE instances
	int looseFruit;      // PU_WUMPA_FRUIT instances
	int guaranteedFruit; // fruitCrates * CTR_CT_WUMPA_PER_CRATE_MIN + looseFruit
	int collectible;     // guaranteedFruit >= CTR_CT_WUMPA_TARGET
};

// Measure a LEV. Returns 1 and fills `out` only when the file's own structure
// could be walked entirely within its bounds; returns 0 for a file that cannot
// be read, whose pointer map or instance table falls outside the payload, or
// whose instance count is implausible.
//
// A 0 is never "no fruit". The caller must treat it as "not measurable" and
// refuse, because an unmeasured capability and a measured false are different
// states and only one of them is a package this build may serve.
int CustomTrackManager_MeasureWumpa(const char *levPath,
	                                 struct CustomTrackWumpaMeasurement *out);

// Stable package states for the Options-menu surface and seed preflight. Scan
// is synchronous in manager-light; VERIFYING is reserved for the UI while it
// is driving a scan and never escapes CustomTrackManager_ScanPackage itself.
enum CustomTrackManagerState
{
	CTR_CT_MANAGER_NOT_INSTALLED = 0,
	CTR_CT_MANAGER_VERIFYING,
	CTR_CT_MANAGER_MISSING_FILES,
	CTR_CT_MANAGER_HASH_MISMATCH,
	CTR_CT_MANAGER_MANIFEST_MISSING,
	CTR_CT_MANAGER_MANIFEST_INVALID,
	CTR_CT_MANAGER_READY,
	CTR_CT_MANAGER_UNSUPPORTED,
	CTR_CT_MANAGER_INCOMPATIBLE,
	CTR_CT_MANAGER_IO_ERROR
};

// A release-owned package identity. Public Alpha6 exposes one instance (Baby T
// Park), but keeping the scanner data-driven prevents the first UI from baking
// in custom_track_1/custom_track_2 fields that cannot grow into one combined
// custom_tracks mapping later.
struct CustomTrackManagerPackage
{
	const char *id;
	const char *packageUuid;
	const char *version;
	const char *title;
	const char *author;
	const char *sourceUrl;
	const char *downloadApiUrl;
	const char *minimumClientVersion;
	const char *minimumApworldVersion;
	const char *levSha256;
	const char *vrmSha256;
	const char *navigationUuid;
	unsigned int navigationRevision;
	int laps;
	int flagCrates;
	int flagCtrLetters;
	int flagRelicCrates;
	int flagAiNav;
	int flagMinimap;
	int flagGhosts;
	// The measured Reach 10 Wumpa capability. Held in the release registry like
	// every other measured flag, and re-derived from the installed bytes on every
	// scan: a package whose files measure something the registry does not claim
	// is refused rather than served.
	int flagWumpaCollectible;
	int flagSpawns;
	int flagCheckpoints;
};

struct CustomTrackManagerStatus
{
	int state; // enum CustomTrackManagerState
	char detail[CTR_CT_MANAGER_DETAIL_MAX];
	char packageRoot[CTR_CT_MANAGER_PATH_MAX];
	char levPath[CTR_CT_MANAGER_PATH_MAX];
	char vrmPath[CTR_CT_MANAGER_PATH_MAX];
	char manifestPath[CTR_CT_MANAGER_PATH_MAX];
	char yamlPath[CTR_CT_MANAGER_PATH_MAX];
	char actualLevSha256[NATIVE_SHA256_HEX_BYTES];
	char actualVrmSha256[NATIVE_SHA256_HEX_BYTES];
	unsigned long levBytes;
	unsigned long vrmBytes;
	// What the installed LEV actually measured. Valid once the scan has reached
	// the measurement step; `wumpaMeasured` is 0 before that and for any file the
	// walk could not complete.
	int wumpaMeasured;
	struct CustomTrackWumpaMeasurement wumpa;
};

// Seed-owned requirement compared against the release-owned registry before a
// package is allowed to arm. This deliberately mirrors every identity and
// capability field carried by custom_tracks v2: a seed may select an installed
// package, but it may not redefine what that package is.
struct CustomTrackManagerRequirement
{
	const char *id;
	const char *packageUuid;
	const char *packageVersion;
	const char *minimumClientVersion;
	const char *minimumApworldVersion;
	const char *levSha256;
	const char *vrmSha256;
	const char *navigationUuid;
	unsigned int navigationRevision;
	int laps;
	int boxes;
	int flagCrates;
	int flagCtrLetters;
	int flagRelicCrates;
	int flagAiNav;
	int flagMinimap;
	int flagGhosts;
	int flagWumpaCollectible;
	int flagSpawns;
	int flagCheckpoints;
};

// The only package public Alpha6 recognizes. Its package UUID is permanent and
// deliberately distinct from navigationUuid: changing navigation compatibility
// must never change content provenance, and vice versa.
const struct CustomTrackManagerPackage *CustomTrackManager_BabyTPark(void);

const char *CustomTrackManager_StateText(int state);

// Create the deterministic non-destructive directory skeleton for a package.
// Existing directories and creator files are left untouched.
int CustomTrackManager_PrepareFolder(const char *assetsRoot,
	                                  const struct CustomTrackManagerPackage *package,
	                                  struct CustomTrackManagerStatus *outStatus);

// Discover and hash the two original files, then validate the manager-owned
// canonical manifest. Saphi's UUID filenames are accepted directly; players do
// not have to rename them to track.lev / track.vrm. No files are written.
int CustomTrackManager_ScanPackage(const char *assetsRoot,
	                                const struct CustomTrackManagerPackage *package,
	                                struct CustomTrackManagerStatus *outStatus);

// Verify again and atomically create/repair manifest.json. The original LEV and
// VRM are never modified. Returns 1 only when the resulting rescan is READY.
int CustomTrackManager_FinalizePackage(const char *assetsRoot,
	                                    const struct CustomTrackManagerPackage *package,
	                                    struct CustomTrackManagerStatus *outStatus);

// Match a seed requirement to the compiled Alpha6 registry, then verify the
// installed package. With autoFinalize != 0, a recognized hash-matching pair
// whose manifest is missing or stale gets the canonical manager-owned manifest
// and is rescanned immediately. Returns an enum CustomTrackManagerState.
int CustomTrackManager_Preflight(const char *assetsRoot,
	                              const struct CustomTrackManagerRequirement *requirement,
	                              int autoFinalize,
	                              struct CustomTrackManagerStatus *outStatus);

// Render the Alpha6-compatible player-YAML fragment. It always emits one
// combined custom_tracks mapping and boxes:false; a package must be READY.
int CustomTrackManager_RenderYaml(const struct CustomTrackManagerPackage *package,
	                               const struct CustomTrackManagerStatus *status,
	                               char *dst, size_t dstSize);

// Save that exact fragment at assets/tracks/custom_tracks.generated.yaml.
// Clipboard integration belongs to the Options-menu layer; the file export is
// the durable fallback and is available from the foundation onward.
int CustomTrackManager_SaveYaml(const char *assetsRoot,
	                             const struct CustomTrackManagerPackage *package,
	                             const struct CustomTrackManagerStatus *status,
	                             struct CustomTrackManagerStatus *outStatus);

#endif // CTR_CUSTOM_TRACKS

#endif // NATIVE_CUSTOM_TRACK_MANAGER_H
