// ap_hooks_race.c -- part of the ap_hooks unit (issue #305).
// Race-side checks: the debug AdvProgress poll, race-placement listener state,
// race classification, class checks, Relic Perfect, trial / Cortex / custom
// track race results, lettersanity and Wumpa checks.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

// ---------------------------------------------------------------------------
// DEBUG poll: log *unmapped* AdvProgress bit changes (non-location state like
// story/hint flags). Mapped locations + goal bits are owned by the events above
// and skipped here. Re-baselines on entering adventure / after loads.
// ---------------------------------------------------------------------------

static u32 ap_dbg_snapshot[6];
static int ap_dbg_armed = 0;

static void AP_PollDebug(struct AdvProgress *adv)
{
	int w, b;

	if (!ap_dbg_armed)
	{
		for (w = 0; w < 6; w++)
			ap_dbg_snapshot[w] = adv->rewards[w];
		ap_dbg_armed = 1;
		return;
	}

	for (w = 0; w < 6; w++)
	{
		u32 cur = adv->rewards[w];
		u32 newly = cur & ~ap_dbg_snapshot[w]; // bits that went 0 -> 1
		if (newly)
		{
			for (b = 0; b < 32; b++)
			{
				if (!(newly & (1u << b)))
					continue;
				int globalBit = (w * 32) + b;
				if (AP_LookupLocationCode(globalBit) >= 0)
					continue; // a mapped location -- owned by the grant-site events
				              // (schema 4: the Oxide bits 115/116 are now real codes
				              // too, so this clause already covers them)
				char msg[160];
				snprintf(msg, sizeof msg,
				         "[AP DBG] unmapped bit set: rewards[%d] bit %d "
				         "(globalBit %d, mask 0x%08x)\n",
				         w, b, globalBit, (u32)(1u << b));
				AP_AppendLog(msg);
			}
		}
		ap_dbg_snapshot[w] = cur;
	}
}

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// RACE-PLACEMENT LISTENER -- observation-only groundwork for the decided
// "podium check ladder" (per-race nested rungs: 1st / 2nd-or-3rd / any-finish).
// The game does NOT persist race placement, so the apworld can't derive it from
// AdvProgress -- this listener captures it LIVE. It sends NOTHING to the AP
// server: it only mirrors the player's live position and records the final
// placement of each finished race into the diagnostic log (ap-read.log) + the
// state dump (ap-state.json), so the apworld location design can be made with a
// full picture of what the engine actually exposes.
//
// Engine surfaces (source-anchored, all in the same unity build):
//   * live rank      -- gGT->drivers[0]->driverRank  (s16, 0 = 1st place;
//                        namespace_Vehicle.h:1528, offset 0x482). Recomputed
//                        every frame by PlayLevel_UpdateLapStats (PlayLevel.c:4).
//   * race finished  -- drivers[0]->actionsFlagSet & ACTION_RACE_FINISHED
//                        (0x2000000; namespace_Vehicle.h:576). Set the frame the
//                        player crosses the line on the final lap, regardless of
//                        placement -- so 2nd/3rd/4th finishes are observable here
//                        even though adventure mode itself only "wins" (awards a
//                        trophy) on 1st (AA_EndEvent_DrawMenu, 222.c:100/449).
//   * P1 == local    -- gGT->drivers[0] is the human player in adventure/boss
//                        races (222.c:69, UI_Rank.c single-player branch).
// ---------------------------------------------------------------------------

// Race-type classification from the mode words at the finish moment, so the
// apworld design knows which surface each placement came from (only the standard
// trophy race is a podium-ladder target; the rest are documented, not gated).
enum
{
	AP_RACE_UNKNOWN = 0,
	AP_RACE_TROPHY  = 1, // standard adventure trophy race (podium-ladder target)
	AP_RACE_BOSS    = 2, // boss race: 2 racers, 1st = win / 2nd = loss only
	AP_RACE_RELIC   = 3, // relic time trial: solo, no standings (rank always 0)
	AP_RACE_CRYSTAL = 4, // crystal challenge: collect-all, no standings
	AP_RACE_TOKEN   = 5, // CTR token challenge (GameMode2 TOKEN_RACE)
	AP_RACE_ARCADE  = 6, // arcade / non-adventure race
};

static const char *AP_RACE_TYPE_NAMES[7] = {
    "unknown", "trophy", "boss", "relic", "crystal", "token", "arcade"};

static const char *AP_RaceTypeName(int t)
{
	return (t >= 0 && t < 7) ? AP_RACE_TYPE_NAMES[t] : "?";
}

// Live placement of the player this frame: driverRank+1 (1 = 1st), or -1 when
// not in a live adventure race. Mirrored into ap-state.json.
static int ap_live_position = -1;

// Last captured final placement (survives the whole session for diff/inspection).
static int ap_last_race_track = -1; // gGT->levelID at the finish moment
static int ap_last_race_place = -1; // driverRank+1 (1 = 1st), -1 = none yet
static int ap_last_race_mode  = AP_RACE_UNKNOWN;

// Held-position live-listener state (reset per race at the finish-flag falling
// edge). ap_held_countdown_seen is the load-gap guard latch (set once the lights
// sequence is observed on a live track, cleared off-track / at END_OF_RACE), the
// same pattern as ap_wumpa.c's g_countdown_seen. The debounce timer holds a
// candidate position (ap_held_cand_pos) and the ms it has stayed stable
// (ap_held_cand_ms); ap_held_best_pos is the best (lowest) position already fanned
// out this race, so an improving hold fires once and a worse-or-equal hold no-ops.
// Mirrored into ap-state.json.
static int ap_held_countdown_seen = 0;
static int ap_held_cand_pos = -1;
static int ap_held_cand_ms  = 0;
static int ap_held_best_pos = 99;

static int AP_ClassifyRace(struct GameTracker *gGT)
{
	int gm1 = gGT->gameMode1;
	int gm2 = gGT->gameMode2;
	if ((gm1 & ADVENTURE_MODE) == 0)
		return AP_RACE_ARCADE;
	if (IS_BOSS_RACE(gm1))
		return AP_RACE_BOSS;
	if (gm1 & RELIC_RACE)
		return AP_RACE_RELIC;
	if (gm1 & CRYSTAL_CHALLENGE)
		return AP_RACE_CRYSTAL;
	if (gm2 & TOKEN_RACE)
		return AP_RACE_TOKEN;
	return AP_RACE_TROPHY;
}

// A custom track's host levelID is only a byte-serving vehicle. Keep retail
// podium checks on the retail identity for ordinary loads; while custom bytes
// are served, return the frozen generic custom-slot identity carried by v4.
static int AP_RetailPodiumTrack(struct GameTracker *gGT)
{
#ifdef CTR_CUSTOM_PACKAGES
	// A custom-page race has no retail podium identity.
	if (MainRaceTrack_OfflineCustomLoad())
		return -1;
#endif
#ifdef CTR_CUSTOM_TRACKS
	// A content-plan race owns its pad's rungs, and only its Trophy Race does
	// (#133 PR 3b); the engine host's levelID never names them.
	if (CustomTrack_StandaloneContext((int)gGT->levelID))
		return AP_StandalonePodiumTrack();
#endif
	// Schema 15: a Cortex Vortex race (pad or cup leg) owns its own rungs.
	// Checked first: its host LevelID 13 is Oxide Station's podium track.
	if (AP_CortexTrackActive())
		return AP_CV_PODIUM_LOGICAL_TRACK;
#ifdef CTR_CUSTOM_TRACKS
	if (ctr_cfg_active() && ctr_cfg.custom_tracks_ok &&
	    CustomTrack_ServingLoad((int)gGT->levelID,
	                            (gGT->gameMode1 & ADVENTURE_CUP) != 0,
	                            gGT->cup.cupID))
		return CTR_CFG_PODIUM_TRACK_COUNT + ctr_cfg.custom_track.slot - 1;
	if (gGT->levelID == 16 || gGT->levelID == 17)
		return AP_TRIAL_PODIUM_LOGICAL_BASE + gGT->levelID - 16;
	return CustomTrack_RetailPodiumLevelID((int)gGT->levelID,
	                                        (gGT->gameMode1 & ADVENTURE_CUP) != 0,
	                                        gGT->cup.cupID);
#else
	return (gGT->levelID == 16 || gGT->levelID == 17) ?
	       AP_TRIAL_PODIUM_LOGICAL_BASE + gGT->levelID - 16 : (int)gGT->levelID;
#endif
}

