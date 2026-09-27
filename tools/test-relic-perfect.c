// Relic Race perfect decision (issue #49), the pure half of the producer that
// RR_EndEvent_UnlockAward (game/223.c) calls at every relic race end.
//
//   cc -Wall -Wextra -Werror -DCTR_AP -I ap -I . -o /tmp/test-relic-perfect
//     tools/test-relic-perfect.c
//
// Exit 0 = every assertion held.
#include <stdio.h>

#include "ap_relic_perfect.h"
#include "ap_race_attempt_logic.h"

static int checks;
static int failures;

static void expect(long got, long want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		printf("FAIL %s: got %ld want %ld\n", name, got, want);
	}
}

static long table[AP_RELIC_PERFECT_TRACK_COUNT];

static AP_RelicPerfectFacts race(int levelID, int broken, int total)
{
	AP_RelicPerfectFacts f;
	f.retail = table;
	f.levelID = levelID;
	f.broken = broken;
	f.total = total;
	f.cortexActive = 0;
	f.customServing = 0;
	f.customCode = -1;
	return f;
}

static void test_frozen_codes(void)
{
	// Engine LevelID -> apworld RELIC_TRACKS index (Sapphire order).
	static const struct { int lid; long code; const char *track; } rows[] = {
		{3, 35012400L, "Crash Cove"},     {6, 35012401L, "Roo's Tubes"},
		{9, 35012402L, "Mystery Caves"},  {8, 35012403L, "Sewer Speedway"},
		{14, 35012404L, "Coco Park"},     {4, 35012405L, "Tiger Temple"},
		{5, 35012406L, "Papu's Pyramid"}, {0, 35012407L, "Dingo Canyon"},
		{2, 35012408L, "Blizzard Bluff"}, {1, 35012409L, "Dragon Mines"},
		{12, 35012410L, "Polar Pass"},    {15, 35012411L, "Tiny Arena"},
		{7, 35012412L, "Hot Air Skyway"}, {10, 35012413L, "Cortex Castle"},
		{11, 35012414L, "N. Gin Labs"},   {13, 35012415L, "Oxide Station"},
		{16, 35012416L, "Slide Coliseum"},{17, 35012417L, "Turbo Track"},
	};
	unsigned i;
	int seen[AP_RELIC_PERFECT_TRACK_COUNT] = {0};
	for (i = 0; i < sizeof rows / sizeof rows[0]; i++)
	{
		expect(AP_RelicPerfectExpectedCode(rows[i].lid), rows[i].code, rows[i].track);
		seen[rows[i].code - AP_RELIC_PERFECT_CODE_BASE]++;
	}
	for (i = 0; i < AP_RELIC_PERFECT_TRACK_COUNT; i++)
		expect(seen[i], 1, "each frozen code owned by exactly one LevelID");
	expect(AP_RelicPerfectExpectedCode(-1), -1, "LevelID -1 has no code");
	expect(AP_RelicPerfectExpectedCode(18), -1, "LevelID 18 has no code");
	expect(AP_RelicPerfectExpectedCode(110), -1, "Cortex Vortex 110 has no code");
}

static void test_canonical_keys(void)
{
	expect(AP_RelicPerfectKeyLevel("0"), 0, "\"0\"");
	expect(AP_RelicPerfectKeyLevel("3"), 3, "\"3\"");
	expect(AP_RelicPerfectKeyLevel("17"), 17, "\"17\"");
	expect(AP_RelicPerfectKeyLevel("18"), -1, "\"18\" out of range");
	expect(AP_RelicPerfectKeyLevel("03"), -1, "leading zero is an alias");
	expect(AP_RelicPerfectKeyLevel("00"), -1, "\"00\"");
	expect(AP_RelicPerfectKeyLevel("+3"), -1, "sign");
	expect(AP_RelicPerfectKeyLevel("-1"), -1, "negative");
	expect(AP_RelicPerfectKeyLevel(" 3"), -1, "leading space");
	expect(AP_RelicPerfectKeyLevel("3 "), -1, "trailing space");
	expect(AP_RelicPerfectKeyLevel("3junk"), -1, "partial number");
	expect(AP_RelicPerfectKeyLevel("3.0"), -1, "decimal point");
	expect(AP_RelicPerfectKeyLevel(""), -1, "empty key");
	expect(AP_RelicPerfectKeyLevel("100"), -1, "three digits");
	expect(AP_RelicPerfectKeyLevel("99999999999"), -1, "oversized");
	expect(AP_RelicPerfectKeyLevel(0), -1, "null key");
}

