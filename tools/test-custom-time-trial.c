// cc -m32 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_NATIVE -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DBUILD=926 -I include -I . tools/test-custom-time-trial.c
// platform/native_custom_offline.c platform/native_custom_records.c platform/native_custom_content_verify.c -lm -o /tmp/test-custom-time-trial
//
// A Time Trial on a custom track (box authoring build) never reaches the
// retail save data. The real MainRaceTrack.c, GAMEPROG.c, MainGameEnd.c and
// GhostTape.c run against a stubbed package on host slot 6, the slot a custom
// race loads through, which is Roo's Tubes for everything retail:
//
// - the high-score pointer names the package's own table, loaded from
//   custom-records/, never gameProgress.highScoreTracks[6];
// - the end of the race computes the new best times against that table and
//   never writes Roo's Tubes' Time Trial flags, N. Tropy state or unlocks;
// - saving the name stores the time in the package table and its file;
// - the ghost is stamped with the custom level id and saved to custom-records/
//   after a run that beat the loaded ghost, and reads back on the next attempt;
// - the same calls without a custom runtime still write the retail table, so
//   the harness would notice if the redirection were always on.
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <common.h>
#include <platform/native_custom_offline.h>
#include <platform/native_custom_package.h>
#include <platform/native_custom_content_verify.h>
#include <platform/native_custom_identity.h>
#include <platform/native_custom_state_identity.h>
#include <platform/native_custom_records.h>

struct sData sdata_static; // common.h binds sdata to it
struct Data data;
static struct GameTracker s_gt;
void CustomTrack_Log(const char *fmt, ...) { (void)fmt; }

#include "../game/MAIN/MainRaceTrack.c"
#include "../game/GAMEPROG.c"
#include "../game/MAIN/MainGameEnd.c"
#include "../game/GhostTape.c"

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

#define PIN "9999999999999999999999999999999999999999999999999999999999999999"
#define UUID "0f8fad5b-d9cb-469f-a165-70867728950e"
#define LEV_SHA "1111111111111111111111111111111111111111111111111111111111111111"
#define VRM_SHA "2222222222222222222222222222222222222222222222222222222222222222"
#define HOST ROO_TUBES
#define MENU 0x27

// ── package stubs: a Time Trial-only track (no AI paths) ───────────────────
struct CustomPackageOwned { int live; };

int CustomPackage_GetManifest(const struct CustomPackageOwned *package, struct CustomPackageManifest *out)
{
	if (package == NULL || out == NULL)
		return 0;
	memset(out, 0, sizeof *out);
	strcpy(out->sha256, PIN);
	strcpy(out->uuid, UUID);
	strcpy(out->title, "Harness Track");
	out->count = 2;
	strcpy(out->files[0].role, "lev");
	strcpy(out->files[0].sha256, LEV_SHA);
	out->files[0].bytes = 1024;
	strcpy(out->files[1].role, "vrm");
	strcpy(out->files[1].sha256, VRM_SHA);
	out->files[1].bytes = 2048;
	return 1;
}

int CustomPackage_GetPairReport(const struct CustomPackageOwned *package, struct CustomContentVerification *out)
{
	if (package == NULL || out == NULL)
		return 0;
	memset(out, 0, sizeof *out);
	out->loadable = 1;
	out->measured.checkpoints = 130;
	out->fileAnalysis[CTR_CCV_TIME_TRIAL].result = CTR_CCV_DETECTED;
	out->fileAnalysis[CTR_CCV_ARCADE].result = CTR_CCV_NOT_DETECTED;
	return 1;
}

int CustomPackage_GetRaceLaps(const struct CustomPackageOwned *package, unsigned int *laps, char *error, size_t errorSize)
{
	(void)package, (void)error, (void)errorSize;
	*laps = 0;
	return 0;
}

int CustomPackage_CopyFile(const struct CustomPackageOwned *package, const char *role, void *destination,
                           size_t capacity, size_t *written)
{
	(void)package, (void)role, (void)destination, (void)capacity;
	if (written)
		*written = 0;
	return 1;
}