// Rung tags -- also the pseudo-bit rung index order (AP_LookupLocationCode):
//   0 held_1st  1 held_3rd  2 held_5th   (LIVE, from the position listener)
//   3 finish_podium  4 finish_any        (FINAL, from the finish edge)
// Used by the ceremony prefix (AP_CeremonyPrefix) and the diagnostic log.
enum
{
	AP_RUNG_HELD_1ST      = 0,
	AP_RUNG_HELD_3RD      = 1,
	AP_RUNG_HELD_5TH      = 2,
	AP_RUNG_FINISH_PODIUM = 3,
	AP_RUNG_FINISH_ANY    = 4,
};

static const char *AP_RungFeedReason(int rungTag)
{
	// AP_RUNG_HELD_1ST/3RD/5TH == 0/1/2 and AP_RUNG_FINISH_PODIUM/ANY == 3/4
	// (the enum above); AP_RungFeedReasonPure is the freestanding pure form
	// tools/test-item-aliases.c pins directly, so this stays the single source
	// of the wording rather than a copy of it.
	return AP_RungFeedReasonPure(rungTag);
}

static void AP_FeedOnRungSent(long code, int rungTag)
{
	long long item = 0;
	int player = -1;
	unsigned flags = 0;
	if (!ap_net_scout_known(code, &item, &player, &flags))
		return;

	char itemRaw[64], playerRaw[64];
	char itemS[AP_FEED_ITEM_CAP], playerS[AP_FEED_PLAYER_CAP];
	char line[AP_FEED_TEXT_CAP];
	if (ap_net_scout_text(code, itemRaw, (int)sizeof itemRaw,
	                      playerRaw, (int)sizeof playerRaw))
	{
		AP_CeremonySanitize(itemRaw, itemS, (int)sizeof itemS);
		AP_CeremonySanitize(playerRaw, playerS, (int)sizeof playerS);
	}
	else
	{
		itemS[0] = '\0';
		playerS[0] = '\0';
	}
	{
		char alias[AP_FEED_ITEM_CAP];
		if (AP_ItemDisplayAlias(item, alias, (int)sizeof alias))
			AP_CeremonySanitize(alias, itemS, (int)sizeof itemS);
	}
	if (AP_CeremonyNameIsBlank(itemS))
		snprintf(itemS, sizeof itemS, "%s", AP_FEED_ITEM_UNKNOWN);

	const char *reason = AP_RungFeedReason(rungTag);
	if (player == ap_net_self_slot())
	{
		snprintf(line, sizeof line, "%s (%s)", itemS, reason);
		AP_FeedRememberSelfRung(item);
		AP_FeedEnqueue(line, AP_ClassFontColor(flags), 1);
	}
	else
	{
		if (playerS[0] == '\0')
			snprintf(playerS, sizeof playerS, "%s", "PLAYER");
		snprintf(line, sizeof line, "%s TO %s (%s)", itemS, playerS, reason);
		AP_FeedEnqueue(line, AP_ClassFontColor(flags), 0);
	}
}

// Held-position debounce: the live position must hold stable for this long before
// its held rung fires, so a one-or-two-frame overtake flicker at a pass cannot
// earn a rung. Short by design (decision 4, 2026-07-16 wayfinder): "if the game
// visibly shows you in 1st for a moment, you earned it" -- long enough to reject
// flicker, short enough that a genuine moment in the lead counts. THE one tunable.
// Time-based (ms), not frames, so it is frame-rate independent (the PC port is not
// pinned to the 30fps NTSC tick). ~0.5s ~= 15 NTSC frames.
#define AP_HELD_DEBOUNCE_MS 500

// Emit one optional-class location check. The absent-code and checked-state
// guards, diagnostic log and network send are common to podium, relic-perfect,
// item-box, itemsanity and lettersanity checks. Ceremony participation and the
// sent-item feed are deliberately caller decisions: not every class is earned in
// a race-end award block, and bursty classes must be able to suppress feed spam.
static int AP_EmitClassCheck(long code,
                              int addToCeremonyLedger, int ledgerBit, int ledgerTag,
                              int toastSentItem, const char *logFmt, ...)
{
	if (!AP_ClassCheckShouldSendPure(code, ap_net_location_checked(code)))
		return 0; // absent this seed, or already checked (re-fire / re-win / reload / reconnect)

	char msg[128];
	va_list args;
	va_start(args, logFmt);
	vsnprintf(msg, sizeof msg, logFmt, args);
	va_end(args);
	AP_AppendLog(msg);

	ap_net_send_location(code); // LocationChecks([code])
	AP_PerfNoteCheckSent();
	if (addToCeremonyLedger)
		AP_CeremonyLedgerAdd(code, ledgerBit, ledgerTag);
	if (toastSentItem)
		AP_FeedOnLocationSent(code);
	return 1;
}

// Hit Character encounter check (schema 16, ticket 06). Routed through the
// shared class-check path so per-seed membership, server-checked dedup and the
// sent-item feed are the same ones every other optional class uses.
int AP_EmitHitCharacterCheck(long code)
{
	return AP_EmitClassCheck(code, 0, -1, -1, 1,
	                         "[AP CHECK] hit character location %ld\n", code);
}

// Relic Race perfect (issue #49) for a custom package, keyed by the package's
// own seed slot, never its borrowed host LevelID. The reserved identity family
// (`Custom Track N: Relic Race Perfect`, 35024000 + N - 1) is not minted and no
// wire carries it yet, so this is always -1 today.
// TODO(#49, custom tracks): read the slot's code from the wire once the family
// is minted and a custom Relic Race mode is admitted; keep it slot-keyed.
static long AP_RelicPerfectCustomCode(int slot)
{
	(void)slot;
	return -1;
}

// Is this load serving custom bytes under a borrowed LevelID? Same question the
// podium identity asks (AP_RetailPodiumTrack), answered conservatively: a
// latched serve fault also counts, because it leaves which bytes loaded in
// doubt and a retail code must never be sent for a track that may not be it.
static int AP_RelicPerfectServingCustom(struct GameTracker *gGT)
{
#ifdef CTR_CUSTOM_PACKAGES
	if (MainRaceTrack_OfflineCustomLoad())
		return 1;
#endif
#ifdef CTR_CUSTOM_TRACKS
	if (CustomTrack_ServeFaultReason() != NULL)
		return 1;
	if (CustomTrack_ServingLoad((int)gGT->levelID,
	                            (gGT->gameMode1 & ADVENTURE_CUP) != 0,
	                            gGT->cup.cupID))
		return 1;
#endif
	(void)gGT;
	return 0;
}

#ifdef CTR_CUSTOM_TRACKS
static long AP_StandalonePerfectCode(int *outRoute);
#endif

void AP_NotifyRelicPerfect(void)
{
	struct GameTracker *gGT;
	struct Driver *driver;
	AP_RelicPerfectFacts facts;
	long code;
	int contentPlan = 0;

#ifdef CTR_CUSTOM_TRACKS
	contentPlan = ap_content_plan_active();
#endif
	if (!ctr_cfg_active() || (!contentPlan && !ctr_cfg.relic_perfect_enabled))
		return;
	// #286: a forced-loss relic attempt pays no finish-class check.
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_RELIC_PERFECT))
		return;
	gGT = sdata ? sdata->gGT : 0;
	driver = gGT ? gGT->drivers[0] : 0;
	if (gGT == 0 || driver == 0 || (gGT->gameMode1 & RELIC_RACE) == 0)
		return;

	memset(&facts, 0, sizeof facts);
#ifdef CTR_CUSTOM_TRACKS
	// A content plan (#133 PR 3c): only the running Relic Race's own pad
	// route can pay, with that pad's perfect code. Nothing else in a
	// content-plan seed sends a perfect.
	if (contentPlan)
	{
		facts.contentCode = AP_StandalonePerfectCode(&facts.contentRoute);
		if (!facts.contentRoute)
			return;
	}
#endif
	facts.retail = ctr_cfg.relic_perfect;
	facts.levelID = (int)gGT->levelID;
	facts.broken = (int)driver->numTimeCrates;
	facts.total = (int)gGT->timeCratesInLEV;
	facts.cortexActive = AP_CortexTrackActive();
	facts.customServing = AP_RelicPerfectServingCustom(gGT);
	facts.customCode = facts.customServing && ctr_cfg.custom_tracks_ok
	                   ? AP_RelicPerfectCustomCode(ctr_cfg.custom_track.slot)
	                   : -1;
	code = AP_RelicPerfectResolvePure(&facts);
	if (code < 0)
		return;
	// Not in the ceremony ledger: the relic award block is the relic's. The
	// sent-item feed line tells the player the perfect paid. The emitter drops
	// a code the room does not have or has already checked (replays, a relic
	// already owned, reconnects) and the network layer holds it while offline.
	AP_EmitClassCheck(code, 0, -1, -1, 1,
	                  "[AP CHECK] Relic Race perfect level=%d crates=%d/%d "
	                  "location %ld\n",
	                  facts.levelID, facts.broken, facts.total, code);
}

