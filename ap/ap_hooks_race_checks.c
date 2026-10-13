// ap_hooks_race_checks.c -- part of the ap_hooks unit (issue #305).
// Podium and held-position rung checks, box checks, the trophy reconcile and
// the per-frame race listener tick.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

// Podium wrapper: preserve the shipped log text and make the two podium-specific
// policies explicit. Rungs join the ceremony ledger and stay out of the feed per
// #63 because one result can fan out into several simultaneous checks.
static void AP_EmitRung(int track, long code, int rungTag, int position,
                        const char *phase)
{
	// Alpha 4 progression-integrity diagnostic. AP_EmitClassCheck deliberately
	// treats a negative code as an absent per-seed rung, but that silent return
	// made "listener did not fire" indistinguishable from "slot_data omitted the
	// rung" in support bundles. A positive configured code can also disagree with
	// the server's checked+missing location set; log that before preserving the
	// existing send attempt so this patch changes evidence only, never behavior.
	if (code < 0)
	{
		// Once per (track, rung, phase) per connect: the per-tick trophy
		// reconcile re-evaluates this constantly, and on a pre-schema-7 seed
		// (held_5th absent everywhere) the unconditional line flooded the log.
		if (AP_CheckDiagOnce(&ap_checkdiag_once, AP_CHECKDIAG_ABSENT,
		                     phase[0] == 'f', track, rungTag))
		{
			char line[208];
			snprintf(line, sizeof line,
			         "[AP CHECK DIAG] podium %s refused: track=%d pos=%d rung=%d "
			         "slot_data_code=%ld (rung absent from seed config; "
			         "repeats suppressed until reconnect)\n",
			         phase, track, position, rungTag, code);
			AP_AppendLog(line);
		}
	}
	else if (!ap_net_location_exists(code))
	{
		// Same once-per-connect gate as the absent branch: the send attempt
		// below is preserved on every call; only the log line is deduplicated.
		if (AP_CheckDiagOnce(&ap_checkdiag_once, AP_CHECKDIAG_MISMATCH,
		                     phase[0] == 'f', track, rungTag))
		{
			char line[224];
			snprintf(line, sizeof line,
			         "[AP CHECK DIAG] podium %s membership mismatch: track=%d pos=%d "
			         "rung=%d slot_data_code=%ld server_exists=0 connected=%d; "
			         "preserving send attempt (repeats suppressed until reconnect)\n",
			         phase, track, position, rungTag, code, ap_net_is_connected());
			AP_AppendLog(line);
		}
	}
	if (AP_EmitClassCheck(code, 1, -1, rungTag, 0,
	                      "[AP CHECK] podium %s: track=%d pos=%d rung=%d location %ld\n",
	                      phase, track, position, rungTag, code))
		AP_FeedOnRungSent(code, rungTag);
}

// Item box wrapper (#109). Two class policies, both from the 2026-08-10 ruling:
// a box is NOT a ceremony award (it is earned mid-race, not in a result block),
// and it DOES take a feed line, because "the check and the feed line are the
// entire effect" -- there is no pickup, no weapon roll and no wumpa to show for
// it, so the feed line is the only acknowledgement the player gets.
void AP_EmitBoxCheck(int levelID, int slot, long code)
{
	AP_EmitClassCheck(code, 0, -1, -1, 1,
	                  "[AP CHECK] item box: level=%d slot=%d (Item Box %d) location %ld\n",
	                  levelID, slot, slot + 1, code);
}

// FINISH fan-out: fan a trophy-race finish out into podium-ladder location checks
// (the native half of feat/podium-checks; the apworld declares the rung locations
// + emits podium_checks slot_data, ap_seedcfg parses it into ctr_cfg.podium). The
// nesting rule "a better result satisfies every rung at or below it" is applied
// HERE from the observed FINAL placement:
//   placement == 1     -> held_1st + held_3rd + held_5th + finish_podium + finish_any
//   placement in {2,3}  -> held_3rd + held_5th + finish_podium + finish_any
//   placement in {4,5}  -> held_5th + finish_any
//   placement >= 6      -> finish_any
// The held_* rungs are earned LIVE mid-race by AP_RaceListenerTick; the finish is
// the WIN FALLBACK (decision 5): a trophy win (placement 1) fans out EVERYTHING,
// covering a missed live capture and server /send_location grants -- and finishing
// AT a position legitimately satisfies the held rungs at or below it. Sends dedup
// against the checked-set, so the live listener + finish fan-out never double-send.
// MUST be called only for AP_RACE_TROPHY finishes (incl. cup legs): relic / token /
// crystal challenges run on the SAME levelIDs as the trophy tracks (only the mode
// word differs), so gating on the levelID range alone would misfire.
static void AP_SendPodiumChecks(int track, int placement)
{
	// #286: the forced-loss attempt owns no finish rung, including the trophy
	// backstop and the connect-time reconnect reconciliation.
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_PODIUM))
		return;
	if (!ctr_cfg_active() || !ctr_cfg.podium_enabled)
		return;
	if (placement < 1)
		return; // -1 = placement unknown -> nothing to fan out

	const ctr_podium_rungs *pr = NULL;
	if (track >= 0 && track < CTR_CFG_PODIUM_TRACK_COUNT)
		pr = &ctr_cfg.podium[track];
	else if (track >= AP_TRIAL_PODIUM_LOGICAL_BASE && track < AP_TRIAL_PODIUM_LOGICAL_BASE+2)
		pr = &ctr_cfg.podium[16+track-AP_TRIAL_PODIUM_LOGICAL_BASE];
	else if (track == AP_CV_PODIUM_LOGICAL_TRACK && ctr_cfg.cortex_track.valid)
		pr = &ctr_cfg.cortex_track.podium;
