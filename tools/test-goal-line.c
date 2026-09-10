// cc -std=c99 -Wall -Wextra -Werror -o /tmp/test-goal-line tools/test-goal-line.c
//
// Issue #322: the pause menu's compact goal checklist. Drives
// ap/ap_goal_line.h over every shape the option matrix can produce -- one,
// two and three conditions, each goal_oxide value, zeroed Boss/Gem arms,
// over-satisfied counters -- and checks each one against the safe pause-page
// width, which is the acceptance the old prose line failed.
#include <stdio.h>
#include <string.h>

#include "../ap/ap_goal_line.h"

static int failures;

#define CHECK(label, expression) do { \
	int passed = !!(expression); \
	printf("%s  %s\n", passed ? "ok  " : "FAIL", label); \
	failures += !passed; \
} while (0)

// FONT_SMALL advance model from the 3 September wording review: 13 px for an
// ordinary glyph, 7 px for a period or colon. This is the documented model the
// approved line was measured against, NOT the engine's own table -- the engine
// draws with DecalFont_DrawLine and its region-specific font metrics. It is
// used here so a future edit that lengthens the line fails the budget instead
// of silently clipping again on someone's stream.
#define AP_PAUSE_SAFE_WIDTH_PX 460

static int LineWidth(const char *s)
{
	int w = 0;
	for (; *s != '\0'; s++)
		w += (*s == '.' || *s == ':') ? 7 : 13;
	return w;
}

static const char *Line(int goalOxide, int goalBosses, int bossesWon,
                        int goalGems, int gemsHeld)
{
	static char out[160];
	if (!AP_GoalLineFormat(out, (int)sizeof out, goalOxide, goalBosses,
	                       bossesWon, goalGems, gemsHeld))
		return "";
	return out;
}

static void ExpectLine(const char *label, const char *expected, int goalOxide,
                       int goalBosses, int bossesWon, int goalGems, int gemsHeld)
{
	const char *got = Line(goalOxide, goalBosses, bossesWon, goalGems, gemsHeld);
	int ok = (strcmp(got, expected) == 0);
	printf("%s  %-52s [%s]\n", ok ? "ok  " : "FAIL", label, got);
	if (!ok)
		printf("      expected [%s]\n", expected);
	failures += !ok;
}