// ── Adventure structure ctr_first (#208) ───────────────────────────────────
int AP_RaceStructureCtrFirst(void)
{
	return ctr_cfg_active() && ctr_cfg.adventure_structure == AP_RS_STRUCTURE_CTR_FIRST;
}

// Under ctr_first a plain Trophy Race win must not send a Trophy Race check
// that the award pairs give to a CTR Challenge win.
int AP_RaceStructureSuppressTrophy(long trophyCode)
{
	return AP_RaceStructureCtrFirst() &&
	       AP_RaceStructureTrophyIsStage1Ctr(&ctr_cfg.stage1_awards, trophyCode);
}

// The same, for a reward bit (retail Trophy bits, Cortex Vortex pseudo-bits).
int AP_RaceStructureSuppressTrophyBit(int rewardBit)
{
	return AP_RaceStructureCtrFirst() &&
	       AP_RaceStructureSuppressTrophy(AP_LookupLocationCode(rewardBit));
}

// A qualifying CTR win (first place with the letters) of `ctrCode` also sends
// its paired Trophy Race check, once: the class emitter drops a code that is
// already checked, so a re-win after Collect or a reconnect sends only what is
// still open. The win is a first-place finish, so the podium rungs of
// `podiumTrack` (-1: none) are earned too, as after a classic Trophy win.
static int AP_RaceStructureCtrWon(long ctrCode, int podiumTrack, int ledgerBit)
{
	long trophy;
	int sent;
	if (!AP_RaceStructureCtrFirst())
		return 0;
	trophy = AP_RaceStructurePairedTrophy(&ctr_cfg.stage1_awards, ctrCode);
	if (trophy <= 0)
		return 0;
	sent = AP_EmitClassCheck(trophy, ledgerBit >= 0, ledgerBit, -1, 1,
	                         "[AP CHECK] ctr_first: CTR Challenge %ld won, Trophy Race location %ld\n",
	                         ctrCode, trophy);
	if (podiumTrack >= 0)
		AP_SendPodiumChecks(podiumTrack, 1);
	ap_state_gen++;
	return sent;
}

// Legacy custom track (#208 ctr_first): its plain race does not earn its
// Trophy, so the Cup standings give no prize podium for it.
int AP_CustomTrackTrophyEarnedByCtr(void)
{
	return ctr_cfg_active() && ctr_cfg.custom_tracks_ok &&
	       AP_RaceStructureSuppressTrophy(ctr_cfg.custom_track.trophy_location);
}

// Retail track, ctr_first, plain Trophy Race won (game/222.c): no Trophy check,
// but first place still earns the podium rungs.
void AP_RaceStructureTrophyRaceWon(int levelID)
{
	char msg[128];
	if (levelID < 0 || levelID >= 16)
		return;
	snprintf(msg, sizeof msg,
	         "[AP CHECK] ctr_first: Trophy Race won on track %d; its Trophy check "
	         "comes from the CTR Challenge, rungs only\n", levelID);
	AP_AppendLog(msg);
	AP_SendPodiumChecks(levelID, 1);
}

void AP_NotifyTrialTrackRace(int levelID, int challenge)
{
	// #286: a forced-loss trial attempt emits no direct trial check or podium rung.
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_TRIAL_TRACK))
		return;

	long code = AP_TrialTrackLocation(levelID, challenge);
	int podium = AP_TRIAL_PODIUM_LOGICAL_BASE + levelID - 16;
	if (challenge == CTR_CFG_TRIAL_TROPHY && AP_RaceStructureSuppressTrophy(code))
	{
		// #208 ctr_first: the CTR Challenge earns this Trophy; the win still
		// earns the rungs.
		AP_SendPodiumChecks(podium, 1);
		return;
	}
	AP_EmitClassCheck(code, 0, -1, -1, 1,
	                  "[AP CHECK] trial track=%d challenge=%d location %ld\n",
	                  levelID, challenge, code);
	if (challenge == CTR_CFG_TRIAL_TROPHY && code > 0)
		AP_SendPodiumChecks(podium, 1);
	if (challenge == CTR_CFG_TRIAL_CTR)
		AP_RaceStructureCtrWon(code, podium, -1);
}

// Schema 15: the Cortex Vortex pad track's race result. Called from the win
// branch of AA_EndEvent_DrawMenu (game/222.c) BEFORE the LevelID-derived reward
// path, which would otherwise award Oxide Station's Trophy (bit 19) or CTR
// token (bit 89). Direct codes only; the ceremony ledger gets the pseudo-bit so
// the award block shows the scouted item.
void AP_NotifyCortexTrackRace(int token)
{
	int bits[2];
	int n, i;

	// #286: a forced-loss Cortex Vortex attempt emits no Cortex check or rung.
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_CORTEX_RACE))
		return;
	if (!AP_CortexTrackActive())
		return;
	n = AP_CortexResultBits(&ctr_cfg.cortex_track,
	                        token ? AP_CV_RACE_TOKEN : AP_CV_RACE_TROPHY, 1, 0,
	                        bits, 2);
	for (i = 0; i < n; i++)
	{
		long code = AP_LookupLocationCode(bits[i]);
		// #208 ctr_first: a plain Trophy Race win leaves this Trophy to the
		// CTR Challenge.
		if (!token && AP_RaceStructureSuppressTrophy(code))
			continue;
		AP_EmitClassCheck(code, 1, bits[i], -1, 1,
		                  "[AP CHECK] Cortex Vortex %s location %ld\n",
		                  token ? "CTR Token Challenge" : "Trophy Race", code);
	}
	// A trophy win is a 1st-place finish: every configured rung is earned.
	if (!token && AP_CortexTrackCode(AP_CV_SLOT_TROPHY) > 0)
		AP_SendPodiumChecks(AP_CV_PODIUM_LOGICAL_TRACK, 1);
	if (token)
		AP_RaceStructureCtrWon(AP_CortexTrackCode(AP_CV_SLOT_TOKEN), AP_CV_PODIUM_LOGICAL_TRACK,
		                       AP_CortexPseudoBit(AP_CV_SLOT_TROPHY));
	ap_state_gen++;
}

// Schema 15: relic award for a Cortex Vortex relic race, replacing
// RR_EndEvent_UnlockAward's LevelID-13 loop (Oxide Station's relic times and
// relic bits 35/53/71). raceTime already carries the all-crates bonus.
void AP_CortexTrackRelicAward(int raceTime)
{
	// #286: a forced-loss Cortex relic attempt emits no relic check or presentation.
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_CORTEX_RELIC))
		return;

	struct GameTracker *gGT = sdata->gGT;
	int bits[3];
	int mask = AP_CortexRelicTiersBeaten(raceTime);
	int n = AP_CortexResultBits(&ctr_cfg.cortex_track, AP_CV_RACE_RELIC, 1, mask, bits, 3);
	int i;

	for (i = 0; i < n; i++)
	{
		int tier = AP_CortexPseudoRelicTier(bits[i]);
		long code = AP_LookupLocationCode(bits[i]);
		int relicTime = AP_CortexRelicTime(tier);

		// Same "already owned, check next tier" rule as the retail loop, read
		// from the server's checked set.
		if (code <= 0 || !ap_net_location_exists(code) || ap_net_location_checked(code))
			continue;
		AP_EmitClassCheck(code, 1, bits[i], -1, 1,
		                  "[AP CHECK] Cortex Vortex relic tier %d location %ld "
		                  "(race time %d, target %d%s)\n",
		                  tier, code, raceTime, relicTime,
		                  AP_CORTEX_RELIC_TARGETS.placeholder ? ", PLACEHOLDER target" : "");
		gGT->podiumRewardID = STATIC_RELIC;
		gGT->gameModeEnd |= NEW_RELIC;
		if (tier > 0)
		{
			sdata->relicTime_1min = relicTime / 0xe100;
			sdata->relicTime_10sec = (relicTime / 0x2580) % 6;
			sdata->relicTime_1sec = (relicTime / 0x3c0) % 10;
			sdata->relicTime_10ms = (relicTime / (0x3c0 / 10)) % 10;
			sdata->relicTime_1ms = ((relicTime * 100) / 0x3c0) % 10;
		}
	}
	ap_state_gen++;
}

