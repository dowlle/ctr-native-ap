// cc -m32 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_AP -DCTR_NATIVE -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DBUILD=926 -I include -I . tools/test-custom-time-trial-player.c
// platform/native_custom_offline.c platform/native_custom_records.c platform/native_custom_content_verify.c -lm -o /tmp/test-custom-time-trial-player
//
// The Time Trial custom pages in the player client (ruling of 2026-11-02): the
// scenarios of test-custom-time-trial.c, built with the player client's
// defines (CTR_AP without CTR_AP_AUTHORING), so the Archipelago code in
// MainRaceTrack.c, GAMEPROG.c and MainGameEnd.c is compiled in. Best times and
// the ghost go to custom-records/ and never to the retail save, exactly as in
// the authoring builds.
//
// The room hooks those files call are stubbed below. A custom-page race never
// has a content-plan race staged: AP_CustomOfflineLaunchAllowed refuses the
// launch while CustomTrack_StandaloneBusy, so CustomTrack_StandaloneContext
// answers "none" and CustomTrack_KeepsNoScores stays 0, as in the authoring
// builds.
struct GameTracker;
struct Driver;
void AP_CupSweepOnLoadRequested(int levelID);
void AP_ContentCupOnLoadRequested(int levelID);
void AP_DeathLinkOnRaceEnd(struct GameTracker *gGT, struct Driver *player);
#include "test-custom-time-trial.c"

const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int levelID)
{
	(void)levelID;
	return NULL;
}

void AP_CupSweepOnLoadRequested(int levelID)
{
	(void)levelID;
}

void AP_ContentCupOnLoadRequested(int levelID)
{
	(void)levelID;
}

void AP_DeathLinkOnRaceEnd(struct GameTracker *gGT, struct Driver *player)
{
	(void)gGT;
	(void)player;
}
