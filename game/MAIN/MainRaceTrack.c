#include <common.h>

#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h>
#endif
#ifdef CTR_CUSTOM_PACKAGES
#include <platform/native_custom_offline.h>
#include <platform/native_custom_package.h>
#include <platform/native_custom_identity.h>
#include <platform/native_custom_state_identity.h>
#include <platform/native_custom_records.h>
CTR_STATIC_ASSERT(sizeof(((struct GameTracker *)0)->lapTime) / sizeof(int) == CTR_OFFLINE_MAX_LAPS);

// Box authoring build: a package chosen on the Arcade or Time Trial custom
// pages races only as a one-player Arcade single race or a Time Trial on its
// host slot, and only in the mode it was started for. Every other mode on that
// slot loads retail bytes.
static int MainRaceTrack_OfflineSingleRaceMode(void)
{
	struct GameTracker *gGT = sdata->gGT;
	if (gGT->numPlyrCurrGame != 1 || (gGT->gameMode2 & CUP_ANY_KIND))
		return 0;
	if ((gGT->gameMode1 & ARCADE_MODE) &&
	    !(gGT->gameMode1 & (ADVENTURE_MODE | ADVENTURE_CUP | ADVENTURE_ARENA | TIME_TRIAL | BATTLE_MODE)))
		return CTR_OFFLINE_MODE_ARCADE;
	if ((gGT->gameMode1 & TIME_TRIAL) &&
	    !(gGT->gameMode1 & (ADVENTURE_MODE | ADVENTURE_CUP | ADVENTURE_ARENA | ARCADE_MODE | BATTLE_MODE |
	                        RELIC_RACE | CRYSTAL_CHALLENGE)))
		return CTR_OFFLINE_MODE_TIME_TRIAL;
	return 0;
}

int MainRaceTrack_OfflineCustomLoad(void)
{
	return CustomOffline_RuntimeServing(sdata->gGT->levelID, MainRaceTrack_OfflineSingleRaceMode());
}

// Engine hooks for the custom runtime. They hand it the engine's race mode
// (CTR_OFFLINE_MODE_*), which is what CustomOffline_RuntimeServing compares
// with the mode the runtime started for. Passing the serving yes/no instead
// (1) matched Arcade by accident and never Time Trial (2), so a custom Time
// Trial read Roo's Tubes from the disc under the custom identity.
int MainRaceTrack_OfflineRuntimeFile(int subfileIndex, size_t *size)
{
	return CustomOffline_RuntimeFile(subfileIndex, sdata->gGT->levelID, MainRaceTrack_OfflineSingleRaceMode(), size);
}

void MainRaceTrack_OfflineLoadFinished(void)
{
	CustomOffline_OnLoadFinished(sdata->gGT->levelID, MainRaceTrack_OfflineSingleRaceMode());
}

void MainRaceTrack_OfflineResidentRestart(void)
{
	CustomOffline_OnResidentRestart(sdata->gGT->levelID, MainRaceTrack_OfflineSingleRaceMode());
}

void MainRaceTrack_OfflineRaceFinished(int humanDriver)
{
	CustomOffline_OnRaceFinished(sdata->gGT->levelID, MainRaceTrack_OfflineSingleRaceMode(), humanDriver);
}

// A custom race whose package the host slot would not serve is refused before
// anything loads: racing the slot's retail geometry (Roo's Tubes) under the
// custom identity must never happen. One log line says why, the runtime ends,
// and the menu comes back on the track list. Returns 1 when refused.
static int MainRaceTrack_OfflineRefuseLoad(int levelID)
{
	char reason[128], line[256];
	int mode = CustomOffline_RuntimeMode();

	if (!CustomOffline_RuntimeRefusal(levelID, MainRaceTrack_OfflineSingleRaceMode(), reason, sizeof reason))
		return 0;
	snprintf(line, sizeof line, "[CustomTracks] custom %s refused: %s; back to the track list\n",
	         mode == CTR_OFFLINE_MODE_TIME_TRIAL ? "Time Trial" : "Arcade race", reason);
	CustomTrack_Log(line);
	CustomOffline_EndRuntime();
	sdata->mainMenuState = MAIN_MENU_TRACK_SELECT;
	sdata->gGT->gameMode1 |= MAIN_MENU;
	return 1;
}

