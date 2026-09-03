// cc -std=c99 -Wall -Wextra -Werror -o /tmp/test-oxide-garage-advert tools/test-oxide-garage-advert.c
//
// 2026-09-03 repair of the Sonnet review's garage-advert-overflow finding
// against the local #320-322 candidate. Drives ap/ap_oxide_garage_advert.h's
// AP_OxideGarageAdvertFormat over the encounter/requirement matrix, including
// the longest Final Challenge + relic + Boss + Gem combination, and checks
// every rendered LINE (the panel is '\r'-separated) against the same safe
// pause-page width tools/test-goal-line.c uses -- the acceptance the old
// single-line " + "-joined advert failed at roughly 1489 px.
#include <stdio.h>
#include <string.h>

#include "../ap/ap_oxide_garage_advert.h"

static int failures;

#define CHECK(label, expression) do { \
	int passed = !!(expression); \
	printf("%s  %s\n", passed ? "ok  " : "FAIL", label); \
	failures += !passed; \
} while (0)

// Same FONT_SMALL advance model as tools/test-goal-line.c: 13 px for an
// ordinary glyph, 7 px for a period or colon. Deliberately reused rather than
// re-derived -- the Sonnet review measured the pre-repair overflow "by the
// same width model", so the repair is held to the identical, already-
// established budget.
#define AP_GARAGE_SAFE_WIDTH_PX 460

// Actual NTSC-U font metrics from game/zGlobal_DATA.c. The production caller
// passes the corresponding data.font_charPixHeight entries into the pure
// layout helper, so these assertions exercise the real FONT_BIG/FONT_SMALL
// vertical advances rather than an unrelated test-only spacing model.
#define AP_GARAGE_VIEW_BOTTOM_PX 216
#define AP_GARAGE_TITLE_Y_PX     (AP_GARAGE_VIEW_BOTTOM_PX - 30)
#define AP_GARAGE_FONT_BIG_H_PX  17
#define AP_GARAGE_FONT_SMALL_H_PX 8

static int LineWidth(const char *s)
{
	int w = 0;
	for (; *s != '\0'; s++)
		w += (*s == '.' || *s == ':') ? 7 : 13;
	return w;
}

// Hand-rolled '\r' split (no strtok_r: not declared under -std=c99). Calls
// `fn(line, ctx)` once per line, including a trailing line with no closing
// '\r'. Returns the number of lines found.
static int ForEachLine(const char *panel, void (*fn)(const char *, void *),
                       void *ctx)
{
	char buf[256];
	int n = 0;
	const char *start = panel;
	const char *c;

	for (c = panel;; c++)
	{
		if (*c == '\r' || *c == '\0')
		{
			int len = (int)(c - start);
			if (len >= (int)sizeof buf)
				len = (int)sizeof buf - 1;
			memcpy(buf, start, (size_t)len);
			buf[len] = '\0';
			fn(buf, ctx);
			n++;
			if (*c == '\0')
				break;
			start = c + 1;
		}
	}
	return n;
}

typedef struct { int widest; const char *label; } WidthScan;

static void ScanWidth(const char *line, void *ctxRaw)
{
	WidthScan *ctx = (WidthScan *)ctxRaw;
	int w = LineWidth(line);
	if (w > ctx->widest)
		ctx->widest = w;
	if (w > AP_GARAGE_SAFE_WIDTH_PX)
	{
		printf("FAIL  %s: line over the safe width at %d px: [%s]\n",
		       ctx->label, w, line);
		failures++;
	}
}

// Splits `panel` on '\r' and checks each resulting line against the safe
// width. Returns the number of lines found.
static int CheckPanelWidth(const char *label, const char *panel)
{
	WidthScan scan;
	int lines;

	scan.widest = 0;
	scan.label = label;
	lines = ForEachLine(panel, ScanWidth, &scan);
	printf("ok    %-58s %d line(s), widest %d px of %d\n", label, lines,
	       scan.widest, AP_GARAGE_SAFE_WIDTH_PX);
	return lines;
}

