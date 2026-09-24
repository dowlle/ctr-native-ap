#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_offline.h>
#include <platform/native_custom_content_verify.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

struct CustomOfflineRequest { struct CustomPackageOwned *package; };
static struct CustomOfflineRequest *activeRequest;
static int activeHost = -1;
static int activeLaps;
static uint64_t runtimeGeneration;
static unsigned int servedRoles;
static int targetLoaded;
static int runCompleted;
static struct CustomOfflineObservation observation;
static int clockValid;
#ifdef CTR_PACKAGE_TESTING
static int testClockEnabled;
static int64_t testClock;
void CustomOffline_TestClock(int enabled, int64_t value)
{
    testClockEnabled = enabled;
    testClock = value;
}
#endif
static int64_t observation_time(void)
{
#ifdef CTR_PACKAGE_TESTING
    if (testClockEnabled) return testClock;
#endif
    return (int64_t)time(NULL);
}

static void clear_observations(void)
{
    servedRoles = 0;
    targetLoaded = 0;
    runCompleted = 0;
    memset(&observation, 0, sizeof observation);
    clockValid = 0;
    if (activeRequest)
    {
        observation.attemptStarted = observation_time();
        clockValid = observation.attemptStarted > 0;
    }
}

static int offline_error(char *error, size_t size, const char *reason)
{
    if (error && size) snprintf(error, size, "%s", reason);
    return 0;
}

int CustomOffline_Prepare(struct CustomPackageOwned **package, const char *expectedPin,
                          struct CustomOfflineRequest **out, char *error, size_t errorSize)
{
    struct CustomPackageManifest manifest;
    struct CustomOfflineRequest *request;
    if (!package || !*package || !out || *out || !expectedPin)
        return offline_error(error, errorSize, "Expected owned package, selected pin and empty offline request");
    if (!CustomPackage_GetManifest(*package, &manifest) || strcmp(expectedPin, manifest.sha256))
        return offline_error(error, errorSize, "Offline selection does not match exact package revision");
    request = malloc(sizeof *request);
    if (!request) return offline_error(error, errorSize, "Cannot allocate offline request");
    request->package = *package;
    *package = NULL;
    *out = request;
    offline_error(error, errorSize, "");
    return 1;
}

void CustomOffline_Free(struct CustomOfflineRequest **request)
{
    if (!request || !*request) return;
    CustomPackage_Free(&(*request)->package);
    free(*request);
    *request = NULL;
}

int CustomOffline_GetManifest(const struct CustomOfflineRequest *request,
                              struct CustomPackageManifest *out)
{
    return request && CustomPackage_GetManifest(request->package, out);
}

int CustomOffline_RetainPackage(const struct CustomOfflineRequest *request, struct CustomPackageOwned **out)
{
    return request && CustomPackage_Retain(request->package, out);
}

int CustomOffline_PackageArcade(const struct CustomPackageOwned *package)
{
    struct CustomContentVerification report;
    unsigned int laps=0;
    return CustomPackage_GetPairReport(package,&report) && report.loadable &&
        report.fileAnalysis[CTR_CCV_ARCADE].result == CTR_CCV_DETECTED && report.measured.spawns >= 8 &&
        CustomPackage_GetRaceLaps(package,&laps,NULL,0) && laps <= CTR_OFFLINE_MAX_LAPS;
}

int CustomOffline_CheckStructure(const struct CustomOfflineRequest *request,
                                 char *error, size_t errorSize)
{
    struct CustomContentVerification report;
    if (!request || !CustomPackage_GetPairReport(request->package, &report) || !report.loadable)
        return offline_error(error, errorSize, "No structurally verified offline package");
    if (report.fileAnalysis[CTR_CCV_ARCADE].result != CTR_CCV_DETECTED)
        return offline_error(error, errorSize, report.fileAnalysis[CTR_CCV_ARCADE].reason);
    if (report.measured.spawns < 8)
        return offline_error(error, errorSize, "Single Race needs eight measured kart spawns");
    offline_error(error, errorSize, "");
    return 1;
}

int CustomOffline_CopyFile(const struct CustomOfflineRequest *request, const char *role,
                           void *destination, size_t capacity, size_t *written)
{
    if (written) *written = 0;
    return request && CustomPackage_CopyFile(request->package, role, destination, capacity, written);
}

