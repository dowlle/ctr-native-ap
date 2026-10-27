// ap_hooks_laps.c -- part of the ap_hooks unit (#344 lap completion checks).
// Not a separate translation unit: game/game_unity.h includes it after
// ap_hooks_content_cups.c, so it uses the unit's includes and static helpers
// (AP_EmitClassCheck and AP_StandaloneRoutePad from ap_hooks_race.c,
// AP_ContentCupPad from ap_hooks_content_cups.c).
//
// `<Track>: Complete One Lap` is sent at the first accepted lap of any race
// with laps on that track: a Trophy Race, CTR Challenge, Relic Race or Cup
// leg, in Adventure. PlayLevel_UpdateLapStats calls AP_LapOnCrossing right
// after it advances a driver's lapIndex at the forward finish crossing, which
// also covers a one-lap race's final crossing. Only the local player's kart
// counts. Boss races are left out: the apworld models no boss route to a lap
// check (worlds/ctr/lap_checks.py), so native does not send one there either.

#ifdef CTR_AP

// The lap check of the running race, or -1. The served track's identity,
// never the engine host's LevelID alone: a content plan names it on its pad or
// cup leg; a normal room keys it by retail LevelID, Cortex Vortex or the
// legacy custom track's slot, as the podium identity does (AP_RetailPodiumTrack).
static long AP_LapCode(struct GameTracker *gGT)
{
	const int level = (int)gGT->levelID;
	const int cup = (gGT->gameMode1 & ADVENTURE_CUP) != 0;
#ifdef CTR_CUSTOM_PACKAGES
	// A custom-page race has no AP identity.
	if (MainRaceTrack_OfflineCustomLoad())
		return -1;
#endif
#ifdef CTR_CUSTOM_TRACKS
	if (ap_content_plan_active())
	{
		const struct CustomTrackRaceContext *context = CustomTrack_StandaloneContext(level);
		const ctr_content_pad *pad = AP_ContentCupPad();
		int64_t code = 0;
		if (pad)
		{
			const int leg = AP_ContentCupLeg();
			// A boss cup's legs carry no lap code (no boss route).
			code = leg >= 0 && leg < AP_CONTENT_CUP_LEGS ? pad->legs[leg].lap : 0;
		}
		else if (context && !context->cup && (context->retail || CustomTrack_ServingLoad(level, 0, 0)) &&
		         (pad = AP_StandaloneRoutePad(context)) != NULL)
			code = pad->lap_location;
		return code > 0 && code <= 2147483647 ? (long)code : -1;
	}
	// Outside a content plan a standalone race has no lap identity.
	if (CustomTrack_StandaloneContext(level))
		return -1;
#endif
	if (!ctr_lap_checks.enabled)
		return -1;
	// Checked first: Cortex Vortex is served on Oxide Station's LevelID 13.
	if (AP_CortexTrackActive())
		return AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_CORTEX, AP_LAP_CORTEX_KEY);
#ifdef CTR_CUSTOM_TRACKS
	if (ctr_cfg.custom_tracks_ok && CustomTrack_ServingLoad(level, cup, gGT->cup.cupID))
		return AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_CUSTOM, ctr_cfg.custom_track.slot);
	if (level == 16 || level == 17)
		return AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_RETAIL, level);
	return AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_RETAIL,
	                     CustomTrack_RetailPodiumLevelID(level, cup, gGT->cup.cupID));
#else
	(void)cup;
	return AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_RETAIL, level);
#endif
}

// An Adventure race with laps: not a boss race, Crystal Challenge, the hub,
// Arcade, Battle, the main menu or the demo.
static int AP_LapRaceMode(const struct GameTracker *gGT)
{
	return (gGT->gameMode1 & ADVENTURE_MODE) && !gGT->boolDemoMode &&
	       !(gGT->gameMode1 & (ADVENTURE_ARENA | ADVENTURE_BOSS | ARCADE_MODE | BATTLE_MODE | CRYSTAL_CHALLENGE | MAIN_MENU));
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
