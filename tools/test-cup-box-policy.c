// Out-of-engine assertions for the Gem Cup AP-box access policy (WO-A3, ruled
// 2026-08-24 10:51 CEST). Compiles the REAL decision: ap/ap_cup_box_policy.h is
// freestanding by design and includes nothing, so this harness links nothing
// from the game and runs on any host, with no disc, no display and no seed.
//
//   cc -Wall -Wextra -DCTR_AP -o /tmp/test-cup-box-policy tools/test-cup-box-policy.c
//   /tmp/test-cup-box-policy
//
// Exit 0 = every assertion held; the failing case is printed otherwise.
//
// The binding behaviour under test, in one sentence: a Gem Cup leg shows,
// collides with and dispatches its authored AP boxes only while the
// corresponding individual race is currently accessible through its randomized
// physical pad, and cup entry alone grants nothing.
//
// What this pins:
//   1. the hub-spine Key table: the exact per-pad values, that battle maps are
//      -1, that the cup pads cost 2, and that out-of-range ids are refused,
//   2. the three access terms, each shown to be INDEPENDENTLY load-bearing
//      (hub Keys, stage 1, racer lock),
//   3. the MIXED CUP, which is the whole point of the ruling: one open leg and
//      one shut leg in the same cup, on the same frame, under fixed AND under
//      shuffled physical-pad mapping,
//   4. the transitions: an unmet stage-1 that becomes met, and an unmet racer
//      lock that becomes met, each flipping exactly one leg,
//   5. structural hub reachability as its own failure mode: a leg whose pad has
//      stage 1 met and no racer lock, refused purely on Keys,
//   6. the non-cup controls: ordinary Adventure races, boss races and Relic
//      Races are never touched by the policy, whatever the pad terms say,
//   7. that cup access alone never opens the box policy -- no combination of
//      cup-side facts reaches an allow without the individual pad's own terms,
//   8. permanent datapackage membership is not an input at all: the policy has
//      no location-liveness parameter, so a box being in the seed forever
//      cannot open a shut leg (the liveness gate stays where it was, in
//      AP_BoxMap_BuildSet's §7 rule).
//
// MUTATION SENSITIVITY. Each of the three terms is asserted in a pair of rows
// that differ in that term alone, so dropping any one of them from
// AP_BoxPadAccessible turns a row red rather than merely losing coverage. The
// non-cup control rows fail if the isCupLeg short-circuit is removed, and the
// mixed-cup rows fail if the policy is hoisted to a per-cup answer.

#include <stdio.h>

#include "../ap/ap_cup_box_policy.h"

static int g_failures = 0;

static void expect_int(int got, int want, const char *what)
{
	if (got == want)
		return;
	printf("FAIL %s: got %d, want %d\n", what, got, want);
	g_failures++;
}

// ---------------------------------------------------------------------------
// A tiny stand-in for the parts of a seed the gather reads, so a scenario reads
// like a seed and not like five loose ints. Nothing here is engine state; the
// harness supplies exactly what ap_boxes.c would have gathered.
// ---------------------------------------------------------------------------
typedef struct
{
	int physPad;   // ctr_cfg_warp_phys(leg destination track)
	int stage1Met; // AP_PadStage1Met(physPad)
	int racerMet;  // ctr_cfg_racer_lock_met(physPad)
} PadFacts;

static int leg_allows(const PadFacts *p, int keysOwned)
{
	return AP_BoxPolicyAllows(1, p->physPad, keysOwned, p->stage1Met, p->racerMet);
}