// The level id identity-keyed code should see: CTR_CUSTOM_LEVEL_ID during a
// custom-page race, the engine's level id otherwise. The host slot stays the
// BIGFILE loading vehicle in gGT->levelID; this is what everything else uses.
int MainRaceTrack_IdentityLevelID(void)
{
	return CustomIdentity_LevelID(sdata->gGT->levelID, MainRaceTrack_OfflineCustomLoad());
}

// A Time Trial on a custom track: its best times and ghost go to
// custom-records/ (native_custom_records.h), never to the memory card or the
// retail high-score table.
int MainRaceTrack_OfflineCustomTimeTrial(void)
{
	return MainRaceTrack_OfflineCustomLoad() && MainRaceTrack_OfflineSingleRaceMode() == CTR_OFFLINE_MODE_TIME_TRIAL;
}

// The records key of the active package: UUID, LEV and VRM digests, laps.
// Valid from the moment the custom page starts the runtime, so the ghost can
// be read in the menu before the level loads.
static int MainRaceTrack_CustomRecordKey(struct CustomRecordKey *key)
{
	struct CustomStateIdentity id;
	CustomOffline_RuntimeStateIdentity(&id);
	return CustomOffline_RuntimeMode() == CTR_OFFLINE_MODE_TIME_TRIAL && id.kind == CTR_CUSTOM_STATE_PACKAGE &&
	       CustomRecords_MakeKey(key, id.uuid, id.levSha256, id.vrmSha256, (unsigned int)CustomOffline_RuntimeLaps());
}

// The Time Trial half of the table (best lap, five best races) for the running
// package, loaded once per custom run. Tracks without a file start from the
// retail defaults (9:59.99, six characters). The Relic half stays default and
// is never saved: a custom track has no relic race.
static struct HighScoreEntry s_customHighScores[12];
static uint64_t s_customHighScoresRun;

struct HighScoreEntry *MainRaceTrack_CustomHighScores(void)
{
	struct CustomRecordKey key;
	struct CustomRecordTimes times;
	uint64_t run = CustomOffline_RuntimeGeneration();
	int i;

	if (run != s_customHighScoresRun)
	{
		s_customHighScoresRun = run;
		for (i = 0; i < 12; i++)
		{
			int characterID = i % 6;
			s_customHighScores[i].time = 0x8c640;
			s_customHighScores[i].characterID = characterID;
			snprintf(s_customHighScores[i].name, sizeof s_customHighScores[i].name, "%s",
			         sdata->lngStrings ? sdata->lngStrings[data.MetaDataCharacters[characterID].name_LNG_short] : "");
		}
		if (MainRaceTrack_CustomRecordKey(&key) && CustomRecords_LoadTimes(CTR_RECORDS_DIR, &key, &times))
		{
			for (i = 0; i < CTR_RECORDS_ENTRIES; i++)
			{
				if (times.entry[i].characterID > NITROS_OXIDE)
					continue;
				s_customHighScores[i].time = times.entry[i].time;
				s_customHighScores[i].characterID = (u16)times.entry[i].characterID;
				memcpy(s_customHighScores[i].name, times.entry[i].name, sizeof s_customHighScores[i].name);
			}
			CustomTrack_Log("[CustomRecords] best times loaded for this track\n");
		}
	}
	return s_customHighScores;
}

void MainRaceTrack_CustomSaveHighScores(void)
{
	struct CustomRecordKey key;
	struct CustomRecordTimes times;
	int i;

	if (!MainRaceTrack_CustomRecordKey(&key))
		return;
	memset(&times, 0, sizeof times);
	for (i = 0; i < CTR_RECORDS_ENTRIES; i++)
	{
		times.entry[i].time = s_customHighScores[i].time;
		times.entry[i].characterID = s_customHighScores[i].characterID;
		memcpy(times.entry[i].name, s_customHighScores[i].name, sizeof times.entry[i].name);
		times.entry[i].name[CTR_RECORDS_NAME - 1] = 0;
	}
	CustomTrack_Log(CustomRecords_SaveTimes(CTR_RECORDS_DIR, &key, &times)
	                    ? "[CustomRecords] best times saved to " CTR_RECORDS_DIR "\n"
	                    : "[CustomRecords] could not save best times to " CTR_RECORDS_DIR "\n");
}

