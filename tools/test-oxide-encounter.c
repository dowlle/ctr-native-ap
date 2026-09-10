// cc -std=c99 -Wall -Wextra -Werror -o /tmp/test-oxide-encounter tools/test-oxide-encounter.c
//
// Issues #320 and #321: the ONE Oxide garage decision -- which encounter is
// next, and is the door open for it. Drives ap/ap_oxide_encounter.h over the
// full truth table the RC ruling of 2026-09-03 names: all four goal_oxide
// values, both encounter phases, no companion arm / Boss-only / Gem-only /
// both, every one-short partial state, and the disabled escape. Plus a
// mutation block that fails if the decision degenerates into any of the
// plausible wrong implementations, including the two SHIPPED ones this rework
// replaces.
#include <stdio.h>

#include "../ap/ap_oxide_encounter.h"
#include "../ap/ap_goal_logic.h"

static int failures;

#define CHECK(label, expression) do { \
	int passed = !!(expression); \
	printf("%s  %s\n", passed ? "ok  " : "FAIL", label); \
	failures += !passed; \
} while (0)

// Shorthand for the decision under test. `req` is the resolved boss_req[4]
// answer (normally "four received Keys"); `relics` is AP_OxideFinalOpen().
static AP_OxideGarageState Eval(int req, int goalOxide, int firstCleared,
                                int relics, int goalBosses, int bossesWon,
                                int goalGems, int gemsHeld)
{
	AP_OxideGarageInputs in;

	in.garageReqMet = req;
	in.goalOxide = goalOxide;
	in.firstCleared = firstCleared;
	in.finalRelicMet = relics;
	in.goalBosses = goalBosses;
	in.bossesWon = bossesWon;
	in.goalGems = goalGems;
	in.gemsHeld = gemsHeld;
	return AP_OxideGarageEvaluate(&in);
}

static int Open(int req, int goalOxide, int firstCleared, int relics,
                int goalBosses, int bossesWon, int goalGems, int gemsHeld)
{
	return Eval(req, goalOxide, firstCleared, relics, goalBosses, bossesWon,
	            goalGems, gemsHeld).open;
}

static int Encounter(int req, int goalOxide, int firstCleared, int relics,
                     int goalBosses, int bossesWon, int goalGems, int gemsHeld)
{
	return Eval(req, goalOxide, firstCleared, relics, goalBosses, bossesWon,
	            goalGems, gemsHeld).encounter;
}

