#ifndef AP_OXIDE_ENCOUNTER_H
#define AP_OXIDE_ENCOUNTER_H

// ---------------------------------------------------------------------------
// Oxide garage: WHICH encounter is next, and IS IT OPEN. One decision.
//
// Replaces ap_oxide_entry.h's AP_OxideEntryReady (WO-A1, 2026-08-26), which
// answered only the first half and deliberately left encounter selection to
// AP_OxideFinalOpen(). Splitting them is what let the two drift: entry
// readiness ANDed every active companion goal arm onto the SHARED garage
// whenever goal_oxide != 0, while selection picked the Final Challenge from
// the relic requirement alone. Two defects fell out of that, both seen in the
// Alpha 7 play session of 1 September:
//
//   * on a 101_percent seed, Oxide's FIRST challenge -- an ordinary four-Key
//     midpoint -- was locked behind the Boss and Gem arms that are supposed to
//     gate only the Final Challenge. The player expected to race Oxide 1 and
//     found the garage shut;
//   * a player who already held enough relics before ever visiting the garage
//     was handed the Final Challenge immediately, skipping the first check.
//
// THE RULE (ruling of 2026-09-03; `goal_oxide` is the wire value):
//
//   value                | Oxide 1                   | Oxide 2
//   ---------------------|---------------------------|---------------------------
//   0 optional / none    | door req                  | door req + relics
//   1 any_percent        | door req + companions     | door req + relics
//   2 101_percent        | door req                  | door req + relics + companions
//   3 disabled           | absent, garage shut       | absent, garage shut
//
// "Companions" is every ACTIVE non-Oxide goal arm (goal_bosses, goal_gems); a
// zero count means that arm is off, exactly as in AP_ComposedGoalMet. They
// gate the SELECTED finale only. Under `optional` Oxide is ordinary content
// and no non-Oxide goal arm touches either race, even when those arms are
// independently active for a Boss-only or Gem-only goal -- turning a non-goal
// garage into a goal gate would lock content the seed never gated.
//
// ENCOUNTER PRIORITY. An UNCLEARED first challenge is always what the garage
// offers, even when the relic requirement for the Final Challenge is already
// satisfied. That is what stops a relic-rich player from skipping the first
// check. Only after the first clear may the garage advance to the Final
// Challenge.
//
// TRUTH SOURCES (the caller's job, restated here because getting it wrong is
// the BUG-D class this area keeps relapsing into):
//   * firstCleared MUST come from AUTHORITATIVE checked-location state
//     (AP_LocationCheckedByBit(AP_GOAL_BIT_OXIDE_FIRST)), never from the
//     ap_oxide_first_beaten session boolean -- that is reset on every
//     reconnect, so a reconnecting player would be offered the first
//     challenge again instead of the Final one.
//   * bossesWon MUST be counted from CHECKED boss-race LOCATIONS
//     (AP_LocationCheckedByBit(ADV_REWARD_FIRST_BOSS_KEY + b)), never from
//     received Keys and never from CHECK_ADV_BIT on bits 94-97 -- those bits
//     are the Key item pool's mirror, which AP_ApplyItems rewrites from
//     RECEIVED items on every reconcile tick. Holding four shuffled Keys is
//     not beating four bosses.
//   * gemsHeld is a DISTINCT-COLOUR count of received Gems
//     (AP_GateCountGemSum); every Gem is a singleton item, so the sum is
//     exactly the distinct count.
//   * finalRelicMet is AP_OxideFinalOpen(), the configured relic-goal mode +
//     count (issue #23). Kept as an input rather than recomputed here so the
//     gate, the verifier and this decision cannot disagree about it.
// All of those are server truth, so the answer stays correct across reconnect,
// profile load and hub re-entry without any latch of its own.
//
// Kept as a pure function over plain ints so tools/test-oxide-encounter.c can
// drive the whole truth table -- including every one-short partial state --
// without an engine, a socket or a seed.
// ---------------------------------------------------------------------------