#ifdef CTR_CUSTOM_TRACKS
	else if (ctr_cfg.custom_tracks_ok &&
	         track == CTR_CFG_PODIUM_TRACK_COUNT + ctr_cfg.custom_track.slot - 1)
		pr = &ctr_cfg.custom_track.podium;
#endif
	if (pr == NULL)
		return;

	if (placement == 1)
		AP_EmitRung(track, pr->held_1st, AP_RUNG_HELD_1ST, placement, "finish");
	if (placement <= 3)
		AP_EmitRung(track, pr->held_3rd, AP_RUNG_HELD_3RD, placement, "finish");
	if (placement <= 5)
		AP_EmitRung(track, pr->held_5th, AP_RUNG_HELD_5TH, placement, "finish");
	if (placement <= 3)
		AP_EmitRung(track, pr->finish_podium, AP_RUNG_FINISH_PODIUM, placement, "finish");
	AP_EmitRung(track, pr->finish_any, AP_RUNG_FINISH_ANY, placement, "finish");
}

// LIVE fan-out: fire the held-position rungs for a debounce-CONFIRMED stable live
// position (1 = 1st). Ladder: holding 1st grants held_3rd + held_5th; holding
// <=3rd grants held_5th. The finish_* rungs are NOT touched here -- they are
// final-position checks owned by AP_SendPodiumChecks. Dedup is automatic.
static void AP_SendHeldChecks(int track, int position)
{
	if (!ctr_cfg_active() || !ctr_cfg.podium_enabled)
		return;
	if (position < 1)
		return;

	const ctr_podium_rungs *pr = NULL;
	if (track >= 0 && track < CTR_CFG_PODIUM_TRACK_COUNT)
		pr = &ctr_cfg.podium[track];
	else if (track >= AP_TRIAL_PODIUM_LOGICAL_BASE && track < AP_TRIAL_PODIUM_LOGICAL_BASE+2)
		pr = &ctr_cfg.podium[16+track-AP_TRIAL_PODIUM_LOGICAL_BASE];
	else if (track == AP_CV_PODIUM_LOGICAL_TRACK && ctr_cfg.cortex_track.valid)
		pr = &ctr_cfg.cortex_track.podium;
#ifdef CTR_CUSTOM_TRACKS
	else if (ctr_cfg.custom_tracks_ok &&
	         track == CTR_CFG_PODIUM_TRACK_COUNT + ctr_cfg.custom_track.slot - 1)
		pr = &ctr_cfg.custom_track.podium;
#endif
	if (pr == NULL)
		return;

	if (position == 1)
		AP_EmitRung(track, pr->held_1st, AP_RUNG_HELD_1ST, position, "held");
	if (position <= 3)
		AP_EmitRung(track, pr->held_3rd, AP_RUNG_HELD_3RD, position, "held");
	if (position <= 5)
		AP_EmitRung(track, pr->held_5th, AP_RUNG_HELD_5TH, position, "held");
}