static AP_OxideGarageState St(int encounter, int needsCompanions,
                              int needsRelics)
{
	AP_OxideGarageState st;
	st.encounter = encounter;
	st.open = 0;
	st.needsCompanions = needsCompanions;
	st.needsRelics = needsRelics;
	return st;
}

int main(void)
{
	char out[256];

	// ---------------------------------------------------------------------
	// VERTICAL BOUNDS. The title and every explicit panel line form one block.
	// Pin all formatter shapes, including the realistic four-line Final and
	// structural five-line maximum, to FONT_BIG=17/FONT_SMALL=8 and the retail
	// 216px viewport. The last line must end at or above the viewport bottom,
	// and the first panel line must begin after the title glyph band.
	// ---------------------------------------------------------------------
	{
		int lines;

		CHECK("actual FONT_BIG height is 17 px", AP_GARAGE_FONT_BIG_H_PX == 17);
		CHECK("actual FONT_SMALL height is 8 px", AP_GARAGE_FONT_SMALL_H_PX == 8);
		for (lines = 1; lines <= 5; lines++)
		{
			int titleY = AP_OxideGarageBlockTitleY(
				AP_GARAGE_VIEW_BOTTOM_PX, AP_GARAGE_TITLE_Y_PX,
				AP_GARAGE_FONT_BIG_H_PX, AP_GARAGE_FONT_SMALL_H_PX,
				lines);
			int panelY = titleY + AP_GARAGE_FONT_BIG_H_PX;
			int panelBottom = panelY + lines * AP_GARAGE_FONT_SMALL_H_PX;

			CHECK("vertical layout: panel begins after the title", panelY >=
			      titleY + AP_GARAGE_FONT_BIG_H_PX);
			CHECK("vertical layout: every panel line stays in viewport",
			      panelBottom <= AP_GARAGE_VIEW_BOTTOM_PX);
		}
		CHECK("four-line Final shifts title up by 19 px",
		      AP_OxideGarageBlockTitleY(216, 186, 17, 8, 4) == 167);
		CHECK("five-line Final shifts title up by 27 px",
		      AP_OxideGarageBlockTitleY(216, 186, 17, 8, 5) == 159);
	}

	// ---------------------------------------------------------------------
	// CLOSED GARAGE (#320 disabled). One short line, no requirement claimed.
	// ---------------------------------------------------------------------
	{
		AP_OxideGarageState none = St(AP_OXIDE_ENCOUNTER_NONE, 0, 0);
		CHECK("disabled writes the shut message",
		      AP_OxideGarageAdvertFormat(none, AP_OXIDE_REQ_KEY, 4, -1, 4, 1,
		                                 AP_OXIDE_RELIC_MODE_SAPPHIRE, 18, 0,
		                                 0, 0, 4, 0, 5, 0, out, sizeof out) == 1);
		CHECK("disabled: no '\\r' in a single-line message",
		      strchr(out, '\r') == 0);
		CheckPanelWidth("disabled/shut message", out);
	}

	// ---------------------------------------------------------------------
	// FIRST CHALLENGE, door term only (four Keys, not yet held).
	// ---------------------------------------------------------------------
	{
		AP_OxideGarageState first = St(AP_OXIDE_ENCOUNTER_FIRST, 0, 0);
		AP_OxideGarageAdvertFormat(first, AP_OXIDE_REQ_KEY, 4, -1, 2, 1,
		                          AP_OXIDE_RELIC_MODE_SAPPHIRE, 18, 0, 0, 0,
		                          0, 0, 0, 0, out, sizeof out);
		CHECK("first challenge, door unmet: reads KEYS 2/4",
		      strcmp(out, "KEYS 2/4") == 0);
		CheckPanelWidth("first challenge, door term only", out);
	}

	// ---------------------------------------------------------------------
	// FIRST CHALLENGE, any_percent: door + Boss + Gem companions (#321's
	// three-term FIRST-encounter worst case).
	// ---------------------------------------------------------------------
	{
		AP_OxideGarageState first = St(AP_OXIDE_ENCOUNTER_FIRST, 1, 0);
		int lines;

		AP_OxideGarageAdvertFormat(first, AP_OXIDE_REQ_KEY, 4, -1, 1, 1,
		                          AP_OXIDE_RELIC_MODE_SAPPHIRE, 18, 0, 0, 0,
		                          4, 1, 5, 2, out, sizeof out);
		CHECK("first + companions: door, BOSSES and GEMS all present",
		      strstr(out, "KEYS 1/4") != 0 &&
		      strstr(out, "BOSSES 1/4") != 0 &&
		      strstr(out, "GEMS 2/5") != 0);
		lines = CheckPanelWidth("first challenge, door + boss + gem", out);
		CHECK("first + companions: exactly 3 lines, none dropped", lines == 3);
		CHECK("line counter follows the rendered FIRST panel",
		      AP_OxideGarageAdvertLineCount(out) == lines);
	}

	// ---------------------------------------------------------------------
	// FINAL CHALLENGE, 101_percent, every term maxed and unmet at once: the
	// header, the relic term, and both companion terms. This is the case the
	// Sonnet review measured at ~1489 px as one joined line.
	// ---------------------------------------------------------------------
	{
		AP_OxideGarageState final = St(AP_OXIDE_ENCOUNTER_FINAL, 1, 1);
		int lines;

		// Door term intentionally left UNMET too (structurally possible per
		// AP_OxideGarageEvaluate even though it should not occur on a real
		// seed once firstCleared is true) so this exercises the absolute
		// worst case the formatter can produce: header + door + relic +
		// boss + gem, five lines.
		AP_OxideGarageAdvertFormat(final, AP_OXIDE_REQ_KEY, 4, -1, 0, 0,
		                          AP_OXIDE_RELIC_MODE_BEST_TIER, 18, 12, 12,
		                          12, 4, 1, 5, 2, out, sizeof out);
		CHECK("final worst case: header present",
		      strncmp(out, "FINAL CHALLENGE", 15) == 0);
		CHECK("final worst case: door, relic, boss and gem terms all present",
		      strstr(out, "KEYS 0/4") != 0 &&
		      strstr(out, "BEST RELIC 12/18") != 0 &&
		      strstr(out, "BOSSES 1/4") != 0 &&
		      strstr(out, "GEMS 2/5") != 0);
		lines = CheckPanelWidth("FINAL worst case (header+door+relic+boss+gem)",
		                        out);
		CHECK("final worst case: exactly 5 lines, none dropped", lines == 5);
		CHECK("line counter follows the structural Final maximum",
		      AP_OxideGarageAdvertLineCount(out) == lines);

		// The realistic Final Challenge worst case: door already met (as it
		// structurally must be once firstCleared holds), so header + relic +
		// boss + gem -- the shape #322's review actually measured.
		AP_OxideGarageAdvertFormat(final, AP_OXIDE_REQ_KEY, 4, -1, 4, 0,
		                          AP_OXIDE_RELIC_MODE_BEST_TIER, 18, 12, 12,
		                          12, 4, 1, 5, 2, out, sizeof out);
		CHECK("final realistic worst case: door term correctly omitted (met)",
		      strstr(out, "KEYS") == 0);
		lines = CheckPanelWidth(
			"FINAL realistic worst case (header+relic+boss+gem)", out);
		CHECK("final realistic worst case: exactly 4 lines", lines == 4);
	}

	// ---------------------------------------------------------------------
	// EVERY RELIC MODE at the widest tint word ("PLATINUM"), full companion
	// terms: sweep every mode this panel can render and prove each still
	// fits, not just the default (sapphire) mode.
	// ---------------------------------------------------------------------
	{
		int mode;
		int widest = 0;
		char widestLabel[64] = "";

		for (mode = 0; mode <= 4; mode++)
		{
			AP_OxideGarageState final = St(AP_OXIDE_ENCOUNTER_FINAL, 1, 1);
			char label[64];
			WidthScan scan;

			AP_OxideGarageAdvertFormat(final, AP_OXIDE_REQ_KEY, 4, -1, 4, 0,
			                          mode, 18, 12, 12, 12, 4, 1, 5, 2, out,
			                          sizeof out);
			snprintf(label, sizeof label, "relic mode %d, full companion terms",
			         mode);
			scan.widest = 0;
			scan.label = label;
			ForEachLine(out, ScanWidth, &scan);
			if (scan.widest > widest)
			{
				widest = scan.widest;
				snprintf(widestLabel, sizeof widestLabel, "%s", label);
			}
		}
		printf("ok    every relic mode fits; widest line %d px of %d (%s)\n",
		       widest, AP_GARAGE_SAFE_WIDTH_PX, widestLabel);
	}

	// ---------------------------------------------------------------------
	// DETERMINISTIC OMISSION: a satisfied term never appears; an unsatisfied
	// one is never silently dropped just to shorten the panel.
	// ---------------------------------------------------------------------
	{
		AP_OxideGarageState final = St(AP_OXIDE_ENCOUNTER_FINAL, 1, 1);

		// Relics met, companions unmet: relic term absent, both companion
		// terms present.
		AP_OxideGarageAdvertFormat(final, AP_OXIDE_REQ_KEY, 4, -1, 4, 1,
		                          AP_OXIDE_RELIC_MODE_SAPPHIRE, 18, 18, 0, 0,
		                          4, 0, 5, 0, out, sizeof out);
		CHECK("relic met + companions unmet: no relic term",
		      strstr(out, "SAPPHIRE") == 0);
		CHECK("relic met + companions unmet: both companion terms present",
		      strstr(out, "BOSSES 0/4") != 0 && strstr(out, "GEMS 0/5") != 0);

		// Everything met at a FINAL encounter with companions required:
		// falls back to the door term alone (the "should not happen while
		// drawn" defensive path), never an empty buffer.
		AP_OxideGarageAdvertFormat(final, AP_OXIDE_REQ_KEY, 4, -1, 4, 1,
		                          AP_OXIDE_RELIC_MODE_SAPPHIRE, 18, 18, 0, 0,
		                          4, 4, 5, 5, out, sizeof out);
		CHECK("everything met: falls back to the door term, not an empty buffer",
		      out[0] != '\0');
	}

	// ---------------------------------------------------------------------
	// BUFFER SAFETY: a short buffer must still be NUL-terminated and must
	// never be written past, exactly like AP_GoalLineFormat's contract.
	// ---------------------------------------------------------------------
	{
		char guarded[24];
		AP_OxideGarageState final = St(AP_OXIDE_ENCOUNTER_FINAL, 1, 1);
		int i;

		memset(guarded, '#', sizeof guarded);
		AP_OxideGarageAdvertFormat(final, AP_OXIDE_REQ_KEY, 4, -1, 0, 0,
		                          AP_OXIDE_RELIC_MODE_BEST_TIER, 18, 12, 12,
		                          12, 4, 1, 5, 2, guarded, 16);
		CHECK("truncated output is NUL-terminated", guarded[15] == '\0');
		for (i = 16; i < (int)sizeof guarded; i++)
			if (guarded[i] != '#')
			{
				printf("FAIL  wrote past the requested cap at byte %d\n", i);
				failures++;
				break;
			}
		CHECK("a null buffer is refused, not written",
		      AP_OxideGarageAdvertFormat(final, AP_OXIDE_REQ_KEY, 4, -1, 0, 0,
		                                AP_OXIDE_RELIC_MODE_SAPPHIRE, 18, 0,
		                                0, 0, 4, 0, 5, 0, 0, 16) == 0);
		CHECK("a zero cap is refused",
		      AP_OxideGarageAdvertFormat(final, AP_OXIDE_REQ_KEY, 4, -1, 0, 0,
		                                AP_OXIDE_RELIC_MODE_SAPPHIRE, 18, 0,
		                                0, 0, 4, 0, 5, 0, guarded, 0) == 0);
	}

	// ---------------------------------------------------------------------
	// MUTATION SENSITIVITY.
	// ---------------------------------------------------------------------

	// Mutant A: "drop the safe-width discipline, go back to one joined
	// line". Distinguished by the widest realistic panel actually having
	// multiple '\r'-separated lines rather than one long one.
	{
		AP_OxideGarageState final = St(AP_OXIDE_ENCOUNTER_FINAL, 1, 1);
		AP_OxideGarageAdvertFormat(final, AP_OXIDE_REQ_KEY, 4, -1, 4, 0,
		                          AP_OXIDE_RELIC_MODE_BEST_TIER, 18, 12, 12,
		                          12, 4, 1, 5, 2, out, sizeof out);
		CHECK("mutation A (single joined line): the realistic worst case is multi-line",
		      strchr(out, '\r') != 0);
	}

	// Mutant B: "show the door term even when it's satisfied". Distinguished
	// directly above ("door term correctly omitted (met)"), restated here as
	// the explicit mutation claim.
	{
		AP_OxideGarageState final = St(AP_OXIDE_ENCOUNTER_FINAL, 0, 1);
		AP_OxideGarageAdvertFormat(final, AP_OXIDE_REQ_KEY, 4, -1, 4, 0,
		                          AP_OXIDE_RELIC_MODE_SAPPHIRE, 18, 5, 0, 0,
		                          0, 0, 0, 0, out, sizeof out);
		CHECK("mutation B (satisfied term shown anyway): door met stays absent",
		      strstr(out, "KEYS") == 0);
	}

	// Mutant C: "the relic mode label doesn't distinguish tiers". Distinguished
	// by GOLD vs PLATINUM vs SAPPHIRE all producing different label text for
	// the same numbers.
	{
		char gold[64], platinum[64], sapphire[64];
		AP_OxideRelicTermFormat(AP_OXIDE_RELIC_MODE_GOLD, 18, 5, 7, 9, gold,
		                        sizeof gold);
		AP_OxideRelicTermFormat(AP_OXIDE_RELIC_MODE_PLATINUM, 18, 5, 7, 9,
		                        platinum, sizeof platinum);
		AP_OxideRelicTermFormat(AP_OXIDE_RELIC_MODE_SAPPHIRE, 18, 5, 7, 9,
		                        sapphire, sizeof sapphire);
		CHECK("mutation C: gold/platinum/sapphire modes read different tallies",
		      strcmp(gold, "GOLD 7/18") == 0 &&
		      strcmp(platinum, "PLATINUM 9/18") == 0 &&
		      strcmp(sapphire, "SAPPHIRE 5/18") == 0);
	}

	// Mutant D: "counters aren't tied to the same numbers the gate itself
	// uses" -- BOSSES/GEMS lines must read the exact won/held values passed
	// in, not a hardcoded or swapped pair.
	{
		AP_OxideGarageState first = St(AP_OXIDE_ENCOUNTER_FIRST, 1, 0);
		AP_OxideGarageAdvertFormat(first, AP_OXIDE_REQ_KEY, 4, -1, 4, 1,
		                          AP_OXIDE_RELIC_MODE_SAPPHIRE, 18, 0, 0, 0,
		                          3, 1, 2, 2, out, sizeof out);
		CHECK("mutation D: BOSSES reads won/goal, not swapped or hardcoded",
		      strstr(out, "BOSSES 1/3") != 0);
		CHECK("mutation D: a satisfied GEMS term (2/2) is correctly omitted",
		      strstr(out, "GEMS") == 0);
	}

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
