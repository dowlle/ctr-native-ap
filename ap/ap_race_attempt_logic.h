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
	AP_DL_RECV_WAIT       = 0, // nothing pending, or DeathLink off: no action
	AP_DL_RECV_MASK_RESET = 1,
	AP_DL_RECV_RACE_LOSS  = 2,
	AP_DL_RECV_DROP       = 3, // pending death outside a live race: discard it
	AP_DL_RECV_HOLD       = 4  // race paused: keep it until the race unpauses
};

// Where the local player is relative to a live adventure race.
enum
{
	AP_DL_WIN_OUTSIDE = 0, // hub, menu, loading, countdown, results, cutscene, other modes
	AP_DL_WIN_PAUSED  = 1, // the same live race, paused
	AP_DL_WIN_LIVE    = 2  // adventure mode, on a track, lights out, running
};

// A received death that is inside a live race but cannot land yet (the mask
// reset's stock gate is not open, or the rank order is not valid) is retried for
// this many live frames and then dropped. It is never held for a later race.
#define AP_DL_GRACE_FRAMES 10

// mode: effective death_link (0 off / 1 mask_reset / 2 any_hit / 3 race_loss).
// Any other nonzero value is an unknown future mode: it deliberately keeps the
// old-client mask-reset fallback.
//
// DeathLink is never queued. A pending death acts only inside a live adventure race
// (adventure mode, on a track, lights out, not paused / menu / cutscene / end of
// race / loading). Outside it the death is DROPPED, in every mode, and is not saved
// for the next race. The single hold: pausing the race must not dodge a death, so a
// death that is pending while the same race is paused is HELD for that pause only.
// It applies when the race unpauses; if the race ends, is quit or restarted from the
// pause menu, or the level unloads first, the window becomes OUTSIDE and it drops.
static inline int AP_DeathLinkReceiveDecision(int mode, int pending,
                                              int adventure, int window)
{
	if (!pending || mode == 0)
		return AP_DL_RECV_WAIT;
	if (!adventure || window == AP_DL_WIN_OUTSIDE)
		return AP_DL_RECV_DROP;
	if (window == AP_DL_WIN_PAUSED)
		return AP_DL_RECV_HOLD;
	if (mode == 3)
		return AP_DL_RECV_RACE_LOSS;
	return AP_DL_RECV_MASK_RESET;
}

// One live frame of the grace countdown for a death that is inside the window but
// has not landed. *left is the frames still allowed; returns 1 when it has run out
// and the death must be dropped. Not stepped while the race is paused.
static inline int AP_DeathLinkGraceStep(int *left)
{
	if (*left > 0)
		(*left)--;
	return *left <= 0;
}

// --- Send triggers ------------------------------------------------------------
// What SENDS a death is separate from the receive effect (death_link). Bitmask:
enum
{
	AP_DL_TRIG_FALL = 1, // mask_grab: fell off / eaten by a plant
	AP_DL_TRIG_HIT  = 2, // weapon_hit: a landed hit
	AP_DL_TRIG_LOSS = 4  // race_loss: lost race, whole cup lost, pause RESTART / EXIT
};

// The legacy coupling, used when the seed has no death_link_send key and for a
// mode the player forces in the OPTIONS row: mask_reset sends falls, any_hit adds
// hits, race_loss sends falls and losses. Any other nonzero mode is an unknown
// future value that keeps the old-client fallback (falls only).
static inline int AP_DeathLinkLegacySendMask(int mode)
{
	if (mode == 0)
		return 0;
	if (mode == 2)
		return AP_DL_TRIG_FALL | AP_DL_TRIG_HIT;
	if (mode == 3)
		return AP_DL_TRIG_FALL | AP_DL_TRIG_LOSS;
	return AP_DL_TRIG_FALL;
}

