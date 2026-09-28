// Discord Rich Presence, game-side glue (issue #366). Part of the unity build
// (game/game_unity.h, CTR_AP only). See ap/ap_discord_logic.h for the design.
//
// Runs from AP_OnFrame. While the "Discord Status" option has never been
// switched on this returns at its first line: no thread, no socket, no work.
// Once on, it looks at the game about once a second, builds the activity text
// and hands a copy to the IPC worker (ap/ap_discord_ipc.cpp). It never touches
// a pipe or socket itself, so Discord being slow or absent cannot stall a frame.

#include "ap_discord_logic.h"
#include "ap_discord_ipc.h"

#include <time.h>

CTR_STATIC_ASSERT(AP_DISCORD_GM1_BATTLE_MODE == BATTLE_MODE);
CTR_STATIC_ASSERT(AP_DISCORD_GM1_MAIN_MENU == MAIN_MENU);
CTR_STATIC_ASSERT(AP_DISCORD_GM1_TIME_TRIAL == TIME_TRIAL);
CTR_STATIC_ASSERT(AP_DISCORD_GM1_ADVENTURE_MODE == ADVENTURE_MODE);
CTR_STATIC_ASSERT(AP_DISCORD_GM1_ADVENTURE_ARENA == ADVENTURE_ARENA);
CTR_STATIC_ASSERT(AP_DISCORD_GM1_ARCADE_MODE == ARCADE_MODE);
CTR_STATIC_ASSERT(AP_DISCORD_GM1_RELIC_RACE == RELIC_RACE);
CTR_STATIC_ASSERT(AP_DISCORD_GM1_CRYSTAL_CHALLENGE == CRYSTAL_CHALLENGE);
CTR_STATIC_ASSERT(AP_DISCORD_GM1_ADVENTURE_CUP == ADVENTURE_CUP);
CTR_STATIC_ASSERT(AP_DISCORD_GM1_GAME_CUTSCENE == GAME_CUTSCENE);
CTR_STATIC_ASSERT(AP_DISCORD_GM1_LOADING == LOADING);
CTR_STATIC_ASSERT(AP_DISCORD_GM2_TOKEN_RACE == TOKEN_RACE);
CTR_STATIC_ASSERT(AP_DISCORD_GM2_CUP_ANY_KIND == CUP_ANY_KIND);
CTR_STATIC_ASSERT(AP_DISCORD_LEVEL_LAST_TRACK == TURBO_TRACK);
CTR_STATIC_ASSERT(AP_DISCORD_LEVEL_LAST_ARENA == LAB_BASEMENT);
CTR_STATIC_ASSERT(AP_DISCORD_LEVEL_FIRST_HUB == GEM_STONE_VALLEY);
CTR_STATIC_ASSERT(AP_DISCORD_LEVEL_LAST_HUB == CITADEL_CITY);
CTR_STATIC_ASSERT(AP_DISCORD_LEVEL_MAIN_MENU == MAIN_MENU_LEVEL);
CTR_STATIC_ASSERT(AP_DISCORD_LEVEL_GARAGE == ADVENTURE_GARAGE);

static void AP_Discord_LogStatus(int status)
{
	switch (status)
	{
	case AP_DISCORD_STATUS_OFF:       AP_LogLine("[AP DISCORD] status off\n"); break;
	case AP_DISCORD_STATUS_SEARCHING: AP_LogLine("[AP DISCORD] Discord not found, retrying in the background\n"); break;
	case AP_DISCORD_STATUS_CONNECTED: AP_LogLine("[AP DISCORD] connected to Discord\n"); break;
	case AP_DISCORD_STATUS_REJECTED:  AP_LogLine("[AP DISCORD] Discord refused the application ID\n"); break;
	default: break;
	}
}

void AP_Discord_OnFrame(struct GameTracker *gGT)
{
	static int s_everOn = 0;
	static unsigned s_tick = 0;
	static int s_lastStatus = AP_DISCORD_STATUS_OFF;
	static int s_noIdLogged = 0;
	static unsigned s_lastErrors = 0;
	static int s_errorLogged = 0;
	static ApDiscordRaceClock s_clock;
	const int enabled = g_config.discordStatus ? 1 : 0;
	const char *appId;
	int status;

	if (!enabled && !s_everOn)
		return;
	s_everOn = 1;
	// About once a second at 30 fps: presence is rate limited far below this.
	if ((s_tick++ % 30u) != 0)
		return;

	appId = AP_Discord_ResolveAppId(g_config.discordAppId, AP_DISCORD_APPLICATION_ID);
	if (enabled && appId == NULL && !s_noIdLogged)
	{
		s_noIdLogged = 1;
		AP_LogLine("[AP DISCORD] Discord Status is on, but this build has no Discord "
		           "application ID; nothing is sent\n");
	}
	ap_discord_configure(enabled, appId);

	status = ap_discord_status();
	if (status != s_lastStatus)
	{
		s_lastStatus = status;
		AP_Discord_LogStatus(status);
		if (status == AP_DISCORD_STATUS_CONNECTED)
			s_errorLogged = 0;
	}
	{
		char msg[128];
		const unsigned errors = ap_discord_error_count(msg, (int)sizeof msg);
		// One line per connection: every refused update moves the count.
		if (errors != s_lastErrors && !s_errorLogged)
		{
			char line[192];
			snprintf(line, sizeof line, "[AP DISCORD] Discord refused a status update: %s\n",
			         msg[0] ? msg : "(no message)");
			AP_LogLine(line);
			s_errorLogged = 1;
		}
		s_lastErrors = errors;
	}

	if (!enabled || appId == NULL || gGT == NULL || sdata == NULL)
		return;

	{
		ApDiscordGameFacts facts;
		ApDiscordInput in;
		ApDiscordActivity act;

		facts.gameMode1 = gGT->gameMode1;
		facts.gameMode2 = gGT->gameMode2;
		facts.levelID = (int)gGT->levelID;
		facts.loading = sdata->Loading.stage != LOAD_IDLE;

		memset(&in, 0, sizeof in);
		in.view = AP_Discord_ClassifyView(&facts);
		if (in.view == AP_DISCORD_VIEW_NONE)
			return; // loading: keep what Discord shows
		in.levelID = facts.levelID;
		in.cupID = gGT->cup.cupID;
		in.cortexVortex = AP_CortexTrackActive();
#ifdef CTR_CUSTOM_TRACKS
		if (!in.cortexVortex && AP_Discord_ViewIsRace(in.view))
			in.customTrack = CustomTrack_ServingLoad(facts.levelID,
			                                         (facts.gameMode1 & ADVENTURE_CUP) != 0,
			                                         gGT->cup.cupID) != 0;
#endif
		in.apConnected = ap_net_is_connected();
		if (in.apConnected)
		{
			in.checked = ap_net_checked_count();
			in.total = ap_net_location_count();
		}
		in.startEpoch = AP_Discord_RaceClockUpdate(&s_clock, in.view, facts.levelID,
		                                           (long long)time(NULL));
		if (AP_Discord_Compose(&in, &act))
			ap_discord_publish(&act);
	}
}

void AP_Discord_Shutdown(void)
{
	ap_discord_shutdown(500);
}
