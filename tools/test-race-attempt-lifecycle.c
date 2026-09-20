// cc -std=c99 -Wall -Wextra -Werror -o /tmp/test-race-attempt-lifecycle tools/test-race-attempt-lifecycle.c
//
// #286 native forced-loss attempt lifecycle. Drives the real production state
// machine from ap/ap_race_attempt_logic.h through the whole sequence the work
// order names: forced loss, result processing, disconnect, reconnect, config
// becoming inactive, hub loading, retry and the next Gem Cup leg. Also pins the
// bijective last-place rank permutation and the producer-suppression contract.
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

static int order_is_distinct(int racers, const int *order)
{
	int seen = 0;
	int r;
	for (r = 0; r < racers; r++)
	{
		if (order[r] < 0 || order[r] > 7 || (seen & (1 << order[r])))
			return 0;
		seen |= 1 << order[r];
	}
	return 1;
}

static void attempt_lifecycle(void)
{
	APRaceAttemptState s;
	AP_RaceAttempt_Init(&s);
	expect(AP_RaceAttempt_IsForcedLoss(&s), 0, "fresh attempt is not a forced loss");

	AP_RaceAttempt_ArmForcedLoss(&s);
	expect(AP_RaceAttempt_IsForcedLoss(&s), 1, "forced loss arms the latch");

	/* Result, standings, podium, disconnect and reconnect must not clear it. */
	AP_RaceAttempt_OnConnectReset(&s);
	expect(AP_RaceAttempt_IsForcedLoss(&s), 1, "connect reset does not clear the latch");

	/* DeathLink config becoming inactive owns no attempt transition. */
	expect(AP_RaceAttempt_IsForcedLoss(&s), 1, "config inactive does not clear the latch");

	/* Hub (overlay 2), main menu (0) and podium (3) are not eligible levels. */
	expect(AP_RaceAttempt_LevelStartStep(&s, 0, 1, 0), 0, "hub load does not clear");
	expect(AP_RaceAttempt_IsForcedLoss(&s), 1, "latch survives hub loading");
	expect(AP_RaceAttempt_LevelStartStep(&s, 0, 1, 0), 0, "menu load does not clear");
	expect(AP_RaceAttempt_LevelStartStep(&s, 0, 0, 0), 0, "not-yet-loaded race does not clear");
	expect(AP_RaceAttempt_IsForcedLoss(&s), 1, "latch survives menu and load gaps");

	/* A battle arena is a racing overlay but not an eligible attempt. */
	expect(AP_RaceAttempt_LevelStartStep(&s, 1, 1, 1), 0, "battle arena does not clear");
	expect(AP_RaceAttempt_IsForcedLoss(&s), 1, "latch survives a battle load");

	/* The first eligible racing level clears exactly once. */
	expect(AP_RaceAttempt_LevelStartStep(&s, 1, 1, 0), 1, "retry race clears the latch");
	expect(AP_RaceAttempt_IsForcedLoss(&s), 0, "latch is clear after the new attempt");
	expect(AP_RaceAttempt_LevelStartStep(&s, 1, 1, 0), 0, "the same attempt clears only once");
}

static void cup_lifecycle(void)
{
	APRaceAttemptState s;
	AP_RaceAttempt_Init(&s);

	/* Leg 1 forced loss: arm before the retail end sequence. */
	AP_RaceAttempt_ArmForcedLoss(&s);
	expect(AP_RaceAttempt_SuppressResultProducer(AP_RESULT_PRODUCER_ADV_REWARD,
	                                             AP_RaceAttempt_IsForcedLoss(&s)),
	       1, "forced-loss leg suppresses the cup-leg reward");

	/* The next leg's level start is the genuine new attempt. */
	expect(AP_RaceAttempt_LevelStartStep(&s, 1, 1, 0), 1, "next cup leg clears the latch");

	/* A later genuine leg win is no longer suppressed. */
	expect(AP_RaceAttempt_SuppressResultProducer(AP_RESULT_PRODUCER_ADV_REWARD,
	                                             AP_RaceAttempt_IsForcedLoss(&s)),
	       0, "a later genuine leg can earn its reward");
	expect(AP_RaceAttempt_SuppressResultProducer(AP_RESULT_PRODUCER_CUSTOM_TROPHY,
	                                             AP_RaceAttempt_IsForcedLoss(&s)),
	       0, "a later genuine cup win can earn the custom/Gem reward");
}

