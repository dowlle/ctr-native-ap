#ifndef NATIVE_CUSTOM_OFFLINE_H
#define NATIVE_CUSTOM_OFFLINE_H
#include <platform/native_custom_package.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* An offline Single Race request owns one exact verified snapshot. It is not
   AP slot data, a retail level identity, a Ready verdict or an active load.
   Admission and engine handoff must precede gameplay, separately. */
struct CustomOfflineRequest;
/* UI eligibility from owned bytes; not gameplay certification. The Reason
   forms also write why a mode is refused, as short menu text ("" when allowed,
   CTR_CCV_ARCADE_REASON_MAX bytes are enough). */
int CustomOffline_PackageArcade(const struct CustomPackageOwned *package);
int CustomOffline_PackageArcadeReason(const struct CustomPackageOwned *package, char *reason, size_t reasonSize);
int CustomOffline_PackageTimeTrialReason(const struct CustomPackageOwned *package, char *reason, size_t reasonSize);
/* Which race a custom runtime serves: an Arcade single race (eight karts) or
   a one-kart Time Trial. The engine's race-mode test must name the same mode
   for the host slot to serve the package. */
#define CTR_OFFLINE_MODE_ARCADE 1
#define CTR_OFFLINE_MODE_TIME_TRIAL 2
/* GameTracker.lapTime has seven entries; PlayLevel indexes it per lap. */
#define CTR_OFFLINE_MAX_LAPS 7
/* The lap count an offline race uses: the pinned race_settings value, or
   CTR_CUSTOM_DEFAULT_LAPS (3, native_custom_identity.h) when the package has no
   race_settings file (Saphi lists no lap count, or an older revision).
   Returns 0 (laps 0) for an invalid sidecar or a value above the engine limit. */
int CustomOffline_PackageLaps(const struct CustomPackageOwned *package, unsigned int *laps,
                              char *error, size_t errorSize);
/* Main-thread asynchronous acquisition of the selected installed revision.
   Shares the single package worker lane with installation/store scans. No game
   launch or certification is performed. Poll: 0 pending/unreportable, 1 success
   (transfers request), -1 idle, -2 refusal. *out must initially be NULL. */
int CustomOffline_StartPrepare(const char *assets, const char *pin);
int CustomOffline_PollPrepare(struct CustomOfflineRequest **out, char *error, size_t errorSize);
/* Engine handoff after admission: transfers the request, never AP seed state.
   Refuses an existing active request or any AP seed. This structural guard is
   not Ready certification; the caller must enforce admission before calling. */
int CustomOffline_BeginRuntime(struct CustomOfflineRequest **request, int hostLevelID, int apSeedPresent,
                               int mode);
int CustomOffline_RuntimeMode(void); /* CTR_OFFLINE_MODE_*, zero when inactive. */
void CustomOffline_EndRuntime(void);
int CustomOffline_RuntimeActive(void);
int CustomOffline_RuntimeLaps(void); /* Authored standalone laps, zero when inactive. */
/* Monotonic run identity; survives teardown, increments only on BeginRuntime. */
uint64_t CustomOffline_RuntimeGeneration(void);
/* A load request keeps the runtime: the host slot's geometry stays resident,
   and the frames still run on it, until the checkered flag covers the screen.
   A request for the host slot (retry) clears the attempt's observations. */
void CustomOffline_OnLoadRequested(int levelID);
/* The level actually starts loading (MainRaceTrack_StartLoad, right before
   LOAD_LevelFile replaces the resident level): leaving the host slot ends the
   custom race here, so its identity lasts exactly as long as its geometry. */
void CustomOffline_OnLoadStarting(int levelID);
/* The loaded package's identity for a savestate (native_custom_state_identity.h):
   "none" without an active runtime. */
struct CustomStateIdentity;
void CustomOffline_RuntimeStateIdentity(struct CustomStateIdentity *out);
/* singleRace is the engine's current race mode (CTR_OFFLINE_MODE_*, 0 for any
   other): the host slot serves the package only in the mode it started for. */
int CustomOffline_RuntimeServing(int levelID, int singleRace);
/* Main-thread engine observations for the current attempt, not certification.
   Load completion requires both retained geometry files to have been served.
   Restart clears observations. Box authoring keys placements on a loaded
   package only (CustomOffline_RuntimeLoaded). */
void CustomOffline_OnLoadFinished(int levelID, int singleRace);
/* Engine in-place restart: only a previously observed resident load can
   carry geometry evidence into the fresh attempt. No old finish survives. */
void CustomOffline_OnResidentRestart(int levelID, int singleRace);
void CustomOffline_OnRaceFinished(int levelID, int singleRace, int humanDriver);
int CustomOffline_RuntimeLoaded(void);
int CustomOffline_RuntimeCompleted(void);
int CustomOffline_RuntimeManifest(struct CustomPackageManifest *out);
struct CustomOfflineObservation
{
    int64_t attemptStarted;
    int64_t targetLoaded;
    int64_t raceFinished;
};
/* Exact first-event Unix times for a completed attempt. Invalid clock ordering
   refuses and zeroes output; repeated events never rewrite timestamps. */
int CustomOffline_RuntimeObservation(struct CustomOfflineObservation *out);
/* The active request's pinned .sca bytes (role "sca"), copied once per run
   into a buffer this module owns. Returns 0 when there is no active request
   or the package has no .sca. The pointer stays valid until the next run
   begins or EndRuntime; callers must not keep it across a level load. */
int CustomOffline_RuntimeAudio(const void **data, size_t *size);
/* BIGFILE group mapping: returns 1 VRM / 2 LEV / 0 unrelated. */
int CustomOffline_RuntimeFile(int subfile, int levelID, int singleRace, size_t *size);
int CustomOffline_ReadRuntimeFile(int role, void *destination, size_t capacity, size_t expectedSize);
/* Success transfers *package to the request and sets *package NULL. Failure
   preserves both outputs. *out must initially be NULL. The expected pin comes
   from the selected library revision, never a UUID/latest-version lookup. */
int CustomOffline_Prepare(struct CustomPackageOwned **package, const char *expectedPin,
                          struct CustomOfflineRequest **out, char *error, size_t errorSize);
void CustomOffline_Free(struct CustomOfflineRequest **request);
int CustomOffline_GetManifest(const struct CustomOfflineRequest *request,
                              struct CustomPackageManifest *out);
int CustomOffline_RetainPackage(const struct CustomOfflineRequest *request, struct CustomPackageOwned **out);
/* Structural prerequisite only: an eight-kart Single Race requires detected
   Arcade structures and eight measured spawns; a Time Trial a lap checkpoint
   graph. Success is NOT certification. */
int CustomOffline_CheckStructure(const struct CustomOfflineRequest *request, int mode,
                                 char *error, size_t errorSize);
/* Copy from the retained snapshot, never reopen paths or consult an AP seed. */
int CustomOffline_CopyFile(const struct CustomOfflineRequest *request, const char *role,
                           void *destination, size_t capacity, size_t *written);
#ifdef __cplusplus
}
#endif
#endif
