// cc -m32 -DCTR_NATIVE -DCTR_INTERNAL -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DCTR_AP -DBUILD=926 -I include -I . tools/test-quick-savestate-race.c platform/native_checkpoint_file.c -o /tmp/test-quick-savestate-race
//
// Drive the production F5/F8 frame path and checkpoint container, using the
// real content race latch and a small stand-in for the engine checkpoint.
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <unistd.h>

#include "../platform/native_savestate.c"

struct sData sdata_static;
static struct GameTracker tracker;
static struct CustomTrackRaceLatch latch = {.lastRequested = -1};
static int restored;
static int captured;
static int mixedAssets;
static int denyOfflinePackage;
static int checkpointRefusal;
static uint64_t roomIdentity;
static char refusal[80];
static struct CustomTrackResidentSource resident;

void Platform_Log(const char *fmt, ...) { (void)fmt; }

void DecalFont_DrawLineOT(char *str, int x, int y, s16 font, int flags, u32 *ot)
{
	(void)x; (void)y; (void)font; (void)flags; (void)ot;
	snprintf(refusal, sizeof refusal, "%s", str);
}

int NativeCheckpoint_GetSize(void) { return sizeof(u32); }

int NativeCheckpoint_Capture(void *dst, int size)
{
	const u32 marker = 42;
	assert(size == NativeCheckpoint_GetSize());
	memcpy(dst, &marker, sizeof marker);
	captured++;
	return 1;
}

int NativeCheckpoint_Restore(const void *src, int size)
{
	u32 marker;
	assert(size == NativeCheckpoint_GetSize());
	memcpy(&marker, src, sizeof marker);
	assert(marker == 42);
	if (denyOfflinePackage)
	{
		checkpointRefusal = 1;
		return 0;
	}
	restored++;
	return 1;
}

void NativeCheckpoint_DrawCustomRefusal(u32 *ot, int visible)
{
	if (checkpointRefusal && visible)
		DecalFont_DrawLineOT("STATE NOT LOADED: OTHER TRACK", 0, 0, 0, 0, ot);
	checkpointRefusal = 0;
}

const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int levelID)
{
	return CustomRaceLatch_Intent(&latch, levelID) ? &latch.current : NULL;
}

const struct CustomTrackResidentSource *CustomTrack_ResidentSource(int levelID)
{
	return resident.kind && resident.levelID == levelID ? &resident : NULL;
}

int CustomTrack_ResidentAssetsMixed(void) { return mixedAssets; }
uint64_t ap_content_plan_room_identity(void) { return roomIdentity; }
static int lookRebinds;
void AP_LooksRebindDrivers(void) { lookRebinds++; }

static void save(void)
{
	NativeSaveState_RequestSave();
	NativeSaveState_BeginFrame();
}

static void loadExpect(const char *message)
{
	const int before = restored;
	const int rebindsBefore = lookRebinds;
	refusal[0] = '\0';
	NativeSaveState_RequestLoad();
	NativeSaveState_BeginFrame();
	NativeSaveState_DrawRefusal(NULL);
	assert(restored == before + (message == NULL));
	// Character looks: every successful restore re-binds driver models.
	assert(lookRebinds == rebindsBefore + (message == NULL));
	assert(strcmp(refusal, message ? message : "") == 0);
}

static void enter(const struct CustomTrackRaceContext *race)
{
	CustomRaceLatch_OnRequest(&latch, 25);
	if (race)
	{
		assert(CustomRaceLatch_Stage(&latch, race));
		tracker.levelID = race->hostLevelID;
	}
	CustomRaceLatch_OnRequest(&latch, tracker.levelID);
}