#ifdef CTR_CUSTOM_TRACKS
int AP_StandaloneRaceActive(void)
{
	return sdata->gGT && CustomTrack_StandaloneContext(sdata->gGT->levelID) != NULL;
}

// The running standalone race still is its content-plan pad's route: same
// seed epoch, pad, hub, laps, slot, host, entry, track and mode code. Shared
// by the Trophy/CTR finish and the Relic Race result below.
static const ctr_content_pad *AP_StandaloneRoutePad(const struct CustomTrackRaceContext *context)
{
	const ctr_content_pad *pad = ap_content_plan_pad(context->physicalPad);
	int64_t codes[3] = {0, 0, 0};
	if (!pad || !pad->occupied || pad->crystal || !context->seedEpoch ||
	    context->seedEpoch != ap_content_plan_epoch() ||
	    ap_content_mode_codes(pad, context->mode, codes) < 1 || codes[0] != context->trophyLocation ||
	    pad->hub != context->returnHub || pad->laps != context->laps || pad->custom_slot != context->slot ||
	    context->retail != (pad->retail_id >= 0) || context->cortex != pad->cortex ||
	    context->hostLevelID != ap_content_pad_host(pad) ||
	    strcmp(pad->entry_id, context->entryID) || strcmp(pad->track_id, context->trackID)) return NULL;
	return pad;
}

// The running standalone race's content-plan podium identity, or -1: only a
// Trophy Race on its pad's route (#133 PR 3b, AP_RetailPodiumTrack).
int AP_StandalonePodiumTrack(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct CustomTrackRaceContext *context = gGT ? CustomTrack_StandaloneContext((int)gGT->levelID) : NULL;
	if (!context || context->mode != AP_CONTENT_MODE_TROPHY || !AP_StandaloneRoutePad(context) ||
	    (!context->retail && !CustomTrack_ServingLoad((int)gGT->levelID, 0, 0)))
		return -1;
	return AP_CONTENT_PODIUM_LOGICAL_BASE + context->physicalPad;
}

// The content-plan pad's five rung codes as a podium row (#133 PR 3b), for
// AP_SendPodiumChecks and AP_SendHeldChecks. 0 when the logical track is not a
// content-plan Trophy Race pad with rungs.
static int AP_ContentPodiumRungs(int track, ctr_podium_rungs *out)
{
	const ctr_content_pad *pad;
	long codes[5];
	int k, any = 0;
	if (track < AP_CONTENT_PODIUM_LOGICAL_BASE || track > AP_CONTENT_PODIUM_LOGICAL_BASE + 110)
		return 0;
	pad = ap_content_plan_pad(track - AP_CONTENT_PODIUM_LOGICAL_BASE);
	if (!pad || !pad->occupied || pad->crystal || ap_content_mode_slot(pad, AP_CONTENT_MODE_TROPHY) < 0)
		return 0;
	for (k = 0; k < 5; ++k)
	{
		codes[k] = pad->rung_locations[k] > 0 && pad->rung_locations[k] <= 2147483647 ? (long)pad->rung_locations[k] : -1;
		any |= codes[k] > 0;
	}
	out->held_1st = codes[0]; out->held_3rd = codes[1]; out->held_5th = codes[2];
	out->finish_podium = codes[3]; out->finish_any = codes[4];
	return any;
}

// Trophy Race and CTR Challenge finish of a standalone race. The context's
// mode decides the award (ap_content_race_award): first place for a Trophy
// route; for a CTR route first place with the letters the CTR Challenge needs
// (all three, or the Lettersanity letters, AP_LetterTokenEarned), and only in
// the CTR Challenge it was staged for. A Relic Race pays in AP_NotifyStandaloneRelic.
int AP_NotifyStandaloneRace(void)
{
	const struct CustomTrackRaceContext *context;
	struct GameTracker *gGT = sdata->gGT;
	int token;
	if (!gGT || !ctr_cfg_active() ||
	    AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_CUSTOM_TROPHY) ||
	    !(gGT->gameMode1 & ADVENTURE_MODE) ||
	    (gGT->gameMode1 & (ARCADE_MODE | ADVENTURE_CUP | ADVENTURE_BOSS | RELIC_RACE | CRYSTAL_CHALLENGE)) ||
	    !gGT->drivers[0])
		return 0;
	context = CustomTrack_StandaloneContext(gGT->levelID);
	token = (gGT->gameMode2 & TOKEN_RACE) != 0;
	if (!context || token != (context->mode == AP_CONTENT_MODE_CTR) ||
	    (context->mode == AP_CONTENT_MODE_CTR
	         ? !(gGT->drivers[0]->driverRank == 0 &&
	             AP_LetterTokenEarned(gGT->levelID, 1, gGT->drivers[0]->PickupLetterHUD.numCollected))
	         : !ap_content_race_award(context->mode, gGT->drivers[0]->driverRank,
	                                  gGT->drivers[0]->PickupLetterHUD.numCollected)))
		return 0;
	if (context->trophyLocation > 2147483647 ||
	    (!context->retail && !CustomTrack_ServingLoad(gGT->levelID, 0, 0)) ||
	    !ap_net_location_exists((long)context->trophyLocation))
		return 0;
	if ((context->seedEpoch || ap_content_plan_active()) && !AP_StandaloneRoutePad(context))
		return 0;
	return AP_EmitClassCheck((long)context->trophyLocation, 0, -1, -1, 1,
	                        "[AP CHECK] standalone entry=%s track=%s slot=%d mode=%d location=%ld\n",
	                        context->entryID, context->trackID, context->slot, context->mode,
	                        (long)context->trophyLocation);
}

// The Reach 10 Wumpa check the running standalone race sends (#133 PR 3b):
// its pad's own check or the global one, only from a Trophy Race or CTR
// Challenge that can reach ten fruit; -1 otherwise. Never the host's code.
static long AP_StandaloneWumpaCode(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct CustomTrackRaceContext *context = gGT ? CustomTrack_StandaloneContext(gGT->levelID) : NULL;
	const ctr_content_pad *pad;
	int64_t code;
	if (!context || !ap_content_plan_active() ||
	    (!context->retail && !CustomTrack_ServingLoad(gGT->levelID, 0, 0)) ||
	    !(pad = AP_StandaloneRoutePad(context)))
		return -1;
	code = ap_content_wumpa_code(ap_content_plan_checks(), pad, context->mode);
	return code > 0 && code <= 2147483647 ? (long)code : -1;
}

// The running content-plan CTR Challenge's pad, for the letter hooks (#133
// PR 3b), or NULL. `track` is the loaded levelID the hooks pass.
static const ctr_content_pad *AP_StandaloneLetterPad(int track)
{
	const struct CustomTrackRaceContext *context = CustomTrack_StandaloneContext(track);
	if (!context || context->mode != AP_CONTENT_MODE_CTR || !ap_content_plan_active())
		return NULL;
	return AP_StandaloneRoutePad(context);
}

// A content-plan CTR Challenge's letters in the legacy Lettersanity encoding.
static void AP_ContentLetterState(const ctr_content_pad *pad, long codes[3], unsigned char received[3])
{
	int l;
	ap_content_letter_codes(pad, codes);
	for (l = 0; l < 3; ++l)
		received[l] = (unsigned char)AP_ContentLetterItemReceived(pad->letter_items[l]);
}

int AP_StandaloneCtrBit(void)
{
	const struct CustomTrackRaceContext *context =
	    sdata->gGT ? CustomTrack_StandaloneContext(sdata->gGT->levelID) : NULL;
	return context && context->mode == AP_CONTENT_MODE_CTR ? AP_CONTENT_BIT_CTR + context->physicalPad : -1;
}

// The running content-plan Relic Race's pad, or NULL.
static const ctr_content_pad *AP_StandaloneRelicPad(void)
{
	const struct CustomTrackRaceContext *context =
	    sdata->gGT ? CustomTrack_StandaloneContext(sdata->gGT->levelID) : NULL;
	const ctr_content_pad *pad;
	if (!context || context->mode != AP_CONTENT_MODE_RELIC) return NULL;
	pad = ap_content_plan_pad(context->physicalPad);
	return pad && ap_content_mode_slot(pad, AP_CONTENT_MODE_RELIC) >= 0 ? pad : NULL;
}