int CustomOffline_BeginRuntime(struct CustomOfflineRequest **request, int hostLevelID, int apSeedPresent)
{
    unsigned int laps;
    if (!request || !*request || activeRequest || runtimeGeneration == UINT64_MAX || apSeedPresent != 0 ||
        hostLevelID < 0 || hostLevelID >= 18 || !CustomOffline_CheckStructure(*request, NULL, 0) ||
        !CustomPackage_GetRaceLaps((*request)->package, &laps, NULL, 0) || laps > CTR_OFFLINE_MAX_LAPS) return 0;
    activeRequest = *request;
    *request = NULL;
    activeHost = hostLevelID;
    activeLaps = (int)laps;
    ++runtimeGeneration;
    clear_observations();
    return 1;
}

void CustomOffline_EndRuntime(void)
{
    CustomOffline_Free(&activeRequest);
    activeHost = -1;
    activeLaps = 0;
    clear_observations();
}

int CustomOffline_RuntimeActive(void) { return activeRequest != NULL; }
uint64_t CustomOffline_RuntimeGeneration(void) { return runtimeGeneration; }
int CustomOffline_RuntimeLaps(void) { return activeLaps; }

void CustomOffline_OnLoadRequested(int levelID)
{
    if (activeRequest && levelID != activeHost) CustomOffline_EndRuntime();
    else clear_observations();
}

int CustomOffline_RuntimeServing(int levelID, int singleRace)
{
    return activeRequest && singleRace == 1 && levelID == activeHost;
}

void CustomOffline_OnLoadFinished(int levelID, int singleRace)
{
    if (CustomOffline_RuntimeServing(levelID, singleRace) && servedRoles == 3 && !targetLoaded)
    {
        targetLoaded = 1;
        observation.targetLoaded = observation_time();
        clockValid = clockValid && observation.targetLoaded >= observation.attemptStarted;
    }
}

void CustomOffline_OnRaceFinished(int levelID, int singleRace, int humanDriver)
{
    if (CustomOffline_RuntimeServing(levelID, singleRace) && targetLoaded && humanDriver == 1 && !runCompleted)
    {
        runCompleted = 1;
        observation.raceFinished = observation_time();
        clockValid = clockValid && observation.raceFinished >= observation.targetLoaded;
    }
}

void CustomOffline_OnResidentRestart(int levelID, int singleRace)
{
    int resident = CustomOffline_RuntimeServing(levelID, singleRace) && targetLoaded && servedRoles == 3;
    clear_observations();
    if (resident)
    {
        servedRoles = 3;
        CustomOffline_OnLoadFinished(levelID, singleRace);
    }
}

int CustomOffline_RuntimeObservation(struct CustomOfflineObservation *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    if (!activeRequest || !targetLoaded || !runCompleted || !clockValid) return 0;
    *out = observation;
    return 1;
}

int CustomOffline_RuntimeLoaded(void) { return targetLoaded; }
int CustomOffline_RuntimeCompleted(void) { return runCompleted; }
int CustomOffline_RuntimeManifest(struct CustomPackageManifest *out)
{
    return CustomOffline_GetManifest(activeRequest, out);
}

int CustomOffline_RuntimeFile(int subfile, int levelID, int singleRace, size_t *size)
{
    struct CustomPackageManifest manifest;
    const char *role;
    unsigned int i;
    if (size) *size = 0;
    if (!size || !CustomOffline_RuntimeServing(levelID, singleRace) ||
        subfile < activeHost * 8 || subfile >= activeHost * 8 + 8) return 0;
    role = (subfile & 1) ? "lev" : "vrm";
    if (!CustomOffline_GetManifest(activeRequest, &manifest)) return 0;
    for (i = 0; i < manifest.count; i++)
        if (!strcmp(manifest.files[i].role, role))
        {
            *size = manifest.files[i].bytes;
            return (subfile & 1) ? 2 : 1;
        }
    return 0;
}

int CustomOffline_ReadRuntimeFile(int role, void *destination, size_t capacity, size_t expectedSize)
{
    struct CustomPackageManifest manifest;
    const char *name = role == 1 ? "vrm" : role == 2 ? "lev" : NULL;
    unsigned int i;
    if (!name || !destination || expectedSize > capacity ||
        !CustomOffline_GetManifest(activeRequest, &manifest)) return 0;
    for (i = 0; i < manifest.count; i++)
        if (!strcmp(manifest.files[i].role, name) && manifest.files[i].bytes == expectedSize)
        {
            if (!CustomOffline_CopyFile(activeRequest, name, destination, capacity, NULL)) return 0;
            memset((char *)destination + expectedSize, 0, capacity - expectedSize);
            servedRoles |= (unsigned int)role;
            return 1;
        }
    return 0;
}
#endif
