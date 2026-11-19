#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <common.h>
#include <platform/native_custom_identity.h>
#include <platform/native_memory.h>
#include <platform/native_recorded_race_data.h>
#include "../../platform/native_clip_scratch.h"
#include "../../ap/ap_navrec.h"

struct sData sdata_static;
struct Data data;
static struct GameTracker tracker;
static struct Level level;
static struct Driver drivers[8];
static struct Instance karts[8];
static struct CustomRecordedRace race;
static int recorded, born, sounds, gridDraws;
static const unsigned char trackUuid[16] = {1};

/* Keep the production grid, bot preparation, crate lifecycle and allocation.
   Karts, sound and screen coordinates are supplied by the harness. */
const struct CustomRecordedRace *MainRaceTrack_RecordedRace(void) { return recorded ? &race : NULL; }
int MainRaceTrack_OfflineCustomLoad(void) { return 1; }
int MainRaceTrack_OfflineCustomTimeTrial(void) { return 0; }
int MainRaceTrack_IdentityLevelID(void) { return CTR_CUSTOM_LEVEL_ID; }
int MainRaceTrack_OfflineNavIdentity(unsigned char uuid[16], unsigned int *revision)
{
	memcpy(uuid, trackUuid, sizeof trackUuid);
	*revision = 1;
	return 1;
}
int CustomTrack_ServingLoad(int levelID, int cup, int cupID) { return 0; }
int CustomTrack_NavIdentityForLoad(int levelID, int cup, int cupID, unsigned char *uuid, unsigned int *rev) { return 0; }
int CustomTrack_OxideFinalServing(int levelID, int bossID, int boss) { return 0; }
int CustomTrack_GarageVenueServing(int levelID, int bossID, int boss) { return -1; }
int CustomTrack_CortexTrackIntent(int levelID, int boss) { return 0; }
int CustomTrack_EventFieldSize(int levelID, int cup, int cupID) { return 0; }
void AP_LogLine(const char *line) { fputs(line, stdout); }
int RngDeadCoed(struct RngDeadCoedState *state) { return 0; }
int SquareRoot0_stub(int value) { return value > 0 ? (int)sqrt(value) : 0; }
int ratan2(int y, int x) { return (int)(atan2(y, x) * (2048.0 / 3.141592653589793)); }
void VehBirth_TeleportSelf(struct Driver *driver, u8 spawnFlag, int spawnPosY)
{
	assert(spawnFlag == VEH_BIRTH_SPAWN_RACE_START && spawnPosY == 0);
	const SVec3 *seat = &level.DriverSpawn[sdata->kartSpawnOrderArray[driver->driverID]].pos;
	CTR_SET_VEC3(driver->posCurr.v, seat->x * 256, (seat->y + 0x80) * 256, seat->z * 256);
}
void VehPhysForce_TranslateMatrix(struct Thread *thread, struct Driver *driver)
{
	assert(thread->inst == driver->instSelf);
	CTR_SET_VEC3(thread->inst->matrix.t, driver->posCurr.x >> 8, driver->posCurr.y >> 8, driver->posCurr.z >> 8);
	gridDraws++;
}
void GhostReplay_Init1(void) {}
void GhostReplay_Init2(void) {}
void GhostTape_Start(void) {}
int LOAD_IsOpen_RacingOrBattle(void) { return 0; }
void RB_MinePool_Init(void) {}
char EngineAudio_InitOnce(u32 soundID, u32 flags) { return 0; }
int OtherFX_Play(u32 soundID, int flags) { return 0; }
void PlaySound3D(u32 soundID, struct Instance *inst) { sounds++; }
void MatrixRotate(MATRIX *dst, MATRIX *src, MATRIX *rot) {}
void ConvertRotToMatrix(MATRIX *matrix, const SVec3 *rot) { memset(matrix, 0, sizeof *matrix); }
void RB_Fruit_GetScreenCoords(struct PushBuffer *pb, struct Instance *inst, s16 *pos) { pos[0] = pos[1] = 0; }
void *MEMPACK_AllocMem(int size) { return NULL; }
void COLL_SearchBSP_CallbackPARAM(struct BSP *root, struct BoundingBox *bbox, CollBspLeafCallback cb,
                                struct ScratchpadStruct *sps) {}
struct MetaDataMODEL *COLL_LevModelMeta(u32 id) { return NULL; }

static struct Model crateModel = {.id = PU_RANDOM_CRATE};
struct Model *AP_BoxModel_GetWeapon(struct GameTracker *gGT) { return &crateModel; }
void AP_BoxModel_SpawnPos(struct Model *model, int x, int y, int z, Vec3 *out)
{
	CTR_SET_VEC3(out->v, x, y, z);
}