// Connect-time backstop: any trophy race whose trophy LOCATION is already
// checked was, by the engine's win-only rule (a trophy is awarded ONLY on 1st),
// finished 1st -- so every podium rung for that track is earned. Sweep all 16
// trophy tracks and fan out (deduped) from that durable signal. Idempotent, so
// running it each connect/tick just no-ops once the rungs are checked. Covers
// races won in a prior session and finishes whose live capture was lost.
static void AP_ReconcilePodiumFromTrophies(void)
{
	if (!ctr_cfg_active() || !ctr_cfg.podium_enabled)
		return;
	for (int lid = 0; lid < CTR_CFG_PODIUM_TRACK_COUNT; lid++)
		if (AP_LocationCheckedByBit(lid + ADV_REWARD_FIRST_TROPHY))
			AP_SendPodiumChecks(lid, 1); // trophy won => 1st => all rungs
	for (int trial = 0; trial < CTR_CFG_TRIAL_TRACK_COUNT; trial++)
		if (AP_TrialTrackLocationChecked(16+trial, CTR_CFG_TRIAL_TROPHY))
			AP_SendPodiumChecks(AP_TRIAL_PODIUM_LOGICAL_BASE+trial, 1);
	// Schema 15: the Cortex Vortex Trophy is a direct code, restored from the
	// server's checked set on every connect like every other trophy.
	if (AP_CortexTrackChecked(AP_CV_SLOT_TROPHY))
		AP_SendPodiumChecks(AP_CV_PODIUM_LOGICAL_TRACK, 1);
#ifdef CTR_CUSTOM_TRACKS
	if (ctr_cfg.custom_tracks_ok && AP_CustomTrackTrophyChecked())
		AP_SendPodiumChecks(CTR_CFG_PODIUM_TRACK_COUNT +
		                    ctr_cfg.custom_track.slot - 1, 1);
#endif
}

