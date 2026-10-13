// ap_hooks_goal.c -- part of the ap_hooks unit (issue #305).
// AP state generation counter and the composed goal: goal state, evaluation
// and the Oxide ending claim.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

// ---------------------------------------------------------------------------
// AP STATE GENERATION (Warp-Pad State Model v2, foundation for live re-birth).
// A monotonically increasing counter bumped whenever something that can change a
// pad's AP_PadState happens: a fresh slot-connect (slot_data activation), a
// received item that changes a gate count, or a location-checked notification.
// A future in-hub re-birth consumer tags each pad with the generation it was
// born at and, on mismatch, rebuilds the pad's in-hub instances so the 3D look
// tracks state changes in real time (item arrives / check completes elsewhere
// while standing in the hub) -- and re-remaps a pad born before a late connect.
//
// NOT yet consumed for re-birth: AH_WarpPad_LInB is a LEVEL-LOAD one-shot (see
// INSTANCE_LevLInBs), and the only teardown pattern the codebase proves is
// destruction (inst->thread=0 + THREAD_FLAG_DEAD, e.g. RB_Crate). A safe in-hub
// re-birth needs a lifecycle this code does not yet establish, so that consumer
// is deferred to a focused, in-game-verified change rather than inferred blind
// (crew verify-first rule). The MAP colour and every gate already recompute
// AP_PadState live each frame, so those two surfaces are honest in real time
// today; only the in-hub 3D structural look and a late-connect pad's destination
// remap wait on this re-birth.
// ---------------------------------------------------------------------------
static unsigned ap_state_gen = 0;

unsigned AP_StateGen(void)
{
	return ap_state_gen;
}

// ---------------------------------------------------------------------------
// LOCATION EVENTS (option A) -- authoritative. Called from the game's reward
// grant sites; logs the check and sends it to the server.
// ---------------------------------------------------------------------------

// Goal completion state. The Oxide beats are game EVENTS (set by AP_NotifyGoal);
// item-based goals read received counts. ap_goal_sent makes the send idempotent.
static int ap_oxide_first_beaten = 0;
static int ap_oxide_final_beaten = 0;
static int ap_goal_sent = 0;
static AP_GoalPresentationState ap_goal_presentation = {0};

// Send StatusUpdate(GOAL) once when the composed per-seed goal is met (issue
// #152). ctr_cfg.goal_oxide/goal_bosses/goal_gems replace the old single
// ctr_cfg.goal switch: the goal is the AND of every ACTIVE condition (0
// means "off" for all three; the apworld's generate_early guarantees at
// least one condition is active, so this is never vacuous on a real seed).
//   goal_oxide 1 (first) -> beat N. Oxide's Challenge
//   goal_oxide 2 (final) -> beat N. Oxide's Final Challenge
//   goal_bosses N (>0)   -> N of the 4 boss races personally won, from the
//                           #458 boss-won flags (AP_ComposedBossesWon). NOT
//                           the checked boss-race LOCATION (Collect, Release
//                           and admin commands check it too), NOT
//                           AP_GateCount(Trophy)>=16, and NOT CHECK_ADV_BIT
//                           on the boss key bits 94-97: those are the Key
//                           item pool (AP_POOL_KEY), so AP_ApplyItems sets
//                           them from RECEIVED keys -- holding 4 shuffled
//                           Keys is not beating 4 bosses (the BUG-D class).
//   goal_gems N (>0)     -> N of the 5 Gems held. AP_GateCountGemSum() is
//                           exactly a distinct-colour count here, because
//                           every Gem is a singleton item (at most one
//                           received copy per colour).
// "Any N" on both counted conditions, mirrored identically on the apworld
// side (dossier C11): a tally compared against the configured count, never
// "the first N in a fixed order".
// Without slot_data (vanilla / no AP config) we keep the legacy behaviour:
// the first Oxide beat is the goal.
static void AP_FeedEnqueue(const char *text, int color, int playCue); // defined with the feed block below

