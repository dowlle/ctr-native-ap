// ap_hooks_location_events.c -- part of the ap_hooks unit (issue #305).
// Location events from the game's reward grant sites: AP_NotifyAdvReward, the
// cup aggregate reward and AP_NotifyGoal.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

// Shared body for the two advance-reward entry points below: the per-race
// producer and the final-standings cup aggregate. The guard lives in the
// wrappers so the cup aggregate can deliberately pass while the latch is set.
static void AP_NotifyAdvRewardImpl(int rewardBit)
{
	char msg[192];
	int newEarn = 0;
	int bossWonNew = 0;

	if (rewardBit < 0 || rewardBit >= 192)
		return;
	// #458: a boss race won here is a personal win, recorded on every win and
	// before the session dedup below, whether or not its location is already
	// checked: a Collect-marked boss is still unbeaten until it is raced.
	if (ctr_cfg_active() && rewardBit >= ADV_REWARD_FIRST_BOSS_KEY &&
	    rewardBit < ADV_REWARD_FIRST_BOSS_KEY + AP_BOSS_WON_BOSS_COUNT)
	{
		unsigned bit = AP_BOSS_WON_BIT(rewardBit - ADV_REWARD_FIRST_BOSS_KEY);
		bossWonNew = (ap_net_boss_won_bits() & bit) == 0;
		ap_net_boss_won_record(bit);
		bossWonNew = bossWonNew && (ap_net_boss_won_bits() & bit) != 0;
	}
	int w = rewardBit >> 5, b = rewardBit & 31;
	if (ap_notified_mask[w] & (1u << b))
	{
		if (bossWonNew)
		{
			AP_GoalArmLiveEvent();
			AP_EvaluateGoal();
		}
		return; // already notified this session
	}
	ap_notified_mask[w] |= (1u << b);

	long code = AP_LookupLocationCode(rewardBit);
	const char *name = AP_TrophyName(rewardBit);
	// code >= 0 only proves the bit is IN the compile-time 101/112-name superset
	// table -- that table is seed-independent, so a removed relic Time Trial
	// (issue #171/#28 R1: a below-count tier is never created, not merely pinned)
	// still resolves a code here. `exists` is the per-seed truth (AP's own
	// checked/missing location set) and is what decides whether there is
	// anything real to send.
	int exists = code >= 0 && ap_net_location_exists(code);

	if (exists)
	{
		snprintf(msg, sizeof msg,
		         "[AP CHECK] location: AP code %ld (bit %d)%s%s\n",
		         code, rewardBit, name ? " -- " : "", name ? name : "");
	}
	else if (code >= 0)
	{
		// Local-grant (#28 R1): the caller's own vanilla-presentation code
		// already ran (every grant site calls UNLOCK_ADV_BIT before this),
		// which is the reward's ONLY record from here -- AP_ApplyItems never
		// touches a local-grant bit (see its own skip), so it persists like a
		// vanilla flag. Nothing to check, nothing to send.
		snprintf(msg, sizeof msg,
		         "[AP LOCAL-GRANT] bit %d has no AP location this seed -- "
		         "granted natively, no check sent%s%s\n",
		         rewardBit, name ? " -- " : "", name ? name : "");
	}
	else
	{
		snprintf(msg, sizeof msg,
		         "[AP CHECK] non-location reward granted: bit %d (no AP code)\n",
		         rewardBit);
	}
	AP_AppendLog(msg);

	if (exists)
	{
		// #63: capture the checked-state BEFORE the send so a session replay /
		// re-win of an already-checked location never re-toasts the sent line.
		int wasChecked = ap_net_location_checked(code);
		newEarn = !wasChecked;
		// #188b: this is the ONE choke point every grant path funnels through,
		// and not every caller gates on AP_LocationCheckedByBit before reaching
		// it (the boss/story reward and Oxide-goal paths don't, unlike the
		// trophy/gem-cup sites) -- so a replay of an already-won event can
		// still get here with wasChecked true. Sending anyway would, while
		// offline, retain the code in g_held_checks and later resend a
		// LocationChecks the server already settled; since the item was
		// already delivered in an earlier connection, no ReceivedItems ever
		// arrives to erase the resend's g_pending_checks entry, so
		// ap_net_checks_in_flight() never returns to 0 and the #107
		// not-completable banner (gated on ap_vf_settled) stays silently
		// withheld for the rest of the connection. wasChecked is false for
		// every genuine first-time earn, so gating the send on it cannot drop
		// a real check -- only a resend of one already server-confirmed.
		if (!wasChecked)
		{
			ap_net_send_location(code); // LocationChecks([code])
			AP_PerfNoteCheckSent();
		}
		ap_state_gen++; // a location was checked -> the owning pad's state may shift
		AP_CeremonyLedgerAdd(code, rewardBit, -1); // feed the race-end award block
		if (!wasChecked)
			AP_FeedOnLocationSent(code); // toast nonlocal sends to the hub feed
	}

	// Podium backstop (event-time): winning a trophy race == finishing 1st ==
	// every podium rung earned. Fire them from THIS reliable award path, not only
	// the live finish-line capture, so a crash/reload/missed edge after the line
	// still delivers the rungs. Trophy bits are the contiguous block
	// [ADV_REWARD_FIRST_TROPHY, ADV_REWARD_FIRST_SAPPHIRE_RELIC); levelID is the
	// offset within it. Deduped inside AP_SendPodiumChecks.
	if (rewardBit >= ADV_REWARD_FIRST_TROPHY &&
	    rewardBit < ADV_REWARD_FIRST_SAPPHIRE_RELIC)
		AP_SendPodiumChecks(rewardBit - ADV_REWARD_FIRST_TROPHY, 1);

	// A personally won boss race can be the last active arm of a composed
	// goal. Keep this as a live edge rather than deriving it from reconnect
	// state, otherwise every reconnect to a completed seed would replay credits.
	// #458: a re-race of a Collect-marked boss is not a new location check but
	// is a new win, so it arms the goal too.
	if ((newEarn || bossWonNew) && rewardBit >= ADV_REWARD_FIRST_BOSS_KEY &&
	    rewardBit < ADV_REWARD_FIRST_BOSS_KEY + 4)
	{
		AP_GoalArmLiveEvent();
		AP_EvaluateGoal();
	}
}