#include "../../game/LIST.c"
#include "../../game/JitPool.c"
#include "../../game/PROC.c"
#include "../../game/INSTANCE.c"
#define BOTS_Driver_Init Unused_BOTS_Driver_Init
#include "../../game/BOTS.c"
#undef BOTS_Driver_Init
#include "recorded-race-tick-prefix.h"
#include "../../game/MAIN/MainInit.c"
#include "../../game/231/RB_Crate.c"
#include "../../ap/ap_navrec.c"

NativeConfig g_config;

struct Driver *VehBirth_Player(int index)
{
	for (int i = 0; i < 8; i++)
		assert(level.DriverSpawn[i].pos.x == race.grid[i].pos[0] &&
		       level.DriverSpawn[i].rot.y == race.grid[i].rot[1]);
	return &drivers[index];
}

struct Driver *BOTS_Driver_Init(int index)
{
	assert(index == ++born);
	int lane = sdata->driver_pathIndexIDs[index];
	assert(lane == index + 2);
	assert(sdata->NavPath_ptrHeader[lane]->numPoints == 32);
	tracker.drivers[index] = &drivers[index];
	tracker.numBotsNextGame++;
	return &drivers[index];
}

static void make_line(struct AP_NavRecNode *nodes, unsigned int *stamps, int radius)
{
	for (int i = 0; i < 32; i++)
	{
		double angle = i * (6.283185307179586 / 32);
		nodes[i].posX = (short)(radius * sin(angle));
		nodes[i].posZ = (short)(radius * cos(angle));
		nodes[i].pathChangeOpcode = AP_NAVREC_NO_LANE_CHANGE;
		nodes[i].goBackCount = i + 1;
		stamps[i] = i * 28;
	}
	AP_NavRecFormat_FillDistances(nodes, 32);
}