// How many of the four boss RACES the player has personally won, in the one
// form every consumer of goal_bosses must use: the #458 boss-won flags
// (ap/ap_boss_won_flags.h), set on each live boss win and kept per seed, team
// and slot in server data storage. NOT the checked boss-race LOCATION: the
// server also checks it for another player's Collect, this player's own
// !collect or !release, and admin commands, so a Collect could complete the
// goal with no boss raced (#458). Never CHECK_ADV_BIT on bits 94-97 and never
// received Keys either -- those are the Key item pool's mirror, rewritten by
// AP_ApplyItems from RECEIVED items on every reconcile tick (the BUG-D class).
// Factored out of AP_EvaluateGoal so the goal evaluator, the goal advert, the
// Oxide garage gate (WO-A1) and the DeathLink win logic cannot drift apart
// about what "won a boss race" means. Counts 0 until the flags are known or a
// win is recorded, which only delays the goal.
static int AP_ComposedBossesWon(void)
{
	return AP_BossWonCount(ap_net_boss_won_bits());
}

void AP_EvaluateGoal(void)
{
	int done;

	if (ap_goal_sent)
		return;

	// #163: never auto-goal on a shape this build did not parse. The verifier
	// already returns early on schema_newer (ap_verify.c) so it never reasons
	// over an unknown shape; the goal evaluator must be at least as careful,
	// because there is NO safe legacy value for ctr_options.goal -- whatever a
	// newer apworld writes there resolves on this build to some real, reachable
	// win condition, so guessing sends GOAL early rather than not at all.
	// (schema_newer is only ever set while slot_data is active, so vanilla /
	// no-config sessions never take this branch.)
	if (ctr_cfg.schema_newer)
	{
		static int warned = 0;
		if (!warned)
		{
			warned = 1;
			AP_LogLine("[AP GOAL] auto-goal disabled: seed schema is newer than this client\n");
			AP_FeedEnqueue("GOAL CHECK OFF: seed needs a newer client", ORANGE, 0);
		}
		return;
	}

	if (!ctr_cfg_active())
	{
		done = ap_oxide_first_beaten;
	}
	else
	{
		int won = AP_ComposedBossesWon();
		done = AP_ComposedGoalMet(
		    ctr_cfg.goal_oxide, ap_oxide_first_beaten, ap_oxide_final_beaten,
		    ctr_cfg.goal_bosses, won,
		    ctr_cfg.goal_gems, AP_GateCountGemSum());
	}

	if (done)
	{
		char msg[128];
		ap_goal_sent = 1;
		snprintf(msg, sizeof msg,
		         "[AP GOAL] goal (oxide=%d bosses=%d gems=%d) complete -- StatusUpdate(GOAL)\n",
		         ctr_cfg_active() ? ctr_cfg.goal_oxide : -1,
		         ctr_cfg_active() ? ctr_cfg.goal_bosses : -1,
		         ctr_cfg_active() ? ctr_cfg.goal_gems : -1);
		AP_AppendLog(msg);
		ap_net_send_goal();
	}

	AP_GoalPresentationEvaluate(&ap_goal_presentation, done);
}

void AP_GoalArmLiveEvent(void)
{
	AP_GoalPresentationArm(&ap_goal_presentation);
}

int AP_GoalClaimOxideEnding(void)
{
	return AP_GoalPresentationClaim(&ap_goal_presentation);
}

// Per-session dedup so a grant site that re-enters (an end-event drawn every
// frame, or a reward path without its own CHECK_ADV_BIT guard) only produces one
// check. 192 bits = the 6 AdvProgress reward words.
static u32 ap_notified_mask[6] = {0};

// Podium fan-out lives with the race listener below; forward-declared here so
// the trophy-award path (a win => 1st => every rung) and the connect-time
// reconciliation sweep can both drive it as crash/miss backstops.
static void AP_SendPodiumChecks(int track, int placement);
static void AP_ReconcilePodiumFromTrophies(void);

#endif // CTR_AP