// ---------------------------------------------------------------------------
// 1. The hub spine
// ---------------------------------------------------------------------------
static void test_hub_table(void)
{
	// The whole table, written out again here on purpose: a harness that
	// derives the expected values from the table under test pins nothing.
	static const int want[AP_HUB_PAD_COUNT] = {
		1, 2, 2, 0, 1, 1, 0, 3, 0, 0, 3, 3, 2, 3, 1, 2,
		1, 1, 3, 1, -1, 0, -1, 2, -1, -1, -1, -1
	};
	int i;

	for (i = 0; i < AP_HUB_PAD_COUNT; i++)
	{
		char what[64];
		snprintf(what, sizeof what, "hub keys for physical pad %d", i);
		expect_int(AP_HubKeysForPad(i), want[i], what);
	}

	// Cup physical pads: the Cups Room door, 2 Keys, for all five.
	for (i = 100; i <= 104; i++)
	{
		char what[64];
		snprintf(what, sizeof what, "hub keys for cup physical pad %d", i);
		expect_int(AP_HubKeysForPad(i), 2, what);
	}

	// Anything else is not an adventure warp pad and can host no individual race.
	expect_int(AP_HubKeysForPad(-1), -1, "hub keys for pad -1");
	expect_int(AP_HubKeysForPad(28), -1, "hub keys for pad 28");
	expect_int(AP_HubKeysForPad(99), -1, "hub keys for pad 99");
	expect_int(AP_HubKeysForPad(105), -1, "hub keys for pad 105");

	// A battle map is refused outright, even with everything else satisfied and
	// a full Key count -- there is no individual race route to it.
	expect_int(AP_BoxPadAccessible(20, 99, 1, 1), 0, "battle map 20 is never a box route");
	expect_int(AP_BoxPadAccessible(24, 99, 1, 1), 0, "battle map 24 is never a box route");
}

// ---------------------------------------------------------------------------
// 2 + 5. The three terms, each independently load-bearing
// ---------------------------------------------------------------------------
static void test_terms_are_independent(void)
{
	// Cortex Castle's pad (10) sits in Citadel City: 3 Keys to stand there.

	// All three met -> allowed. This is the row every negative row below differs
	// from in exactly one term.
	expect_int(AP_BoxPadAccessible(10, 3, 1, 1), 1, "pad 10: keys+stage1+racer all met");

	// Hub term alone withheld. Stage 1 met, no racer lock, boxes still refused:
	// this is the row that fails if the policy is reduced to
	// ctr_cfg_warp_unlocked, which carries no Key term at all.
	expect_int(AP_BoxPadAccessible(10, 2, 1, 1), 0, "pad 10: one Key short of Citadel City");
	expect_int(AP_BoxPadAccessible(10, 0, 1, 1), 0, "pad 10: zero Keys");

	// Stage 1 alone withheld.
	expect_int(AP_BoxPadAccessible(10, 3, 0, 1), 0, "pad 10: stage 1 unmet");

	// Racer lock alone withheld.
	expect_int(AP_BoxPadAccessible(10, 3, 1, 0), 0, "pad 10: racer lock unmet");

	// A N. Sanity Beach pad needs no Key, so a zero-Key file reaches it: the hub
	// term must not be a blanket "some Keys required".
	expect_int(AP_BoxPadAccessible(3, 0, 1, 1), 1, "pad 3 (Crash Cove): reachable on zero Keys");
	expect_int(AP_BoxPadAccessible(3, 0, 0, 1), 0, "pad 3: stage 1 still gates it");

	// Exactly-enough Keys is enough; the comparison is >=, not >.
	expect_int(AP_BoxPadAccessible(0, 1, 1, 1), 1, "pad 0 (Lost Ruins): exactly 1 Key suffices");
	expect_int(AP_BoxPadAccessible(0, 0, 1, 1), 0, "pad 0: zero Keys refused");

	// A leg whose individual race is hosted on a CUP physical pad under merged
	// shuffle: the Cups Room spine applies, and the cup pad's own stage 1 and
	// racer lock still have to be met.
	expect_int(AP_BoxPadAccessible(102, 2, 1, 1), 1, "leg hosted on cup pad 102: open");
	expect_int(AP_BoxPadAccessible(102, 1, 1, 1), 0, "leg hosted on cup pad 102: one Key short");
	expect_int(AP_BoxPadAccessible(102, 2, 0, 1), 0, "leg hosted on cup pad 102: stage 1 unmet");
	expect_int(AP_BoxPadAccessible(102, 2, 1, 0), 0, "leg hosted on cup pad 102: racer unmet");

	// A leg hosted on an ARENA physical pad, same shape (arenas are in the
	// shuffle pool and AP_PadStage1Met routes their class).
	expect_int(AP_BoxPadAccessible(18, 3, 1, 1), 1, "leg hosted on arena pad 18: open");
	expect_int(AP_BoxPadAccessible(18, 2, 1, 1), 0, "leg hosted on arena pad 18: one Key short");
}

