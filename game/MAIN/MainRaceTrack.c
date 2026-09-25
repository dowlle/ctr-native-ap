#include <common.h>

#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h>
#endif
#ifdef CTR_CUSTOM_PACKAGES
#include <platform/native_custom_offline.h>
#include <platform/native_custom_package.h>
#include <platform/native_custom_identity.h>
CTR_STATIC_ASSERT(sizeof(((struct GameTracker *)0)->lapTime) / sizeof(int) == CTR_OFFLINE_MAX_LAPS);

// Box authoring build: a package chosen on the Arcade custom pages races only
// as a one-player Arcade single race on its host slot. Every other mode on that
// slot loads retail bytes.
static int MainRaceTrack_OfflineSingleRaceMode(void)
{
	struct GameTracker *gGT = sdata->gGT;
	return gGT->numPlyrCurrGame == 1 && (gGT->gameMode1 & ARCADE_MODE) &&
	       !(gGT->gameMode1 & (ADVENTURE_MODE | ADVENTURE_CUP | ADVENTURE_ARENA | TIME_TRIAL | BATTLE_MODE)) &&
	       !(gGT->gameMode2 & CUP_ANY_KIND);
}

int MainRaceTrack_OfflineCustomLoad(void)
{
	return CustomOffline_RuntimeServing(sdata->gGT->levelID, MainRaceTrack_OfflineSingleRaceMode());
}

// The level id identity-keyed code should see: CTR_CUSTOM_LEVEL_ID during a
// custom-page race, the engine's level id otherwise. The host slot stays the
// BIGFILE loading vehicle in gGT->levelID; this is what everything else uses.
int MainRaceTrack_IdentityLevelID(void)
{
	return CustomIdentity_LevelID(sdata->gGT->levelID, MainRaceTrack_OfflineCustomLoad());
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
