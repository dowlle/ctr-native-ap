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

int main(void)
{
	int mode;

	forced_state_tests();
	loss_kind_tests();

	/* A pending death outside a live adventure race window never acts. */
	for (mode = 0; mode <= 4; mode++)
	{
		expect(AP_DeathLinkReceiveDecision(mode, 0, 1, 1), AP_DL_RECV_WAIT,
		       "no pending death -> wait");
		expect(AP_DeathLinkReceiveDecision(mode, 1, 0, 1), AP_DL_RECV_WAIT,
		       "non-adventure -> wait");
		expect(AP_DeathLinkReceiveDecision(mode, 1, 1, 0), AP_DL_RECV_WAIT,
		       "outside race window -> wait");
	}

	/* Off ignores a pending death. */
	expect(AP_DeathLinkReceiveDecision(0, 1, 1, 1), AP_DL_RECV_WAIT,
	       "mode 0 (off) ignores the death");

	/* Existing enabled values keep the mask reset. */
	expect(AP_DeathLinkReceiveDecision(1, 1, 1, 1), AP_DL_RECV_MASK_RESET,
	       "mode 1 (mask_reset) unchanged");
	expect(AP_DeathLinkReceiveDecision(2, 1, 1, 1), AP_DL_RECV_MASK_RESET,
	       "mode 2 (any_hit) unchanged");

	/* The new value selects the race loss only in a valid race window. */
	expect(AP_DeathLinkReceiveDecision(3, 1, 1, 1), AP_DL_RECV_RACE_LOSS,
	       "mode 3 (race_loss) in window -> race loss");
	expect(AP_DeathLinkReceiveDecision(3, 1, 1, 0), AP_DL_RECV_WAIT,
	       "mode 3 outside window stays queued");

	/* An unknown future nonzero value degrades to the old-client mask reset. */
	expect(AP_DeathLinkReceiveDecision(4, 1, 1, 1), AP_DL_RECV_MASK_RESET,
	       "unknown future mode -> mask-reset fallback");

	printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