// The saved ghost of the running package, read into the engine's playing
// buffer (0x3e00 bytes). Refused unless it is a complete retail-format ghost.
int MainRaceTrack_CustomLoadGhost(struct GhostHeader *dst)
{
	struct CustomRecordKey key;
	size_t bytes = 0;

	if (dst == NULL || !MainRaceTrack_CustomRecordKey(&key) ||
	    !CustomRecords_LoadGhost(CTR_RECORDS_DIR, &key, dst, 0x3e00, &bytes))
		return 0;
	if (bytes < sizeof(struct GhostHeader) || dst->version != -4 || dst->size < 0 ||
	    bytes != sizeof(struct GhostHeader) + (size_t)dst->size || dst->size > 0x3dd4 || dst->characterID < 0 ||
	    dst->characterID > NITROS_OXIDE || dst->timeElapsedInRace <= 0)
	{
		memset(dst, 0, sizeof(struct GhostHeader));
		CustomTrack_Log("[CustomRecords] saved ghost refused: not a complete ghost\n");
		return 0;
	}
	CustomTrack_Log("[CustomRecords] saved ghost loaded (%d bytes)\n", (int)bytes);
	return 1;
}

void MainRaceTrack_CustomSaveGhost(const struct GhostHeader *gh)
{
	struct CustomRecordKey key;

	if (gh == NULL || gh->version != -4 || gh->size <= 0 || gh->size > 0x3dd4 || !MainRaceTrack_CustomRecordKey(&key))
		return;
	CustomTrack_Log(CustomRecords_SaveGhost(CTR_RECORDS_DIR, &key, gh, sizeof(struct GhostHeader) + (size_t)gh->size)
	                    ? "[CustomRecords] ghost saved to " CTR_RECORDS_DIR "\n"
	                    : "[CustomRecords] could not save the ghost to " CTR_RECORDS_DIR "\n");
}

// The running package's title in banner form, or NULL outside a custom race.
const char *MainRaceTrack_OfflineCustomTitle(void)
{
	static char s_title[32];
	struct CustomPackageManifest manifest;

	if (!MainRaceTrack_OfflineCustomLoad() || !CustomOffline_RuntimeManifest(&manifest))
		return NULL;
	CustomIdentity_BannerName(s_title, sizeof s_title, manifest.title);
	return s_title;
}
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003cf7c-0x8003cfc0.
void MainRaceTrack_StartLoad(s16 levelID)
{
	// clear backup,
	// keep music,
	// destroy "most" fx, let menu fx play to end
	howl_StopAudio(1, 0, 0);

	ElimBG_Deactivate(sdata->gGT);

#ifdef CTR_CUSTOM_PACKAGES
	if (MainRaceTrack_OfflineRefuseLoad(levelID))
		levelID = MAIN_MENU_LEVEL;
	// The package's own lap count, from its pinned race settings.
	if (CustomOffline_RuntimeServing(levelID, MainRaceTrack_OfflineSingleRaceMode()))
		sdata->gGT->numLaps = CustomOffline_RuntimeLaps();
	// Leaving the host slot ends the custom race here, not at the request: the
	// custom geometry stays resident and the race frames keep running on it
	// until the checkered flag covers the screen, and they must keep the custom
	// identity (no Roo's Tubes ambience or bubbles) until it is replaced.
	CustomOffline_OnLoadStarting(levelID);
#endif
	LOAD_LevelFile(levelID);
	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003cfc0-0x8003d024.
void MainRaceTrack_RequestLoad(s16 levelID)
{
#ifdef CTR_CUSTOM_PACKAGES
	// A retry keeps the custom race. Leaving the host slot ends it only when
	// the load starts (MainRaceTrack_StartLoad).
	CustomOffline_OnLoadRequested(levelID);
#endif
	// Turn off HUD
	sdata->gGT->hudFlags &= 0xfe;

	if (RaceFlag_IsFullyOffScreen() == 1)
	{
		RaceFlag_BeginTransition(1);
	}
	RaceFlag_ResetTextAnim();

	sdata->Loading.stage = LOAD_REQUESTED;
	sdata->Loading.Lev_ID_To_Load = levelID;
#ifdef CTR_CUSTOM_TRACKS
	// Every level request passes through here, so this is where the Cortex
	// Vortex pad-track serving state (schema 15) consumes the entry site's
	// explicit selection, or ends when anything other than LevelID 13 loads.
	// See include/platform/native_cortex_track_latch.h.
	CustomTrack_CortexTrackOnRequestLoad(levelID);
#endif
	return;
}