// ---------------------------------------------------------------------------
// 3 + 4 + 7. The mixed cup, fixed and shuffled, with both transitions
// ---------------------------------------------------------------------------
static void test_mixed_cup(void)
{
	int keys;

	// ---- FIXED (vanilla) physical-pad mapping ----
	// Green Gem Cup as the audit measured it: Roo's Tubes (pad 6, N. Sanity, 0
	// Keys), Coco Park (pad 14, Lost Ruins, 1 Key), Polar Pass (pad 12, Glacier
	// Park, 2 Keys), Cortex Castle (pad 10, Citadel City, 3 Keys). Every leg's
	// stage 1 is met and no leg pad carries a racer lock, so the ONLY term left
	// is the hub spine. The player holds 2 Keys: enough for the Cups Room door,
	// so the cup is open and being raced, and not enough for Citadel City.
	PadFacts roos   = { 6, 1, 1 };
	PadFacts coco   = { 14, 1, 1 };
	PadFacts polar  = { 12, 1, 1 };
	PadFacts cortex = { 10, 1, 1 };

	keys = 2; // Cups Room open, Citadel City shut
	expect_int(leg_allows(&roos, keys), 1, "fixed cup: Roo's Tubes leg has boxes");
	expect_int(leg_allows(&coco, keys), 1, "fixed cup: Coco Park leg has boxes");
	expect_int(leg_allows(&polar, keys), 1, "fixed cup: Polar Pass leg has boxes");
	expect_int(leg_allows(&cortex, keys), 0, "fixed cup: Cortex Castle leg has NO boxes");

	// THE RULING, stated as an assertion: the cup being open (the player is in
	// it, on this very leg) did not open the shut leg. If the policy were ever
	// hoisted to a per-cup answer, the four rows above could not disagree.
	expect_int(leg_allows(&cortex, keys) == leg_allows(&roos, keys), 0,
	           "cup access alone never opens a leg: the same cup mixes both answers");

	// ---- SHUFFLED physical-pad mapping ----
	// The same four leg TRACKS, now loaded by different physical pads: the
	// policy keys off the pad ctr_cfg_warp_phys resolved, never off the track.
	// Roo's Tubes is now hosted on Citadel City's Oxide Station pad (13), and
	// Cortex Castle on N. Sanity's Crash Cove pad (3). The two answers must
	// swap, which is what "through its randomized physical pad" means.
	PadFacts roos_shuf   = { 13, 1, 1 };
	PadFacts cortex_shuf = { 3, 1, 1 };

	expect_int(leg_allows(&roos_shuf, keys), 0, "shuffled cup: Roo's Tubes leg now has NO boxes");
	expect_int(leg_allows(&cortex_shuf, keys), 1, "shuffled cup: Cortex Castle leg now HAS boxes");

	// ---- STRUCTURAL HUB TRANSITION ----
	// Nothing about the pads changes; the player receives the third Key. The
	// shut leg opens and the open legs stay open.
	keys = 3;
	expect_int(leg_allows(&cortex, keys), 1, "hub transition: third Key opens the Cortex Castle leg");
	expect_int(leg_allows(&roos, keys), 1, "hub transition: the open legs stay open");
	expect_int(leg_allows(&roos_shuf, keys), 1, "hub transition: the shuffled leg opens too");

	// ---- STAGE-1 TRANSITION ----
	// A leg standing behind a met hub door whose pad requirement is not yet
	// satisfied, then satisfied. One leg flips; its cup-mates do not.
	{
		PadFacts gated = { 12, 0, 1 }; // Polar Pass pad, stage 1 unmet
		expect_int(leg_allows(&gated, 3), 0, "stage-1 transition: unmet -> no boxes");
		gated.stage1Met = 1;
		expect_int(leg_allows(&gated, 3), 1, "stage-1 transition: met -> boxes");
		expect_int(leg_allows(&coco, 3), 1, "stage-1 transition: the sibling leg is unaffected");
	}

	// ---- RACER-LOCK TRANSITION ----
	// The audit's Red Gem Cup shape: Cortex Castle's pad locked to a racer the
	// player does not own. Everything else is met.
	{
		PadFacts locked = { 10, 1, 0 };
		expect_int(leg_allows(&locked, 3), 0, "racer-lock transition: unowned racer -> no boxes");
		locked.racerMet = 1;
		expect_int(leg_allows(&locked, 3), 1, "racer-lock transition: racer received -> boxes");
	}

	// ---- A CUP WHOSE OWN PAD IS WIDE OPEN AND WHOSE LEGS ARE ALL SHUT ----
	// The strongest form of "cup entry alone grants no box logic": the player is
	// standing in the cup, every leg refuses.
	{
		PadFacts shut[4] = { { 7, 1, 1 }, { 10, 1, 1 }, { 11, 1, 1 }, { 13, 1, 1 } };
		int leg;
		for (leg = 0; leg < 4; leg++)
		{
			char what[80];
			snprintf(what, sizeof what, "all-Citadel cup on 2 Keys: leg %d refuses", leg);
			expect_int(leg_allows(&shut[leg], 2), 0, what);
		}
	}
}