int CustomPackage_Retain(struct CustomPackageOwned *package, struct CustomPackageOwned **out)
{
	*out = package;
	return 1;
}

void CustomPackage_Free(struct CustomPackageOwned **package)
{
	if (package && *package)
	{
		free(*package);
		*package = NULL;
	}
}

// ── engine stubs ────────────────────────────────────────────────────────────
void howl_StopAudio(b32 a, b32 b, b32 c) { (void)a, (void)b, (void)c; }
void ElimBG_Deactivate(struct GameTracker *gGT) { (void)gGT; }
int RaceFlag_IsFullyOffScreen(void) { return 0; }
void RaceFlag_BeginTransition(int direction) { (void)direction; }
void RaceFlag_ResetTextAnim(void) {}
void CustomTrack_CortexTrackOnRequestLoad(int levelID) { (void)levelID; }
void RR_EndEvent_UnlockAward(void) {}
void OtherFX_Stop2(int soundID_count) { (void)soundID_count; }
void SubmitName_RestoreName(s16 param_1) { (void)param_1; }
void UI_VsQuipAssignAll(void) {}
void Podium_InitModels(struct GameTracker *gGT) { (void)gGT; }
void BOTS_Driver_Convert(struct Driver *driver) { (void)driver; }
void UI_RaceEnd_GetDriverClock(struct Driver *d) { (void)d; }
void LOAD_LevelFile(int levelID)
{
	sdata->gGT->levelID = (s16)levelID;
}

static char s_lng[64][24];
static char *s_lngPtrs[0x400];
static struct Driver s_player;
static struct Instance s_playerInst;
static struct Thread s_playerThread;
static unsigned char s_ghostRecord[0x3e00];
static unsigned char s_ghostPlaying[0x3e00];

static void start_time_trial(void)
{
	struct CustomPackageOwned *package = calloc(1, sizeof *package);
	struct CustomOfflineRequest *request = NULL;
	char error[128];

	s_gt.levelID = MENU;
	s_gt.numPlyrCurrGame = 1;
	s_gt.gameMode1 = TIME_TRIAL;
	s_gt.gameMode2 = 0;
	CHECK(CustomOffline_Prepare(&package, PIN, &request, error, sizeof error) == 1);
	// An Arcade start is refused: no AI paths.
	CHECK(CustomOffline_CheckStructure(request, CTR_OFFLINE_MODE_ARCADE, error, sizeof error) == 0);
	CHECK(strcmp(error, "No AI paths") == 0);
	CHECK(CustomOffline_BeginRuntime(&request, HOST, 0, CTR_OFFLINE_MODE_TIME_TRIAL) == 1);
	MainRaceTrack_RequestLoad(HOST);
	MainRaceTrack_StartLoad(HOST);
	GAMEPROG_GetPtrHighScoreTrack(); // what MainMain.c does when the load finishes
}

static void end_race(int raceTime)
{
	s_player.timeElapsedInRace = raceTime;
	s_gt.lapTime[0] = raceTime / 3 + 100;
	s_gt.lapTime[1] = raceTime / 3;
	s_gt.lapTime[2] = raceTime - s_gt.lapTime[0] - s_gt.lapTime[1];
	s_gt.gameModeEnd = s_gt.gameMode1 & GAME_MODE_END_RETAINED_MODE_MASK;
	MainGameEnd_SoloRaceGetReward(1);
}