// Called every frame (all game modes) from AP_OnFrame, BEFORE the adventure
// throttle/early-return, so the finish transition is never missed. Self-gates to
// real races via drivers[0] + ACTION_RACE_FINISHED + LOAD_IDLE. Captures
// placement, then fans out podium-ladder checks for trophy-race finishes.
static void AP_RaceListenerTick(struct GameTracker *gGT)
{
	// Rising-edge detector on the player's ACTION_RACE_FINISHED bit -- fires the
	// capture exactly once per race and re-arms automatically when the flag
	// clears (next race start). Same idiom as the M-key overlay toggle above; it
	// naturally ignores the multi-frame ceremony / retry menu / reload that all
	// hold the flag set. Persisting across a hub visit is harmless (0->1 only).
	static int ap_finish_prev = 0;

	struct Driver *p = gGT->drivers[0]; // P1 == the local player in adventure

	// Live-position mirror: only meaningful with a valid player driver in an
	// adventure context; -1 otherwise so a diff can't misread a stale hub rank.
	if (p != NULL && (gGT->gameMode1 & ADVENTURE_MODE) != 0)
		ap_live_position = (p->driverRank >= 0) ? p->driverRank + 1 : -1;
	else
		ap_live_position = -1;

	if (p == NULL)
		return;

	// Genuine in-race context: adventure mode with the level fully loaded. During
	// a hub/track load the drivers[0] slot can hold uninitialised leftovers, and
	// reading ACTION_RACE_FINISHED off that garbage would fire a bogus finish
	// capture -- a spurious "[AP RACE] track=.. final_place=.." line logged during
	// hub transitions (issue #4). A real finish is always reached with the level
	// loaded, so requiring LOAD_IDLE never drops a true finish. The raw flag still
	// drives the falling-edge ledger reset below, so ceremony timing is unchanged.
	int inRace = (gGT->gameMode1 & ADVENTURE_MODE) != 0 &&
	             sdata->Loading.stage == LOAD_IDLE;

	int finishedNow = (p->actionsFlagSet & ACTION_RACE_FINISHED) != 0;
	if (finishedNow && !ap_finish_prev && inRace)
	{
		int podiumTrack;

		// driverRank is frozen once you cross the line (nobody can pass a
		// finished racer), so this is the authoritative final placement.
		ap_last_race_track = (int)gGT->levelID;
		ap_last_race_place = (p->driverRank >= 0) ? p->driverRank + 1 : -1;
		ap_last_race_mode  = AP_ClassifyRace(gGT);
		podiumTrack = AP_RetailPodiumTrack(gGT);

		char m[160];
		snprintf(m, sizeof m,
		         "[AP RACE] track=%d final_place=%d race_type=%s "
		         "(t=%u adv=%d load=%d)\n",
		         ap_last_race_track, ap_last_race_place,
		         AP_RaceTypeName(ap_last_race_mode), (unsigned)gGT->timer,
		         (gGT->gameMode1 & ADVENTURE_MODE) != 0 ? 1 : 0,
		         (int)sdata->Loading.stage);
		AP_AppendLog(m);

		// Native fan-out: only trophy races carry podium rungs (relic/token/
		// crystal share the same levelIDs, so the type gate is load-bearing).
		if (ap_last_race_mode == AP_RACE_TROPHY)
			AP_SendPodiumChecks(podiumTrack, ap_last_race_place);
	}
	else if (!finishedNow && ap_finish_prev)
	{
		// Falling edge = the previous race's finished-state cleared, i.e. a new
		// race has started. Clear the ceremony ledger here rather than on the
		// finish edge: relic tiers (RR_EndEvent_UnlockAward) and the continue-
		// press grants land AT/AFTER the finish, so resetting at race start keeps
		// this race's entries intact through its whole ceremony regardless of
		// within-frame ordering.
		AP_CeremonyLedgerReset();
		// Re-arm the held-position listener for the next race / cup leg: forget the
		// debounce candidate and the best-fired position so the new track's held
		// rungs can fire fresh (each cup leg loads its own destination track).
		ap_held_best_pos = 99;
		ap_held_cand_pos = -1;
		ap_held_cand_ms  = 0;
	}
	ap_finish_prev = finishedNow;

	// ── LIVE held-position listener (mid-race) ──────────────────────────────
	// Fire the held-position rungs the moment the live rank holds stable for the
	// debounce window. GUARD CHAIN (the load-gap lesson, bit us twice on 2026-07-17):
	// flags/timer alone are NOT enough -- the few frames after a level load carry
	// stale gameMode1/trafficLightsTimer, and the adventure hub passes a raw
	// flag/timer test too. So the fire is gated on the SAME proven predicate class
	// as the wumpa/deathlink receive windows:
	//   (1) LOAD_IsOpen_RacingOrBattle() -- the stock on-a-track overlay test, false
	//       in the hub and during the load gap;
	//   (2) the countdown latch -- set only after the lights sequence is observed on
	//       a live track, which can never happen inside the load gap;
	//   (3) not START/END_OF_RACE / MAIN_MENU / GAME_CUTSCENE / PAUSE_ALL, lights out;
	//   (4) AP_ClassifyRace == TROPHY -- trophy races AND cup legs (legs classify as
	//       trophy), never relic/token/crystal/boss/arcade.
	// The debounce (position must hold AP_HELD_DEBOUNCE_MS) then rejects overtake
	// flicker: any position change resets the stability timer, so a 1-2 frame swap
	// at a pass never confirms.
	{
		int onTrack = LOAD_IsOpen_RacingOrBattle();
		int podiumTrack = AP_RetailPodiumTrack(gGT);
		if (AP_HeldRaceRearmStep(
		        onTrack, sdata->Loading.stage == LOAD_IDLE,
		        (gGT->gameMode1 & END_OF_RACE) != 0,
		        gGT->trafficLightsTimer, &ap_held_countdown_seen))
		{
			// A newly observed countdown is the authoritative start of an
			// attempt. Restart, Exit to Map, and track/cup-leg changes can tear
			// down an attempt without an ACTION_RACE_FINISHED falling edge.
			ap_held_best_pos = 99;
			ap_held_cand_pos = -1;
			ap_held_cand_ms  = 0;
		}

		int heldWindow =
		    (gGT->gameMode1 &
		     (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) == 0 &&
		    gGT->trafficLightsTimer < 1 && onTrack && ap_held_countdown_seen;

		if (heldWindow && AP_ClassifyRace(gGT) == AP_RACE_TROPHY &&
		    podiumTrack >= 0 && ap_live_position >= 1)
		{
			int elapsedMs = gGT->elapsedTimeMS;
			if (elapsedMs <= 0)
				elapsedMs = 32; // defensive: a paused/odd frame must not stall the timer

			if (ap_live_position == ap_held_cand_pos)
			{
				ap_held_cand_ms += elapsedMs;
			}
			else
			{
				ap_held_cand_pos = ap_live_position; // position changed -> restart debounce
				ap_held_cand_ms  = 0;
			}

			// Confirmed hold: fire only when it IMPROVES on the best already fanned
			// out this race (a lower rank number is better). The ladder inside
			// AP_SendHeldChecks grants every held rung at or below the confirmed
			// position, so a later worse hold has nothing new to add.
			if (ap_held_cand_ms >= AP_HELD_DEBOUNCE_MS && ap_held_cand_pos < ap_held_best_pos)
			{
				AP_SendHeldChecks(podiumTrack, ap_held_cand_pos);
				ap_held_best_pos = ap_held_cand_pos;
			}
		}
		else
		{
			// Outside the held window (hub / load / menu / pause / non-trophy): stop
			// debouncing. ap_held_best_pos is preserved so a mid-race pause cannot
			// re-earn an already-fired rung; it resets only at the race-start edge.
			ap_held_cand_pos = -1;
			ap_held_cand_ms  = 0;
		}
	}
}

#endif // CTR_AP