int main(void)
{
	int oxide, bosses, gems, won, held;

	// ---------------------------------------------------------------------
	// LABELS. OXIDE for the any_percent goal, OXIDE 2 for the Final Challenge
	// goal, and no Oxide segment at all for optional (0) or disabled (3) --
	// in neither is an Oxide race a completion condition.
	// ---------------------------------------------------------------------
	ExpectLine("any% alone", "GOAL: OXIDE", AP_OXIDE_GOAL_ANY, 0, 0, 0, 0);
	ExpectLine("final goal alone", "GOAL: OXIDE 2", AP_OXIDE_GOAL_FINAL, 0, 0, 0, 0);
	ExpectLine("optional + bosses", "GOAL: BOSS 0/4",
	           AP_OXIDE_GOAL_OPTIONAL, 4, 0, 0, 0);
	ExpectLine("disabled + bosses", "GOAL: BOSS 0/4",
	           AP_OXIDE_GOAL_DISABLED, 4, 0, 0, 0);
	ExpectLine("disabled + gems", "GOAL: GEM 2/5",
	           AP_OXIDE_GOAL_DISABLED, 0, 0, 5, 2);

	// ---------------------------------------------------------------------
	// OMISSION. A zero requirement drops its whole segment, and the remaining
	// separators stay well formed (#322 acceptance 4).
	// ---------------------------------------------------------------------
	ExpectLine("bosses only", "GOAL: BOSS 3/4", AP_OXIDE_GOAL_OPTIONAL, 4, 3, 0, 0);
	ExpectLine("gems only", "GOAL: GEM 4/5", AP_OXIDE_GOAL_OPTIONAL, 0, 0, 5, 4);
	ExpectLine("bosses and gems", "GOAL: BOSS 3/4 - GEM 4/5",
	           AP_OXIDE_GOAL_OPTIONAL, 4, 3, 5, 4);
	ExpectLine("any% and gems", "GOAL: OXIDE - GEM 4/5",
	           AP_OXIDE_GOAL_ANY, 0, 0, 5, 4);
	ExpectLine("any% and bosses", "GOAL: OXIDE - BOSS 1/2",
	           AP_OXIDE_GOAL_ANY, 2, 1, 0, 0);
	ExpectLine("the approved maximum shape", "GOAL: OXIDE 2 - BOSS 3/4 - GEM 4/5",
	           AP_OXIDE_GOAL_FINAL, 4, 3, 5, 4);

	// ---------------------------------------------------------------------
	// COUNTERS. Progress over the CONFIGURED requirement, capped once
	// satisfied: three wins in a two-boss goal reads BOSS 2/2, not BOSS 3/2
	// (#322's worked example).
	// ---------------------------------------------------------------------
	ExpectLine("boss counter caps at the requirement", "GOAL: BOSS 2/2",
	           AP_OXIDE_GOAL_OPTIONAL, 2, 3, 0, 0);
	ExpectLine("gem counter caps at the requirement", "GOAL: GEM 3/3",
	           AP_OXIDE_GOAL_OPTIONAL, 0, 0, 3, 5);
	ExpectLine("exactly satisfied is not capped away", "GOAL: BOSS 4/4",
	           AP_OXIDE_GOAL_OPTIONAL, 4, 4, 0, 0);
	ExpectLine("zero progress reads zero", "GOAL: BOSS 0/4 - GEM 0/5",
	           AP_OXIDE_GOAL_OPTIONAL, 4, 0, 5, 0);
	ExpectLine("a negative tally cannot print a minus", "GOAL: BOSS 0/4",
	           AP_OXIDE_GOAL_OPTIONAL, 4, -1, 0, 0);

	// ---------------------------------------------------------------------
	// NOTHING TO SAY. An all-off goal (which generate_early rejects, so it
	// cannot reach a real client) writes nothing and returns 0, rather than a
	// bare "GOAL:".
	// ---------------------------------------------------------------------
	{
		char out[160];
		CHECK("all-off returns 0",
		      !AP_GoalLineFormat(out, (int)sizeof out, AP_OXIDE_GOAL_OPTIONAL,
		                         0, 0, 0, 0));
		CHECK("all-off leaves the buffer empty", out[0] == '\0');
		CHECK("disabled with no arm returns 0",
		      !AP_GoalLineFormat(out, (int)sizeof out, AP_OXIDE_GOAL_DISABLED,
		                         0, 0, 0, 0));
		CHECK("a null buffer is refused, not written",
		      !AP_GoalLineFormat(0, 16, AP_OXIDE_GOAL_ANY, 4, 0, 5, 0));
		CHECK("a zero cap is refused",
		      !AP_GoalLineFormat(out, 0, AP_OXIDE_GOAL_ANY, 4, 0, 5, 0));
	}

	// ---------------------------------------------------------------------
	// TRUNCATION. A short buffer must still be NUL-terminated and must never
	// be written past. AH_Pause.c gives 160 bytes, so this is defence against
	// a future caller, not the live path.
	// ---------------------------------------------------------------------
	{
		char small[12];
		char guarded[24];
		int i;

		memset(guarded, '#', sizeof guarded);
		AP_GoalLineFormat(guarded, 12, AP_OXIDE_GOAL_FINAL, 4, 3, 5, 4);
		CHECK("truncated output is NUL-terminated", guarded[11] == '\0');
		for (i = 12; i < (int)sizeof guarded; i++)
			if (guarded[i] != '#')
			{
				printf("FAIL  wrote past the requested cap at byte %d\n", i);
				failures++;
				break;
			}
		CHECK("truncated output stays within the cap",
		      (int)strlen(guarded) < 12);
		AP_GoalLineFormat(small, (int)sizeof small, AP_OXIDE_GOAL_ANY, 0, 0, 0, 0);
		CHECK("a short line still fits a small buffer",
		      strcmp(small, "GOAL: OXIDE") == 0);
	}

	// ---------------------------------------------------------------------
	// THE WIDTH BUDGET (#322 acceptance 1 and 2). EVERY shape the option
	// matrix can produce must fit the safe pause-page width. This is the
	// acceptance the shipped prose line failed at roughly 1444 px.
	// ---------------------------------------------------------------------
	{
		int widest = 0;
		char widestLine[160];
		int shapes = 0;

		widestLine[0] = '\0';
		for (oxide = 0; oxide <= 3; oxide++)
		for (bosses = 0; bosses <= 4; bosses++)
		for (won = 0; won <= 4; won++)
		for (gems = 0; gems <= 5; gems++)
		for (held = 0; held <= 5; held++)
		{
			const char *line = Line(oxide, bosses, won, gems, held);
			int w;

			if (line[0] == '\0')
				continue; // the all-off shape, rejected at generation
			w = LineWidth(line);
			shapes++;
			if (w > widest)
			{
				widest = w;
				snprintf(widestLine, sizeof widestLine, "%s", line);
			}
			if (w > AP_PAUSE_SAFE_WIDTH_PX)
			{
				printf("FAIL  over the safe width at %d px: [%s]\n", w, line);
				failures++;
			}
		}
		printf("ok    %d shapes fit; widest %d px of %d: [%s]\n",
		       shapes, widest, AP_PAUSE_SAFE_WIDTH_PX, widestLine);
		// Every counter in range is a single digit, so all three-segment
		// shapes tie at the same width -- which one the scan happens to
		// report first is not the claim. The claim is that the widest shape
		// the matrix can produce is exactly the approved maximum's width.
		CHECK("the approved maximum measures the reviewed 436 px",
		      LineWidth("GOAL: OXIDE 2 - BOSS 3/4 - GEM 4/5") == 436);
		CHECK("no shape is wider than the approved maximum",
		      widest == LineWidth("GOAL: OXIDE 2 - BOSS 3/4 - GEM 4/5"));
		CHECK("the widest shape is a full three-segment OXIDE 2 line",
		      strncmp(widestLine, "GOAL: OXIDE 2 - BOSS ", 21) == 0 &&
		      strstr(widestLine, " - GEM ") != 0);
		CHECK("the first proposed shape really did not fit (why the labels shortened)",
		      LineWidth("GOAL: OXIDE FINAL - BOSSES 3/4 - GEMS 4/5")
		          > AP_PAUSE_SAFE_WIDTH_PX);
		CHECK("the replaced prose line was far over budget",
		      LineWidth("Goal: beat N. Oxide's Final Challenge (not yet) and win "
		                "4 of 4 boss races (have 0) and hold 5 of 5 Gems (have 0)")
		          > 3 * AP_PAUSE_SAFE_WIDTH_PX);
	}

	// ---------------------------------------------------------------------
	// NO WRAP, NO SCROLL, NO MARQUEE (#322 acceptance 3): the output is a
	// single line with no control characters the engine could break on.
	// DecalFont_DrawMultiLine treats '\r' as an explicit break, so its
	// absence is the property that keeps this on one line.
	// ---------------------------------------------------------------------
	{
		int bad = 0;

		for (oxide = 0; oxide <= 3; oxide++)
		for (bosses = 0; bosses <= 4; bosses++)
		for (gems = 0; gems <= 5; gems++)
		{
			const char *line = Line(oxide, bosses, 1, gems, 1);
			const char *c;
			for (c = line; *c != '\0'; c++)
				if (*c == '\n' || *c == '\r' || *c == '\t')
					bad++;
		}
		CHECK("no shape contains a line break or tab", bad == 0);
	}

	// ---------------------------------------------------------------------
	// MUTATION SENSITIVITY.
	// ---------------------------------------------------------------------

	// Mutant A: "show the Oxide segment for every goal_oxide value". The
	// optional and disabled rows above are what catch it; restated here as
	// the explicit claim.
	CHECK("mutation A (Oxide segment always shown): optional omits it",
	      strcmp(Line(AP_OXIDE_GOAL_OPTIONAL, 4, 1, 0, 0), "GOAL: BOSS 1/4") == 0);
	CHECK("mutation A: disabled omits it too",
	      strcmp(Line(AP_OXIDE_GOAL_DISABLED, 0, 0, 5, 1), "GOAL: GEM 1/5") == 0);

	// Mutant B: "OXIDE and OXIDE 2 swapped". Distinguished by both labels.
	CHECK("mutation B (labels swapped): any% is OXIDE",
	      strcmp(Line(AP_OXIDE_GOAL_ANY, 0, 0, 0, 0), "GOAL: OXIDE") == 0);
	CHECK("mutation B: the final goal is OXIDE 2",
	      strcmp(Line(AP_OXIDE_GOAL_FINAL, 0, 0, 0, 0), "GOAL: OXIDE 2") == 0);

	// Mutant C: "drop the cap". Distinguished by the over-satisfied rows.
	CHECK("mutation C (uncapped counter): 3 of 2 bosses reads 2/2",
	      strcmp(Line(AP_OXIDE_GOAL_OPTIONAL, 2, 3, 0, 0), "GOAL: BOSS 2/2") == 0);

	// Mutant D: "emit a segment for a zero requirement". Distinguished by the
	// single-condition shapes, which would grow a stray "BOSS 0/0".
	CHECK("mutation D (zero segment emitted): gems-only stays one segment",
	      strcmp(Line(AP_OXIDE_GOAL_OPTIONAL, 0, 0, 5, 2), "GOAL: GEM 2/5") == 0);

	// Mutant E: "leading or trailing separator". Distinguished directly,
	// because a stray " - " is exactly what an omitted-segment bug leaves.
	{
		const char *line = Line(AP_OXIDE_GOAL_ANY, 0, 0, 5, 2);
		CHECK("mutation E (stray separator): no ' - ' before the first segment",
		      strstr(line, ": - ") == 0);
		CHECK("mutation E: no trailing separator",
		      strcmp(line + strlen(line) - 3, " - ") != 0);
	}

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