static void check_race(void)
{
	recorded = 1;
	for (int i = 0; i < 8; i++)
	{
		race.grid[i].pos[0] = 100 + i;
		race.grid[i].rot[1] = 400 + i;
	}
	for (int i = 0; i < 7; i++)
	{
		race.set.laps[i].nodeCount = 32;
		snprintf(race.set.driverNames[i], sizeof race.set.driverNames[i], "Driver %d", i);
		make_line(race.nodes[i], race.stamps[i], 1000 + i * 20);
	}
	race.crates[0].pos[2] = 1060;
	race.crates[0].rot[1] = 190;
	race.crateCount = 1;

	MainInit_Drivers(&tracker);
	assert(born == 7);
	unsigned int seen = 0;
	for (int lane = 3; lane < 10; lane++)
	{
		int lap = s_navrecLaneLapIndex[lane];
		assert(lap >= 0 && lap < 7 && (seen & (1u << lap)) == 0);
		seen |= 1u << lap;
		assert(!strcmp(s_navrecLaneDriverName[lane], race.set.driverNames[lap]));
		assert(s_navrecLane[lane].wire[0].specialBits & AP_NAVREC_SPECIAL_LEVEL_INST_COLL);
	}
	for (int lane = 0; lane < 3; lane++)
		assert(!memcmp(s_navrecLane[lane].wire, s_navrecLane[lane + 3].wire, 32 * sizeof(struct AP_NavRecNode)));
	assert(AP_NavRecLabel_LaneFor(s_navrecLaneOwner, BOTS_NAV_LANE_MAX, s_navrecRecordedMask, 7, 9) == 9);
	puts("Eight spawn transforms precede birth; seven bots use seven lanes and the first three back the track");

	struct InstanceSlot { struct Instance inst; struct InstDrawPerPlayer draw; };
	CTR_STATIC_ASSERT(sizeof(struct InstanceSlot) == sizeof(struct Instance) + sizeof(struct InstDrawPerPlayer));
	static struct InstanceSlot instances[4];
	static struct Thread threads[4];
	static unsigned char stacks[4][0x48] __attribute__((aligned(4)));
	MainInit_NativePoolAt(&tracker.JitPools.instance, 4, sizeof instances[0], instances);
	MainInit_NativePoolAt(&tracker.JitPools.thread, 4, sizeof threads[0], threads);
	MainInit_NativePoolAt(&tracker.JitPools.smallStack, 4, sizeof stacks[0], stacks);
	RB_CrateWeapon_SpawnRecorded(&tracker, &race);
	struct Thread *crate = tracker.threadBuckets[STATIC].thread;
	assert(crate && !crate->next && crate->inst->model == &crateModel);
	assert(crate->inst->matrix.t[2] == race.crates[0].pos[2]);
	assert(crate->inst->scale.x == 0x1000);

	drivers[0].instSelf = &karts[0];
	drivers[0].heldItemID = 0xf;
	karts[0].matrix.t[2] = 1060;
	tracker.gameMode1 |= PAUSE_1;
	crate->funcThTick(crate);
	assert(sounds == 0 && drivers[0].heldItemID == 0xf);
	tracker.gameMode1 &= ~PAUSE_1;
	crate->funcThTick(crate);
	assert(sounds == 1 && drivers[0].heldItemID == 0x10 && drivers[0].itemRollTimer == 90);
	assert(crate->inst->scale.x == 0);
	struct Crate *state = crate->object;
	assert(state->cooldown == 30);
	struct Model savedModel = {0};
	struct ModelHeader savedHeader = {0};
	struct InstDrawPerPlayer *draw = INST_GETIDPP(crate->inst);
	crate->inst->model = &savedModel;
	draw->mh = &savedHeader;
	crate->name = "saved crate address";
	RB_CrateWeapon_RebindRecorded(&tracker);
	assert(crate->inst->model == &crateModel && crate->name == s_recordedCrateName);
	assert(draw->pushBuffer == &tracker.pushBuffer[0] && !draw->mh);
	assert(state->cooldown == 30 && crate->inst->scale.x == 0);
	crate->funcThTick(crate);
	assert(state->cooldown == 30);
	karts[0].matrix.t[2] = 0;
	for (int i = 0; i < 30; i++) crate->funcThTick(crate);
	assert(state->cooldown == 0 && crate->inst->scale.x == 0);
	for (int i = 0; i < 16; i++) crate->funcThTick(crate);
	assert(crate->inst->scale.x == 0x1000);
	drivers[1].instSelf = &karts[1];
	drivers[1].actionsFlagSet = ACTION_BOT;
	drivers[1].heldItemID = 0xf;
	karts[1].matrix.t[2] = 1060;
	crate->funcThTick(crate);
	assert(sounds == 2 && drivers[1].heldItemID == 0xf && crate->inst->scale.x == 0);
	puts("Owned crate breaks on player and bot contact, grants the retail roll to the player, pauses and respawns");

	static struct NavFrame stale;
	for (int i = 1; i < 8; i++)
	{
		drivers[i].actionsFlagSet = ACTION_BOT;
		drivers[i].botData.botPath = i + 2;
		drivers[i].botData.botNavFrame = &stale;
	}
	AP_NavRec_AfterCheckpointRestore();
	assert(s_navrecRecordedMask == 0x3f8);
	for (int i = 1; i < 8; i++)
	{
		struct NavFrame *lane = sdata->NavPath_ptrNavFrameArray[i + 2];
		assert(drivers[i].botData.botNavFrame >= lane && drivers[i].botData.botNavFrame < lane + 32);
	}
	puts("Admitted package restore rebinds all seven bots");

	struct Driver *bot = &drivers[7];
	bot->instSelf = &karts[7];
	struct Thread botThread = {.object = bot, .inst = bot->instSelf};
	bot->driverID = 7;
	sdata->kartSpawnOrderArray[7] = 7;
	BOTS_GotoStartingLine(bot);
	struct NavFrame *first = sdata->NavPath_ptrNavFrameArray[9];
	assert(bot->botData.botNavFrame == first && (bot->botData.botFlags & BOT_FLAG_ESTIMATE_NAV));
	assert(bot->rotCurr.y == ((race.grid[7].rot[1] + 0x400) & 0xff0));
	bot->turbo_MeterRoomLeft = 1;
	tracker.trafficLightsTimer = 480;
	ExerciseRecordedTick(&botThread);
	assert(gridDraws == 1 && bot->turbo_MeterRoomLeft == 1);
	assert(bot->instSelf->matrix.t[0] == race.grid[7].pos[0]);
	assert(bot->posCurr.x == race.grid[7].pos[0] * 256);
	tracker.trafficLightsTimer = 0;
	ExerciseRecordedTick(&botThread);
	assert(gridDraws == 1 && bot->turbo_MeterRoomLeft == 0);
	assert(bot->botData.estimatePosition.x == race.grid[7].pos[0]);
	assert(bot->botData.botNavFrame == first && bot->botData.distToNextNavXZ > 0);
	puts("Dedicated bots hold their own seat until green and then approach the assigned line");

	int savedOrder[BOTS_NAV_LANE_MAX];
	memcpy(savedOrder, s_navrecLaneLapIndex, sizeof savedOrder);
	unsigned int savedSeed = sdata->recordedRaceSeed;
	unsigned int savedSession = sdata->recordedRaceSession;
	bot->botData.botFlags |= BOT_FLAG_STARTLINE_INIT_DONE;
	bot->botData.navProgressRemainder = 32 * 256;
	bot->posCurr.x += 24 * 256;
	s_navrecSeed = savedSeed ^ 0x60c3f1a5u;
	AP_NavRec_PublishRecordedRace(&race, tracker.levelID);
	assert(memcmp(savedOrder, s_navrecLaneLapIndex, sizeof savedOrder) != 0);
	AP_NavRec_AfterCheckpointRestore();
	assert(s_navrecSeed == savedSeed && !memcmp(savedOrder, s_navrecLaneLapIndex, sizeof savedOrder));
	assert(bot->botData.botNavFrame == first && bot->botData.navProgressRemainder == 32 * 256);
	assert(bot->botData.estimatePosition.x == race.grid[7].pos[0]);

	// A checkpoint from another process keeps the running process's new shuffle.
	sdata->recordedRaceSession = savedSession ^ 1u;
	s_navrecSeed = savedSeed ^ 0x60c3f1a5u;
	AP_NavRec_AfterCheckpointRestore();
	assert(s_navrecSeed != savedSeed && sdata->recordedRaceSession == savedSession);
	assert(bot->botData.botNavFrame == first && bot->botData.navProgressRemainder == 0);
	assert(bot->botData.estimatePosition.x == race.grid[7].pos[0] + 24);
	puts("Same-process restore preserves the shuffle and approach progress. A different session rejoins from the saved position.");
}

