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

// Hit Character on content plans (#133 PR 5b). A content-plan race seats the
// encounter field on custom and Cortex Vortex tracks too, like a retail race,
// so the loader lets a custom-served load apply while a plan is active. A
// legacy room keeps its custom-served loads on the stock field.
int AP_ContentHitServesCustom(void)
{
	return ap_content_plan_active();
}

// The wire destination of the running content-plan cup's frozen field:
// 200 + the cup's name index (the encounter block's `content_cups`), or -1.
int AP_ContentCupHitDestination(void)
{
	const ctr_content_pad *pad = AP_ContentCupPad();
	return pad ? CTR_CFG_HIT_CONTENT_CUP_DEST + pad->cup_index : -1;
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
	return pad ? pad->physical : -1;
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
	int swept;
	long win, sweep;
	if (!pad) return 0;
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