int main(void)
{
	static const char otherRace[] = "STATE NOT LOADED: OTHER RACE";
	char dir[] = "/tmp/ctr-quick-savestate-XXXXXX";
	struct CustomTrackRaceContext plan = {
		.hostLevelID = 6, .physicalPad = 3, .returnHub = 25, .slot = 1, .laps = 3,
		.seedEpoch = 1, .roomIdentity = 123, .trophyLocation = 35016300,
		.entryID = "pad-3", .trackID = "custom-a", .packageRevision = "1.2.0",
	};
	struct CustomTrackRaceContext other;
	const u32 oldCheckpoint = 42;

	assert(mkdtemp(dir) && chdir(dir) == 0);
	sdata->Loading.stage = LOAD_IDLE;
	sdata->gGT = &tracker;
	tracker.gameMode1 = ADVENTURE_MODE;
	tracker.levelID = 6;
	tracker.numPlyrCurrGame = 1;
	tracker.numLaps = 3;
	memset(plan.levSha256, 'a', 64);
	memset(plan.vrmSha256, 'b', 64);

	save();
	tracker.gameMode1 |= PAUSE_1 | END_OF_RACE;
	loadExpect(NULL);
	tracker.levelID = 7;
	loadExpect(otherRace);
	tracker.levelID = 6;
	tracker.gameMode2 = TOKEN_RACE;
	loadExpect(otherRace);
	tracker.gameMode2 = 0;
	tracker.gameMode1 = ADVENTURE_MODE;

	enter(&plan);
	loadExpect(otherRace); // Retail and custom share host slot 6.
	save();
	loadExpect(NULL);
	sdata->Loading.stage = LOAD_REQUESTED;
	loadExpect("STATE NOT LOADED: LEVEL LOADING");
	sdata->Loading.stage = LOAD_IDLE;
	enter(&plan);
	loadExpect(NULL); // Leaving and returning to the same admission.
	other = plan;
	other.seedEpoch = 2;
	enter(&other);
	loadExpect(NULL); // A session generation counter is not a race identity.
	other = plan;
	strcpy(other.trackID, "custom-b");
	enter(&other);
	loadExpect(otherRace);
	other = plan;
	strcpy(other.packageRevision, "1.3.0");
	enter(&other);
	loadExpect(otherRace);
	other = plan;
	other.levSha256[0] = 'c';
	enter(&other);
	loadExpect(otherRace);
	other = plan;
	other.mode = 1;
	enter(&other);
	loadExpect(otherRace);
	other = plan;
	other.roomIdentity++;
	enter(&other);
	loadExpect(otherRace);
	other = plan;
	other.retail = 1;
	other.slot = 0;
	other.levSha256[0] = other.vrmSha256[0] = '\0';
	enter(&other);
	loadExpect(otherRace);
	save();
	loadExpect(NULL);
	enter(NULL);
	loadExpect(otherRace); // A retail plan pad is distinct from plain retail.

	tracker.levelID = 13;
	resident.levelID = 13;
	resident.kind = 1;
	save();
	loadExpect(NULL);
	resident.kind = 0;
	loadExpect(otherRace); // Oxide Station would otherwise supply the recording identity.

	tracker.gameMode1 = ADVENTURE_MODE | ADVENTURE_BOSS;
	roomIdentity = 123;
	NativeSaveState_BeginLevelLoad();
	save();
	roomIdentity = 124;
	loadExpect(NULL); // Reconnect while the same boss encounter is resident.
	NativeSaveState_BeginLevelLoad();
	loadExpect(otherRace); // Same retail garage, different bound boss encounter.
	save();
	resident.kind = 2;
	strcpy(resident.packageRevision, "1.0.0");
	loadExpect(otherRace); // A custom garage shares the retail garage's engine host.
	save();
	loadExpect(NULL);
	strcpy(resident.packageRevision, "1.1.0");
	loadExpect(otherRace);
	resident.kind = 0;

	save();
	const int beforeMixed = captured;
	mixedAssets = 1;
	save();
	assert(captured == beforeMixed);
	loadExpect("STATE NOT LOADED: MIXED TRACK");
	mixedAssets = 0;
	loadExpect(NULL); // F5 on the mixed load preserved the valid save.

	tracker.levelID = 18;
	tracker.gameMode1 = BATTLE_MODE | POINT_LIMIT;
	save();
	loadExpect(NULL);
	tracker.gameMode1 = BATTLE_MODE | LIFE_LIMIT;
	loadExpect(otherRace);
	tracker.gameMode1 = BATTLE_MODE | TIME_LIMIT;
	loadExpect(otherRace);

	tracker.levelID = 6;
	tracker.gameMode1 = ARCADE_MODE;
	tracker.boolDemoMode = 1;
	save();
	loadExpect(NULL);
	tracker.boolDemoMode = 0;
	loadExpect(otherRace);

	save();
	denyOfflinePackage = 1;
	loadExpect("STATE NOT LOADED: OTHER TRACK");
	denyOfflinePackage = 0;

	assert(NativeCheckpointFile_WriteSingle(NATIVE_SAVESTATE_QUICK_PATH, &oldCheckpoint,
	                                        sizeof oldCheckpoint, 0, 0));
	loadExpect("STATE NOT LOADED: UNREADABLE");
	assert(unlink(NATIVE_SAVESTATE_QUICK_PATH) == 0);
	assert(rmdir(NATIVE_SAVESTATE_DIR) == 0);
	assert(rmdir(NATIVE_SAVESTATE_ROOT) == 0);
	assert(chdir("/") == 0 && rmdir(dir) == 0);
	puts("quick savestate race identity passed");
	return 0;
}