static void write_lap(void)
{
	struct AP_NavRecNode nodes[32] = {0};
	unsigned int stamps[32], size;
	const struct AP_NavRecNode *paths[] = {nodes};
	const unsigned int *times[] = {stamps};
	const struct AP_NavRecLapInfo lap = {.nodeCount = 32, .lapFrames = 900, .sampleCount = 900, .clean = 1};
	struct AP_NavRecMeta meta = {.levelId = 6, .identityKind = AP_NAVREC_IDENTITY_CUSTOM,
	                            .trackUuid = {1}, .navRevision = 1, .driverName = "Restore fixture"};
	unsigned char bytes[4096];
	make_line(nodes, stamps, 1000);
	assert(AP_NavRecFormat_Write(bytes, sizeof bytes, &meta, &lap, 1, paths, times, &size) == AP_NAVREC_OK);
	assert(mkdir("ap-navpaths", 0700) == 0);
	FILE *out = fopen("ap-navpaths/navpath-6-001.navlap", "wb");
	assert(out && fwrite(bytes, 1, size, out) == size && !fclose(out));
}

static void check_local_restore(int expectRecording)
{
	static struct {
		struct NavHeader header;
		struct NavFrame frames[33];
	} authored[3];
	struct NavHeader *table[3];
	level.LevNavTable = table;
	g_config.navUseRecorded = 1;
	for (int lane = 0; lane < 3; lane++)
	{
		table[lane] = &authored[lane].header;
		table[lane]->magicNumber = -0x1303;
		table[lane]->numPoints = 32;
		table[lane]->last = authored[lane].frames + 32;
		BOTS_InitNavPath(&tracker, lane);
	}
	/* The checkpoint relocator restored pointers to the current process's
	   lane storage before this hook. That storage starts empty here. */
	sdata->NavPath_ptrHeader[0] = &s_navrecLaneHeader[0];
	sdata->NavPath_ptrNavFrameArray[0] = s_navrecLane[0].engine;
	drivers[1].driverID = 1;
	drivers[1].actionsFlagSet = ACTION_BOT;
	drivers[1].botData.botPath = 0;
	drivers[1].botData.botNavFrame = s_navrecLane[0].engine;
	tracker.drivers[1] = &drivers[1];
	tracker.numBotsNextGame = 1;
	g_config.navRecord = 1;
	AP_NavRec_Tick(&tracker);
	assert(!s_navrecArmed && !s_navrecScratch && s_navrecBankCount == 0);
	assert(!s_navrecLanesLive);
	AP_NavRec_AfterCheckpointRestore();
	struct NavFrame *lane = sdata->NavPath_ptrNavFrameArray[0];
	assert(drivers[1].botData.botNavFrame >= lane && drivers[1].botData.botNavFrame < lane + 32);
	assert(!!s_navrecRecordedMask == expectRecording);
	assert((lane == s_navrecLane[0].engine) == expectRecording);
	assert(table[0]->last == authored[0].frames + 32);
	puts(expectRecording ? "Fresh process reloaded its local recording and rebound the bot"
	                     : "Fresh process without the recording rebuilt the authored lane and rebound the bot");
}

int main(int argc, char **argv)
{
	static s16 difficulty[BOTS_DIFFICULTY_PARAM_COUNT];
	assert(argc == 2);
	sdata->gGT = &tracker;
	sdata->arcade_difficultyParams = difficulty;
	tracker.level1 = &level;
	tracker.levelID = 6;
	tracker.gameMode1 = ARCADE_MODE;
	tracker.numPlyrCurrGame = 1;
	if (!strcmp(argv[1], "race")) check_race();
	else if (!strcmp(argv[1], "write")) write_lap();
	else check_local_restore(!strcmp(argv[1], "present"));
}