static void test_resolve(void)
{
	AP_RelicPerfectFacts f;
	int i;
	for (i = 0; i < AP_RELIC_PERFECT_TRACK_COUNT; i++)
		table[i] = AP_RelicPerfectExpectedCode(i);

	f = race(3, 12, 12);
	expect(AP_RelicPerfectResolvePure(&f), 35012400L, "Crash Cove, every crate");
	f = race(3, 11, 12);
	expect(AP_RelicPerfectResolvePure(&f), -1, "one crate missed sends nothing");
	f = race(3, 0, 0);
	expect(AP_RelicPerfectResolvePure(&f), -1, "a level with no crates is no perfect");
	f = race(3, 13, 12);
	expect(AP_RelicPerfectResolvePure(&f), -1, "counter over total is not a perfect");
	f = race(16, 20, 20);
	expect(AP_RelicPerfectResolvePure(&f), 35012416L, "Slide Coliseum");
	f = race(17, 20, 20);
	expect(AP_RelicPerfectResolvePure(&f), 35012417L, "Turbo Track");
	f = race(18, 5, 5);
	expect(AP_RelicPerfectResolvePure(&f), -1, "LevelID 18 is not a relic track");
	f = race(-1, 5, 5);
	expect(AP_RelicPerfectResolvePure(&f), -1, "negative LevelID");

	// Borrowed LevelIDs never pay the retail track's code.
	f = race(13, 9, 9);
	f.cortexActive = 1;
	expect(AP_RelicPerfectResolvePure(&f), -1, "Cortex Vortex on 13: no Oxide Station code");
	f = race(6, 9, 9);
	f.customServing = 1;
	expect(AP_RelicPerfectResolvePure(&f), -1, "custom on host 6: no Roo's Tubes code");
	f.customCode = 35024003L;
	expect(AP_RelicPerfectResolvePure(&f), 35024003L,
	       "custom with its own slot code resolves to that code only");
	f.broken = 8;
	expect(AP_RelicPerfectResolvePure(&f), -1, "custom missed crate sends nothing");

	// A seed without this relic race (the Cortex Vortex dropped destination).
	table[3] = -1;
	f = race(3, 12, 12);
	expect(AP_RelicPerfectResolvePure(&f), -1, "absent row sends nothing");
	table[3] = AP_RelicPerfectExpectedCode(3);

	f = race(3, 12, 12);
	f.retail = 0;
	expect(AP_RelicPerfectResolvePure(&f), -1, "no table sends nothing");
	expect(AP_RelicPerfectResolvePure(0), -1, "no facts sends nothing");
}

static void test_forced_loss_guard(void)
{
	// The producer asks this before resolving: a forced DeathLink loss blocks
	// the perfect exactly like the relic unlock; a normal attempt does not.
	expect(AP_RaceAttempt_SuppressResultProducer(AP_RESULT_PRODUCER_RELIC_PERFECT, 1), 1,
	       "forced loss blocks the perfect");
	expect(AP_RaceAttempt_SuppressResultProducer(AP_RESULT_PRODUCER_RELIC_PERFECT, 0), 0,
	       "normal finish does not");
	expect(AP_RaceAttempt_SuppressResultProducer(AP_RESULT_PRODUCER_RELIC_PERFECT, 1),
	       AP_RaceAttempt_SuppressResultProducer(AP_RESULT_PRODUCER_RELIC_UNLOCK, 1),
	       "same predicate as the relic unlock");
}

int main(void)
{
	test_frozen_codes();
	test_canonical_keys();
	test_resolve();
	test_forced_loss_guard();
	printf("%s relic perfect (%d checks, %d failures)\n",
	       failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
