#ifndef NATIVE_RECORDED_RACE_H
#define NATIVE_RECORDED_RACE_H

#include <stddef.h>
#include <stdint.h>

#define CTR_RECORDED_GRID_COUNT 8
#define CTR_RECORDED_CRATE_MAX 32

struct CustomRacePlacement
{
    int16_t pos[3], rot[3];
};

struct CustomRecordedRace;
struct CustomPackageOwned;
struct GameTracker;

#ifdef __cplusplus
extern "C" {
#endif

/* 1 admitted, 0 ordinary package, -1 recognized race with a refusal reason.
   If out is provided, it receives an owned snapshot on admission. */
int CustomPackage_GetRecordedRace(const struct CustomPackageOwned *package,
    struct CustomRecordedRace **out, char *reason, size_t reasonSize);
void CustomRecordedRace_Free(struct CustomRecordedRace *race);
const struct CustomRecordedRace *CustomOffline_RuntimeRecordedRace(int levelID, int mode);
const struct CustomRecordedRace *MainRaceTrack_RecordedRace(void);
void RB_CrateWeapon_SpawnRecorded(struct GameTracker *gGT, const struct CustomRecordedRace *race);
void RB_CrateWeapon_RebindRecorded(struct GameTracker *gGT);

const char *CustomRecordedRace_CheckGeometry(const unsigned char *lev, size_t size,
    const struct CustomRecordedRace *race);

#ifdef __cplusplus
}
#endif
#endif
