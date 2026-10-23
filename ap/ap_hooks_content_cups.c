// ap_hooks_content_cups.c -- part of the ap_hooks unit (#133 PR 5).
// Not a separate translation unit: game/game_unity.h includes it after
// ap_hooks_race.c, so it uses the unit's includes and static helpers
// (AP_ContentArmTrack and AP_ContentLegContext from ap_hooks_custom_content.c,
// AP_EmitClassCheck from ap_hooks_race.c).
//
// Content-plan cups: the running cup's pad and leg, the next leg's load
// between two legs, the per-leg Cup Leg Victory and sweep record, and the
// cup's end (Win on points, sweep). The engine's Adventure Cup machinery runs
// the cup on engine cup 0; the cup race context (native_custom_race_context.h)
// carries the plan's leg across loads and restarts.

#ifdef CTR_AP

#ifdef CTR_CUSTOM_TRACKS

// ── Content-plan cups (#133 PR 5) ──
// The running cup's pad, or NULL: a cup context of this seed epoch on its
// pad's route, on the leg it names (track, host, laps), serving the leg's own
// bytes when the leg is not retail.
const ctr_content_pad *AP_ContentCupPad(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct CustomTrackRaceContext *c = gGT ? CustomTrack_StandaloneContext(gGT->levelID) : NULL;
	const ctr_content_pad *pad;
	const ctr_content_leg *l;
	if (!c || !c->cup || !ap_content_plan_active() || !c->seedEpoch || c->seedEpoch != ap_content_plan_epoch() ||
	    c->leg < 0 || c->leg >= AP_CONTENT_CUP_LEGS) return NULL;
	pad = ap_content_plan_pad(c->physicalPad);
	if (!pad || !pad->occupied || !pad->cup || pad->trophy_location != c->trophyLocation ||
	    pad->hub != c->returnHub || strcmp(pad->entry_id, c->entryID)) return NULL;
	l = &pad->legs[c->leg];
	if (ap_content_leg_host(l) != c->hostLevelID || l->laps != c->laps || strcmp(l->track_id, c->trackID) ||
	    c->retail != (l->retail_id >= 0) || c->cortex != l->cortex) return NULL;
	if (!c->retail && !CustomTrack_ServingLoad(gGT->levelID, 0, 0)) return NULL;
	return pad;
}

// The ordinary garage whose boss cup is running (#133 PR 6b), or -1.
int AP_ContentGarageCupRunning(void)
{
	const ctr_content_pad *pad = AP_ContentCupPad();
	return pad && pad->physical >= AP_CONTENT_GARAGE_CUP_PAD && pad->physical < AP_CONTENT_GARAGE_CUP_PAD + 4
	       ? pad->physical - AP_CONTENT_GARAGE_CUP_PAD : -1;
}

// The certified restart point count of the running boss cup leg, or -1.
int AP_ContentGarageCupLegPoints(void)
{
	int g = AP_ContentGarageCupRunning(), leg = AP_ContentCupLeg();
	const ctr_content_bosses *b = ap_content_plan_bosses();
	return g >= 0 && leg >= 0 && leg < AP_CONTENT_CUP_LEGS ? b->cup_points[g][leg] : -1;
}

// Garage entry (#133 PR 6b): stage leg 0 of garage `g`'s boss cup. Returns
// the engine level to load, -1 when the leg cannot be served (the garage
// refuses the entry), or -2 when the garage holds no boss cup.
int AP_ContentGarageCupPrepare(int g)
{
	const ctr_content_pad *pad = ap_content_plan_pad(AP_CONTENT_GARAGE_CUP_PAD + g);
	struct CustomTrackRaceContext race;
	if (!pad) return -2;
	if (CustomTrack_StandaloneBusy() || AP_ContentGarageCupReady(g) != 1) return -1;
	AP_ContentLegContext(pad, 0, &race);
	if (!AP_ContentArmTrack(pad->legs[0].cortex, pad->legs[0].package_index, &race) ||
	    !CustomTrack_StageStandaloneRace(&race)) return -1;
	CustomTrack_CortexTrackSelectNextLoad(0);
	return race.hostLevelID;
}

