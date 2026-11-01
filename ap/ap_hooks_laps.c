// ap_hooks_laps.c -- part of the ap_hooks unit (#344 lap completion checks).
// Not a separate translation unit: game/game_unity.h includes it after
// ap_hooks_content_cups.c, so it uses the unit's includes and static helpers
// (AP_EmitClassCheck and AP_StandaloneRoutePad from ap_hooks_race.c,
// AP_ContentCupPad from ap_hooks_content_cups.c).
//
// `<Track>: Complete One Lap` is sent at the first accepted lap of any race
// with laps on that track: a Trophy Race, CTR Challenge, Relic Race, cup leg or
// boss race, in Adventure. PlayLevel_UpdateLapStats calls AP_LapOnCrossing right
// after it advances a driver's lapIndex at the forward finish crossing, which
// also covers a one-lap race's final crossing. Only the local player's kart
// counts.

#ifdef CTR_AP

// The lap check of the running race, or -1. The plan names it on its pad, cup
// leg or boss encounter, never by the engine host's LevelID.
static long AP_LapCode(struct GameTracker *gGT)
{
#ifdef CTR_CUSTOM_TRACKS
	const int level = (int)gGT->levelID;
	const struct CustomTrackRaceContext *context = CustomTrack_StandaloneContext(level);
	const ctr_content_pad *pad;
	int64_t code = 0;
#ifdef CTR_CUSTOM_PACKAGES
	// A custom-page race has no AP identity.
	if (MainRaceTrack_OfflineCustomLoad())
		return -1;
#endif
	if (!ap_content_plan_active())
		return -1;
	if ((pad = AP_ContentCupPad()) != NULL)
	{
		const int leg = AP_ContentCupLeg();
		code = leg >= 0 && leg < AP_CONTENT_CUP_LEGS ? pad->legs[leg].lap : 0;
	}
	else if (IS_BOSS_RACE(gGT->gameMode1))
		code = gGT->bossID >= 0 && gGT->bossID < 6 ? ap_content_plan_bosses()->lap[(int)gGT->bossID] : 0;
	else if (context && !context->cup && (context->retail || CustomTrack_ServingLoad(level, 0, 0)) &&
	         (pad = AP_StandaloneRoutePad(context)) != NULL)
		code = pad->lap_location;
	return code > 0 && code <= 2147483647 ? (long)code : -1;
#else
	(void)gGT;
	return -1;
#endif
}

// An Adventure race with laps: not a Crystal Challenge, the hub, Arcade,
// Battle, the main menu or the demo.
static int AP_LapRaceMode(const struct GameTracker *gGT)
{
	return (gGT->gameMode1 & ADVENTURE_MODE) && !gGT->boolDemoMode &&
	       !(gGT->gameMode1 & (ADVENTURE_ARENA | ARCADE_MODE | BATTLE_MODE | CRYSTAL_CHALLENGE | MAIN_MENU));
}

void AP_LapOnCrossing(struct GameTracker *gGT, struct Driver *driver, int driverSlot)
{
	long code;
	if (!gGT || !driver || !ctr_cfg_active() ||
	    !AP_LapCrossingCounts(driverSlot, (driver->actionsFlagSet & ACTION_BOT) != 0, driver->lapIndex,
	                          AP_LapRaceMode(gGT)))
		return;
	code = AP_LapCode(gGT);
	if (code <= 0 || !ap_net_location_exists(code) || ap_net_location_checked(code))
		return;
	AP_EmitClassCheck(code, 0, -1, -1, 1, "[AP CHECK] lap level=%d lap=%d location=%ld\n",
	                  (int)gGT->levelID, (int)driver->lapIndex, code);
}

#endif // CTR_AP
