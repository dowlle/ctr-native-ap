// cc -m32 -DCTR_NATIVE -DCTR_AP -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DBUILD=926 -I include -I . tools/test-custom-finish.c -lm -o /tmp/test-custom-finish
// Runs the production lap/finish function with deterministic progress and
// package identity seams. No third-party bytes are included.
#include <assert.h>
#include <common.h>
#include <platform/native_custom_finish.h>
void AP_LapOnCrossing(struct GameTracker *, struct Driver *, int);
#include "../game/PlayLevel.c"
struct sData sdata_static;
struct Data data;
static struct GameTracker gt;
static struct Driver driver;
static struct QuadBlock quad;
static struct Level level;
static struct CheckpointNode node;
static struct Instance instance;
static struct Thread thread;
static int terrain, nextProgress, laps, finishes, ends;
int CustomTrack_FinishTerrain(const struct CustomTrackLoadContext *ctx) {(void)ctx; return terrain;}
int MainRaceTrack_OfflineCustomLoad(void) {return 0;}
int CustomOffline_RuntimeFinishTerrain(void) {return -1;}
void VehLap_UpdateProgress(struct Driver *d) {d->distanceToFinish_curr = nextProgress;}
void AP_LapOnCrossing(struct GameTracker *g, struct Driver *d, int slot)
{assert(g == &gt && d == &driver && slot == 0); assert(d->lapIndex == 1); laps++;}
void MainRaceTrack_OfflineRaceFinished(int human) {assert(human); finishes++;}
void UI_SaveLapTime(int n, int t, s16 id) {(void)n;(void)t;(void)id;}
int OtherFX_Play(u32 id, int flags) {(void)id;(void)flags;return 0;}
void Voiceline_ClearTimeStamp(void) {}
void Voiceline_RequestPlay(u32 a, u32 b, u32 c) {(void)a;(void)b;(void)c;}
void BOTS_Driver_Convert(struct Driver *d) {(void)d;}
int VehPickState_NewState(struct Driver *a, int b, struct Driver *c, int d)
{(void)a;(void)b;(void)c;(void)d;return 0;}
void MainGameEnd_Initialize(void) {ends++;gt.gameMode1 |= END_OF_RACE;}
static void reset(void)
{
    memset(&gt, 0, sizeof gt); memset(&driver, 0, sizeof driver); memset(&quad, 0, sizeof quad);
    sdata->gGT = &gt; sdata->numPlayersFinishedRace = 0;
    gt.drivers[0] = gt.driversInRaceOrder[0] = &driver; gt.numPlyrCurrGame = 1;
    level.ptr_restart_points = &node; node.distToFinish = 17000; gt.level1 = &level;
    driver.instSelf = &instance; instance.thread = &thread; thread.modelIndex = DYNAMIC_PLAYER;
    gt.numLaps = 3; driver.driverRank = -1; terrain = 4; laps = finishes = ends = 0;
    nextProgress = 500;
}
int main(void)
{
    const char *lev = "e22b87a73009b94251f5216424cf07875a02e076eb8461b902624c2b5277ad45";
    const char *vrm = "5de0e46152d8fdce3e0cbe7cdd860516d98d98105106f5d81c043b39df9f0753";
    assert(CustomFinish_Terrain(lev, vrm) == 4);
    assert(CustomFinish_Terrain("736982024dc32b02d1813d724acd7d7fca36a2cbd639b93d895309ae1795d4c6",
           "e403d7b8c42268eeeb43cb3ad75a32127d90d71d378c7c4de65444e45391620a") == 4);
    assert(CustomFinish_Terrain(lev, lev) == -1 && CustomFinish_Terrain(NULL, vrm) == -1);
    assert(CustomFinish_ImportBlock(lev, vrm) == NULL);
    reset(); nextProgress = 33000; driver.distanceToFinish_curr = 500;
    PlayLevel_UpdateLapStats(); assert(!laps && !finishes && gt.numLaps == 1);
    quad.terrain_type = 4; driver.underDriver = &quad;
    PlayLevel_UpdateLapStats(); assert(!laps); // Flying over the end does not finish.
    driver.currBlockTouching = &quad; driver.actionsFlagSet |= ACTION_BEHIND_START_LINE;
    PlayLevel_UpdateLapStats(); assert(laps == 1 && finishes == 1 && ends == 1);
    assert(driver.actionsFlagSet & ACTION_RACE_FINISHED);
    PlayLevel_UpdateLapStats(); assert(laps == 1 && finishes == 1 && ends == 1);
    reset(); terrain = -1; driver.currBlockTouching = &quad; quad.terrain_type = 4;
    PlayLevel_UpdateLapStats(); assert(!laps && !finishes && gt.numLaps == 3);
    nextProgress = 33000; driver.distanceToFinish_curr = 500;
    PlayLevel_UpdateLapStats(); assert(laps == 1 && !finishes && driver.lapIndex == 1);
    puts("custom finish: passed"); return 0;
}