// ---------------------------------------------------------------------------
// 6. Non-cup controls
// ---------------------------------------------------------------------------
static void test_non_cup_controls(void)
{
	// An ordinary Adventure trophy race, a boss race and a Relic Race all reach
	// the policy with isCupLeg = 0. They keep the Alpha 4 rule: the race type
	// gate upstream has already said yes, and this policy adds nothing. The pad
	// arguments are deliberately hostile -- a refused pad, zero Keys, unmet
	// stage 1, unmet racer lock -- because none of them may be consulted.
	expect_int(AP_BoxPolicyAllows(0, 10, 0, 0, 0), 1, "non-cup race: policy does not gate it");
	expect_int(AP_BoxPolicyAllows(0, -1, 0, 0, 0), 1, "non-cup race: no pad resolved, still allowed");
	expect_int(AP_BoxPolicyAllows(0, 20, 0, 0, 0), 1, "non-cup race: battle-map pad id is irrelevant");
	expect_int(AP_BoxPolicyAllows(0, 104, 9, 1, 1), 1, "non-cup race: cup pad id is irrelevant");

	// And the same arguments as a cup leg are refused, so the two branches are
	// genuinely different code paths and not an accident of the inputs.
	expect_int(AP_BoxPolicyAllows(1, 10, 0, 0, 0), 0, "cup leg with the same facts is refused");
	expect_int(AP_BoxPolicyAllows(1, -1, 0, 0, 0), 0, "cup leg with no pad resolved is refused");
}

// ---------------------------------------------------------------------------
// 8. Permanent datapackage membership is not an input
// ---------------------------------------------------------------------------
static void test_membership_is_not_an_input(void)
{
	// The 270 box location ids are frozen and permanent: every one of them is in
	// the datapackage of every CTR world, forever. That is what made the Alpha 4
	// behaviour look defensible on a cup leg -- the location exists, so why not
	// send it. The policy takes no liveness argument at all, so membership
	// cannot reach it: two calls with identical pad facts are identical answers
	// whatever the seed carries, and a shut leg stays shut.
	//
	// Stated as an assertion the compiler can hold us to: the decision is a pure
	// function of exactly four values, so it is deterministic across repeats.
	int a = AP_BoxPolicyAllows(1, 10, 2, 1, 1);
	int b = AP_BoxPolicyAllows(1, 10, 2, 1, 1);
	expect_int(a, b, "the policy is a pure function of its four arguments");
	expect_int(a, 0, "a permanently-in-datapackage box on a shut leg is still refused");
}

int main(void)
{
	test_hub_table();
	test_terms_are_independent();
	test_mixed_cup();
	test_non_cup_controls();
	test_membership_is_not_an_input();

	if (g_failures != 0)
	{
		printf("%d assertion(s) failed\n", g_failures);
		return 1;
	}
	printf("test-cup-box-policy: all assertions held\n");
	return 0;
}
