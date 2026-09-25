// cc -m32 -DCTR_NATIVE -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DBUILD=926 -I include -I . tools/test-custom-exit-window.c
// platform/native_custom_offline.c platform/native_custom_records.c platform/native_custom_content_verify.c -lm -o /tmp/test-custom-exit-window
//
// The custom race's identity lasts exactly as long as its geometry (box
// authoring build). The real MainRaceTrack.c and native_custom_offline.c run
// against a stubbed package and engine. After Quit, Change Level or Change
// Character on the Arcade results menu the race frames keep running on the
// custom geometry until the checkered flag covers the screen, so the custom
// identity must hold through that window and end in MainRaceTrack_StartLoad
// right before LOAD_LevelFile replaces the level. A retry keeps it. The
// savestate identity follows the same lifecycle.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <common.h>
#include <platform/native_custom_offline.h>
#include <platform/native_custom_package.h>
#include <platform/native_custom_content_verify.h>
#include <platform/native_custom_identity.h>
#include <platform/native_custom_state_identity.h>

struct sData sdata_static; // common.h binds sdata to it
struct Data data;          // MainRaceTrack.c's record defaults read character names
static struct GameTracker s_gt;
void CustomTrack_Log(const char *fmt, ...) { (void)fmt; }

// The real file, in this unit: common.h defines sdata per translation unit.
#include "../game/MAIN/MainRaceTrack.c"

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

#define PIN "9999999999999999999999999999999999999999999999999999999999999999"
#define UUID "0f8fad5b-d9cb-469f-a165-70867728950e"
#define LEV_SHA "1111111111111111111111111111111111111111111111111111111111111111"
#define VRM_SHA "2222222222222222222222222222222222222222222222222222222222222222"
#define HOST 6
#define MENU 0x27

// ── package stubs ───────────────────────────────────────────────────────────
struct CustomPackageOwned
{
	int live;
};

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
	out->measured.spawns = 8;
	out->fileAnalysis[CTR_CCV_ARCADE].result = CTR_CCV_DETECTED;
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
static int s_levelFileCalls;
static int s_runtimeActiveAtLevelFile = -1;
static int s_identityAtLevelFile = -1;

void howl_StopAudio(b32 a, b32 b, b32 c) { (void)a, (void)b, (void)c; }
void ElimBG_Deactivate(struct GameTracker *gGT) { (void)gGT; }
int RaceFlag_IsFullyOffScreen(void) { return 0; }
void RaceFlag_BeginTransition(int direction) { (void)direction; }
void RaceFlag_ResetTextAnim(void) {}
void CustomTrack_CortexTrackOnRequestLoad(int levelID) { (void)levelID; }

// What the engine sees at the instant the level is replaced: the level still
// resident is the old one until this call.
void LOAD_LevelFile(int levelID)
{
	s_levelFileCalls++;
	s_runtimeActiveAtLevelFile = CustomOffline_RuntimeActive();
	s_identityAtLevelFile = MainRaceTrack_IdentityLevelID();
	sdata->gGT->prevLEV = sdata->gGT->levelID;
	sdata->gGT->levelID = (s16)levelID;
	sdata->Loading.stage = LOAD_TEN_STAGES_0;
	// Stage 0 (LOAD_TenStages.c:118, 150, 163-166): the upcoming player
	// count goes live; the menu keeps it aside and runs with 4.
	sdata->gGT->numPlyrCurrGame = sdata->gGT->numPlyrNextGame;
	sdata->gGT->gameMode1 &= ~MAIN_MENU;
	if (levelID == MAIN_MENU_LEVEL)
	{
		sdata->gGT->gameMode1 |= MAIN_MENU;
		sdata->gGT->numPlyrNextGame = sdata->gGT->numPlyrCurrGame;
		sdata->gGT->numPlyrCurrGame = 4;
	}
}

// ── helpers ─────────────────────────────────────────────────────────────────
// On the menu the engine runs with 4 players and keeps the race's count in
// numPlyrNextGame (LOAD_TenStages.c:163-166).
static void enter_arcade_menu(void)
{
	s_gt.levelID = MENU;
	s_gt.numPlyrCurrGame = 4;
	s_gt.numPlyrNextGame = 1;
	s_gt.gameMode1 = ARCADE_MODE | MAIN_MENU;
	s_gt.gameMode2 = 0;
	sdata->Loading.stage = LOAD_IDLE;
}

// The selector's handoff (MM_CustomTrackSelect.c): an owned request goes to the
// runtime on the host slot, then the menu loads the host slot.
static void start_custom_race(void)
{
	struct CustomPackageOwned *package = calloc(1, sizeof *package);
	struct CustomOfflineRequest *request = NULL;
	char error[128];

	CHECK(CustomOffline_Prepare(&package, PIN, &request, error, sizeof error) == 1);
	CHECK(CustomOffline_BeginRuntime(&request, HOST, 0, CTR_OFFLINE_MODE_ARCADE) == 1);
	MainRaceTrack_RequestLoad(HOST);
	MainRaceTrack_StartLoad(HOST);
	sdata->Loading.stage = LOAD_IDLE;
}

static int state_kind(void)
{
	struct CustomStateIdentity id;
	CustomOffline_RuntimeStateIdentity(&id);
	return CustomStateIdentity_Valid(&id) ? (int)id.kind : -1;
}

