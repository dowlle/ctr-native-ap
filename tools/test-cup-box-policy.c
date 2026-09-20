// Out-of-engine assertions for the alternate-route AP-box access policy (WO-A3,
// ruled 2026-08-24 10:51 CEST for Gem Cup legs; extended to boss races by the
// 2026-09-12 ruling). Compiles the REAL decision: ap/ap_cup_box_policy.h is
// freestanding by design and includes nothing, so this harness links nothing
// from the game and runs on any host, with no disc, no display and no seed.
//
//   cc -Wall -Wextra -DCTR_AP -o /tmp/test-cup-box-policy tools/test-cup-box-policy.c
//   /tmp/test-cup-box-policy
//
// Exit 0 = every assertion held; the failing case is printed otherwise.
//
// The binding behaviour under test, in one sentence: a Gem Cup leg or a boss
// race shows, collides with and dispatches its track's authored AP boxes only
// while the corresponding individual race is currently accessible through its
// randomized physical pad, and cup entry or garage access alone grants nothing.
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
//   6. the own-pad controls: ordinary Adventure races, Relic Races and the
//      custom encounter overrides are never touched by the policy, whatever the
//      pad terms say,
//   6b. the BOSS ARM (2026-09-12 ruling): a boss race asks exactly the same
//      question about the track it loaded, with Komodo Joe / Dragon Mines as the
//      named case, and boss garage access alone opens nothing,
//   7. that cup access alone never opens the box policy -- no combination of
//      cup-side facts reaches an allow without the individual pad's own terms,
//   7b. the PRESENTATION split (#354): a refused alternate route now stands its
//      boxes TRANSLUCENT instead of standing them down, so every refused row
//      above is asserted a second time as "stands, but is not collectable", and
//      the two older truthiness values (0 = nothing, 1 = collectable) are pinned,
//   8. permanent datapackage membership is not an input at all: the policy has
//      no location-liveness parameter, so a box being in the seed forever
//      cannot open a shut leg (the liveness gate stays where it was, in
//      AP_BoxMap_BuildSet's §7 rule).
//
// MUTATION SENSITIVITY. Each of the three terms is asserted in a pair of rows
// that differ in that term alone, so dropping any one of them from
// AP_BoxPadAccessible turns a row red rather than merely losing coverage. The
// own-pad control rows fail if the route short-circuit is removed, the boss rows
// fail if AP_BOX_ROUTE_BOSS stops being an alternate route, and the mixed-cup
// rows fail if the policy is hoisted to a per-cup answer.

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
	return AP_BoxPolicyAllows(AP_BOX_ROUTE_CUP_LEG, p->physPad, keysOwned,
	                          p->stage1Met, p->racerMet);
}