int main(void)
{
	char dir[] = "/tmp/ctr-tt-records-XXXXXX";
	struct GameProgress before;
	int i;

	CHECK(mkdtemp(dir) != NULL);
	CHECK(chdir(dir) == 0); // custom-records/ is relative, next to the exe

	sdata->gGT = &s_gt;
	sdata->lngStrings = s_lngPtrs;
	for (i = 0; i < 64; i++)
	{
		snprintf(s_lng[i], sizeof s_lng[i], "NAME%d", i);
		s_lngPtrs[i] = s_lng[i];
	}
	for (i = 0; i < 16; i++)
		data.MetaDataCharacters[i].name_LNG_short = (s16)i;
	s_gt.numLaps = 3;
	s_gt.drivers[0] = &s_player;
	s_player.instSelf = &s_playerInst; // GhostTape_WriteMoves reads the kart's position
	s_gt.threadBuckets[PLAYER].thread = &s_playerThread;
	sdata->GhostRecording.ptrEndOffset = (char *)s_ghostRecord + sizeof s_ghostRecord;
	s_playerThread.object = &s_player;
	data.metaDataLEV[HOST].timeTrial = 0x7fffffff; // N. Tropy would always be beaten
	sdata->GhostRecording.ptrGhost = (struct GhostHeader *)s_ghostRecord;
	sdata->GhostRecording.ptrStartOffset = GHOSTHEADER_GETRECORDBUFFER(s_ghostRecord);
	sdata->ptrGhostTapePlaying = (struct GhostHeader *)s_ghostPlaying;
	GAMEPROG_ResetHighScores(&sdata->gameProgress);
	before = sdata->gameProgress;

	// ── a custom Time Trial ────────────────────────────────────────────────
	start_time_trial();
	CHECK(MainRaceTrack_OfflineCustomLoad());
	CHECK(MainRaceTrack_OfflineCustomTimeTrial());
	CHECK(MainRaceTrack_IdentityLevelID() == CTR_CUSTOM_LEVEL_ID);
	CHECK(s_gt.numLaps == CTR_CUSTOM_DEFAULT_LAPS);
	// The table is the package's, not inside the retail save data.
	CHECK((char *)sdata->ptrActiveHighScoreEntry < (char *)&sdata->gameProgress ||
	      (char *)sdata->ptrActiveHighScoreEntry >= (char *)(&sdata->gameProgress + 1));
	CHECK(sdata->ptrActiveHighScoreEntry == MainRaceTrack_CustomHighScores());
	CHECK(sdata->ptrActiveHighScoreEntry[1].time == 0x8c640); // no file yet: defaults
	CHECK(strcmp(sdata->ptrActiveHighScoreEntry[1].name, "NAME1") == 0);

	// The ghost is stamped with the custom identity, not Roo's Tubes.
	GhostTape_Start();
	CHECK(((struct GhostHeader *)s_ghostRecord)->levelID == CTR_CUSTOM_LEVEL_ID);

	// The race ends with a new best time and lap.
	end_race(60000);
	CHECK((s8)s_gt.newHighScoreIndex == 0);
	CHECK((s_gt.gameModeEnd & NEW_HIGH_SCORE) != 0 && (s_gt.gameModeEnd & NEW_BEST_LAP) != 0);
	// Nothing of Roo's Tubes changed: flags, times, unlocks, all of it.
	CHECK(memcmp(&before, &sdata->gameProgress, sizeof before) == 0);
	CHECK((s_gt.gameModeEnd & (NTROPY_JUST_OPENED | NTROPY_JUST_BEAT)) == 0);

	// Saving the name stores the time in the package's table and its file.
	strcpy(s_gt.prevNameEntered, "TESTRACER");
	MainGameEnd_SoloRaceSaveHighScore();
	MainRaceTrack_CustomSaveHighScores();
	CHECK(sdata->ptrActiveHighScoreEntry[1].time == 60000);
	CHECK(strcmp(sdata->ptrActiveHighScoreEntry[1].name, "TESTRACER") == 0);
	CHECK(sdata->ptrActiveHighScoreEntry[0].time == (u32)s_gt.lapTime[2]); // the fastest of the three
	CHECK(memcmp(&before, &sdata->gameProgress, sizeof before) == 0);
	{
		struct CustomRecordKey key;
		struct CustomRecordTimes times;
		CHECK(CustomRecords_MakeKey(&key, UUID, LEV_SHA, VRM_SHA, CTR_CUSTOM_DEFAULT_LAPS));
		CHECK(CustomRecords_LoadTimes(CTR_RECORDS_DIR, &key, &times));
		CHECK(times.entry[1].time == 60000 && strcmp(times.entry[1].name, "TESTRACER") == 0);
		CHECK(times.entry[2].time == 0x8c640);
	}

	// The run's ghost goes to custom-records/ and reads back.
	{
		struct GhostHeader *gh = (struct GhostHeader *)s_ghostRecord;
		memset(GHOSTHEADER_GETRECORDBUFFER(gh), 0x5a, 0x200);
		gh->version = -4;
		gh->size = 0x200;
		gh->characterID = TINY_TIGER;
		gh->timeElapsedInRace = 60000;
		MainRaceTrack_CustomSaveGhost(gh);
		memset(s_ghostPlaying, 0, sizeof s_ghostPlaying);
		CHECK(MainRaceTrack_CustomLoadGhost((struct GhostHeader *)s_ghostPlaying) == 1);
		CHECK(memcmp(s_ghostPlaying, s_ghostRecord, sizeof(struct GhostHeader) + 0x200) == 0);
		CHECK(((struct GhostHeader *)s_ghostPlaying)->levelID == CTR_CUSTOM_LEVEL_ID);
		// A damaged engine header in a file with a valid digest is still refused.
		gh->version = 3;
		{
			struct CustomRecordKey key;
			CHECK(CustomRecords_MakeKey(&key, UUID, LEV_SHA, VRM_SHA, CTR_CUSTOM_DEFAULT_LAPS));
			CHECK(CustomRecords_SaveGhost(CTR_RECORDS_DIR, &key, gh, sizeof(struct GhostHeader) + 0x200));
		}
		CHECK(MainRaceTrack_CustomLoadGhost((struct GhostHeader *)s_ghostPlaying) == 0);
		CHECK(((struct GhostHeader *)s_ghostPlaying)->version == 0);
		gh->version = -4;
		MainRaceTrack_CustomSaveGhost(gh);
	}

	// The engine's end of race (MainGameEnd_Initialize): with no ghost loaded,
	// the run is the new best ghost and goes to custom-records/ by itself.
	{
		struct GhostHeader *gh = (struct GhostHeader *)s_ghostRecord;
		struct CustomRecordKey key;
		char path[256], stem[CTR_RECORDS_STEM_MAX];
		CHECK(CustomRecords_MakeKey(&key, UUID, LEV_SHA, VRM_SHA, CTR_CUSTOM_DEFAULT_LAPS));
		CHECK(CustomRecords_Stem(&key, stem, sizeof stem));
		snprintf(path, sizeof path, CTR_RECORDS_DIR "/%s.ghost", stem);
		remove(path);
		GhostTape_Start();
		gh->size = 0; // GhostTape_End measures it from the write pointer
		sdata->GhostRecording.ptrCurrOffset = sdata->GhostRecording.ptrStartOffset + 0x300;
		memset(sdata->GhostRecording.ptrStartOffset, 0x33, 0x300);
		sdata->boolReplayHumanGhost = 0;
		sdata->boolGhostTooBigToSave = 0;
		s_player.timeElapsedInRace = 58000;
		s_gt.gameMode1 = TIME_TRIAL;
		MainGameEnd_Initialize();
		CHECK((s_gt.gameModeEnd & PLAYER_GHOST_BEAT) != 0);
		CHECK(MainRaceTrack_CustomLoadGhost((struct GhostHeader *)s_ghostPlaying) == 1);
		CHECK(((struct GhostHeader *)s_ghostPlaying)->timeElapsedInRace == 58000);
		CHECK(memcmp(&before, &sdata->gameProgress, sizeof before) == 0);
		// A run slower than the loaded ghost keeps the file as it was.
		sdata->boolReplayHumanGhost = 1;
		GhostTape_Start();
		sdata->GhostRecording.ptrCurrOffset = sdata->GhostRecording.ptrStartOffset + 0x100;
		s_player.timeElapsedInRace = 59000;
		s_gt.gameMode1 = TIME_TRIAL;
		MainGameEnd_Initialize();
		CHECK((s_gt.gameModeEnd & PLAYER_GHOST_BEAT) == 0);
		CHECK(MainRaceTrack_CustomLoadGhost((struct GhostHeader *)s_ghostPlaying) == 1);
		CHECK(((struct GhostHeader *)s_ghostPlaying)->timeElapsedInRace == 58000);
		// A run too long for the ghost buffer is never written.
		sdata->boolReplayHumanGhost = 0;
		GhostTape_Start();
		sdata->GhostRecording.ptrCurrOffset = sdata->GhostRecording.ptrStartOffset + 0x100;
		sdata->boolGhostTooBigToSave = 1;
		s_player.timeElapsedInRace = 50000;
		s_gt.gameMode1 = TIME_TRIAL;
		MainGameEnd_Initialize();
		CHECK(MainRaceTrack_CustomLoadGhost((struct GhostHeader *)s_ghostPlaying) == 1);
		CHECK(((struct GhostHeader *)s_ghostPlaying)->timeElapsedInRace == 58000);
		sdata->boolGhostTooBigToSave = 0;
		s_gt.gameMode1 = TIME_TRIAL;
	}

	// Leave to the menu: the next attempt reads the saved table fresh.
	MainRaceTrack_RequestLoad(MENU);
	MainRaceTrack_StartLoad(MENU);
	CHECK(!CustomOffline_RuntimeActive());
	start_time_trial();
	CHECK(sdata->ptrActiveHighScoreEntry[1].time == 60000);
	CHECK(MainRaceTrack_CustomLoadGhost((struct GhostHeader *)s_ghostPlaying) == 1);
	end_race(70000); // slower: second place
	CHECK((s8)s_gt.newHighScoreIndex == 1);
	CHECK(memcmp(&before, &sdata->gameProgress, sizeof before) == 0);
	MainRaceTrack_RequestLoad(MENU);
	MainRaceTrack_StartLoad(MENU);

	// An Arcade race of the same package does not serve in Time Trial and the
	// other way round (the runtime keeps the mode it was started for).
	{
		struct CustomPackageOwned *package = calloc(1, sizeof *package);
		struct CustomOfflineRequest *request = NULL;
		char error[128];
		CHECK(CustomOffline_Prepare(&package, PIN, &request, error, sizeof error) == 1);
		CHECK(CustomOffline_BeginRuntime(&request, HOST, 0, CTR_OFFLINE_MODE_TIME_TRIAL) == 1);
		s_gt.levelID = HOST;
		s_gt.gameMode1 = ARCADE_MODE;
		CHECK(!MainRaceTrack_OfflineCustomLoad());
		CHECK(MainRaceTrack_IdentityLevelID() == HOST);
		s_gt.gameMode1 = TIME_TRIAL | RELIC_RACE;
		CHECK(!MainRaceTrack_OfflineCustomLoad());
		s_gt.gameMode1 = TIME_TRIAL;
		CHECK(MainRaceTrack_OfflineCustomLoad());
		s_gt.numPlyrCurrGame = 2;
		CHECK(!MainRaceTrack_OfflineCustomLoad());
		s_gt.numPlyrCurrGame = 1;
		CustomOffline_EndRuntime();
	}

	// ── control: a retail Time Trial on the same slot writes retail data ──
	s_gt.levelID = HOST;
	s_gt.gameMode1 = TIME_TRIAL;
	CHECK(!MainRaceTrack_OfflineCustomLoad());
	GAMEPROG_GetPtrHighScoreTrack();
	CHECK(sdata->ptrActiveHighScoreEntry == &sdata->gameProgress.highScoreTracks[HOST].scoreEntry[0]);
	GhostTape_Start();
	CHECK(((struct GhostHeader *)s_ghostRecord)->levelID == HOST);
	end_race(60000);
	CHECK(sdata->gameProgress.highScoreTracks[HOST].timeTrialFlags != before.highScoreTracks[HOST].timeTrialFlags);
	MainGameEnd_SoloRaceSaveHighScore();
	CHECK(sdata->gameProgress.highScoreTracks[HOST].scoreEntry[1].time == 60000);

	{
		char cmd[128];
		snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
		if (chdir("/") != 0 || system(cmd) != 0)
			printf("note: could not remove %s\n", dir);
	}
	printf("custom time trial: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
