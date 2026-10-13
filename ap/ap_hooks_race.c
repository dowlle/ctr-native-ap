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

void AP_NotifyRelicPerfect(void)
{
	struct GameTracker *gGT;
	struct Driver *driver;
	AP_RelicPerfectFacts facts;
	long code;

	if (!ctr_cfg_active() || !ctr_cfg.relic_perfect_enabled)
		return;
	// #286: a forced-loss relic attempt pays no finish-class check.
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_RELIC_PERFECT))
		return;
	gGT = sdata ? sdata->gGT : 0;
	driver = gGT ? gGT->drivers[0] : 0;
	if (gGT == 0 || driver == 0 || (gGT->gameMode1 & RELIC_RACE) == 0)
		return;

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

void AP_NotifyTrialTrackRace(int levelID, int challenge)
{
	// #286: a forced-loss trial attempt emits no direct trial check or podium rung.
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_TRIAL_TRACK))
		return;

	long code = AP_TrialTrackLocation(levelID, challenge);
	AP_EmitClassCheck(code, 0, -1, -1, 1,
	                  "[AP CHECK] trial track=%d challenge=%d location %ld\n",
	                  levelID, challenge, code);
	if (challenge == CTR_CFG_TRIAL_TROPHY && code > 0)
		AP_SendPodiumChecks(AP_TRIAL_PODIUM_LOGICAL_BASE+levelID-16, 1);
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
		AP_EmitClassCheck(code, 1, bits[i], -1, 1,
		                  "[AP CHECK] Cortex Vortex %s location %ld\n",
		                  token ? "CTR Token Challenge" : "Trophy Race", code);
	}
	// A trophy win is a 1st-place finish: every configured rung is earned.
	if (!token && AP_CortexTrackCode(AP_CV_SLOT_TROPHY) > 0)
		AP_SendPodiumChecks(AP_CV_PODIUM_LOGICAL_TRACK, 1);
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
	sent = AP_EmitClassCheck(ctr_cfg.custom_track.trophy_location,
	                         1, AP_CUSTOM_TROPHY_PSEUDO_BIT, -1, 1,
	                         "[AP CHECK] custom track slot=%d Trophy Race location %ld\n",
	                         ctr_cfg.custom_track.slot,
	                         ctr_cfg.custom_track.trophy_location);
	AP_CustomTrophyCeremonyArm(&ap_custom_trophy_ceremony, sent);
	if (sent) ap_custom_ceremony_bit = AP_CUSTOM_TROPHY_PSEUDO_BIT;
	podiumTrack = CTR_CFG_PODIUM_TRACK_COUNT + ctr_cfg.custom_track.slot - 1;
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
#else
int AP_CustomTrackTrophyChecked(void) { return 0; }
void AP_NotifyCustomTrackTrophy(void) {}
void AP_NotifyCupAggregateCustomTrackTrophy(void) {}
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
	if (sent)
	{
#ifdef CTR_CUSTOM_TRACKS
		ap_custom_ceremony_bit = AP_CUSTOM_CTR_PSEUDO_BIT;
		AP_CustomTrophyCeremonyArm(&ap_custom_trophy_ceremony, sent);
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