int main(void)
{
	sdata->gGT = &s_gt;

	// No custom race: a request and a load change nothing.
	enter_arcade_menu();
	CHECK(state_kind() == CTR_CUSTOM_STATE_NONE);
	MainRaceTrack_RequestLoad(HOST);
	MainRaceTrack_StartLoad(HOST);
	CHECK(s_runtimeActiveAtLevelFile == 0);
	CHECK(!MainRaceTrack_OfflineCustomLoad());
	CHECK(MainRaceTrack_IdentityLevelID() == HOST);

	// A custom race on the host slot.
	enter_arcade_menu();
	start_custom_race();
	CHECK(s_runtimeActiveAtLevelFile == 1);
	CHECK(s_gt.levelID == HOST);
	CHECK(s_gt.numLaps == CTR_CUSTOM_DEFAULT_LAPS);
	CHECK(MainRaceTrack_OfflineCustomLoad());
	CHECK(MainRaceTrack_IdentityLevelID() == CTR_CUSTOM_LEVEL_ID);
	CHECK(state_kind() == CTR_CUSTOM_STATE_PACKAGE);
	{
		struct CustomStateIdentity live, expected;
		CustomOffline_RuntimeStateIdentity(&live);
		CustomStateIdentity_Package(&expected, UUID, LEV_SHA, VRM_SHA);
		expected.mode = CTR_OFFLINE_MODE_ARCADE;
		CHECK(CustomStateIdentity_RestoreAllowed(&expected, &live));
		expected.mode = CTR_OFFLINE_MODE_TIME_TRIAL; // same track, other race mode
		CHECK(!CustomStateIdentity_RestoreAllowed(&expected, &live));
	}

	// Quit on the Arcade results menu: the request alone keeps the identity.
	// These are the frames before the checkered flag covers the screen; the
	// race logic still runs on the custom geometry with levelID on the host.
	MainRaceTrack_RequestLoad(MENU);
	CHECK(sdata->Loading.stage == LOAD_REQUESTED);
	CHECK(CustomOffline_RuntimeActive());
	CHECK(s_gt.levelID == HOST);
	CHECK(MainRaceTrack_OfflineCustomLoad());
	CHECK(MainRaceTrack_IdentityLevelID() == CTR_CUSTOM_LEVEL_ID);
	CHECK(state_kind() == CTR_CUSTOM_STATE_PACKAGE);
	// Not Roo's Tubes: no hardcoded ambience or bubbles for this id.
	CHECK(MainRaceTrack_IdentityLevelID() >= CTR_CUSTOM_AMBIENT_LEVEL_LIMIT);
	CHECK(MainRaceTrack_IdentityLevelID() != ROO_TUBES);

	// The flag covers the screen: the load starts, and the custom race ends
	// right before the level is replaced, not a frame earlier or later.
	s_levelFileCalls = 0;
	MainRaceTrack_StartLoad(MENU);
	CHECK(s_levelFileCalls == 1);
	CHECK(s_runtimeActiveAtLevelFile == 0);
	CHECK(s_identityAtLevelFile == HOST); // levelID not yet replaced, runtime already over
	CHECK(s_gt.levelID == MENU);
	CHECK(!CustomOffline_RuntimeActive());
	CHECK(!MainRaceTrack_OfflineCustomLoad());
	CHECK(MainRaceTrack_IdentityLevelID() == MENU);
	CHECK(state_kind() == CTR_CUSTOM_STATE_NONE);

	// A later retail load of the host slot is retail.
	sdata->Loading.stage = LOAD_IDLE;
	MainRaceTrack_RequestLoad(HOST);
	MainRaceTrack_StartLoad(HOST);
	CHECK(s_runtimeActiveAtLevelFile == 0);
	CHECK(!MainRaceTrack_OfflineCustomLoad());
	CHECK(MainRaceTrack_IdentityLevelID() == HOST);

	// Change Level / Change Character behave as Quit: same request, same end.
	enter_arcade_menu();
	start_custom_race();
	MainRaceTrack_RequestLoad(MENU);
	CHECK(MainRaceTrack_IdentityLevelID() == CTR_CUSTOM_LEVEL_ID);
	MainRaceTrack_StartLoad(MENU);
	CHECK(!CustomOffline_RuntimeActive());

	// A retry reloads the host slot and keeps the custom race.
	enter_arcade_menu();
	start_custom_race();
	MainRaceTrack_RequestLoad(HOST);
	CHECK(CustomOffline_RuntimeActive());
	MainRaceTrack_StartLoad(HOST);
	CHECK(s_runtimeActiveAtLevelFile == 1);
	CHECK(MainRaceTrack_OfflineCustomLoad());
	CHECK(MainRaceTrack_IdentityLevelID() == CTR_CUSTOM_LEVEL_ID);

	// The latest request is the one that loads: leave, then retry before the
	// flag is down, keeps the race; retry, then leave, ends it at the load.
	MainRaceTrack_RequestLoad(MENU);
	MainRaceTrack_RequestLoad(HOST);
	MainRaceTrack_StartLoad(HOST);
	CHECK(s_runtimeActiveAtLevelFile == 1);
	CHECK(MainRaceTrack_OfflineCustomLoad());
	MainRaceTrack_RequestLoad(HOST);
	MainRaceTrack_RequestLoad(MENU);
	CHECK(MainRaceTrack_OfflineCustomLoad());
	MainRaceTrack_StartLoad(MENU);
	CHECK(s_runtimeActiveAtLevelFile == 0);
	CHECK(!CustomOffline_RuntimeActive());

	// A second custom race can begin once the first has ended.
	enter_arcade_menu();
	start_custom_race();
	CHECK(MainRaceTrack_OfflineCustomLoad());
	MainRaceTrack_RequestLoad(MENU);
	MainRaceTrack_StartLoad(MENU);
	CHECK(!CustomOffline_RuntimeActive());

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