// Are the bytes of every leg of garage `g`'s boss cup ready (#133 PR 6b)?
// Retail legs always; Cortex Vortex and custom legs once scanned. Returns -1
// when the garage holds no boss cup.
int AP_ContentGarageCupReady(int g)
{
	const ctr_content_pad *pad = ap_content_plan_pad(AP_CONTENT_GARAGE_CUP_PAD + g);
	int n;
	if (!pad) return -1;
	for (n = 0; n < AP_CONTENT_CUP_LEGS; ++n)
		if (!AP_ContentTrackReady(pad->legs[n].retail_id >= 0, pad->legs[n].cortex, pad->legs[n].package_index))
			return 0;
	return 1;
}

// After a boss cup (#133 PR 6b), won, lost or left: return as a boss race
// does, to the garage of the hub the cup was entered from, and leave boss
// mode. Returns 1 when a boss cup was running.
int AP_ContentGarageCupReturn(struct GameTracker *gGT, int g)
{
	if (g < 0 || g >= 4 || !gGT) return 0;
	sdata->Loading.OnBegin.AddBitsConfig8 |= SPAWN_AT_BOSS;
	sdata->Loading.OnBegin.RemBitsConfig0 |= ADVENTURE_BOSS | ADVENTURE_CUP;
	gGT->levelID = (s16)AP_BOSS_VENUE_GARAGE[g].homeLevel;
	return 1;
}

// The wire destination of the running content-plan cup's frozen field:
// 200 + the cup's name index (the encounter block's `content_cups`), or -1.
// A boss cup seats no Hit field (#133 PR 6b): the boss arm loads two karts.
int AP_ContentCupHitDestination(void)
{
	const ctr_content_pad *pad = AP_ContentCupPad();
	return pad && AP_ContentGarageCupRunning() < 0 ? CTR_CFG_HIT_CONTENT_CUP_DEST + pad->cup_index : -1;
}

// The running cup leg's index, or -1.
int AP_ContentCupLeg(void)
{
	struct GameTracker *gGT = sdata->gGT;
	return AP_ContentCupPad() ? CustomTrack_StandaloneContext(gGT->levelID)->leg : -1;
}

// The running cup's name for the standings title, or NULL.
const char *AP_ContentCupName(void)
{
	const ctr_content_pad *pad = AP_ContentCupPad();
	return pad ? pad->display_name : NULL;
}

// The pad the hub return puts the kart on after the cup, or -1.
int AP_ContentCupPadLevel(void)
{
	const ctr_content_pad *pad = AP_ContentCupPad();
	// A boss cup returns to its garage instead (AP_ContentGarageCupReturn).
	return pad && AP_ContentGarageCupRunning() < 0 ? pad->physical : -1;
}

// Between two legs: arm and stage leg `leg` of the running cup. Returns the
// engine level to load and sets *outLaps, or -1 when the leg cannot be served
// (the caller ends the cup at the hub).
int AP_ContentCupNextLeg(int leg, int *outLaps)
{
	const ctr_content_pad *pad = AP_ContentCupPad();
	struct CustomTrackRaceContext race;
	int ok;
	char line[200];
	if (!pad || leg < 1 || leg >= AP_CONTENT_CUP_LEGS || leg != AP_ContentCupLeg() + 1) return -1;
	AP_ContentLegContext(pad, leg, &race);
	if (!CustomTrack_BeginCupLegSwitch()) return -1;
	ok = AP_ContentArmTrack(pad->legs[leg].cortex, pad->legs[leg].package_index, &race) &&
	     CustomTrack_StageCupLeg(&race);
	CustomTrack_EndCupLegSwitch();
	snprintf(line, sizeof line, "[CustomTracks] cup %s leg %d: %s on host %d, %d laps%s\n",
	         pad->entry_id, leg + 1, pad->legs[leg].track_id, race.hostLevelID, race.laps,
	         ok ? "" : " REFUSED, the cup ends at the hub");
	AP_AppendLog(line);
	if (!ok) return -1;
	CustomTrack_CortexTrackSelectNextLoad(0);
	if (outLaps) *outLaps = race.laps;
	return race.hostLevelID;
}

// The sweep attempt of the running cup: first place in all four legs of one
// uninterrupted attempt (the #246 attempt record, engine cup 0).
static AP_CupSweepAttempt ap_content_cup_attempt;