// The effective send mask. seedSend is slot_data death_link_send (-1 = absent),
// seedMode is slot_data death_link, cfgMode is the OPTIONS DeathLink row (-1 =
// follow the seed, 0 = off, else a forced mode) and the three overrides are the
// per-trigger rows (-1 = follow, 0 = force off, 1 = force on). A forced receive
// mode brings its own legacy coupling as the default; a per-trigger row then
// overrides that one trigger. Independent of the receive effect.
static inline int AP_DeathLinkSendMask(int seedSend, int seedMode, int cfgMode,
                                       int ovFall, int ovHit, int ovLoss)
{
	int mask;

	if (cfgMode == -1)
		mask = (seedSend >= 0) ? (seedSend & 7) : AP_DeathLinkLegacySendMask(seedMode);
	else
		mask = AP_DeathLinkLegacySendMask(cfgMode);
	if (ovFall == 0) mask &= ~AP_DL_TRIG_FALL; else if (ovFall == 1) mask |= AP_DL_TRIG_FALL;
	if (ovHit == 0)  mask &= ~AP_DL_TRIG_HIT;  else if (ovHit == 1)  mask |= AP_DL_TRIG_HIT;
	if (ovLoss == 0) mask &= ~AP_DL_TRIG_LOSS; else if (ovLoss == 1) mask |= AP_DL_TRIG_LOSS;
	return mask;
}

// The resolved DeathLink state: what the client receives and what it sends.
typedef struct APDeathLinkState
{
	int recv; // effective receive mode: 0 off / 1 mask_reset / 2 any_hit / 3 race_loss
	int send; // effective send mask (AP_DL_TRIG_*)
} APDeathLinkState;

// Resolves the seed and the OPTIONS rows into one state. There is no one-way
// DeathLink: anything that sends also receives.
//   * The DeathLink row forced OFF (cfgMode 0) is fully off: no receive, no sends,
//     no tag, whatever the per-trigger rows say.
//   * Otherwise a send trigger on while the receive would be off (the seed says
//     death_link 0 but sets death_link_send bits, or a per-trigger row is forced ON)
//     makes the effective receive mask_reset.
//   * Receive on with every send trigger off stays allowed.
static inline APDeathLinkState AP_DeathLinkResolve(int seedRecv, int seedSend,
                                                   int cfgMode, int ovFall,
                                                   int ovHit, int ovLoss)
{
	APDeathLinkState r;
	int mask;

	r.recv = 0;
	r.send = 0;
	if (cfgMode == 0)
		return r;
	mask = AP_DeathLinkSendMask(seedSend, seedRecv, cfgMode, ovFall, ovHit, ovLoss);
	r.recv = (cfgMode > 0) ? cfgMode : seedRecv;
	if (r.recv == 0 && mask != 0)
		r.recv = 1;
	r.send = mask;
	return r;
}

// The connection tag is declared while anything receives or sends.
static inline int AP_DeathLinkTagWanted(int recvMode, int sendMask)
{
	return recvMode != 0 || sendMask != 0;
}

// --- Loss sends (the race_loss trigger) ----------------------------------------
// A loss the game itself computes SENDS a DeathLink when the race_loss trigger is
// on. The events come from results the game already has, not new rules:
//   AP_DL_EV_RACE_END    the local race ended (MainGameEnd_Initialize): a trophy,
//                        boss or CTR Challenge race finished outside 1st, or a
//                        Crystal Challenge ended with crystals missing. A Gem Cup
//                        leg and a Relic Race are never a loss here (a cup leg
//                        counts only through AP_DL_EV_CUP_END; a relic race has no
//                        fail state in retail).
//   AP_DL_EV_CUP_END     the Gem Cup standings closed with the cup lost.
//   AP_DL_EV_PAUSE_*     RESTART or EXIT TO MAP from the pause menu mid-race.
// A loss caused by a received DeathLink (forced-loss latch set) never sends.
enum
{
	AP_DL_EV_RACE_END      = 0,
	AP_DL_EV_CUP_END       = 1,
	AP_DL_EV_PAUSE_RESTART = 2,
	AP_DL_EV_PAUSE_QUIT    = 3
};

enum
{
	AP_DL_CAUSE_NONE = 0,
	AP_DL_CAUSE_LOST_RACE,
	AP_DL_CAUSE_LOST_CRYSTAL,
	AP_DL_CAUSE_LOST_CUP,
	AP_DL_CAUSE_RESTART,
	AP_DL_CAUSE_QUIT
};