int main(void)
{
	int mode, i;

	// ---------------------------------------------------------------------
	// ENCOUNTER SELECTION. An uncleared first challenge is always what the
	// garage offers, even when the relic requirement is already satisfied.
	// This is the second half of #321's defect: the shipped selector chose
	// the Final Challenge from the relic gate alone, so a relic-rich player
	// skipped the first check entirely.
	// ---------------------------------------------------------------------
	for (mode = 0; mode <= 2; mode++)
	{
		char label[110];

		snprintf(label, sizeof label,
		         "goal_oxide=%d: uncleared first is offered even with relics in hand",
		         mode);
		CHECK(label, Encounter(1, mode, 0, 1, 0, 0, 0, 0) == AP_OXIDE_ENCOUNTER_FIRST);

		snprintf(label, sizeof label,
		         "goal_oxide=%d: after the first clear the garage offers the final",
		         mode);
		CHECK(label, Encounter(1, mode, 1, 1, 0, 0, 0, 0) == AP_OXIDE_ENCOUNTER_FINAL);

		snprintf(label, sizeof label,
		         "goal_oxide=%d: the encounter is chosen even at a shut door", mode);
		CHECK(label, Encounter(0, mode, 0, 0, 0, 0, 0, 0) == AP_OXIDE_ENCOUNTER_FIRST);
	}

	// ---------------------------------------------------------------------
	// The configured door requirement is necessary for BOTH encounters in
	// every mode. Nothing below may open a door whose own requirement is
	// unmet.
	// ---------------------------------------------------------------------
	CHECK("req unmet shuts the first challenge (optional)",
	                                       !Open(0, 0, 0, 1, 0, 0, 0, 0));
	CHECK("req unmet shuts the first challenge (any%)",
	                                       !Open(0, 1, 0, 1, 0, 0, 0, 0));
	CHECK("req unmet shuts the first challenge (final goal)",
	                                       !Open(0, 2, 0, 1, 0, 0, 0, 0));
	CHECK("req unmet shuts the final challenge",
	                                       !Open(0, 2, 1, 1, 0, 0, 0, 0));
	CHECK("req unmet shuts it with every companion arm met",
	                                       !Open(0, 1, 0, 1, 4, 4, 5, 5));

	// ---------------------------------------------------------------------
	// goal_oxide 0 (optional / none). Oxide is ordinary content: four Keys
	// for the first challenge, plus the relic gate for the Final Challenge,
	// and NO non-Oxide goal arm touches either -- even when those arms are
	// independently active for a Boss-only or Gem-only goal.
	// ---------------------------------------------------------------------
	CHECK("optional: first opens on the door requirement",
	                                        Open(1, 0, 0, 0, 0, 0, 0, 0));
	CHECK("optional: first ignores unmet boss and gem arms",
	                                        Open(1, 0, 0, 0, 4, 0, 5, 0));
	CHECK("optional: final needs its relics",
	                                       !Open(1, 0, 1, 0, 0, 0, 0, 0));
	CHECK("optional: final opens on relics",
	                                        Open(1, 0, 1, 1, 0, 0, 0, 0));
	CHECK("optional: final ignores unmet boss and gem arms",
	                                        Open(1, 0, 1, 1, 4, 0, 5, 0));

	// ---------------------------------------------------------------------
	// goal_oxide 1 (any_percent). The FIRST challenge is the finale, so it
	// takes the companion arms. The Final Challenge is not the finale and
	// takes only the relic gate.
	// ---------------------------------------------------------------------
	CHECK("any%: no companion arm -> first opens on the door requirement",
	                                        Open(1, 1, 0, 0, 0, 0, 0, 0));
	CHECK("REPORTED: any% + 4 bosses, req met, 0 won -> first stays shut",
	                                       !Open(1, 1, 0, 0, 4, 0, 0, 0));
	CHECK("any%: bosses one short -> first stays shut",
	                                       !Open(1, 1, 0, 0, 4, 3, 0, 0));
	CHECK("any%: bosses exact -> first opens",
	                                        Open(1, 1, 0, 0, 4, 4, 0, 0));
	CHECK("any%: bosses over-satisfied -> first opens",
	                                        Open(1, 1, 0, 0, 2, 4, 0, 0));
	CHECK("any%: gems one short -> first stays shut",
	                                       !Open(1, 1, 0, 0, 0, 0, 5, 4));
	CHECK("any%: gems exact -> first opens",
	                                        Open(1, 1, 0, 0, 0, 0, 5, 5));
	CHECK("any%: three-way, bosses short -> first stays shut",
	                                       !Open(1, 1, 0, 0, 4, 3, 5, 5));
	CHECK("any%: three-way, gems short -> first stays shut",
	                                       !Open(1, 1, 0, 0, 4, 4, 5, 4));
	CHECK("any%: three-way complete -> first opens",
	                                        Open(1, 1, 0, 0, 4, 4, 5, 5));
	CHECK("any%: the final challenge takes relics, not the companion arms",
	                                        Open(1, 1, 1, 1, 4, 0, 5, 0));
	CHECK("any%: the final challenge still needs its relics",
	                                       !Open(1, 1, 1, 0, 4, 4, 5, 5));

	// ---------------------------------------------------------------------
	// goal_oxide 2 (101_percent). THE ALPHA 7 REGRESSION. The first
	// challenge stays the ordinary four-Key midpoint; the companion arms move
	// to the Final Challenge, alongside the relic gate.
	// ---------------------------------------------------------------------
	CHECK("ALPHA 7: final goal leaves the first challenge on the door requirement",
	                                        Open(1, 2, 0, 0, 4, 0, 5, 0));
	CHECK("final goal: first opens with zero boss wins and zero Gems",
	                                        Open(1, 2, 0, 0, 4, 0, 0, 0));
	CHECK("final goal: final needs its relics",
	                                       !Open(1, 2, 1, 0, 4, 4, 5, 5));
	CHECK("final goal: final with bosses one short stays shut",
	                                       !Open(1, 2, 1, 1, 4, 3, 0, 0));
	CHECK("final goal: final with bosses exact opens",
	                                        Open(1, 2, 1, 1, 4, 4, 0, 0));
	CHECK("final goal: final with gems one short stays shut",
	                                       !Open(1, 2, 1, 1, 0, 0, 5, 4));
	CHECK("final goal: final with gems exact opens",
	                                        Open(1, 2, 1, 1, 0, 0, 5, 5));
	CHECK("final goal: three-way, bosses short -> final stays shut",
	                                       !Open(1, 2, 1, 1, 4, 3, 5, 5));
	CHECK("final goal: three-way, gems short -> final stays shut",
	                                       !Open(1, 2, 1, 1, 4, 4, 5, 4));
	CHECK("final goal: three-way complete -> final opens",
	                                        Open(1, 2, 1, 1, 4, 4, 5, 5));
	CHECK("final goal: no companion arm -> final opens on relics alone",
	                                        Open(1, 2, 1, 1, 0, 0, 0, 0));

	// ---------------------------------------------------------------------
	// goal_oxide 3 (disabled, issue #320). No encounter exists and no state
	// of the world opens the garage.
	// ---------------------------------------------------------------------
	CHECK("disabled: no encounter is offered",
	      Encounter(1, 3, 0, 1, 0, 0, 0, 0) == AP_OXIDE_ENCOUNTER_NONE);
	CHECK("disabled: no encounter after a (impossible) first clear either",
	      Encounter(1, 3, 1, 1, 0, 0, 0, 0) == AP_OXIDE_ENCOUNTER_NONE);
	CHECK("disabled: shut with the door requirement met",
	                                       !Open(1, 3, 0, 1, 0, 0, 0, 0));
	CHECK("disabled: shut with every goal arm satisfied",
	                                       !Open(1, 3, 1, 1, 4, 4, 5, 5));
	{
		int req, cleared, relics, gb, bw, gg, gh;
		int rows = 0;

		for (req = 0; req <= 1; req++)
		for (cleared = 0; cleared <= 1; cleared++)
		for (relics = 0; relics <= 1; relics++)
		for (gb = 0; gb <= 4; gb += 4)
		for (bw = 0; bw <= 4; bw += 4)
		for (gg = 0; gg <= 5; gg += 5)
		for (gh = 0; gh <= 5; gh += 5)
		{
			if (Open(req, 3, cleared, relics, gb, bw, gg, gh))
			{
				printf("FAIL  disabled opened: req=%d cleared=%d relics=%d\n",
				       req, cleared, relics);
				failures++;
			}
			rows++;
		}
		printf("ok    disabled stays shut over %d exhaustive rows\n", rows);
	}

	// ---------------------------------------------------------------------
	// WHICH TERMS THE SELECTED ENCOUNTER TAKES. The locked-door advert reads
	// these to list only the blockers that actually apply, so they are part
	// of the contract, not an implementation detail.
	// ---------------------------------------------------------------------
	CHECK("needsRelics is off for the first challenge",
	      !Eval(1, 2, 0, 0, 0, 0, 0, 0).needsRelics);
	CHECK("needsRelics is on for the final challenge",
	      Eval(1, 2, 1, 0, 0, 0, 0, 0).needsRelics);
	CHECK("any%: needsCompanions is on for the first challenge",
	      Eval(1, 1, 0, 0, 4, 0, 0, 0).needsCompanions);
	CHECK("any%: needsCompanions is off for the final challenge",
	      !Eval(1, 1, 1, 0, 4, 0, 0, 0).needsCompanions);
	CHECK("final goal: needsCompanions is off for the first challenge",
	      !Eval(1, 2, 0, 0, 4, 0, 0, 0).needsCompanions);
	CHECK("final goal: needsCompanions is on for the final challenge",
	      Eval(1, 2, 1, 0, 4, 0, 0, 0).needsCompanions);
	CHECK("optional: needsCompanions is off for both encounters",
	      !Eval(1, 0, 0, 0, 4, 0, 0, 0).needsCompanions &&
	      !Eval(1, 0, 1, 0, 4, 0, 0, 0).needsCompanions);

	// ---------------------------------------------------------------------
	// Every one-short partial state on the SELECTED finale, enumerated rather
	// than sampled: dropping ANY single term must shut the door, and the
	// complete state must open it. Run for both Oxide goal modes, each on the
	// encounter that mode actually gates.
	// ---------------------------------------------------------------------
	for (mode = 1; mode <= 2; mode++)
	{
		int cleared = (mode == 2);       // the finale is Oxide 2 under 101%
		int relicsNeeded = (mode == 2);  // ...which also takes the relic gate
		char label[110];

		snprintf(label, sizeof label,
		         "enumerated (goal_oxide=%d): complete conjunction opens the finale",
		         mode);
		CHECK(label, Open(1, mode, cleared, 1, 4, 4, 5, 5));

		for (i = 0; i < 5; i++)
		{
			int req = (i == 0) ? 0 : 1;
			int relics = (i == 1 && relicsNeeded) ? 0 : 1;
			int won = (i == 2) ? 3 : 4;
			int gems = (i == 3) ? 4 : 5;
			// i == 4 is the control row: nothing dropped, must open.
			int expectOpen = (i == 4) || (i == 1 && !relicsNeeded);

			snprintf(label, sizeof label,
			         "enumerated (goal_oxide=%d): drop term %d -> %s",
			         mode, i, expectOpen ? "open" : "shut");
			CHECK(label, Open(req, mode, cleared, relics, 4, won, 5, gems)
			             == expectOpen);
		}
	}

	// ---------------------------------------------------------------------
	// MUTATION SENSITIVITY. Each block asserts an ANSWER only the correct
	// decision gives, so a mutant flipping that behaviour turns the row red.
	// ---------------------------------------------------------------------

	// Mutant A: the SHIPPED Alpha 4 gate -- "drop the companion conjunction
	// entirely". Distinguished by the reported any% row.
	CHECK("mutation A (no companions): reported any% row must be shut",
	                                       !Open(1, 1, 0, 0, 4, 0, 0, 0));

	// Mutant B: "apply the conjunction unconditionally, even when Oxide is not
	// the goal". Distinguished by the optional rows.
	CHECK("mutation B (always compose): optional must open with arms unmet",
	                                        Open(1, 0, 0, 1, 4, 0, 5, 0));

	// Mutant C: the SHIPPED Alpha 7 gate -- "apply the conjunction to the
	// whole garage whenever goal_oxide != 0", ignoring which encounter is
	// selected. Distinguished by the Alpha 7 regression row.
	CHECK("mutation C (shared-door conjunction): final goal's first must open",
	                                        Open(1, 2, 0, 0, 4, 0, 5, 0));

	// Mutant D: "select the encounter from the relic gate alone" (the shipped
	// selector). Distinguished by the priority rows.
	CHECK("mutation D (relic-only selector): uncleared first takes priority",
	      Encounter(1, 2, 0, 1, 0, 0, 0, 0) == AP_OXIDE_ENCOUNTER_FIRST);

	// Mutant E: "OR the terms instead of ANDing them".
	CHECK("mutation E (OR not AND): bosses met, gems unmet must be shut",
	                                       !Open(1, 1, 0, 0, 4, 4, 5, 0));
	CHECK("mutation E (OR not AND): gems met, bosses unmet must be shut",
	                                       !Open(1, 1, 0, 0, 4, 0, 5, 5));
	CHECK("mutation E (OR not AND): arms met, req unmet must be shut",
	                                       !Open(0, 1, 0, 0, 4, 4, 5, 5));

	// Mutant F: ">= becomes >" (off by one at the exact boundary).
	CHECK("mutation F (strict >): exactly enough bosses must open",
	                                        Open(1, 1, 0, 0, 4, 4, 0, 0));
	CHECK("mutation F (strict >): exactly enough gems must open",
	                                        Open(1, 1, 0, 0, 0, 0, 5, 5));

	// Mutant G: "the companion arms are required even when their count is 0",
	// e.g. a hard `bossesWon < 4` with no activity test.
	CHECK("mutation G (inactive arm still required): must open",
	                                        Open(1, 1, 0, 0, 0, 0, 0, 0));

	// Mutant H: "the relic gate is dropped from the final challenge".
	CHECK("mutation H (no relic gate): final must be shut without relics",
	                                       !Open(1, 0, 1, 0, 0, 0, 0, 0));

	// Mutant I: "disabled falls through to the optional path" (the value the
	// schema bump exists to stop an old client mishandling).
	CHECK("mutation I (disabled treated as optional): must stay shut",
	                                       !Open(1, 3, 0, 1, 0, 0, 0, 0));

	// Mutant J: "the door requirement is skipped once the goal is complete".
	CHECK("mutation J (goal short-circuits req): req unmet must be shut",
	                                       !Open(0, 2, 1, 1, 4, 4, 5, 5));

	// ---------------------------------------------------------------------
	// CONTRACT: reaching the seed's FINALE must never be looser than goal
	// completion on the companion terms, and never tighter either. The goal
	// evaluator (ap/ap_goal_logic.h) is the authority those terms are copied
	// from, so cross-check the two directly over the whole small space.
	// ---------------------------------------------------------------------
	{
		int gb, bw, gg, gh, m;
		int rows = 0;

		for (m = 1; m <= 2; m++)
		for (gb = 0; gb <= 4; gb++)
		for (bw = 0; bw <= 4; bw++)
		for (gg = 0; gg <= 5; gg++)
		for (gh = 0; gh <= 5; gh++)
		{
			// Beating the finale is what the player is at the door to DO, so
			// ask the goal predicate with the Oxide arm satisfied: what remains
			// is exactly the companion conjunction. The finale is Oxide 1 under
			// any%, Oxide 2 under 101%, and the relic gate is met either way.
			int cleared = (m == 2);
			int goalMet = AP_ComposedGoalMet(m, 1, 1, gb, bw, gg, gh);
			int doorOpen = Open(1, m, cleared, 1, gb, bw, gg, gh);

			if (goalMet && !doorOpen)
			{
				printf("FAIL  goal met but finale shut: mode=%d gb=%d bw=%d gg=%d gh=%d\n",
				       m, gb, bw, gg, gh);
				failures++;
			}
			if (doorOpen && !goalMet)
			{
				printf("FAIL  finale open but arms unmet: mode=%d gb=%d bw=%d gg=%d gh=%d\n",
				       m, gb, bw, gg, gh);
				failures++;
			}
			rows++;
		}
		printf("ok    finale cross-check vs AP_ComposedGoalMet over %d rows\n", rows);
	}

	// ---------------------------------------------------------------------
	// CONTRACT: the NON-finale encounter must never inherit the companion
	// terms. Under 101% that is Oxide 1 (the Alpha 7 defect); under any% it
	// is Oxide 2. Both must answer purely from the door requirement and, for
	// Oxide 2, the relic gate.
	// ---------------------------------------------------------------------
	{
		int gb, bw, gg, gh;
		int rows = 0;

		for (gb = 0; gb <= 4; gb++)
		for (bw = 0; bw <= 4; bw++)
		for (gg = 0; gg <= 5; gg++)
		for (gh = 0; gh <= 5; gh++)
		{
			if (!Open(1, 2, 0, 0, gb, bw, gg, gh))
			{
				printf("FAIL  final-goal first challenge shut: gb=%d bw=%d gg=%d gh=%d\n",
				       gb, bw, gg, gh);
				failures++;
			}
			if (!Open(1, 1, 1, 1, gb, bw, gg, gh))
			{
				printf("FAIL  any%% final challenge shut: gb=%d bw=%d gg=%d gh=%d\n",
				       gb, bw, gg, gh);
				failures++;
			}
			rows++;
		}
		printf("ok    non-finale encounter is arm-free over %d rows\n", rows);
	}

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