// goal_oxide wire values (frozen; the apworld's Options.OxideGoal integers).
#define AP_OXIDE_GOAL_OPTIONAL 0
#define AP_OXIDE_GOAL_ANY      1
#define AP_OXIDE_GOAL_FINAL    2
#define AP_OXIDE_GOAL_DISABLED 3

// Which encounter the garage is currently offering.
#define AP_OXIDE_ENCOUNTER_NONE  0 // no Oxide content in this seed
#define AP_OXIDE_ENCOUNTER_FIRST 1 // N. Oxide's Challenge
#define AP_OXIDE_ENCOUNTER_FINAL 2 // N. Oxide's Final Challenge

typedef struct AP_OxideGarageInputs
{
	int garageReqMet;  // the configured door requirement, boss_req[4]
	int goalOxide;     // ctr_cfg.goal_oxide
	int firstCleared;  // AUTHORITATIVE checked-location state, see above
	int finalRelicMet; // AP_OxideFinalOpen()
	int goalBosses;
	int bossesWon;
	int goalGems;
	int gemsHeld;
} AP_OxideGarageInputs;

typedef struct AP_OxideGarageState
{
	int encounter;       // AP_OXIDE_ENCOUNTER_*
	int open;            // may the player go through the door right now
	int needsCompanions; // does the SELECTED encounter take the companion arms
	int needsRelics;     // does the SELECTED encounter take the relic gate
} AP_OxideGarageState;

// Is every ACTIVE companion goal arm satisfied? A zero count means that arm is
// off, exactly as in AP_ComposedGoalMet -- the goal evaluator is the authority
// these terms are copied from.
static inline int AP_OxideCompanionsMet(const AP_OxideGarageInputs *in)
{
	if (in->goalBosses > 0 && in->bossesWon < in->goalBosses)
		return 0;
	if (in->goalGems > 0 && in->gemsHeld < in->goalGems)
		return 0;
	return 1;
}

static inline AP_OxideGarageState AP_OxideGarageEvaluate(
	const AP_OxideGarageInputs *in)
{
	AP_OxideGarageState st;

	st.encounter = AP_OXIDE_ENCOUNTER_NONE;
	st.open = 0;
	st.needsCompanions = 0;
	st.needsRelics = 0;

	// Issue #320: `disabled` removes both races from the seed, so there is no
	// encounter to offer and no state of the world opens this garage. Checked
	// before anything else so no later term can reopen it.
	if (in->goalOxide == AP_OXIDE_GOAL_DISABLED)
		return st;

	// An uncleared first challenge is always what the garage offers next.
	st.encounter = in->firstCleared ? AP_OXIDE_ENCOUNTER_FINAL
	                               : AP_OXIDE_ENCOUNTER_FIRST;

	if (st.encounter == AP_OXIDE_ENCOUNTER_FIRST)
	{
		st.needsCompanions = (in->goalOxide == AP_OXIDE_GOAL_ANY);
		st.needsRelics = 0;
	}
	else
	{
		st.needsCompanions = (in->goalOxide == AP_OXIDE_GOAL_FINAL);
		st.needsRelics = 1;
	}

	// The configured door requirement is necessary for either encounter. It is
	// checked here rather than folded into the terms above so a seed emitting
	// something other than four Keys keeps its own requirement, and so the
	// advert can still name the encounter at a door that requirement shuts.
	if (!in->garageReqMet)
		return st;
	if (st.needsRelics && !in->finalRelicMet)
		return st;
	if (st.needsCompanions && !AP_OxideCompanionsMet(in))
		return st;

	st.open = 1;
	return st;
}

// Convenience wrappers for the two questions the engine asks separately.
static inline int AP_OxideGarageIsOpen(const AP_OxideGarageInputs *in)
{
	return AP_OxideGarageEvaluate(in).open;
}

static inline int AP_OxideGarageOffersFinal(const AP_OxideGarageInputs *in)
{
	return AP_OxideGarageEvaluate(in).encounter == AP_OXIDE_ENCOUNTER_FINAL;
}

#endif // AP_OXIDE_ENCOUNTER_H