// Retail tracks keep their retail relic targets; a custom track uses its
// package's targets, never the host slot's. Cortex Vortex uses the targets
// its legacy pad track uses (AP_CortexRelicTime, placeholders until the
// track author supplies real times).
int AP_StandaloneRelicTarget(int tier)
{
	const ctr_content_pad *pad = AP_StandaloneRelicPad();
	const ctr_content_package *pkg;
	if (tier < 0 || tier > 2 || !pad) return -1;
	if (pad->retail_id >= 0)
		return pad->retail_id < 18 ? data.RelicTime[pad->retail_id * 3 + tier] : -1;
	if (pad->cortex)
		return AP_CortexRelicTime(tier);
	pkg = ap_content_plan_package(pad->package_index);
	return pkg && pkg->has_relic_targets ? pkg->relic_targets[tier] : -1;
}

// The running content-plan Relic Race's tier code, -2 when none is running.
long AP_StandaloneRelicCode(int tier)
{
	const ctr_content_pad *pad = AP_StandaloneRelicPad();
	if (!pad) return -2;
	return tier >= 0 && tier < 3 ? (long)pad->relic_locations[tier] : -1;
}

// The running content-plan Relic Race's perfect code (#133 PR 3c). *outRoute
// is 1 when a content-plan Relic Race is on its pad's route, serving its own
// bytes; the code is then that pad's perfect check, or -1 when the seed has
// none. Never the engine host's retail code.
static long AP_StandalonePerfectCode(int *outRoute)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct CustomTrackRaceContext *context = gGT ? CustomTrack_StandaloneContext(gGT->levelID) : NULL;
	const ctr_content_pad *pad;
	*outRoute = 0;
	if (!gGT || !context || context->mode != AP_CONTENT_MODE_RELIC || !(gGT->gameMode1 & ADVENTURE_MODE) ||
	    (gGT->gameMode1 & (ARCADE_MODE | ADVENTURE_CUP | ADVENTURE_BOSS | CRYSTAL_CHALLENGE)) ||
	    (gGT->gameMode2 & TOKEN_RACE) ||
	    (!context->retail && !CustomTrack_ServingLoad(gGT->levelID, 0, 0)) ||
	    !(pad = AP_StandaloneRoutePad(context)))
		return -1;
	*outRoute = 1;
	return ap_content_plan_checks()->relic_perfect && pad->perfect_location > 0 &&
	       pad->perfect_location <= 2147483647 ? (long)pad->perfect_location : -1;
}

int AP_NotifyStandaloneRelic(int raceTime)
{
	const struct CustomTrackRaceContext *context;
	const ctr_content_pad *pad;
	struct GameTracker *gGT = sdata->gGT;
	int targets[3], t, sent = 0;
	unsigned tiers;
	if (!gGT || !ctr_cfg_active() || !(gGT->gameMode1 & ADVENTURE_MODE) || !(gGT->gameMode1 & RELIC_RACE) ||
	    (gGT->gameMode1 & (ARCADE_MODE | ADVENTURE_CUP | ADVENTURE_BOSS | CRYSTAL_CHALLENGE)) ||
	    (gGT->gameMode2 & TOKEN_RACE))
		return 0;
	context = CustomTrack_StandaloneContext(gGT->levelID);
	if (!context || context->mode != AP_CONTENT_MODE_RELIC ||
	    (!context->retail && !CustomTrack_ServingLoad(gGT->levelID, 0, 0)) ||
	    !(pad = AP_StandaloneRoutePad(context)))
		return 0;
	for (t = 0; t < 3; ++t) targets[t] = AP_StandaloneRelicTarget(t);
	tiers = ap_content_relic_tiers(raceTime, targets);
	for (t = 0; t < 3; ++t)
	{
		long code = (long)pad->relic_locations[t];
		// Same "already owned, check the next tier" rule as the retail loop,
		// read from the server's checked set.
		if (!(tiers & (1u << t)) || code <= 0 || !ap_net_location_exists(code) || ap_net_location_checked(code))
			continue;
		if (!AP_EmitClassCheck(code, 1, AP_CONTENT_BIT_RELIC + pad->physical * 3 + t, -1, 1,
		                       "[AP CHECK] standalone relic entry=%s track=%s tier=%d location=%ld "
		                       "(race time %d, target %d)\n",
		                       context->entryID, context->trackID, t, code, raceTime, targets[t]))
			continue;
		++sent;
		gGT->podiumRewardID = STATIC_RELIC;
		gGT->gameModeEnd |= NEW_RELIC;
		if (t > 0)
		{
			sdata->relicTime_1min = targets[t] / 0xe100;
			sdata->relicTime_10sec = (targets[t] / 0x2580) % 6;
			sdata->relicTime_1sec = (targets[t] / 0x3c0) % 10;
			sdata->relicTime_10ms = (targets[t] / (0x3c0 / 10)) % 10;
			sdata->relicTime_1ms = ((targets[t] * 100) / 0x3c0) % 10;
		}
	}
	ap_state_gen++;
	return sent;
}

int AP_CustomTrackTrophyChecked(void)
{
	return ctr_cfg_active() && ctr_cfg.custom_tracks_ok &&
	       ctr_cfg.custom_track.trophy_location > 0 &&
	       ap_net_location_checked(ctr_cfg.custom_track.trophy_location);
}

static void AP_NotifyCustomTrackTrophyImpl(void)
{
	int podiumTrack;
	int sent;
	if (!ctr_cfg_active() || !ctr_cfg.custom_tracks_ok ||
	    (sdata->gGT && (sdata->gGT->gameMode2 & TOKEN_RACE)))
		return;
	podiumTrack = CTR_CFG_PODIUM_TRACK_COUNT + ctr_cfg.custom_track.slot - 1;
	// #208 ctr_first: the custom CTR Challenge earns this Trophy; the win
	// still earns the rungs.
	if (AP_RaceStructureSuppressTrophy(ctr_cfg.custom_track.trophy_location))
	{
		AP_SendPodiumChecks(podiumTrack, 1);
		return;
	}
	sent = AP_EmitClassCheck(ctr_cfg.custom_track.trophy_location,
	                         1, AP_CUSTOM_TROPHY_PSEUDO_BIT, -1, 1,
	                         "[AP CHECK] custom track slot=%d Trophy Race location %ld\n",
	                         ctr_cfg.custom_track.slot,
	                         ctr_cfg.custom_track.trophy_location);
	AP_CustomTrophyCeremonyArm(&ap_custom_trophy_ceremony, sent);
	if (sent) ap_custom_ceremony_bit = AP_CUSTOM_TROPHY_PSEUDO_BIT;
	AP_SendPodiumChecks(podiumTrack, 1);
}

// #286: the per-race custom-Trophy producer. Blocked for the forced-loss attempt.
void AP_NotifyCustomTrackTrophy(void)
{
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_CUSTOM_TROPHY))
		return;
	AP_NotifyCustomTrackTrophyImpl();
}

// #286: the final Gem Cup standings aggregate for a custom cup. The cup reward
// (custom Trophy location) is granted in full while the latch is set; the finish
// rung send inside the impl observes AP_RESULT_PRODUCER_PODIUM and stays blocked.
void AP_NotifyCupAggregateCustomTrackTrophy(void)
{
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_CUP_AGGREGATE))
		return;
	AP_NotifyCustomTrackTrophyImpl();
}