// Did the local player lose this adventure race, by the game's own result?
// rank is driverRank (0 = 1st). Cup legs and relic races are never a loss here.
// A Crystal Challenge is lost when fewer crystals than the level holds were
// collected (CC_EndEvent_DrawMenu didLose); its driverRank means nothing.
static inline int AP_DeathLinkRaceEndLost(int adventure, int cupLeg, int relic,
                                          int crystal, int rank, int crystals,
                                          int crystalsNeeded)
{
	if (!adventure || cupLeg || relic)
		return 0;
	if (crystal)
		return crystals < crystalsNeeded;
	return rank != 0;
}

// The loss send decision: returns the cause to send, or AP_DL_CAUSE_NONE. Only the
// race_loss trigger sends losses. lost is the event's own loss result
// (AP_DeathLinkRaceEndLost for RACE_END, cup lost for CUP_END; ignored for the
// pause events). forcedLossLatch is the #286 latch: the race was ended by a
// received death, so nothing about it sends, including a quit or restart right
// after it. inLiveRace is only read for the pause events.
static inline int AP_DeathLinkLossSendDecision(int sendMask, int adventure,
                                               int forcedLossLatch, int event,
                                               int lost, int inLiveRace)
{
	if ((sendMask & AP_DL_TRIG_LOSS) == 0 || !adventure || forcedLossLatch)
		return AP_DL_CAUSE_NONE;
	switch (event)
	{
	case AP_DL_EV_RACE_END:
		return lost ? AP_DL_CAUSE_LOST_RACE : AP_DL_CAUSE_NONE;
	case AP_DL_EV_CUP_END:
		return lost ? AP_DL_CAUSE_LOST_CUP : AP_DL_CAUSE_NONE;
	case AP_DL_EV_PAUSE_RESTART:
		return inLiveRace ? AP_DL_CAUSE_RESTART : AP_DL_CAUSE_NONE;
	case AP_DL_EV_PAUSE_QUIT:
		return inLiveRace ? AP_DL_CAUSE_QUIT : AP_DL_CAUSE_NONE;
	}
	return AP_DL_CAUSE_NONE;
}

// Fall and hit sends: the trigger bit is on and the attempt is not a forced loss
// (a received death's own aftermath never sends).
static inline int AP_DeathLinkTriggerSends(int sendMask, int trigger, int forcedLossLatch)
{
	return (sendMask & trigger) != 0 && !forcedLossLatch;
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
	AP_RESULT_PRODUCER_RELIC_PERFECT, // #49 AP_NotifyRelicPerfect: same predicate
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

// --- Forced-loss kind and the retail "local finished last" state ------------
// A received race_loss ends the attempt in one of three ways, chosen from the
// same precedence AP_ClassifyRace uses (boss, then relic, then crystal):
//   RANKED  : trophy / boss / token / cup-leg race with other racers. The local
//             driver finishes last, exactly like a retail last-place finish.
//   CRYSTAL : Crystal Challenge. It ends the way the retail clock does when the
//             time runs out (UI_DrawLimitClock): finished flag plus the common
//             end-of-event initializer, with too few crystals so it reads TRY AGAIN.
//   RELIC   : solo Relic Race. There is no failed state in the result screen,
//             so the attempt leaves through the pause menu's EXIT TO MAP route.
enum
{
	AP_LOSS_RANKED = 0,
	AP_LOSS_CRYSTAL,
	AP_LOSS_RELIC
};

static inline int AP_RaceAttempt_LossKind(int boss, int relic, int crystal)
{
	if (boss)
		return AP_LOSS_RANKED;
	if (relic)
		return AP_LOSS_RELIC;
	if (crystal)
		return AP_LOSS_CRYSTAL;
	return AP_LOSS_RANKED;
}

// PlayLevel_UpdateLapStats rebuilds the rank order every frame on the rule that
// finished drivers hold ranks 0..numPlayersFinishedRace-1 (retail finishes
// drivers in order). The forced loss therefore has to reach the state a retail
// last-place finish leaves behind: every driver ranked ahead of the local driver
// is finished, and numPlayersFinishedRace counts all racers. Marking only the
// local driver leaves the bots unranked and NULL slots in driversInRaceOrder.
static inline int AP_RaceAttempt_RankIsFinishedAfterLoss(int racers, int rank)
{
	return rank >= 0 && rank < racers - 1;
}

static inline int AP_RaceAttempt_FinishedCountAfterLoss(int racers)
{
	return racers;
}

#endif // AP_RACE_ATTEMPT_LOGIC_H
