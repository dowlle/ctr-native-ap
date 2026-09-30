// cc -std=c99 -Wall -Wextra -Werror -o /tmp/test-deathlink-race-loss tools/test-deathlink-race-loss.c
//
// #286 DeathLink receive policy: the pure decision production uses to choose
// between the existing mask reset (modes 1/2), the new race-loss (mode 3), and
// the queued wait. Asserts the value-3 behavior and that values 0/1/2 and the
// unknown-future-value fallback are unchanged.
#include <stdio.h>
#include "../ap/ap_race_attempt_logic.h"

static int failures;
static void expect(int got, int want, const char *name)
{
	printf("%s %s (got %d, want %d)\n", got == want ? "PASS" : "FAIL",
	       name, got, want);
	if (got != want)
		failures++;
}

/* Minimal model of the rank rebuild in PlayLevel_UpdateLapStats
 * (game/PlayLevel.c): finished drivers keep their rank, unfinished drivers are
 * ranked by progress starting at (highest finished rank + 1), then
 * driversInRaceOrder[rank] is filled from the ranks. A NULL slot (-1 here) is
 * what AA_EndEvent_DrawMenu dereferences. */
typedef struct
{
	int finished;
	int rank;
	int progress; /* larger is farther ahead, only used while unfinished */
} ModelDriver;

static void model_rebuild(ModelDriver *d, int racers, int *order)
{
	int i, cur = 0, r;
	int placed[8] = {0};

	for (i = 0; i < racers; i++)
	{
		if (!d[i].finished)
			d[i].rank = -1;
		else if (d[i].rank + 1 > cur)
			cur = d[i].rank + 1;
	}
	for (; cur < 8; cur++)
	{
		int best = -1;
		for (i = 0; i < racers; i++)
			if (d[i].rank == -1 && !placed[i] &&
			    (best < 0 || d[i].progress > d[best].progress))
				best = i;
		if (best < 0)
			break;
		d[best].rank = cur;
		placed[best] = 1;
	}
	for (r = 0; r < 8; r++)
		order[r] = -1;
	for (i = 0; i < racers; i++)
		if (d[i].rank > -1 && d[i].rank < 8)
			order[d[i].rank] = i;
}

static int order_is_full(const int *order, int racers)
{
	int r;
	for (r = 0; r < racers; r++)
		if (order[r] < 0)
			return 0;
	return 1;
}

/* Build the state AP_DeathLinkApplyRaceLoss writes for `racers` drivers where
 * the local driver (slot 0) was at rank localRank, then rebuild. */
static void forced_state(ModelDriver *d, int racers, int localRank, int markAhead)
{
	int order[8], r, i;

	for (i = 0; i < racers; i++)
	{
		d[i].finished = 0;
		d[i].rank = -1;
		d[i].progress = racers - i; /* slot 0 farthest ahead, bots behind */
	}
	for (r = 0; r < racers; r++)
		order[r] = r; /* rank r held by slot r ... */
	/* ... except the local driver (slot 0) sits at localRank */
	order[0] = localRank;
	order[localRank] = 0;
	AP_RaceAttempt_ApplyLastPlaceSwap(racers, localRank, order);
	for (r = 0; r < racers; r++)
	{
		d[order[r]].rank = r;
		if (markAhead && AP_RaceAttempt_RankIsFinishedAfterLoss(racers, r))
			d[order[r]].finished = 1;
	}
	d[0].finished = 1; /* local is marked finished either way */
}

