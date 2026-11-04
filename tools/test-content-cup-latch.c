// Content-plan cup race context across legs (#133 PR 5): a cup leg context is
// valid only as a Trophy-style leg 0..3, the next leg stages only from the
// running cup's previous leg, a restart keeps the leg, a leg advance moves
// to the staged leg, an exit ends it; and the sweep attempt record survives
// four first places and drops on a restart or a lost leg.
// cc -Wall -Wextra -DCTR_AP -I ap -I . -I include -o /tmp/test-content-cup-latch tools/test-content-cup-latch.c
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "include/platform/native_custom_race_context.h"
#include "ap/ap_cup_sweeps.h"

static int checks, failures;
static void expect(int good, const char *why)
{
	++checks;
	if (!good) { ++failures; printf("FAIL: %s\n", why); }
}

static struct CustomTrackRaceContext leg(int n, int host)
{
	struct CustomTrackRaceContext c;
	memset(&c, 0, sizeof c);
	c.hostLevelID = host; c.physicalPad = 3; c.returnHub = 26; c.laps = 1; c.retail = 1;
	c.seedEpoch = 7; c.trophyLocation = 35027004; c.cup = 1; c.leg = n; c.mode = 0;
	snprintf(c.entryID, sizeof c.entryID, "entry/cup:4");
	snprintf(c.trackID, sizeof c.trackID, "retail:%d", host);
	return c;
}

int main(void)
{
	struct CustomTrackRaceLatch l;
	struct CustomTrackRaceContext c = leg(0, 5), d;
	AP_CupSweepAttempt a;
	int n;

	expect(CustomRaceContext_Valid(&c), "a retail cup leg is valid");
	d = c; d.leg = 4; expect(!CustomRaceContext_Valid(&d), "leg 4 is invalid");
	d = c; d.mode = 1; expect(!CustomRaceContext_Valid(&d), "a cup leg is a Trophy-style race");
	d = c; d.cup = 0; d.leg = 2; expect(!CustomRaceContext_Valid(&d), "a standalone race has no leg");
	d = c; d.crystal = 1; d.hostLevelID = 21; d.timeLimitTicks = 100; expect(!CustomRaceContext_Valid(&d), "no crystal cup");
	d = c; d.retail = 0; d.slot = 2; d.hostLevelID = 6;
	memset(d.levSha256, 'a', 64); d.levSha256[64] = 0; memset(d.vrmSha256, 'b', 64); d.vrmSha256[64] = 0;
	expect(CustomRaceContext_Valid(&d), "a custom cup leg with pins is valid");

	memset(&l, 0, sizeof l); l.lastRequested = -1;
	expect(CustomRaceLatch_Stage(&l, &c), "leg 0 stages from the pad");
	CustomRaceLatch_OnRequest(&l, 5);
	expect(CustomRaceLatch_Intent(&l, 5) && l.current.leg == 0, "leg 0 is current after its load");
	CustomRaceLatch_OnRequest(&l, 5);
	expect(CustomRaceLatch_Intent(&l, 5) && l.current.leg == 0, "a restart keeps leg 0");
	d = leg(2, 9); expect(!CustomRaceLatch_StageNext(&l, &d), "a skipped leg does not stage");
	d = leg(1, 9); strcpy(d.entryID, "entry/cup:5"); expect(!CustomRaceLatch_StageNext(&l, &d), "another cup does not stage");
	d = leg(1, 9); d.seedEpoch = 8; expect(!CustomRaceLatch_StageNext(&l, &d), "another seed epoch does not stage");
	d = leg(1, 9); expect(CustomRaceLatch_StageNext(&l, &d), "leg 1 stages after leg 0");
	expect(!CustomRaceLatch_StageNext(&l, &d), "one staged leg at a time");
	CustomRaceLatch_OnRequest(&l, 9);
	expect(CustomRaceLatch_Intent(&l, 9) && l.current.leg == 1 && !CustomRaceLatch_Intent(&l, 5), "leg 1 is current");
	d = leg(2, 9); expect(CustomRaceLatch_StageNext(&l, &d), "a repeated track stages as the next leg");
	CustomRaceLatch_OnRequest(&l, 9);
	expect(CustomRaceLatch_Intent(&l, 9) && l.current.leg == 2, "the same host moves to the staged leg, not a restart");
	CustomRaceLatch_OnRequest(&l, 26);
	expect(!l.active && !CustomRaceLatch_Intent(&l, 9), "an exit to the hub ends the cup context");
	d = leg(3, 1); expect(!CustomRaceLatch_StageNext(&l, &d), "no leg stages after the cup ended");

	// Sweep attempt: four first places in one attempt.
	AP_CupSweepEnter(&a, 0);
	for (n = 0; n < 4; ++n) { AP_CupSweepStartLeg(&a, 0, n, 1); AP_CupSweepRecord(&a, 0, n, 1, 0); }
	expect(AP_CupSweepReady(&a, 0, 4), "four first places sweep");
	AP_CupSweepEnter(&a, 0);
	AP_CupSweepStartLeg(&a, 0, 0, 1);
	AP_CupSweepStartLeg(&a, 0, 0, 1); // restart of leg 0
	for (n = 1; n < 4; ++n) { AP_CupSweepStartLeg(&a, 0, n, 1); AP_CupSweepRecord(&a, 0, n, 1, 0); }
	expect(!AP_CupSweepReady(&a, 0, 4), "a restart drops the sweep");
	AP_CupSweepEnter(&a, 0);
	for (n = 0; n < 4; ++n) { AP_CupSweepStartLeg(&a, 0, n, 1); AP_CupSweepRecord(&a, 0, n, n != 2, 0); }
	expect(!AP_CupSweepReady(&a, 0, 4), "a lost leg drops the sweep");
	AP_CupSweepEnter(&a, 0);
	for (n = 0; n < 4; ++n) { AP_CupSweepStartLeg(&a, 0, n, 1); AP_CupSweepRecord(&a, 0, n, 1, n == 3); }
	expect(!AP_CupSweepReady(&a, 0, 4), "a forced loss drops the sweep");

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