void AP_ContentCupEnter(void)
{
	AP_CupSweepEnter(&ap_content_cup_attempt, 0);
}

void AP_ContentCupOnLevelStart(void)
{
	if (!ap_content_cup_attempt.active) return;
	AP_CupSweepStartLeg(&ap_content_cup_attempt, 0, AP_ContentCupLeg(), AP_ContentCupPad() != NULL);
}

void AP_ContentCupOnLoadRequested(int level)
{
	(void)level;
	// A load that is not the next staged leg of this cup ends the attempt.
	if (ap_content_cup_attempt.active && ap_content_cup_attempt.started) AP_CupSweepReset(&ap_content_cup_attempt);
}

// First frame of a leg's standings: the leg's Cup Leg Victory on first place
// (not after a forced loss), and the sweep record.
void AP_ContentCupLegResult(struct GameTracker *gGT)
{
	const ctr_content_pad *pad = AP_ContentCupPad();
	int leg = AP_ContentCupLeg(), first, forced;
	long code;
	if (!pad || !gGT || !gGT->drivers[0]) return;
	first = gGT->drivers[0]->driverRank == 0;
	forced = AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_ADV_REWARD);
	AP_CupSweepRecord(&ap_content_cup_attempt, 0, leg, first, forced);
	code = pad->legs[leg].victory > 0 && pad->legs[leg].victory <= 2147483647 ? (long)pad->legs[leg].victory : -1;
	if (!first || forced || code <= 0 || !ap_net_location_exists(code) || ap_net_location_checked(code)) return;
	AP_EmitClassCheck(code, 0, -1, -1, 1, "[AP CHECK] cup %s leg %d Cup Leg Victory location=%ld\n",
	                  pad->entry_id, leg + 1, code);
}

// The cup is over (last standings). Returns 1 when a content-plan cup was
// running: a points win pays its Win (granted in full after a forced loss on
// the final leg, #286) and, with all four legs won in one attempt, its sweep.
// A lost cup is the caller's race-loss send. Sets the podium to a trophy.
int AP_ContentCupFinish(struct GameTracker *gGT, int won)
{
	const ctr_content_pad *pad = AP_ContentCupPad();
	int swept, garage = AP_ContentGarageCupRunning();
	long win, sweep;
	if (!pad) return 0;
	// A boss cup (#133 PR 6b): a points win completes the garage's encounter
	// exactly as its boss race would: the reward path sends the frozen Boss
	// Race check and records the personal boss win. It is the cup-aggregate
	// entry (paid in full after a forced loss on the final leg, #286), which
	// a standalone cup context does not block, unlike AP_NotifyAdvReward.
	if (garage >= 0)
	{
		AP_CupSweepReset(&ap_content_cup_attempt);
		if (won)
			AP_NotifyCupAggregateReward(garage + ADV_REWARD_FIRST_BOSS_KEY);
		gGT->podiumRewardID = 0;
		return 1;
	}
	swept = AP_CupSweepReady(&ap_content_cup_attempt, 0, AP_CONTENT_CUP_LEGS) &&
	        !AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_ADV_REWARD);
	AP_CupSweepReset(&ap_content_cup_attempt);
	if (!won) return 1;
	win = pad->trophy_location > 0 && pad->trophy_location <= 2147483647 ? (long)pad->trophy_location : -1;
	sweep = pad->sweep_location > 0 && pad->sweep_location <= 2147483647 ? (long)pad->sweep_location : -1;
	if (win > 0 && ap_net_location_exists(win) && !AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_CUP_AGGREGATE))
		AP_EmitClassCheck(win, 0, -1, -1, 1, "[AP CHECK] cup %s Win location=%ld\n", pad->entry_id, win);
	if (swept && sweep > 0 && ap_net_location_exists(sweep))
		AP_EmitClassCheck(sweep, 0, -1, -1, 1, "[AP CHECK] cup %s Win All Legs location=%ld\n", pad->entry_id, sweep);
	gGT->podiumRewardID = STATIC_TROPHY;
	AP_SkipPodium(gGT->podiumRewardID);
	return 1;
}

#endif // CTR_CUSTOM_TRACKS

#endif // CTR_AP