static void forced_state_tests(void)
{
	ModelDriver d[8];
	int order[8];
	int racers, localRank;

	/* Only marking the local driver leaves NULL slots: the crash. */
	forced_state(d, 8, 0, 0);
	model_rebuild(d, 8, order);
	expect(order_is_full(order, 8), 0, "local-only finish leaves NULL rank slots (the crash)");

	/* Marking every driver ahead as finished fills every slot. */
	for (racers = 2; racers <= 8; racers++)
		for (localRank = 0; localRank < racers; localRank++)
		{
			int i, allFinished = 1;
			forced_state(d, racers, localRank, 1);
			model_rebuild(d, racers, order);
			expect(order_is_full(order, racers), 1, "forced loss fills every rank slot");
			expect(d[0].rank, racers - 1, "local driver ends at the last rank");
			expect(order[racers - 1], 0, "last slot holds the local driver");
			for (i = 0; i < racers; i++)
				if (!d[i].finished)
					allFinished = 0;
			expect(allFinished, 1, "every driver is finished after the forced loss");
		}

	expect(AP_RaceAttempt_FinishedCountAfterLoss(8), 8, "finished count covers all racers");
	expect(AP_RaceAttempt_FinishedCountAfterLoss(2), 2, "boss race finished count is 2");
	expect(AP_RaceAttempt_RankIsFinishedAfterLoss(8, 6), 1, "rank ahead of last is finished");
	expect(AP_RaceAttempt_RankIsFinishedAfterLoss(8, 7), 0, "last rank is the local driver, marked separately");
	expect(AP_RaceAttempt_RankIsFinishedAfterLoss(8, -1), 0, "no rank is not finished");
}

static void loss_kind_tests(void)
{
	/* (boss, relic, crystal): same precedence as AP_ClassifyRace. */
	expect(AP_RaceAttempt_LossKind(0, 0, 0), AP_LOSS_RANKED, "plain race is ranked");
	expect(AP_RaceAttempt_LossKind(1, 0, 0), AP_LOSS_RANKED, "boss race is ranked");
	expect(AP_RaceAttempt_LossKind(0, 1, 0), AP_LOSS_RELIC, "relic race is abandoned");
	expect(AP_RaceAttempt_LossKind(0, 0, 1), AP_LOSS_CRYSTAL, "crystal challenge fails");
	expect(AP_RaceAttempt_LossKind(1, 1, 1), AP_LOSS_RANKED, "boss wins over stale bits");
	expect(AP_RaceAttempt_LossKind(0, 1, 1), AP_LOSS_RELIC, "relic wins over crystal");
}