// #286: the per-race advance-reward producer (Trophy, token, relic, boss).
// Blocked for the whole forced-loss attempt.
void AP_NotifyAdvReward(int rewardBit)
{
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_ADV_REWARD))
		return;
	AP_NotifyAdvRewardImpl(rewardBit);
}

// #286: the final Gem Cup standings aggregate. The cup reward is decided on
// accumulated points, so a cup won even after a forced loss on the final leg
// grants its check in full. Used only by the UI_CupStandings final cup-win
// block; every per-race caller stays on AP_NotifyAdvReward above.
void AP_NotifyCupAggregateReward(int rewardBit)
{
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_CUP_AGGREGATE))
		return;
	AP_NotifyAdvRewardImpl(rewardBit);
}

void AP_NotifyGoal(int oxideSecond)
{
	// #286: a forced-loss attempt must not arm or complete the goal.
	if (AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_GOAL))
		return;

	// Record the Oxide beat as a game EVENT. Whether it completes the seed
	// depends on ctr_cfg.goal_oxide (issue #152) -- with goal_oxide off (0)
	// or set to the OTHER Oxide race, beating this one is NOT the win by
	// itself -- so defer the StatusUpdate(GOAL) decision to AP_EvaluateGoal
	// (which also fires per-frame for the item-based conditions).
	AP_GoalArmLiveEvent();
	if (oxideSecond)
		ap_oxide_final_beaten = 1;
	else
		ap_oxide_first_beaten = 1;

	// #458: Oxide's Challenge personally cleared, kept across reconnects in the
	// boss-won flags. The opt-in final race also collects the first reward
	// (below), so it counts as clearing the first challenge too.
	if (ctr_cfg_active() &&
	    (!oxideSecond || (ctr_cfg.goal_oxide == 2 && ctr_cfg.oxide_1_optional)))
		ap_net_boss_won_record(AP_BOSS_WON_OXIDE_FIRST);

	// As of schema 4 the two Oxide beats are also REAL location checks
	// (35011104 / 35011105). AP_NotifyAdvReward looks up the bit's code in
	// AP_LOCATION_TABLE and sends the LocationCheck with per-session dedup, so a
	// pre-rework seed (bits not in its table) simply no-ops here.
	AP_NotifyAdvReward(oxideSecond ? AP_GOAL_BIT_OXIDE_SECOND
	                               : AP_GOAL_BIT_OXIDE_FIRST);
	// The first reward stays a real randomized location. A successful opt-in
	// final race collects it too; entry/loss never grants either reward.
	if (oxideSecond && ctr_cfg_active() && ctr_cfg.goal_oxide == 2 &&
	    ctr_cfg.oxide_1_optional)
		AP_NotifyAdvReward(AP_GOAL_BIT_OXIDE_FIRST);

	AP_EvaluateGoal();
}

#endif // CTR_AP