// The running content-plan race's win check, for the race-loss DeathLink
// stakes (#133 PR 4a): its pad's Trophy Race or CTR Token Challenge, or a
// Crystal Challenge's completion; 0 for a Relic Race. *outNeedsItems is 1
// while a CTR Challenge still waits for a pooled letter item (Lettersanity
// modes 2 and 3), because its token cannot pay until those arrive.
// *outBoost is the win check's Progressive Boost rank (#133 PR 4c, the plan
// check's `boost`, 0 when none) and *outRacer the pad's racer lock it binds,
// or -1. Returns 0 when no content-plan race on its pad's route is running.
int AP_ContentWinFacts(long *outCode, int *outNeedsItems, int *outBoost, int *outRacer)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct CustomTrackRaceContext *context;
	const ctr_content_pad *pad;
	int64_t code = 0;
	int l;
	*outCode = 0;
	*outNeedsItems = 0;
	*outBoost = 0;
	*outRacer = -1;
	if (!gGT || !ap_content_plan_active())
		return 0;
	if ((pad = AP_ContentCrystalPad()) != NULL)
	{
		*outCode = pad->trophy_location > 0 && pad->trophy_location <= 2147483647 ? (long)pad->trophy_location : 0;
		return 1;
	}
	// A content-plan cup (#133 PR 5), decided at the cup's first leg and kept
	// for every leg: its Win, or once the Win is collected its Win All Legs
	// (the legacy Gem Cup win set holds both), with that check's rank.
	// A boss cup (#133 PR 6b): the garage's own completion check and rank,
	// bound to no racer, like the boss race below.
	if (AP_ContentGarageCupRunning() >= 0 && (pad = AP_ContentCupPad()) != NULL)
	{
		code = pad->trophy_location;
		*outBoost = ap_content_plan_check_boost(code);
		*outCode = ap_net_location_exists((long)code) ? (long)code : 0;
		return 1;
	}
	if ((pad = AP_ContentCupPad()) != NULL)
	{
		const int sweep = pad->sweep_location > 0 && ap_net_location_checked((long)pad->trophy_location);
		code = sweep ? pad->sweep_location : pad->trophy_location;
		*outRacer = pad->racer_lock;
		*outBoost = sweep ? pad->sweep_boost : pad->trophy_boost;
		*outCode = code > 0 && code <= 2147483647 ? (long)code : 0;
		return 1;
	}
	// A boss race in a content plan's garage (#133 PR 6): the garage's own
	// completion check (gGT->bossID stays the physical garage on every venue;
	// 4 is N. Oxide's Challenge, 5 his Final Challenge) with that check's
	// rank, bound to no racer. The garage's door already held its requirement.
	if (ap_content_plan_bosses()->present && (gGT->gameMode1 & ADVENTURE_BOSS) &&
	    !(gGT->gameMode1 & ADVENTURE_CUP) && gGT->bossID >= 0 && gGT->bossID <= 5)
	{
		code = AP_BOSS_VENUE_LOCATION_BASE + gGT->bossID;
		*outBoost = ap_content_plan_check_boost(code);
		*outCode = ap_net_location_exists((long)code) ? (long)code : 0;
		return 1;
	}
	context = CustomTrack_StandaloneContext(gGT->levelID);
	if (!context || context->crystal || !(pad = AP_StandaloneRoutePad(context)))
		return 0;
	*outRacer = pad->racer_lock;
	if (context->mode == AP_CONTENT_MODE_TROPHY)
	{
		code = pad->trophy_location;
		*outBoost = pad->trophy_boost;
	}
	else if (context->mode == AP_CONTENT_MODE_CTR)
	{
		code = pad->ctr_location;
		*outBoost = pad->ctr_boost;
		for (l = 0; l < 3; ++l)
			if (pad->letter_items[l] > 0 && !AP_ContentLetterItemReceived(pad->letter_items[l]))
				*outNeedsItems = 1;
	}
	*outCode = code > 0 && code <= 2147483647 ? (long)code : 0;
	return 1;
}
#else
int AP_CustomTrackTrophyChecked(void) { return 0; }
void AP_NotifyCustomTrackTrophy(void) {}
void AP_NotifyCupAggregateCustomTrackTrophy(void) {}
int AP_ContentWinFacts(long *outCode, int *outNeedsItems, int *outBoost, int *outRacer)
{
	*outCode = 0;
	*outNeedsItems = 0;
	*outBoost = 0;
	*outRacer = -1;
	return 0;
}
#endif

static int AP_CustomLetterSlot(int track)
{
	(void)track;
#ifdef CTR_CUSTOM_TRACKS
	struct GameTracker *gGT = sdata->gGT;
	if (gGT && ctr_cfg_active() && ctr_cfg.custom_tracks_ok &&
	    CustomTrack_ServingLoad(track,
	                            (gGT->gameMode1 & ADVENTURE_CUP) != 0,
	                            gGT->cup.cupID))
		return ctr_cfg.custom_track.slot;
#endif
	return 0;
}

int AP_CustomTrackCtrChecked(void)
{
	return ctr_cfg_active() && ctr_cfg.custom_tracks_ok && ctr_cfg.custom_ctr_enabled &&
	       ctr_cfg.custom_ctr_location > 0 && ap_net_location_checked(ctr_cfg.custom_ctr_location);
}

// Schema 15: while the Cortex Vortex pad track is on screen, the letter hooks'
// LevelID 13 means the LEV's own C/T/R (instances 77..79), never Oxide
// Station's lettersanity row.
static int AP_CortexLetterTrack(int track)
{
	return track == CTR_CFG_CORTEX_HOST_LEVEL && AP_CortexTrackActive();
}

static void AP_NotifyCustomTrackCtrImpl(int didWin, int collected)
{
	struct GameTracker *gGT = sdata->gGT;
	if (!gGT || !(gGT->gameMode2 & TOKEN_RACE) || !ctr_cfg.custom_ctr_enabled ||
	    !ap_net_location_exists(ctr_cfg.custom_ctr_location) ||
	    !AP_CustomLetterSlot(ctr_cfg.custom_track.host_level_id) ||
	    !AP_LetterTokenEarned(ctr_cfg.custom_track.host_level_id, didWin, collected))
		return;
	int sent = AP_EmitClassCheck(ctr_cfg.custom_ctr_location, 0, -1, -1, 1,
	                  "[AP CHECK] custom track slot=%d CTR challenge location %ld\n",
	                  ctr_cfg.custom_track.slot, ctr_cfg.custom_ctr_location);
	// #208 ctr_first: the qualifying CTR win also sends the paired Trophy.
	// When only the Trophy is new (the CTR check was collected), the
	// ceremony shows the Trophy.
#ifdef CTR_CUSTOM_TRACKS
	int pairSent = AP_RaceStructureCtrWon(ctr_cfg.custom_ctr_location,
	                                      CTR_CFG_PODIUM_TRACK_COUNT + ctr_cfg.custom_track.slot - 1,
	                                      AP_CUSTOM_TROPHY_PSEUDO_BIT);
#else
	int pairSent = AP_RaceStructureCtrWon(ctr_cfg.custom_ctr_location,
	                                      CTR_CFG_PODIUM_TRACK_COUNT + ctr_cfg.custom_track.slot - 1, -1);
#endif
	if (sent || pairSent)
	{
#ifdef CTR_CUSTOM_TRACKS
		ap_custom_ceremony_bit = sent ? AP_CUSTOM_CTR_PSEUDO_BIT : AP_CUSTOM_TROPHY_PSEUDO_BIT;
		AP_CustomTrophyCeremonyArm(&ap_custom_trophy_ceremony, 1);
#endif
	}
}

// #286: the per-race custom-CTR producer. Blocked for the forced-loss attempt.
void AP_NotifyCustomTrackCtr(int didWin, int collected)
{
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_CUSTOM_CTR))
		return;
	AP_NotifyCustomTrackCtrImpl(didWin, collected);
}

// #286: the final Gem Cup standings aggregate for a custom cup's CTR challenge.
// The cup reward is granted in full while the latch is set.
void AP_NotifyCupAggregateCustomTrackCtr(int didWin, int collected)
{
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_CUP_AGGREGATE))
		return;
	AP_NotifyCustomTrackCtrImpl(didWin, collected);
}

int AP_LetterAvailable(int track, int letter)
{
	int slot = AP_CustomLetterSlot(track);
	if (letter < 0 || letter >= 3) return 1;
#ifdef CTR_CUSTOM_TRACKS
	{
		const ctr_content_pad *pad = AP_StandaloneLetterPad(track);
		if (pad)
		{
			long codes[3];
			unsigned char received[3];
			AP_ContentLetterState(pad, codes, received);
			return AP_LetterAvailablePure(1, ap_content_plan_checks()->lettersanity, codes[letter], received[letter]);
		}
	}
#endif
	if (AP_CortexLetterTrack(track))
		return !ctr_cfg_active() ||
		       AP_LetterAvailablePure(1, ctr_cfg.lettersanity_mode,
		                              AP_CortexTrackCode(AP_CV_SLOT_LETTER0 + letter),
		                              ap_cortex_letter_received[letter]);
	if (slot > 0 && slot <= AP_CUSTOM_LETTER_SLOT_COUNT)
		return AP_LetterAvailablePure(1, ctr_cfg.custom_lettersanity_mode,
		                              ctr_cfg.custom_letter_locations[letter],
		                              ap_custom_letter_received[slot - 1][letter]);
	if (!ctr_cfg_active() || ctr_cfg.lettersanity_mode < 2) return 1;
	if (track < 0 || track >= CTR_CFG_LETTER_TRACK_COUNT || letter < 0 || letter >= 3) return 1;
	return AP_LetterAvailablePure(1, ctr_cfg.lettersanity_mode,
	                              ctr_cfg.lettersanity_locations[track][letter],
	                              ap_letter_received[track][letter]);
}

