#ifndef AP_RACE_ATTEMPT_LOGIC_H
#define AP_RACE_ATTEMPT_LOGIC_H

// Freestanding #286 forced-loss attempt logic: the DeathLink receive decision,
// the attempt-owned forced-loss latch, the eligible-level-start classifier and
// the bijective last-place rank permutation. Extracted from ap_deathlink.c so a
// host harness can drive the whole lifecycle without linking the engine, while
// production calls these exact functions. No engine types or headers here.
//
// The invariant the latch encodes: a received race-loss death belongs to the
// RACE ATTEMPT, not to the network connection. It is armed before the retail
// end-of-event sequence and survives the result screen, standings, podium,
// disconnect, reconnect, slot_data replacement, DeathLink config inactivity and
// hub/menu/cutscene loads. It clears exactly once when the next fully loaded,
// eligible racing level starts. Network state never owns or resets it.

// --- Receive decision -------------------------------------------------------
enum
{
	AP_DL_RECV_WAIT       = 0,
	AP_DL_RECV_MASK_RESET = 1,
	AP_DL_RECV_RACE_LOSS  = 2
};

// mode: effective death_link (0 off / 1 mask_reset / 2 any_hit / 3 race_loss).
// Any other nonzero value is an unknown future mode: it deliberately keeps the
// old-client mask-reset fallback. A pending death only acts inside a live
// adventure race window, so hub / menu / loading / countdown / result receives
// stay queued for the next valid race.
static inline int AP_DeathLinkReceiveDecision(int mode, int pending,
                                              int adventure, int raceWindow)
{
	if (!pending || mode == 0 || !adventure || !raceWindow)
		return AP_DL_RECV_WAIT;
	if (mode == 3)
		return AP_DL_RECV_RACE_LOSS;
	return AP_DL_RECV_MASK_RESET;
}

// --- Attempt-owned forced-loss latch ----------------------------------------
typedef struct APRaceAttemptState
{
	int forcedLoss;
} APRaceAttemptState;

static inline void AP_RaceAttempt_Init(APRaceAttemptState *s)
{
	s->forcedLoss = 0;
}

// A connection reset clears transport and queue state, never the attempt latch:
// a disconnect or reconnect during the result sequence must not re-enable the
// failed attempt's finish-derived checks. Kept as an explicit no-op so the
// contract is asserted rather than implied by an absent call.
static inline void AP_RaceAttempt_OnConnectReset(APRaceAttemptState *s)
{
	(void)s;
}

static inline void AP_RaceAttempt_ArmForcedLoss(APRaceAttemptState *s)
{
	s->forcedLoss = 1;
}

static inline int AP_RaceAttempt_IsForcedLoss(const APRaceAttemptState *s)
{
	return s->forcedLoss;
}

// An eligible racing level: fully loaded, on the race/battle thread overlay,
// and not a battle arena. The hub (overlay 2), main menu (0), podium (3) and
// cutscene loads all reach MainGameStart but must NOT clear the latch.
static inline int AP_RaceAttempt_LevelStartEligible(int onTrack, int loadingIdle,
                                                    int battle)
{
	return onTrack && loadingIdle && !battle;
}

// Clears the latch exactly once, on the first eligible racing level after the
// forced loss. Returns 1 when it cleared. Hub / menu / battle loads return 0
// and leave the latch set.
static inline int AP_RaceAttempt_LevelStartStep(APRaceAttemptState *s,
                                                int onTrack, int loadingIdle,
                                                int battle)
{
	if (!s->forcedLoss)
		return 0;
	if (!AP_RaceAttempt_LevelStartEligible(onTrack, loadingIdle, battle))
		return 0;
	s->forcedLoss = 0;
	return 1;
}

// --- Result-derived producer classes ----------------------------------------
// Every guarded producer names its class here, and production reaches this
// decision through AP_RaceAttempt_ProducerBlocked (ap_deathlink.h) with the live
// latch, so the harness exercises the same call the guards use. The attempt latch
// suppresses every finish-class producer; mid-race classes (held rungs, item
// boxes, letters, itemsanity) never name a class because they are emitted before
// the latch is armed.
//
// AP_RESULT_PRODUCER_CUP_AGGREGATE is the one deliberate exception (product
// ruling, 2026-09-20 23:26 CEST): the overall Gem Cup reward is an
// aggregate of accumulated points awarded from the final standings, not a
// finish of the forced leg. A cup won on points grants its reward in full while
// the latch is still set; every per-race and finish producer of that leg stays
// blocked. The class exists so the cup path is guarded through the same tested
// helper as everything else, without clearing the latch early.
enum
{
	AP_RESULT_PRODUCER_PODIUM = 0,
	AP_RESULT_PRODUCER_ADV_REWARD,
	AP_RESULT_PRODUCER_GOAL,
	AP_RESULT_PRODUCER_TRIAL_TRACK,
	AP_RESULT_PRODUCER_CORTEX_RACE,
	AP_RESULT_PRODUCER_CORTEX_RELIC,
	AP_RESULT_PRODUCER_CUSTOM_TROPHY,
	AP_RESULT_PRODUCER_CUSTOM_CTR,
	AP_RESULT_PRODUCER_RELIC_UNLOCK,
	AP_RESULT_PRODUCER_RELIC_PERFECT, // #49 follow-up: same predicate
	AP_RESULT_PRODUCER_CUP_AGGREGATE, // overall cup reward: allowed while latched
	AP_RESULT_PRODUCER_COUNT
};

static inline int AP_RaceAttempt_SuppressResultProducer(int producerClass,
                                                        int forcedLoss)
{
	if (!forcedLoss)
		return 0;
	// The cup aggregate is the only producer the latch deliberately lets
	// through: the cup was won on accumulated points, so its reward is granted
	// as in retail even though the final leg was a forced loss.
	if (producerClass == AP_RESULT_PRODUCER_CUP_AGGREGATE)
		return 0;
	return 1;
}

// --- Last-place rank permutation --------------------------------------------
// order[0..racers-1] maps rank -> driver slot id. Valid iff every entry is a
// distinct slot in 0..7. That is the whole bijection contract: N distinct
// participating drivers occupying ranks 0..N-1.
static inline int AP_RaceAttempt_OrderIsDistinct(int racers, const int *order)
{
	int seen = 0;
	int r;

	if (racers < 1 || racers > 8)
		return 0;
	for (r = 0; r < racers; r++)
	{
		int slot = order[r];
		if (slot < 0 || slot > 7)
			return 0;
		if (seen & (1 << slot))
			return 0;
		seen |= 1 << slot;
	}
	return 1;
}

// Swap the local driver to last place and the previous last-place driver into
// the local driver's former rank. order must already be a distinct ordering.
// Returns 1 when the swap was applied, 0 when the input is invalid: the caller
// then defers without consuming the queued death.
static inline int AP_RaceAttempt_ApplyLastPlaceSwap(int racers, int localRank,
                                                    int *order)
{
	int last, tmp;

	if (!AP_RaceAttempt_OrderIsDistinct(racers, order))
		return 0;
	if (localRank < 0 || localRank >= racers)
		return 0;

	last = racers - 1;
	tmp = order[localRank];
	order[localRank] = order[last];
	order[last] = tmp;
	return 1;
}

#endif // AP_RACE_ATTEMPT_LOGIC_H
