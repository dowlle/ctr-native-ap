#ifndef AP_OXIDE_ENTRY_H
#define AP_OXIDE_ENTRY_H

// ---------------------------------------------------------------------------
// Oxide garage ENTRY readiness (WO-A1, composed goals #152 + Oxide gate).
//
// THE DEFECT THIS EXISTS TO CLOSE. AP_EvaluateGoal ANDs every ACTIVE composed
// goal condition (goal_oxide / goal_bosses / goal_gems) and ap_verify.c mirrors
// it, but the Oxide GARAGE never learned about the companion conditions: both
// gate sites in game/232/AH_Garage.c asked only AP_BossReqMet(&boss_req[4]),
// which the apworld resolves to {type:2,count:4} -- four RECEIVED Keys. So on
// `Any% + All Four Bosses` the door opened on Key 4 with zero boss races won,
// which reads to the player as "you are ready / you have finished", and the
// only thing that actually knew better was the goal evaluator that then
// declined to send GOAL. Reported from the 2026-08-25 Alpha 4 play session.
//
// THE RULE. When Oxide is part of the goal at all (goal_oxide != 0), his garage
// is the goal gate, so it opens only once EVERY active goal condition is true.
// When goal_oxide == 0 Oxide is not a goal condition and the garage must stay
// an ordinary door: the companion conjunction is NOT applied, because turning a
// non-goal garage into a goal gate would lock content the seed never gated.
//
// WHAT THIS IS NOT. It is not the first-versus-final encounter selector. Which
// Oxide encounter loads once you are inside stays AP_OxideFinalOpen()'s
// decision (relic-goal mode + count, issue #23) and is deliberately kept
// separate -- entry readiness and encounter selection answer different
// questions and must not be collapsed.
//
// TRUTH SOURCES (the caller's job, restated here because getting it wrong is
// the BUG-D class this whole area keeps relapsing into):
//   * bossesWon MUST be counted from CHECKED boss-race LOCATIONS
//     (AP_LocationCheckedByBit(ADV_REWARD_FIRST_BOSS_KEY + b)), never from
//     received Keys and never from CHECK_ADV_BIT on bits 94-97 -- those bits
//     are the Key item pool's mirror, which AP_ApplyItems rewrites from
//     RECEIVED items on every reconcile tick. Holding four shuffled Keys is
//     not beating four bosses.
//   * gemsHeld is a DISTINCT-COLOUR count of received Gems
//     (AP_GateCountGemSum); every Gem is a singleton item, so the sum is
//     exactly the distinct count.
// Both are server truth, so the answer stays correct across reconnect,
// profile load and hub re-entry without any latch of its own.
//
// Kept as a pure function over plain ints so tools/test-oxide-entry.c can drive
// the whole truth table -- including every one-short partial state -- without
// an engine, a socket or a seed.
// ---------------------------------------------------------------------------

// `garageReqMet` is the ordinary configured Oxide garage requirement already
// resolved by the caller (AP_BossReqMet(&ctr_cfg.boss_req[4]), normally four
// received Keys). Returns non-zero when the garage should be open.
static inline int AP_OxideEntryReady(int garageReqMet,
                                     int goalOxide,
                                     int goalBosses, int bossesWon,
                                     int goalGems, int gemsHeld)
{
	// The configured door requirement is necessary in every seed. It is checked
	// first so a seed that emits something other than four Keys keeps its own
	// requirement rather than inheriting the goal's.
	if (!garageReqMet)
		return 0;

	// Oxide is not a goal condition -> ordinary door, no composed conjunction.
	if (goalOxide == 0)
		return 1;

	// Oxide IS the goal gate -> every ACTIVE companion condition must hold.
	// A zero count means that arm is off, exactly as in AP_ComposedGoalMet.
	if (goalBosses > 0 && bossesWon < goalBosses)
		return 0;
	if (goalGems > 0 && gemsHeld < goalGems)
		return 0;

	return 1;
}

#endif // AP_OXIDE_ENTRY_H