// The adventure-map tracker is drawn from the hub, where AP_CortexLetterTrack is
// false because LevelID 13 is not loaded, so the Cortex Vortex row cannot ask
// AP_LetterAvailable (#379). Same rule, same mode field and same received state
// as the AP_CortexLetterTrack branch above, minus the loaded-level question.
int AP_CortexLetterAvailableForTracker(int letter)
{
	if (letter < 0 || letter >= 3) return 1;
	return !ctr_cfg_active() ||
	       AP_LetterAvailablePure(1, ctr_cfg.lettersanity_mode,
	                              AP_CortexTrackCode(AP_CV_SLOT_LETTER0 + letter),
	                              ap_cortex_letter_received[letter]);
}

long AP_LetterLocation(int track, int letter)
{
#ifdef CTR_CUSTOM_TRACKS
	{
		const ctr_content_pad *pad = AP_StandaloneLetterPad(track);
		if (pad)
		{
			long codes[3];
			ap_content_letter_codes(pad, codes);
			return letter >= 0 && letter < 3 ? codes[letter] : -1;
		}
	}
#endif
	if (AP_CortexLetterTrack(track))
		return letter >= 0 && letter < 3 ? AP_CortexTrackCode(AP_CV_SLOT_LETTER0 + letter) : -1;
	if (AP_CustomLetterSlot(track))
		return letter >= 0 && letter < 3 ? ctr_cfg.custom_letter_locations[letter] : -1;
	if (!ctr_cfg_active() || track < 0 || track >= CTR_CFG_LETTER_TRACK_COUNT || letter < 0 || letter >= 3) return -1;
	return ctr_cfg.lettersanity_locations[track][letter];
}

void AP_LetterCollected(int track, int letter)
{
	// #319: a letter whose scouted item belongs to another slot must queue the
	// normal sent-item feed line, same as any other class. AP_EmitClassCheck's
	// own-checked guard still dedupes a re-touch, reload or reconnect, and
	// AP_FeedOnLocationSent still suppresses the toast for a local recipient
	// (that case is already shown once through the received-item path).
	AP_EmitClassCheck(AP_LetterLocation(track, letter), 0, -1, 0, AP_LETTER_TOAST_SENT_ITEM,
	                  "[AP LETTER] track=%d%s letter=%d\n", track,
	                  AP_CortexLetterTrack(track) ? " (Cortex Vortex)" : "", letter);
}

void AP_LetterUnavailableTouched(int track, int letter)
{
	static const char names[CTR_CFG_LETTER_COUNT] = {'C', 'T', 'R'};
	char msg[160];
	long code = AP_LetterLocation(track, letter);

	if (track < 0 || track >= CTR_CFG_LETTER_TRACK_COUNT ||
	    letter < 0 || letter >= CTR_CFG_LETTER_COUNT)
		return;
	snprintf(msg, sizeof msg,
	         "[AP LETTER] pickup refused: track=%d%s letter=%c location=%ld; Lettersanity item not received\n",
	         track, AP_CortexLetterTrack(track) ? " (Cortex Vortex)" : "", names[letter], code);
	AP_AppendLog(msg);
}

// Gather the facts the Wumpa dispatch reads. Split from the decision so
// tools/test-wumpa-dispatch.c can pin the truth table out of engine; this half
// is the part that needs a live GameTracker.
//
// The DESTINATION, not the physical pad. `gGT->levelID` during a race is the
// track actually loaded, which under destination shuffle IS the destination --
// so loading Crash Cove from another pad or as a Gem Cup leg already reads as
// Crash Cove here and the identity rule needs no special case. The apworld
// gives the location an OR route from the standalone race and every legging Cup,
// so native must not re-impose the former individual-pad gate here.
static void AP_WumpaGatherFacts(struct GameTracker *gGT,
                                struct AP_WumpaDispatchFacts *facts)
{
	int level;

	memset(facts, 0, sizeof *facts);
	facts->wumpa = &ctr_cfg.wumpa;
	facts->destLevelID = -1;
	facts->servingCupLevelID = -1;
	facts->seedPackageUuid = ctr_cfg.custom_track.package_uuid;
	facts->seedCustomOk = ctr_cfg.custom_tracks_ok;
	facts->seedWumpaCollectible = ctr_cfg.custom_track.flags.wumpa_collectible;
	facts->oxideFinalCode = ctr_cfg.oxide_final_venue.wumpa_location;

	if (gGT == 0)
		return;
	level = (int)gGT->levelID;
	if (level >= 0 && level < CTR_CFG_WUMPA_TRACK_COUNT)
		facts->destLevelID = level;

#ifdef CTR_CUSTOM_TRACKS
	// Standalone routes never inherit the engine host's Wumpa identity or a
	// legacy Gem Cup's Wumpa route. A content-plan Wumpa route is separate.
	if (CustomTrack_StandaloneContext(level))
	{
		facts->destLevelID = -1;
		return;
	}
	// A content-plan garage venue on custom bytes (#133 PR 6b) races on the
	// home track's LevelID; content plans route no Wumpa from boss races, so
	// it must not resolve to the home track's code.
	if (CustomTrack_GarageVenueServing(level, gGT->bossID, (gGT->gameMode1 & ADVENTURE_BOSS) != 0) >= 0)
	{
		facts->destLevelID = -1;
		return;
	}
	facts->servingOxideFinal = CustomTrack_OxideFinalServing(
	    level, gGT->bossID, (gGT->gameMode1 & ADVENTURE_BOSS) != 0);
	if (facts->servingOxideFinal)
		return;
	// Schema 15: the Cortex Vortex pad track, from a pad or as a Gem Cup leg.
	// Its host LevelID 13 must never resolve to Oxide Station's retail code.
	facts->servingCortexTrack = AP_CortexTrackActive();
	facts->cortexTrackCode = AP_CortexTrackCode(AP_CV_SLOT_WUMPA);
	if (facts->servingCortexTrack)
		return;
	// The SAME predicate that owns the custom bytes answers whether this load is
	// the event race. Asking anything else here is how a recording's identity
	// and the geometry it was recorded against drift apart, and the argument
	// applies just as directly to a location code.
	facts->servingCustom = CustomTrack_ServingLoad(
	    level, (gGT->gameMode1 & ADVENTURE_CUP) != 0, gGT->cup.cupID);
	facts->customFaulted = CustomTrack_ServeFaultReason() != NULL;
	if (facts->servingCustom)
		facts->servingCupLevelID = 100 + gGT->cup.cupID;
#endif
	if (facts->servingCustom)
		return; // the retail terms below describe a race that is not running

	// A gem-cup leg, and NOT the custom event race: that race sets ADVENTURE_CUP
	// for reward routing but legs no track, which is why the test above returns
	// before reaching this line.
	facts->isCupLeg = (gGT->gameMode1 & ADVENTURE_CUP) != 0;
}

void AP_WumpaReachedTen(struct Driver *driver)
{
	struct AP_WumpaDispatchFacts facts;
	char msg[192];
	int reason = AP_WUMPA_SENT;
	long code;

	if (driver == 0 || sdata == 0 || sdata->gGT == 0 ||
	    driver != sdata->gGT->drivers[0])
		return;

#ifdef CTR_CUSTOM_TRACKS
	// A content-plan race (#133 PR 3b) sends its pad's own Reach 10 Wumpa
	// check or the global one, never the engine host's retail code.
	if (CustomTrack_StandaloneContext((int)sdata->gGT->levelID))
	{
		code = AP_StandaloneWumpaCode();
		if (code < 0)
		{
			if (ap_content_plan_checks()->wumpa)
			{
				snprintf(msg, sizeof msg,
				         "[AP WUMPA] reached 10 fruit, no check sent: no Wumpa route for this "
				         "content-plan race (level=%d)\n", (int)sdata->gGT->levelID);
				AP_AppendLog(msg);
			}
			return;
		}
		if (!ap_net_location_exists(code))
			return;
		AP_EmitClassCheck(code, 0, -1, 0, 1,
		                  "[AP WUMPA] reached 10 fruit (content plan, location %ld)\n", code);
		return;
	}
#endif

	AP_WumpaGatherFacts(sdata->gGT, &facts);
	code = AP_WumpaResolveCode(&facts, &reason);
	if (code < 0)
	{
		// Refusals are logged rather than swallowed. A player whose per-track
		// check did not fire has to be able to tell "this seed has no check for
		// this track" from "the client refused the content it was handed", and
		// only one of those is worth reporting as a bug. Mode-off is the single
		// silent case, because an off seed would otherwise log on every race.
		if (reason != AP_WUMPA_REFUSE_MODE_OFF)
		{
			snprintf(msg, sizeof msg,
			         "[AP WUMPA] reached 10 fruit, no check sent: %s "
			         "(level=%d cupLeg=%d custom=%d)\n",
			         AP_WumpaRefusalText(reason), facts.destLevelID,
			         facts.isCupLeg, facts.servingCustom);
			AP_AppendLog(msg);
		}
		return;
	}

	// Server location membership stays the final gate, exactly as it was before
	// per-track: the wire says which codes this seed MINTED, the server says
	// which ones are live for this slot, and a code that is not live is not sent.
	// AP_EmitClassCheck adds the ordinary already-checked dedup on top, which is
	// what makes reaching 10 twice on one track one location and reaching it on
	// two tracks two locations.
	if (!ap_net_location_exists(code))
		return;
	AP_EmitClassCheck(code, 0, -1, 0, 1,
	                  "[AP WUMPA] reached 10 fruit (location %ld)\n", code);
}

