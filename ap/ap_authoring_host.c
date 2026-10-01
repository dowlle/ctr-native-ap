// The authoring client without Archipelago: the few AP-layer services the
// authoring modules call. See ap_authoring_host.h for the contract. Part of the
// main.c unity build (game/game_unity.h), after the modules it drives.

#include "ap_authoring_host.h"

#if !defined(CTR_AP) && (defined(CTR_CUSTOM_PACKAGES) || defined(CTR_BOX_AUTHORING) || defined(CTR_AI_LAP_RECORDER))

#include <common.h>
#include <stdio.h>

// Next to config.ini, in the working directory. The AP client's ctr-ap.log is a
// different file, so an authoring client unpacked beside a player client never
// writes into the player's log.
#define AUTHORING_LOG_FILE "ctr-authoring.log"

void AP_LogLine(const char *msg)
{
	static FILE *log;
	static int opened;

	if (msg == NULL)
		return;
	fputs(msg, stdout);
	if (!opened)
	{
		opened = 1;
		log = fopen(AUTHORING_LOG_FILE, "a");
	}
	if (log != NULL)
	{
		fputs(msg, log);
		fflush(log);
	}
}

#ifdef CTR_CUSTOM_PACKAGES
// In an AP build an Arcade custom page refuses to launch while a seed is
// loaded, because the seed owns the session (ap_hooks.c). There is no seed here.
int AP_CustomOfflineLaunchAllowed(void)
{
	return 1;
}
#endif

#if defined(CTR_BOX_AUTHORING) || defined(CTR_AI_LAP_RECORDER)
// The same order ap_onframe_body (ap_hooks.c) uses for these modules: park the
// AP-logo marker model, then author mode, then the spawn loader, so a placement
// dropped this frame gets its marker in the same frame. The lap recorder runs
// later in that body and only reads the frame, so its place after them is the
// same. Every call self-gates on its own option.
void Authoring_OnFrame(struct GameTracker *gGT)
{
	if (gGT == NULL)
		return;
#ifdef CTR_BOX_AUTHORING
	AP_MarkerModel_Register(gGT);
	AP_Author_OnFrame(gGT);
	AP_Spawn_OnFrame(gGT);
#endif
#ifdef CTR_AI_LAP_RECORDER
	AP_NavRec_Tick(gGT);
#endif
}
#endif

#endif
