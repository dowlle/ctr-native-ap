#include "platform/native_savestate.h"

#include <macros.h>

#if defined(CTR_INTERNAL)
#include <common.h>
#include <limits.h>
#include "platform/native_checkpoint.h"
#include "platform/native_checkpoint_file.h"
#include "platform/native_log.h"
#ifdef CTR_CUSTOM_TRACKS
#include "platform/native_custom_tracks.h"
#endif

#include <errno.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define NATIVE_SAVESTATE_ROOT       "debug"
#define NATIVE_SAVESTATE_DIR        "debug/states"
#define NATIVE_SAVESTATE_QUICK_PATH "debug/states/quick.ctrstates"
#define NATIVE_SAVESTATE_RACE_MAGIC 0x31495351u // QSI1

// F5/F8 append this to the checkpoint payload. Replay checkpoints and the
// checkpoint layout used by other callers stay independent of quick saves.
// Fixed-width fields and zeroed padding make the record directly comparable.
struct NativeSaveStateRaceIdentity
{
	uint64_t roomIdentity;
	int64_t routeLocation;
	u32 magic;
	s32 levelID, players, laps;
	u32 mode1, mode2;
	s32 boss, cup, leg;
	s32 source, plan, planRetail, planMode, pad, slot;
	s32 timeLimitTicks;
	char entryID[65], trackID[65], revision[65];
	char levSha256[65], vrmSha256[65];
	char reserved[11];
};
CTR_STATIC_ASSERT(sizeof(struct NativeSaveStateRaceIdentity) == 416);

global_variable s32 s_quickSaveRequested;
global_variable s32 s_quickLoadRequested;
global_variable u8 *s_quickPayload;
global_variable int s_quickPayloadSize;
global_variable int s_quickRefusalFrames;
global_variable char *s_quickRefusal;

internal int NativeSaveState_SnapshotIdentity(struct NativeSaveStateRaceIdentity *id)
{
	const struct GameTracker *gt = sdata->gGT;

	// A request can already have moved the route latch while the previous
	// level is still on screen. Never pair those facts in a quick save.
	if (gt == NULL || sdata->Loading.stage != LOAD_IDLE)
		return 0;

	memset(id, 0, sizeof *id);
	id->magic = NATIVE_SAVESTATE_RACE_MAGIC;
	id->levelID = gt->levelID;
	id->players = gt->numPlyrCurrGame;
	id->laps = gt->numLaps;
	id->mode1 = gt->gameMode1 & (BATTLE_MODE | MAIN_MENU | TIME_TRIAL | ADVENTURE_MODE |
	                            ADVENTURE_ARENA | ARCADE_MODE | RELIC_RACE | CRYSTAL_CHALLENGE |
	                            ADVENTURE_CUP | GAME_CUTSCENE | ADVENTURE_BOSS);
	id->mode2 = gt->gameMode2 & (TOKEN_RACE | CUP_ANY_KIND | CREDITS);
	if (id->mode1 & ADVENTURE_BOSS)
		id->boss = gt->bossID;
	if ((id->mode1 & ADVENTURE_CUP) || (id->mode2 & CUP_ANY_KIND))
	{
		id->cup = gt->cup.cupID;
		id->leg = gt->cup.trackIndex;
	}
#ifdef CTR_CUSTOM_TRACKS
	const struct CustomTrackRaceContext *race = CustomTrack_StandaloneContext(gt->levelID);
	if (race != NULL)
	{
		id->plan = 1;
		id->roomIdentity = race->roomIdentity;
		id->routeLocation = race->trophyLocation;
		id->planRetail = race->retail;
		id->planMode = race->mode;
		id->pad = race->physicalPad;
		id->slot = race->slot;
		id->timeLimitTicks = race->timeLimitTicks;
		id->leg = race->leg;
		memcpy(id->entryID, race->entryID, sizeof id->entryID);
		memcpy(id->trackID, race->trackID, sizeof id->trackID);
		memcpy(id->revision, race->packageRevision, sizeof id->revision);
		memcpy(id->levSha256, race->levSha256, sizeof id->levSha256);
		memcpy(id->vrmSha256, race->vrmSha256, sizeof id->vrmSha256);
	}
	else
	{
		struct CustomTrackLoadContext load = {
			.levelID = gt->levelID,
			.adventureCupActive = (id->mode1 & ADVENTURE_CUP) != 0,
			.cupID = gt->cup.cupID,
			.bossID = gt->bossID,
			.adventureBossActive = (id->mode1 & ADVENTURE_BOSS) != 0,
		};
		id->source = CustomTrack_SaveStateSource(&load, id->levSha256, id->vrmSha256);
	}
#endif
	return 1;
}

internal int NativeSaveState_Refuse(char *message, const char *reason)
{
	s_quickRefusal = message;
	s_quickRefusalFrames = 180;
	Platform_Log("[CTR State] restore refused: %s\n", reason);
	return 0;
}

void NativeSaveState_DrawRefusal(void)
{
	if (s_quickRefusalFrames > 0)
	{
		s_quickRefusalFrames--;
		DecalFont_DrawLine(s_quickRefusal, 0x100, 0x60, FONT_SMALL, JUSTIFY_CENTER | ORANGE);
	}
}

internal s32 NativeSaveState_PathExists(const char *path)
{
	struct stat st;

	if (path == NULL)
	{
		return 0;
	}

	return stat(path, &st) == 0;
}

