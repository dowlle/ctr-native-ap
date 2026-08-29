#ifndef NATIVE_CUSTOM_TRACK_MANAGER_H
#define NATIVE_CUSTOM_TRACK_MANAGER_H

#ifdef CTR_CUSTOM_TRACKS

#include <stddef.h>

#include <platform/native_sha256.h>

#define CTR_CT_MANAGER_PATH_MAX 1024
#define CTR_CT_MANAGER_DETAIL_MAX 256
#define CTR_CT_MANAGER_UUID_BYTES 37

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

// Hash the two original files and validate the manager-owned canonical
// manifest. No files are written. READY means files, identity and manifest all
// agree; verified files without a manifest return MANIFEST_MISSING.
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