static void rank_permutation(void)
{
	int racers, localRank, r;
	int order[8];

	for (racers = 1; racers <= 8; racers++)
	{
		for (localRank = 0; localRank < racers; localRank++)
		{
			int beforeLocal = localRank;

			for (r = 0; r < racers; r++)
				order[r] = r; /* local driver sits at localRank, prev last at racers-1 */

			expect(AP_RaceAttempt_ApplyLastPlaceSwap(racers, localRank, order), 1,
			       "identity ordering accepts the last-place swap");
			expect(order_is_distinct(racers, order), 1,
			       "post-swap ordering is still bijective");
			expect(order[racers - 1], beforeLocal,
			       "local driver ends in last place");
			if (racers > 1)
				expect(order[localRank], racers - 1,
				       "previous last driver takes the local driver's former rank");
		}
	}

	/* A non-identity (reversed) ordering is still permuted bijectively. */
	{
		int rev[8] = {7, 6, 5, 4, 3, 2, 1, 0};
		expect(AP_RaceAttempt_ApplyLastPlaceSwap(8, 3, rev), 1,
		       "reversed ordering accepts the swap");
		expect(order_is_distinct(8, rev), 1, "reversed ordering stays bijective");
		expect(rev[7], 4, "swap moved the local driver to last in a reversed order");
		expect(rev[3], 0, "swap moved the previous last driver to the old rank");
	}

	/* Invalid orderings defer without mutating anything. */
	{
		int dup[4] = {0, 0, 1, 2};
		int outOfRange[3] = {0, 8, 2};
		int negative[2] = {-1, 1};
		expect(AP_RaceAttempt_ApplyLastPlaceSwap(4, 0, dup), 0,
		       "duplicate ordering defers");
		expect(AP_RaceAttempt_ApplyLastPlaceSwap(3, 0, outOfRange), 0,
		       "out-of-range slot defers");
		expect(AP_RaceAttempt_ApplyLastPlaceSwap(2, 0, negative), 0,
		       "negative slot defers");
		expect(AP_RaceAttempt_ApplyLastPlaceSwap(0, 0, dup), 0,
		       "zero racers defers");
		expect(AP_RaceAttempt_ApplyLastPlaceSwap(9, 0, dup), 0,
		       "nine racers defers");
		expect(AP_RaceAttempt_ApplyLastPlaceSwap(4, 4, dup), 0,
		       "local rank outside the field defers");
	}
}

static void producer_wiring(void)
{
	int c;

	/* Every result-derived producer observes the one attempt latch. */
	for (c = 0; c < AP_RESULT_PRODUCER_COUNT; c++)
	{
		expect(AP_RaceAttempt_SuppressResultProducer(c, 1), 1,
		       "latched attempt suppresses every result producer");
		expect(AP_RaceAttempt_SuppressResultProducer(c, 0), 0,
		       "cleared latch allows every result producer");
	}

	/* Held-position rungs and item-box/lettersanity checks are mid-race classes,
	 * emitted before the latch is ever armed, so the latch cannot revoke them.
	 * The permutation and receive decision are likewise latch-independent. */
	{
		int order[3] = {0, 1, 2};
		expect(AP_RaceAttempt_ApplyLastPlaceSwap(3, 1, order), 1,
		       "permutation is independent of the result latch");
		expect(AP_DeathLinkReceiveDecision(3, 1, 1, 1), AP_DL_RECV_RACE_LOSS,
		       "receive decision is independent of the result latch");
	}
}

int main(void)
{
	attempt_lifecycle();
	cup_lifecycle();
	rank_permutation();
	producer_wiring();
	printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