static void AP_CortexLetterCodes(long codes[3])
{
	int l;
	for (l = 0; l < 3; l++)
		codes[l] = AP_CortexTrackCode(AP_CV_SLOT_LETTER0 + l);
}

int AP_LettersRequiredMet(int track)
{
	int slot = AP_CustomLetterSlot(track);
#ifdef CTR_CUSTOM_TRACKS
	{
		const ctr_content_pad *pad = AP_StandaloneLetterPad(track);
		if (pad)
		{
			long codes[3];
			unsigned char received[3];
			AP_ContentLetterState(pad, codes, received);
			return AP_LettersRequiredMetPure(1, ap_content_plan_checks()->lettersanity, codes, received);
		}
	}
#endif
	if (AP_CortexLetterTrack(track))
	{
		long codes[3];
		AP_CortexLetterCodes(codes);
		return !ctr_cfg_active() ||
		       AP_LettersRequiredMetPure(1, ctr_cfg.lettersanity_mode, codes,
		                                 ap_cortex_letter_received);
	}
	if (slot > 0 && slot <= AP_CUSTOM_LETTER_SLOT_COUNT)
		return AP_LettersRequiredMetPure(1, ctr_cfg.custom_lettersanity_mode,
		                                 ctr_cfg.custom_letter_locations,
		                                 ap_custom_letter_received[slot - 1]);
	if (!ctr_cfg_active() || ctr_cfg.lettersanity_mode < 2) return 1;
	if (track < 0 || track >= CTR_CFG_LETTER_TRACK_COUNT) return 1;
	return AP_LettersRequiredMetPure(1, ctr_cfg.lettersanity_mode,
	                                 ctr_cfg.lettersanity_locations[track],
	                                 ap_letter_received[track]);
}

int AP_LettersRequiredCount(int track)
{
#ifdef CTR_CUSTOM_TRACKS
	{
		const ctr_content_pad *pad = AP_StandaloneLetterPad(track);
		if (pad)
		{
			long codes[3];
			ap_content_letter_codes(pad, codes);
			return AP_LettersRequiredCountPure(1, ap_content_plan_checks()->lettersanity, codes);
		}
	}
#endif
	if (AP_CortexLetterTrack(track))
	{
		long codes[3];
		AP_CortexLetterCodes(codes);
		return ctr_cfg_active() ? AP_LettersRequiredCountPure(1, ctr_cfg.lettersanity_mode, codes) : 3;
	}
	if (AP_CustomLetterSlot(track))
		return AP_LettersRequiredCountPure(1, ctr_cfg.custom_lettersanity_mode,
		                                   ctr_cfg.custom_letter_locations);
	if (!ctr_cfg_active() || ctr_cfg.lettersanity_mode < 2) return 3;
	if (track < 0 || track >= CTR_CFG_LETTER_TRACK_COUNT) return 3;
	return AP_LettersRequiredCountPure(1, ctr_cfg.lettersanity_mode,
	                                   ctr_cfg.lettersanity_locations[track]);
}

static void AP_FeedLetterReadyUpdates(void)
{
	// Track-wide receipt events, not current-race events. Initial inventory is
	// baselined while the shared feed is unprimed, including reconnect replay.
	static const char *const names[CTR_CFG_LETTER_TRACK_COUNT] = {
		"Dingo Canyon", "Dragon Mines", "Blizzard Bluff", "Crash Cove",
		"Tiger Temple", "Papu's Pyramid", "Roo's Tubes", "Hot Air Skyway",
		"Sewer Speedway", "Mystery Caves", "Cortex Castle", "N. Gin Labs",
		"Polar Pass", "Oxide Station", "Coco Park", "Tiny Arena",
		"Slide Coliseum", "Turbo Track"
	};
	int track;
	for (track = 0; track < CTR_CFG_LETTER_TRACK_COUNT; track++)
	{
		int ready = ctr_cfg_active() && ctr_cfg.lettersanity_mode >= 2 &&
			AP_LettersRequiredCountPure(1, ctr_cfg.lettersanity_mode,
			                          ctr_cfg.lettersanity_locations[track]) > 0 &&
			AP_LettersRequiredMetPure(1, ctr_cfg.lettersanity_mode,
			                        ctr_cfg.lettersanity_locations[track],
			                        ap_letter_received[track]);
		if (ready && !ap_feed_letters_ready[track] && ap_feed_primed && ap_hub_feed_on)
		{
			char line[128];
			snprintf(line, sizeof line, "%s: Got CTR letters",
			         names[track]);
			AP_FeedEnqueue(line, LIGHT_GREEN, 1);
		}
		ap_feed_letters_ready[track] = (unsigned char)ready;
	}
}

// Issue #377: "Oxide Final Challenge is open", once per seed, on reaching go
// mode for the Final Challenge, independent of whether the Oxide scene plays.
// "Shown" is bit 1 of the same server data storage key as the scene flag
// (ap/ap_oxide_scene_seen.h), so it survives a restart and a fresh room starts
// clean. Nothing is shown or recorded until that key has been read, and while
// the item feed is hidden the message waits rather than being spent unseen.
static void AP_FeedOxideFinalOpenUpdate(void)
{
	int beatSecond;

	if (!ctr_cfg_active() || !ap_feed_primed || !ap_hub_feed_on)
		return;
	if (!ap_net_oxide_scene_known() || ap_net_oxide_open_msg_shown())
		return;
	if (sdata == NULL || sdata->gGT == NULL)
		return;

	beatSecond = CHECK_ADV_BIT(sdata->advProgress.rewards, ADV_REWARD_BEAT_OXIDE_SECOND) != 0;
	if (!AP_OxideFinalOpenMsgWanted(1, 1, 1, 0, AP_OxideFinalGoMode(), beatSecond))
		return;

	AP_FeedEnqueue("Oxide Final Challenge is open", OXIDE_LIGHT_GREEN, 1);
	ap_net_oxide_open_msg_record();
}

int AP_LetterTokenEarned(int track, int didWin, int collected)
{
	int slot = AP_CustomLetterSlot(track);
#ifdef CTR_CUSTOM_TRACKS
	{
		// A content-plan CTR Challenge (#133 PR 3b): with Lettersanity off or
		// in mode 1 this is the retail rule (a win with three letters).
		const ctr_content_pad *pad = AP_StandaloneLetterPad(track);
		if (pad)
		{
			long codes[3];
			unsigned char received[3];
			AP_ContentLetterState(pad, codes, received);
			return AP_LetterTokenEarnedPure(didWin, collected, 1, ap_content_plan_checks()->lettersanity,
			                                codes, received);
		}
	}
#endif
	if (AP_CortexLetterTrack(track))
	{
		long codes[3];
		if (!ctr_cfg_active() || ctr_cfg.lettersanity_mode < 2)
			return didWin && collected == 3;
		AP_CortexLetterCodes(codes);
		return AP_LetterTokenEarnedPure(didWin, collected, 1, ctr_cfg.lettersanity_mode,
		                                codes, ap_cortex_letter_received);
	}
	if (slot > 0 && slot <= AP_CUSTOM_LETTER_SLOT_COUNT)
		return AP_LetterTokenEarnedPure(didWin, collected, 1,
		                                ctr_cfg.custom_lettersanity_mode,
		                                ctr_cfg.custom_letter_locations,
		                                ap_custom_letter_received[slot - 1]);
	if (!ctr_cfg_active() || ctr_cfg.lettersanity_mode < 2 ||
	    track < 0 || track >= CTR_CFG_LETTER_TRACK_COUNT)
		return didWin && collected == 3;
	return AP_LetterTokenEarnedPure(didWin, collected, 1,
	                                ctr_cfg.lettersanity_mode,
	                                ctr_cfg.lettersanity_locations[track],
	                                ap_letter_received[track]);
}

#endif // CTR_AP