// The same gather, reached from a boss garage instead of a cup. physPad is
// ctr_cfg_warp_phys(boss venue track), i.e. the pad that individually loads the
// track the boss race is being run on.
static int boss_allows(const PadFacts *p, int keysOwned)
{
	return AP_BoxPolicyAllows(AP_BOX_ROUTE_BOSS, p->physPad, keysOwned,
	                          p->stage1Met, p->racerMet);
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
// 6. Own-pad controls
// ---------------------------------------------------------------------------
static void test_own_pad_controls(void)
{
	// An ordinary Adventure trophy race, a Relic Race, and the custom-track
	// encounter overrides (the event race and the custom Oxide final venue, whose
	// host LevelID resolves to an unrelated pad) all reach the policy with
	// AP_BOX_ROUTE_OWN_PAD. They keep the Alpha 4 rule: the race-type gate
	// upstream has already said yes, and this policy adds nothing. The pad
	// arguments are deliberately hostile -- a refused pad, zero Keys, unmet
	// stage 1, unmet racer lock -- because none of them may be consulted.
	expect_int(AP_BoxPolicyAllows(AP_BOX_ROUTE_OWN_PAD, 10, 0, 0, 0), 1,
	           "own-pad race: policy does not gate it");
	expect_int(AP_BoxPolicyAllows(AP_BOX_ROUTE_OWN_PAD, -1, 0, 0, 0), 1,
	           "own-pad race: no pad resolved, still allowed");
	expect_int(AP_BoxPolicyAllows(AP_BOX_ROUTE_OWN_PAD, 20, 0, 0, 0), 1,
	           "own-pad race: battle-map pad id is irrelevant");
	expect_int(AP_BoxPolicyAllows(AP_BOX_ROUTE_OWN_PAD, 104, 9, 1, 1), 1,
	           "own-pad race: cup pad id is irrelevant");

	// The route predicate itself, so the three-way split is pinned rather than
	// inferred from the allow answers.
	expect_int(AP_BoxRouteIsAlternate(AP_BOX_ROUTE_OWN_PAD), 0, "own pad is not an alternate route");
	expect_int(AP_BoxRouteIsAlternate(AP_BOX_ROUTE_CUP_LEG), 1, "a cup leg is an alternate route");
	expect_int(AP_BoxRouteIsAlternate(AP_BOX_ROUTE_BOSS), 1, "a boss race is an alternate route");

	// The numeric values the older two-state calls relied on.
	expect_int(AP_BOX_ROUTE_OWN_PAD, 0, "own-pad route keeps value 0");
	expect_int(AP_BOX_ROUTE_CUP_LEG, 1, "cup-leg route keeps value 1");

	// And the same arguments on either alternate route are refused, so the
	// branches are genuinely different code paths and not an accident of inputs.
	expect_int(AP_BoxPolicyAllows(AP_BOX_ROUTE_CUP_LEG, 10, 0, 0, 0), 0,
	           "cup leg with the same facts is refused");
	expect_int(AP_BoxPolicyAllows(AP_BOX_ROUTE_CUP_LEG, -1, 0, 0, 0), 0,
	           "cup leg with no pad resolved is refused");
	expect_int(AP_BoxPolicyAllows(AP_BOX_ROUTE_BOSS, 10, 0, 0, 0), 0,
	           "boss race with the same facts is refused");
	expect_int(AP_BoxPolicyAllows(AP_BOX_ROUTE_BOSS, -1, 0, 0, 0), 0,
	           "boss race with no pad resolved is refused");
}

// ---------------------------------------------------------------------------
// 6b. The boss arm (the 2026-09-12 ruling)
// ---------------------------------------------------------------------------
//
// Physical pads of the five retail boss venues, from the hub table above:
//   Roo's Tubes      pad 6  N. Sanity    0 Keys  (Ripper Roo)
//   Papu's Pyramid   pad 5  Lost Ruins   1 Key   (Papu Papu)
//   Dragon Mines     pad 1  Glacier Park 2 Keys  (Komodo Joe)
//   Hot Air Skyway   pad 7  Citadel City 3 Keys  (Pinstripe)
//   Oxide Station    pad 13 Citadel City 3 Keys  (N. Oxide)
static void test_boss_arm(void)
{
	// ---- THE NAMED CASE: Komodo Joe on Dragon Mines ----
	// The reported bug: the Komodo Joe race let Dragon Mines' AP boxes be
	// collected while the Dragon Mines pad was still shut. Each of the three
	// terms is shown to shut it on its own, and then to open it.
	{
		PadFacts dragon = { 1, 1, 1 }; // Dragon Mines pad, stage 1 met, no racer lock

		// Hub term. Glacier Park costs 2 Keys; the Komodo Joe garage can be open
		// on fewer, which is exactly the reported state.
		expect_int(boss_allows(&dragon, 0), 0, "Komodo Joe: zero Keys -> no Dragon Mines boxes");
		expect_int(boss_allows(&dragon, 1), 0, "Komodo Joe: one Key short of Glacier Park -> no boxes");
		expect_int(boss_allows(&dragon, 2), 1, "Komodo Joe: second Key -> Dragon Mines boxes appear");

		// Stage-1 term alone.
		dragon.stage1Met = 0;
		expect_int(boss_allows(&dragon, 9), 0, "Komodo Joe: Dragon Mines pad stage 1 unmet -> no boxes");
		dragon.stage1Met = 1;
		expect_int(boss_allows(&dragon, 9), 1, "Komodo Joe: stage 1 met -> boxes");

		// Racer-lock term alone.
		dragon.racerMet = 0;
		expect_int(boss_allows(&dragon, 9), 0, "Komodo Joe: Dragon Mines pad racer lock unmet -> no boxes");
		dragon.racerMet = 1;
		expect_int(boss_allows(&dragon, 9), 1, "Komodo Joe: racer received -> boxes");
	}

	// ---- RECEIVED-ITEM TRANSITION, the primary in-game acceptance row ----
	// Nothing about the venue changes; the Key lands mid-session. The boss race's
	// answer flips, which is what the per-frame rebuild trigger reacts to.
	{
		PadFacts dragon = { 1, 1, 1 };
		expect_int(boss_allows(&dragon, 1), 0, "received-item transition: before the Key, no boxes");
		expect_int(boss_allows(&dragon, 2), 1, "received-item transition: after the Key, boxes");
	}

	// ---- EVERY BOSS VENUE, pad locked then open ----
	// One row per boss at exactly one Key short of its own hub, and one row at
	// exactly enough. Ripper Roo's N. Sanity venue costs nothing, so its "locked"
	// form has to come from a pad term rather than the spine.
	{
		PadFacts papu      = { 5, 1, 1 };  // 1 Key
		PadFacts komodo    = { 1, 1, 1 };  // 2 Keys
		PadFacts pinstripe = { 7, 1, 1 };  // 3 Keys
		PadFacts oxide     = { 13, 1, 1 }; // 3 Keys
		PadFacts roo       = { 6, 0, 1 };  // 0 Keys; shut on stage 1 instead

		expect_int(boss_allows(&papu, 0), 0, "Papu Papu: Lost Ruins shut -> no Papu's Pyramid boxes");
		expect_int(boss_allows(&papu, 1), 1, "Papu Papu: Lost Ruins open -> boxes");
		expect_int(boss_allows(&komodo, 1), 0, "Komodo Joe: Glacier Park shut -> no boxes");
		expect_int(boss_allows(&komodo, 2), 1, "Komodo Joe: Glacier Park open -> boxes");
		expect_int(boss_allows(&pinstripe, 2), 0, "Pinstripe: Citadel City shut -> no Hot Air Skyway boxes");
		expect_int(boss_allows(&pinstripe, 3), 1, "Pinstripe: Citadel City open -> boxes");
		expect_int(boss_allows(&oxide, 2), 0, "N. Oxide: Citadel City shut -> no Oxide Station boxes");
		expect_int(boss_allows(&oxide, 3), 1, "N. Oxide: Citadel City open -> boxes");
		expect_int(boss_allows(&roo, 0), 0, "Ripper Roo: Roo's Tubes stage 1 unmet -> no boxes");
		roo.stage1Met = 1;
		expect_int(boss_allows(&roo, 0), 1, "Ripper Roo: zero-Key venue with stage 1 met -> boxes");
	}

	// ---- SHUFFLED DESTINATION PADS ----
	// The boss race still loads Dragon Mines, but under destination shuffle the
	// pad that individually loads Dragon Mines is now Crash Cove's (pad 3, zero
	// Keys), and Hot Air Skyway is now behind Glacier Park's Polar Pass pad (12).
	// The policy keys off the resolved PAD, never the track, so both answers move.
	{
		PadFacts dragon_shuf    = { 3, 1, 1 };
		PadFacts pinstripe_shuf = { 12, 1, 1 };

		expect_int(boss_allows(&dragon_shuf, 0), 1,
		           "shuffled: Dragon Mines now loads from a zero-Key pad -> boss race HAS boxes");
		expect_int(boss_allows(&pinstripe_shuf, 2), 1,
		           "shuffled: Hot Air Skyway now loads from a Glacier pad -> boxes on 2 Keys");
		expect_int(boss_allows(&pinstripe_shuf, 1), 0,
		           "shuffled: the new pad's own hub still gates it");
	}

	// ---- GARAGE ACCESS ALONE GRANTS NOTHING ----
	// The strongest form: the player is standing in the boss race, having met
	// every garage requirement there is, and the track's own pad is shut. The
	// policy takes no garage-side argument at all, so no boss-side fact can reach
	// an allow.
	{
		PadFacts shut = { 13, 1, 1 };
		expect_int(boss_allows(&shut, 2), 0, "boss garage access alone never opens the box policy");
		expect_int(boss_allows(&shut, 2), leg_allows(&shut, 2),
		           "boss and cup routes give the same answer for the same pad facts");
	}

	// ---- A BOSS VENUE THAT IS NOT AN ADVENTURE PAD AT ALL ----
	// If a destination ever resolves to a battle map or out of range, there is no
	// individual route to it and the boxes stand down rather than defaulting open.
	expect_int(AP_BoxPolicyAllows(AP_BOX_ROUTE_BOSS, 20, 99, 1, 1), 0,
	           "boss race resolving to a battle map: no individual route, no boxes");
	expect_int(AP_BoxPolicyAllows(AP_BOX_ROUTE_BOSS, 105, 99, 1, 1), 0,
	           "boss race resolving out of range: no boxes");
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
	int a = AP_BoxPolicyAllows(AP_BOX_ROUTE_CUP_LEG, 10, 2, 1, 1);
	int b = AP_BoxPolicyAllows(AP_BOX_ROUTE_CUP_LEG, 10, 2, 1, 1);
	expect_int(a, b, "the policy is a pure function of its four arguments");
	expect_int(a, 0, "a permanently-in-datapackage box on a shut leg is still refused");
}

// ---------------------------------------------------------------------------
// 7b. Presentation (#354)
// ---------------------------------------------------------------------------
//
// The access answer did not move: AP_BoxPolicyAllows is still the ONLY thing
// that decides whether a check can be sent. What changed is what a refusal looks
// like -- nothing at all, until #354; a translucent, uncollectable crate after
// it. These rows pin that the two questions cannot drift apart.
static void test_presentation(void)
{
	// The enum's numeric contract. SOLID is 1 and NONE is 0 so that every older
	// truthiness test of the allow answer keeps its meaning; GHOST is the new
	// state and has to be asked for by name.
	expect_int(AP_BOX_PRESENT_NONE, 0, "NONE keeps value 0");
	expect_int(AP_BOX_PRESENT_SOLID, 1, "SOLID keeps value 1");
	expect_int(AP_BOX_PRESENT_GHOST, 2, "GHOST is the third value");

	// The two predicates every engine caller uses, over the whole enum.
	expect_int(AP_BoxPresentationStands(AP_BOX_PRESENT_NONE), 0, "NONE spawns nothing");
	expect_int(AP_BoxPresentationStands(AP_BOX_PRESENT_SOLID), 1, "SOLID spawns");
	expect_int(AP_BoxPresentationStands(AP_BOX_PRESENT_GHOST), 1, "GHOST spawns too");
	expect_int(AP_BoxPresentationCollectable(AP_BOX_PRESENT_NONE), 0, "NONE is not collectable");
	expect_int(AP_BoxPresentationCollectable(AP_BOX_PRESENT_SOLID), 1, "SOLID is collectable");
	expect_int(AP_BoxPresentationCollectable(AP_BOX_PRESENT_GHOST), 0,
	           "GHOST is NOT collectable -- the whole safety of #354");

	// An OPEN leg is unchanged: solid and collectable.
	{
		PadFacts roos = { 6, 1, 1 };
		expect_int(AP_BoxPresentationFor(AP_BOX_ROUTE_CUP_LEG, roos.physPad, 2,
		                                 roos.stage1Met, roos.racerMet),
		           AP_BOX_PRESENT_SOLID, "open cup leg: solid boxes");
	}

	// A SHUT leg, in each of the three independent failure modes, now GHOSTS
	// rather than disappearing -- and stays uncollectable in all three.
	{
		PadFacts hub    = { 10, 1, 1 }; // Citadel City pad, refused on Keys
		PadFacts stage1 = { 12, 0, 1 };
		PadFacts racer  = { 10, 1, 0 };
		int leg;
		PadFacts *rows[3];
		int keys[3] = { 2, 3, 3 };
		const char *names[3] = { "hub-shut leg", "stage-1-shut leg", "racer-locked leg" };

		rows[0] = &hub; rows[1] = &stage1; rows[2] = &racer;
		for (leg = 0; leg < 3; leg++)
		{
			char what[96];
			int present = AP_BoxPresentationFor(AP_BOX_ROUTE_CUP_LEG, rows[leg]->physPad,
			                                    keys[leg], rows[leg]->stage1Met,
			                                    rows[leg]->racerMet);
			snprintf(what, sizeof what, "%s: boxes stand translucent", names[leg]);
			expect_int(present, AP_BOX_PRESENT_GHOST, what);
			snprintf(what, sizeof what, "%s: still not collectable", names[leg]);
			expect_int(AP_BoxPresentationCollectable(present), 0, what);
			snprintf(what, sizeof what, "%s: presentation agrees with the access answer", names[leg]);
			expect_int(AP_BoxPresentationCollectable(present),
			           leg_allows(rows[leg], keys[leg]), what);
		}
	}

	// The BOSS route gets the identical treatment: the Komodo Joe case shows
	// Dragon Mines' boxes as ghosts while the Dragon Mines pad is shut, and turns
	// them solid on the Key that opens it.
	{
		PadFacts dragon = { 1, 1, 1 };
		expect_int(AP_BoxPresentationFor(AP_BOX_ROUTE_BOSS, dragon.physPad, 1,
		                                 dragon.stage1Met, dragon.racerMet),
		           AP_BOX_PRESENT_GHOST, "Komodo Joe with Glacier Park shut: translucent boxes");
		expect_int(AP_BoxPresentationFor(AP_BOX_ROUTE_BOSS, dragon.physPad, 2,
		                                 dragon.stage1Met, dragon.racerMet),
		           AP_BOX_PRESENT_SOLID, "Komodo Joe after the second Key: solid boxes");
	}

	// An OWN-PAD race can never ghost: it does not consult the pad terms at all,
	// so the hostile arguments that refuse an alternate route still come back
	// solid. This row fails if the presentation layer is ever applied ahead of
	// the route short-circuit instead of behind it.
	expect_int(AP_BoxPresentationFor(AP_BOX_ROUTE_OWN_PAD, 10, 0, 0, 0),
	           AP_BOX_PRESENT_SOLID, "own-pad race is never ghosted");
	expect_int(AP_BoxPresentationFor(AP_BOX_ROUTE_OWN_PAD, -1, 0, 0, 0),
	           AP_BOX_PRESENT_SOLID, "own-pad race with no pad resolved is still solid");

	// A route that resolves to no adventure pad at all still ghosts rather than
	// opening: presentation is cosmetic and may never rescue a refused access
	// answer.
	expect_int(AP_BoxPresentationFor(AP_BOX_ROUTE_BOSS, 20, 99, 1, 1),
	           AP_BOX_PRESENT_GHOST, "battle-map venue: translucent, never collectable");
	expect_int(AP_BoxPresentationCollectable(
	               AP_BoxPresentationFor(AP_BOX_ROUTE_BOSS, 20, 99, 1, 1)), 0,
	           "battle-map venue sends nothing");

	// THE INVARIANT, swept: over every route, a wide pad range and every
	// combination of the two boolean terms at every plausible Key count,
	// collectability is EXACTLY the old allow answer. Presentation added a state;
	// it moved no check.
	{
		int route, pad, keys, st, rc;
		int mismatches = 0;

		for (route = 0; route <= 2; route++)
			for (pad = -2; pad <= 106; pad++)
				for (keys = 0; keys <= 5; keys++)
					for (st = 0; st <= 1; st++)
						for (rc = 0; rc <= 1; rc++)
						{
							int allow = AP_BoxPolicyAllows(route, pad, keys, st, rc);
							int present = AP_BoxPresentationFor(route, pad, keys, st, rc);
							if (AP_BoxPresentationCollectable(present) != allow)
								mismatches++;
							if (!AP_BoxPresentationStands(present))
								mismatches++; // the policy itself never returns NONE
						}
		expect_int(mismatches, 0,
		           "over the full sweep: collectable == the access answer, and every "
		           "policy answer stands something");
	}
}

int main(void)
{
	test_hub_table();
	test_terms_are_independent();
	test_mixed_cup();
	test_own_pad_controls();
	test_boss_arm();
	test_presentation();
	test_membership_is_not_an_input();

	if (g_failures != 0)
	{
		printf("%d assertion(s) failed\n", g_failures);
		return 1;
	}
	printf("test-cup-box-policy: all assertions held\n");
	return 0;
}