/* ---- Send triggers (death_link_send) and race_loss sends ---- */
static void send_trigger_tests(void)
{
	int m;

	/* Legacy coupling from death_link, used when the seed has no death_link_send. */
	expect(AP_DeathLinkLegacySendMask(0), 0, "legacy: off sends nothing");
	expect(AP_DeathLinkLegacySendMask(1), AP_DL_TRIG_FALL, "legacy: mask_reset -> fall");
	expect(AP_DeathLinkLegacySendMask(2), AP_DL_TRIG_FALL | AP_DL_TRIG_HIT, "legacy: any_hit -> fall+hit");
	expect(AP_DeathLinkLegacySendMask(3), AP_DL_TRIG_FALL | AP_DL_TRIG_LOSS, "legacy: race_loss -> fall+loss");
	expect(AP_DeathLinkLegacySendMask(9), AP_DL_TRIG_FALL, "legacy: unknown future mode -> fall");

	/* Seed follow: key absent (-1) uses the legacy coupling of death_link. */
	for (m = 0; m <= 3; m++)
		expect(AP_DeathLinkSendMask(-1, m, -1, -1, -1, -1), AP_DeathLinkLegacySendMask(m),
		       "absent death_link_send follows the legacy coupling");
	/* A present key wins over death_link, independent of the receive effect. */
	expect(AP_DeathLinkSendMask(4, 1, -1, -1, -1, -1), AP_DL_TRIG_LOSS,
	       "death_link_send 4 with receive mask_reset: loss only");
	expect(AP_DeathLinkSendMask(0, 3, -1, -1, -1, -1), 0,
	       "death_link_send 0 with receive race_loss: sends nothing");
	expect(AP_DeathLinkSendMask(7, 0, -1, -1, -1, -1), 7,
	       "death_link_send 7 with receive off: every trigger sends");
	expect(AP_DeathLinkSendMask(15, 0, -1, -1, -1, -1), 7, "unknown future bits are ignored");

	/* A forced receive mode brings its own legacy coupling as the send default. */
	expect(AP_DeathLinkSendMask(7, 3, 2, -1, -1, -1), AP_DL_TRIG_FALL | AP_DL_TRIG_HIT,
	       "forced any_hit replaces the seed's send mask");
	expect(AP_DeathLinkSendMask(7, 3, 0, -1, -1, -1), 0, "forced off sends nothing by default");

	/* Per-trigger rows: 0 forces a trigger off, 1 forces it on, -1 follows. */
	expect(AP_DeathLinkSendMask(-1, 3, -1, 0, -1, -1), AP_DL_TRIG_LOSS,
	       "SEND FALL off: falls stop, race_loss keeps sending");
	expect(AP_DeathLinkSendMask(-1, 3, -1, -1, -1, 0), AP_DL_TRIG_FALL,
	       "SEND LOSS off: losses stop, falls keep sending");
	expect(AP_DeathLinkSendMask(-1, 1, -1, -1, 1, 1), 7,
	       "rows can switch triggers on over a mask_reset seed");
	expect(AP_DeathLinkSendMask(-1, 0, 0, -1, -1, 1), AP_DL_TRIG_LOSS,
	       "receive forced off but SEND LOSS on: sends losses only");

	/* No one-way DeathLink: anything that sends also receives. seedRecv, seedSend,
	 * cfgMode, ovFall, ovHit, ovLoss -> {recv, send}. */
	{
		APDeathLinkState st;

		st = AP_DeathLinkResolve(0, 4, -1, -1, -1, -1);
		expect(st.recv, 1, "seed death_link 0 with send bits: receive becomes mask_reset");
		expect(st.send, AP_DL_TRIG_LOSS, "seed death_link 0 with send bits: the bits still send");
		st = AP_DeathLinkResolve(0, 0, -1, -1, -1, -1);
		expect(st.recv, 0, "seed off, no send bits: fully off");
		expect(st.send, 0, "seed off, no send bits: nothing sends");
		st = AP_DeathLinkResolve(0, -1, -1, -1, -1, 1);
		expect(st.recv, 1, "a send row ON over an off seed turns receive on as mask_reset");
		expect(st.send, AP_DL_TRIG_LOSS, "and only that trigger sends");
		st = AP_DeathLinkResolve(3, 7, 0, 1, 1, 1);
		expect(st.recv, 0, "DeathLink row OFF: no receive whatever the send rows say");
		expect(st.send, 0, "DeathLink row OFF: no sends whatever the send rows say");
		st = AP_DeathLinkResolve(2, 0, -1, -1, -1, -1);
		expect(st.recv, 2, "receive-only (all sends off) is allowed");
		expect(st.send, 0, "receive-only sends nothing");
		st = AP_DeathLinkResolve(3, -1, -1, 0, 0, 0);
		expect(st.recv, 3, "all send rows OFF keep the seed's receive effect");
		expect(st.send, 0, "all send rows OFF: nothing sends");
		st = AP_DeathLinkResolve(0, 0, 2, -1, -1, -1);
		expect(st.recv, 2, "forced any_hit receives as any_hit");
		expect(st.send, AP_DL_TRIG_FALL | AP_DL_TRIG_HIT, "forced any_hit brings its legacy sends");
		st = AP_DeathLinkResolve(1, -1, -1, -1, -1, -1);
		expect(st.send, AP_DL_TRIG_FALL, "legacy seed mask_reset sends falls");
	}

	/* The tag is declared while anything receives or sends. */
	expect(AP_DeathLinkTagWanted(0, 0), 0, "tag off when nothing receives or sends");
	expect(AP_DeathLinkTagWanted(1, 0), 1, "tag on for receive only");
	expect(AP_DeathLinkTagWanted(0, AP_DL_TRIG_LOSS), 1, "tag on for send only");

	/* Race end: the game's own loss result. (adv, cupLeg, relic, crystal, rank, have, need) */
	expect(AP_DeathLinkRaceEndLost(1, 0, 0, 0, 0, 0, 0), 0, "1st place is not a loss");
	expect(AP_DeathLinkRaceEndLost(1, 0, 0, 0, 1, 0, 0), 1, "2nd place is a loss");
	expect(AP_DeathLinkRaceEndLost(1, 0, 0, 0, 7, 0, 0), 1, "8th place is a loss");
	expect(AP_DeathLinkRaceEndLost(1, 1, 0, 0, 5, 0, 0), 0, "a lost cup leg alone is not a loss");
	expect(AP_DeathLinkRaceEndLost(1, 0, 1, 0, 3, 0, 0), 0, "a relic race has no fail state");
	expect(AP_DeathLinkRaceEndLost(0, 0, 0, 0, 5, 0, 0), 0, "not adventure: never");
	expect(AP_DeathLinkRaceEndLost(1, 0, 0, 1, 0, 19, 20), 1, "crystals missing is a loss");
	expect(AP_DeathLinkRaceEndLost(1, 0, 0, 1, 5, 20, 20), 0, "all crystals is a win, rank ignored");

	/* The loss send decision. (sendMask, adv, latch, event, lost, inLiveRace) */
	{
		int loss = AP_DL_TRIG_LOSS;

		expect(AP_DeathLinkLossSendDecision(loss, 1, 0, AP_DL_EV_RACE_END, 1, 0), AP_DL_CAUSE_LOST_RACE,
		       "a lost race sends");
		expect(AP_DeathLinkLossSendDecision(loss, 1, 0, AP_DL_EV_RACE_END, 0, 0), AP_DL_CAUSE_NONE,
		       "a won race sends nothing");
		expect(AP_DeathLinkLossSendDecision(loss, 1, 0, AP_DL_EV_CUP_END, 1, 0), AP_DL_CAUSE_LOST_CUP,
		       "a lost cup sends");
		expect(AP_DeathLinkLossSendDecision(loss, 1, 0, AP_DL_EV_CUP_END, 0, 0), AP_DL_CAUSE_NONE,
		       "a won cup sends nothing");
		expect(AP_DeathLinkLossSendDecision(loss, 1, 0, AP_DL_EV_PAUSE_RESTART, 1, 1), AP_DL_CAUSE_RESTART,
		       "pause RESTART mid-race sends");
		expect(AP_DeathLinkLossSendDecision(loss, 1, 0, AP_DL_EV_PAUSE_QUIT, 1, 1), AP_DL_CAUSE_QUIT,
		       "pause EXIT TO MAP mid-race sends");
		expect(AP_DeathLinkLossSendDecision(loss, 1, 0, AP_DL_EV_PAUSE_RESTART, 1, 0), AP_DL_CAUSE_NONE,
		       "RESTART outside a live race sends nothing");
		expect(AP_DeathLinkLossSendDecision(loss, 0, 0, AP_DL_EV_RACE_END, 1, 0), AP_DL_CAUSE_NONE,
		       "outside adventure mode sends nothing");
		/* A received death's forced loss never sends, nor a quit/restart after it. */
		for (m = AP_DL_EV_RACE_END; m <= AP_DL_EV_PAUSE_QUIT; m++)
			expect(AP_DeathLinkLossSendDecision(loss, 1, 1, m, 1, 1), AP_DL_CAUSE_NONE,
			       "forced-loss latch: no send from any loss event");
		/* Other triggers and modes are unchanged: only the loss trigger sends losses. */
		expect(AP_DeathLinkLossSendDecision(AP_DL_TRIG_FALL | AP_DL_TRIG_HIT, 1, 0, AP_DL_EV_RACE_END, 1, 0),
		       AP_DL_CAUSE_NONE, "fall and hit triggers never send a lost race");
		expect(AP_DeathLinkLossSendDecision(0, 1, 0, AP_DL_EV_PAUSE_QUIT, 1, 1), AP_DL_CAUSE_NONE,
		       "no trigger: nothing sends");
	}
	expect(AP_DeathLinkTriggerSends(AP_DL_TRIG_FALL, AP_DL_TRIG_FALL, 0), 1, "fall trigger on sends");
	expect(AP_DeathLinkTriggerSends(AP_DL_TRIG_LOSS, AP_DL_TRIG_FALL, 0), 0, "fall trigger off: no send");
	expect(AP_DeathLinkTriggerSends(AP_DL_TRIG_HIT, AP_DL_TRIG_HIT, 1), 0,
	       "a received death's aftermath never sends a hit");
}