internal s32 NativeSaveState_MakeDir(const char *path)
{
	if ((path == NULL) || (path[0] == '\0'))
	{
		return 0;
	}

#if defined(_WIN32)
	if (_mkdir(path) == 0)
#else
	if (mkdir(path, 0755) == 0)
#endif
	{
		return 1;
	}

	return (errno == EEXIST) && NativeSaveState_PathExists(path);
}

internal s32 NativeSaveState_PrepareDir(void)
{
	return NativeSaveState_MakeDir(NATIVE_SAVESTATE_ROOT) && NativeSaveState_MakeDir(NATIVE_SAVESTATE_DIR);
}

internal s32 NativeSaveState_PreparePayload(void)
{
	const int checkpointSize = NativeCheckpoint_GetSize();

	if (checkpointSize <= 0 || checkpointSize > INT_MAX - (int)sizeof(struct NativeSaveStateRaceIdentity))
	{
		Platform_Log("[CTR State] invalid quick state size: %d\n", checkpointSize);
		return 0;
	}

	const int payloadSize = checkpointSize + (int)sizeof(struct NativeSaveStateRaceIdentity);
	if (payloadSize != s_quickPayloadSize)
	{
		u8 *payload = (u8 *)realloc(s_quickPayload, (size_t)payloadSize);

		if (payload == NULL)
		{
			Platform_Log("[CTR State] failed to allocate quick state buffer: %d bytes\n", payloadSize);
			return 0;
		}

		s_quickPayload = payload;
		s_quickPayloadSize = payloadSize;
	}

	return 1;
}

internal s32 NativeSaveState_SaveQuick(void)
{
	struct NativeSaveStateRaceIdentity identity;
	if (!NativeSaveState_SnapshotIdentity(&identity))
	{
		Platform_Log("[CTR State] quick save unavailable during level transitions\n");
		return 0;
	}
	if (!NativeSaveState_PrepareDir())
	{
		Platform_Log("[CTR State] failed to create quick state directory: %s\n", NATIVE_SAVESTATE_DIR);
		return 0;
	}

	if (!NativeSaveState_PreparePayload())
	{
		return 0;
	}

	const int checkpointSize = s_quickPayloadSize - (int)sizeof identity;
	if (!NativeCheckpoint_Capture(s_quickPayload, checkpointSize))
	{
		Platform_Log("[CTR State] failed to capture quick state\n");
		return 0;
	}
	memcpy(s_quickPayload + checkpointSize, &identity, sizeof identity);

	if (!NativeCheckpointFile_WriteSingle(NATIVE_SAVESTATE_QUICK_PATH, s_quickPayload, s_quickPayloadSize, 0, 0))
	{
		Platform_Log("[CTR State] failed to write quick state: %s\n", NATIVE_SAVESTATE_QUICK_PATH);
		return 0;
	}

	Platform_Log("[CTR State] saved quick state: %s\n", NATIVE_SAVESTATE_QUICK_PATH);
	return 1;
}

internal s32 NativeSaveState_LoadQuick(void)
{
	struct NativeCheckpointFileRecordInfo info;
	struct NativeSaveStateRaceIdentity live, saved;

	if (!NativeSaveState_SnapshotIdentity(&live))
		return NativeSaveState_Refuse("STATE NOT LOADED: LEVEL LOADING", "level transition in progress");

	if (!NativeSaveState_PreparePayload())
	{
		return 0;
	}

	if (!NativeCheckpointFile_ReadSingle(NATIVE_SAVESTATE_QUICK_PATH, s_quickPayload, s_quickPayloadSize, &info))
	{
		return NativeSaveState_Refuse("STATE NOT LOADED: UNREADABLE", "missing, damaged or older quick state");
	}

	const int checkpointSize = s_quickPayloadSize - (int)sizeof saved;
	memcpy(&saved, s_quickPayload + checkpointSize, sizeof saved);
	if (saved.magic != NATIVE_SAVESTATE_RACE_MAGIC)
		return NativeSaveState_Refuse("STATE NOT LOADED: UNREADABLE", "quick state has no recognized race identity");
	if (memcmp(&saved, &live, sizeof saved) != 0)
		return NativeSaveState_Refuse("STATE NOT LOADED: OTHER RACE", "quick state belongs to a different race");

	if (!NativeCheckpoint_Restore(s_quickPayload, checkpointSize))
	{
		Platform_Log("[CTR State] failed to restore quick state: %s\n", NATIVE_SAVESTATE_QUICK_PATH);
		return 0;
	}

	s_quickRefusalFrames = 0;
	Platform_Log("[CTR State] loaded quick state checksum=0x%08x: %s\n", info.checksum, NATIVE_SAVESTATE_QUICK_PATH);
	return 1;
}

void NativeSaveState_RequestSave(void)
{
	if (s_quickSaveRequested == 0)
	{
		s_quickSaveRequested = 1;
		Platform_Log("[CTR State] quick save requested\n");
	}
}

void NativeSaveState_RequestLoad(void)
{
	if (s_quickLoadRequested == 0)
	{
		s_quickLoadRequested = 1;
		Platform_Log("[CTR State] quick load requested\n");
	}
}

void NativeSaveState_BeginFrame(void)
{
	if (s_quickSaveRequested != 0)
	{
		s_quickSaveRequested = 0;
		NativeSaveState_SaveQuick();
	}

	if (s_quickLoadRequested != 0)
	{
		s_quickLoadRequested = 0;
		NativeSaveState_LoadQuick();
	}
}
#endif
