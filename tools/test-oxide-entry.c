// cc -std=c99 -Wall -Wextra -Werror -o /tmp/test-oxide-entry tools/test-oxide-entry.c
//
// WO-A1: Oxide garage entry readiness. Drives ap/ap_oxide_entry.h over the full
// truth table the work order names -- every single condition, every pair, the
// three-way combination, every one-short partial state, the goal_oxide == 0
// escape, and the "received Keys are not boss wins" separation -- plus a
// mutation block that fails if the predicate degenerates into any of the
// plausible wrong implementations.
#include <stdio.h>

#include "../ap/ap_oxide_entry.h"
#include "../ap/ap_goal_logic.h"

static int failures;

#define CHECK(label, expression) do { \
	int passed = !!(expression); \
	printf("%s  %s\n", passed ? "ok  " : "FAIL", label); \
	failures += !passed; \
} while (0)

// Shorthand for the predicate under test. `req` is the resolved boss_req[4]
// answer (normally "four received Keys"), which the engine wrapper computes.
static int Ready(int req, int goalOxide, int goalBosses, int bossesWon,
                 int goalGems, int gemsHeld)
{
	return AP_OxideEntryReady(req, goalOxide, goalBosses, bossesWon,
	                          goalGems, gemsHeld);
}

int main(void)
{
	int oxideMode, i;

	// ---------------------------------------------------------------------
	// The configured door requirement is necessary in EVERY shape. Nothing
	// below may open a door whose own requirement is unmet.
	// ---------------------------------------------------------------------
	CHECK("req unmet stays shut (no goal at all)",  !Ready(0, 0, 0, 0, 0, 0));
	CHECK("req unmet stays shut (oxide first)",     !Ready(0, 1, 0, 0, 0, 0));
	CHECK("req unmet stays shut (oxide final)",     !Ready(0, 2, 0, 0, 0, 0));
	CHECK("req unmet stays shut with all companions met",
	                                                !Ready(0, 1, 4, 4, 5, 5));

	// ---------------------------------------------------------------------
	// Oxide alone: first and final. No companion condition is active, so the
	// door is exactly its configured requirement.
	// ---------------------------------------------------------------------
	CHECK("oxide first alone opens on req",          Ready(1, 1, 0, 0, 0, 0));
	CHECK("oxide final alone opens on req",          Ready(1, 2, 0, 0, 0, 0));

	// ---------------------------------------------------------------------
	// Oxide + bosses. The exact reported combination: `Any% + All Four
	// Bosses`, four Keys received, boss races not won. This row is the
	// regression -- it was TRUE (door open) on shipped Alpha 4.
	// ---------------------------------------------------------------------
	CHECK("REPORTED: any% + 4 bosses, req met, 0 bosses won stays shut",
	                                                !Ready(1, 1, 4, 0, 0, 0));
	CHECK("oxide + bosses one short stays shut",    !Ready(1, 1, 4, 3, 0, 0));
	CHECK("oxide + bosses complete opens",           Ready(1, 1, 4, 4, 0, 0));
	CHECK("oxide + bosses over-satisfied opens",     Ready(1, 1, 2, 4, 0, 0));
	CHECK("oxide final + bosses one short stays shut",
	                                                !Ready(1, 2, 4, 3, 0, 0));
	CHECK("oxide final + bosses complete opens",     Ready(1, 2, 4, 4, 0, 0));

	// ---------------------------------------------------------------------
	// Oxide + Gems.
	// ---------------------------------------------------------------------
	CHECK("oxide + gems zero held stays shut",      !Ready(1, 1, 0, 0, 5, 0));
	CHECK("oxide + gems one short stays shut",      !Ready(1, 1, 0, 0, 5, 4));
	CHECK("oxide + gems complete opens",             Ready(1, 1, 0, 0, 5, 5));
	CHECK("oxide + gems over-satisfied opens",       Ready(1, 1, 0, 0, 3, 5));

	// ---------------------------------------------------------------------
	// All three. Every one-short partial state stays shut; only the complete
	// conjunction opens.
	// ---------------------------------------------------------------------
	CHECK("three-way: bosses short stays shut",     !Ready(1, 1, 4, 3, 5, 5));
	CHECK("three-way: gems short stays shut",       !Ready(1, 1, 4, 4, 5, 4));
	CHECK("three-way: both short stays shut",       !Ready(1, 1, 4, 3, 5, 4));
	CHECK("three-way: req short stays shut",        !Ready(0, 1, 4, 4, 5, 5));
	CHECK("three-way complete opens",                Ready(1, 1, 4, 4, 5, 5));
	CHECK("three-way complete opens (oxide final)",  Ready(1, 2, 4, 4, 5, 5));

	// ---------------------------------------------------------------------
	// goal_oxide == 0: Oxide is NOT a goal condition, so his garage must stay
	// an ordinary door. Applying the companion conjunction here would lock
	// content the seed never gated.
	// ---------------------------------------------------------------------
	CHECK("goal_oxide 0 opens on req with bosses unmet",
	                                                 Ready(1, 0, 4, 0, 0, 0));
	CHECK("goal_oxide 0 opens on req with gems unmet",
	                                                 Ready(1, 0, 0, 0, 5, 0));
	CHECK("goal_oxide 0 opens on req with both unmet",
	                                                 Ready(1, 0, 4, 0, 5, 0));

	// ---------------------------------------------------------------------
	// The BUG-D separation, stated as a data fact this predicate can enforce:
	// the boss term is `bossesWon`, a CHECKED-location count, and it is a
	// SEPARATE argument from `garageReqMet` (which is the received-Key gate).
	// Four Keys received with no boss race checked is req=1, bossesWon=0.
	// ---------------------------------------------------------------------
	CHECK("4 Keys received, 0 boss locations checked -> shut",
	                                                !Ready(1, 1, 4, 0, 0, 0));
	CHECK("4 Keys received, 4 boss locations checked -> open",
	                                                 Ready(1, 1, 4, 4, 0, 0));

	// ---------------------------------------------------------------------
	// Every one-short partial state, enumerated rather than sampled: for the
	// full three-way conjunction, dropping ANY single term must shut the door,
	// and the complete state must open it. Run for both Oxide modes.
	// ---------------------------------------------------------------------
	for (oxideMode = 1; oxideMode <= 2; oxideMode++)
	{
		char label[96];
		int allOpen = Ready(1, oxideMode, 4, 4, 5, 5);

		snprintf(label, sizeof label,
		         "enumerated (goal_oxide=%d): complete conjunction opens", oxideMode);
		CHECK(label, allOpen);

		for (i = 0; i < 4; i++)
		{
			int req = (i == 0) ? 0 : 1;
			int won = (i == 1) ? 3 : 4;
			int gems = (i == 2) ? 4 : 5;
			// i == 3 is the control row: nothing dropped, must open.
			int expectOpen = (i == 3);

			snprintf(label, sizeof label,
			         "enumerated (goal_oxide=%d): drop term %d -> %s",
			         oxideMode, i, expectOpen ? "open" : "shut");
			CHECK(label, Ready(req, oxideMode, 4, won, 5, gems) == expectOpen);
		}
	}

	// ---------------------------------------------------------------------
	// MUTATION SENSITIVITY. Each block below is a distinct wrong
	// implementation this suite must be able to tell apart from the right
	// one. A row here asserts an ANSWER that only the correct predicate
	// gives, so a mutant flipping that behaviour turns the row red.
	// ---------------------------------------------------------------------

	// Mutant A: "drop the companion conjunction entirely" (the shipped Alpha 4
	// behaviour). Distinguished by the reported row.
	CHECK("mutation A (no companions): reported row must be shut",
	                                                !Ready(1, 1, 4, 0, 0, 0));

	// Mutant B: "apply the conjunction unconditionally, even when goal_oxide
	// is 0". Distinguished by the goal_oxide == 0 rows.
	CHECK("mutation B (always compose): goal_oxide 0 must be open",
	                                                 Ready(1, 0, 4, 0, 5, 0));

	// Mutant C: "OR the terms instead of ANDing them". Distinguished by any
	// row where one term holds and another does not.
	CHECK("mutation C (OR not AND): bosses met, gems unmet must be shut",
	                                                !Ready(1, 1, 4, 4, 5, 0));
	CHECK("mutation C (OR not AND): gems met, bosses unmet must be shut",
	                                                !Ready(1, 1, 4, 0, 5, 5));
	CHECK("mutation C (OR not AND): companions met, req unmet must be shut",
	                                                !Ready(0, 1, 4, 4, 5, 5));

	// Mutant D: ">= becomes >" (off by one at the exact boundary).
	CHECK("mutation D (strict >): exactly enough bosses must open",
	                                                 Ready(1, 1, 4, 4, 0, 0));
	CHECK("mutation D (strict >): exactly enough gems must open",
	                                                 Ready(1, 1, 0, 0, 5, 5));

	// Mutant E: "the companion arms are required even when their count is 0",
	// e.g. a hard `bossesWon < 4` with no activity test. Distinguished by an
	// inactive arm with a zero tally. (The narrower `>` -> `>=` edit on the
	// activity test alone is an EQUIVALENT mutant -- with goalBosses == 0 the
	// comparison `bossesWon < 0` is false for every real tally -- so it is
	// deliberately not claimed here.)
	CHECK("mutation E (inactive arm still required): must open",
	                                                 Ready(1, 1, 0, 0, 0, 0));

	// Mutant E2: "only the FINAL challenge composes the companions"
	// (goalOxide != 2 escapes). Distinguished by the reported any% row above
	// and by every goal_oxide == 1 companion row.
	CHECK("mutation E2 (final-only composition): any% + gems must be shut",
	                                                !Ready(1, 1, 0, 0, 5, 0));

	// Mutant F: "the requirement check is skipped when the goal is complete".
	CHECK("mutation F (goal short-circuits req): req unmet must be shut",
	                                                !Ready(0, 2, 4, 4, 5, 5));

	// ---------------------------------------------------------------------
	// CONTRACT: entry readiness must never be LOOSER than goal completion on
	// the companion terms. The goal evaluator (ap/ap_goal_logic.h) is the
	// authority those terms are copied from, so cross-check the two directly
	// over the whole small space. If the composed goal is met and the door
	// requirement is met, the door must be open -- otherwise a player could
	// satisfy the goal at a door that never opened.
	// ---------------------------------------------------------------------
	{
		int gb, bw, gg, gh, mode;
		int rows = 0;

		for (mode = 1; mode <= 2; mode++)
		for (gb = 0; gb <= 4; gb++)
		for (bw = 0; bw <= 4; bw++)
		for (gg = 0; gg <= 5; gg++)
		for (gh = 0; gh <= 5; gh++)
		{
			// Oxide beaten is what the player is at the door to DO, so ask the
			// goal predicate with the Oxide arm satisfied: what remains is
			// exactly the companion conjunction.
			int goalMet = AP_ComposedGoalMet(mode, 1, 1, gb, bw, gg, gh);
			int doorOpen = Ready(1, mode, gb, bw, gg, gh);

			if (goalMet && !doorOpen)
			{
				printf("FAIL  goal met but door shut: mode=%d gb=%d bw=%d gg=%d gh=%d\n",
				       mode, gb, bw, gg, gh);
				failures++;
			}
			if (doorOpen && !goalMet)
			{
				printf("FAIL  door open but companions unmet: mode=%d gb=%d bw=%d gg=%d gh=%d\n",
				       mode, gb, bw, gg, gh);
				failures++;
			}
			rows++;
		}
		printf("ok    cross-check vs AP_ComposedGoalMet over %d rows\n", rows);
	}

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