int main(void)
{
	int mode;

	forced_state_tests();
	send_trigger_tests();
	loss_kind_tests();

	/* DeathLink is never queued: a pending death outside a live adventure race
	 * window is DROPPED in every enabled mode, never held for the next race. */
	for (mode = 0; mode <= 4; mode++)
	{
		int drop = (mode == 0) ? AP_DL_RECV_WAIT : AP_DL_RECV_DROP;

		expect(AP_DeathLinkReceiveDecision(mode, 0, 1, AP_DL_WIN_LIVE), AP_DL_RECV_WAIT,
		       "no pending death -> wait");
		expect(AP_DeathLinkReceiveDecision(mode, 0, 0, 0), AP_DL_RECV_WAIT,
		       "no pending death outside a race -> nothing to drop");
		expect(AP_DeathLinkReceiveDecision(mode, 1, 0, AP_DL_WIN_LIVE), drop,
		       "non-adventure -> drop");
		expect(AP_DeathLinkReceiveDecision(mode, 1, 1, 0), drop,
		       "outside race window (hub, menu, loading, countdown, pause, results) -> drop");
	}

	/* Pausing the race must not dodge a death: a pending death is HELD while the
	 * same race is paused (any enabled mode) and applies when it runs again. */
	for (mode = 1; mode <= 4; mode++)
	{
		expect(AP_DeathLinkReceiveDecision(mode, 1, 1, AP_DL_WIN_PAUSED), AP_DL_RECV_HOLD,
		       "a death pending in a paused race is held for the pause");
		expect(AP_DeathLinkReceiveDecision(mode, 1, 0, AP_DL_WIN_PAUSED), AP_DL_RECV_DROP,
		       "a paused non-adventure race holds nothing");
	}
	expect(AP_DeathLinkReceiveDecision(0, 1, 1, AP_DL_WIN_PAUSED), AP_DL_RECV_WAIT,
	       "off holds nothing while paused");
	expect(AP_DeathLinkReceiveDecision(3, 1, 1, AP_DL_WIN_LIVE), AP_DL_RECV_RACE_LOSS,
	       "unpaused: the held death applies as race loss");
	expect(AP_DeathLinkReceiveDecision(1, 1, 1, AP_DL_WIN_LIVE), AP_DL_RECV_MASK_RESET,
	       "unpaused: the held death applies as mask reset");
	/* Leaving the race from the pause menu (quit, restart, exit), or the level
	 * unloading, makes the window outside: the held death is dropped, not kept. */
	for (mode = 1; mode <= 4; mode++)
		expect(AP_DeathLinkReceiveDecision(mode, 1, 1, AP_DL_WIN_OUTSIDE), AP_DL_RECV_DROP,
		       "a held death is dropped when the race is left before unpausing");

	/* Off ignores a pending death (no popup either: the feature is off). */
	expect(AP_DeathLinkReceiveDecision(0, 1, 1, AP_DL_WIN_LIVE), AP_DL_RECV_WAIT,
	       "mode 0 (off) ignores the death");

	/* Existing enabled values keep the mask reset. */
	expect(AP_DeathLinkReceiveDecision(1, 1, 1, AP_DL_WIN_LIVE), AP_DL_RECV_MASK_RESET,
	       "mode 1 (mask_reset) unchanged");
	expect(AP_DeathLinkReceiveDecision(2, 1, 1, AP_DL_WIN_LIVE), AP_DL_RECV_MASK_RESET,
	       "mode 2 (any_hit) unchanged");

	/* The new value selects the race loss only in a valid race window. */
	expect(AP_DeathLinkReceiveDecision(3, 1, 1, AP_DL_WIN_LIVE), AP_DL_RECV_RACE_LOSS,
	       "mode 3 (race_loss) in window -> race loss");
	expect(AP_DeathLinkReceiveDecision(3, 1, 1, 0), AP_DL_RECV_DROP,
	       "mode 3 outside window is dropped");

	/* An unknown future nonzero value degrades to the old-client mask reset. */
	expect(AP_DeathLinkReceiveDecision(4, 1, 1, AP_DL_WIN_LIVE), AP_DL_RECV_MASK_RESET,
	       "unknown future mode -> mask-reset fallback");

	/* Grace: an in-window death that cannot land yet is retried for exactly
	 * AP_DL_GRACE_FRAMES frames and then dropped. */
	{
		int left = AP_DL_GRACE_FRAMES, frames = 0;

		while (!AP_DeathLinkGraceStep(&left) && frames < 1000)
			frames++;
		expect(frames + 1, AP_DL_GRACE_FRAMES, "grace lasts AP_DL_GRACE_FRAMES frames");
		expect(AP_DL_GRACE_FRAMES >= 3 && AP_DL_GRACE_FRAMES <= 20, 1,
		       "grace is a short handful of frames");
		left = 0;
		expect(AP_DeathLinkGraceStep(&left), 1, "no grace left -> expired");
	}

	printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
