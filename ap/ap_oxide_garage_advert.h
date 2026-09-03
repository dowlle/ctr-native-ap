#ifndef AP_OXIDE_GARAGE_ADVERT_H
#define AP_OXIDE_GARAGE_ADVERT_H

#include <stdio.h>

#include "ap_oxide_encounter.h"

// ---------------------------------------------------------------------------
// Oxide's locked-garage advert panel (issue #321 implementation check 5),
// 2026-09-03 repair of the Sonnet review's garage-overflow finding.
//
// WHAT WAS WRONG. AP_OxideGateAdvert (ap_hooks.c) used to join every blocking
// term -- the shared door requirement, the Final Challenge relic threshold,
// the Boss and Gem companion counts -- into ONE string with " + " and hand it
// to AH_Garage.c, which drew it with the non-wrapping DecalFont_DrawLine into
// a 128-byte buffer. The worst case (Final Challenge naming plus a relic term
// plus Boss and Gem terms) measured roughly 1489 px against the ~460 px safe
// width the same pixel model gives the approved pause checklist (see
// ap_goal_line.h) -- worse than the advert this branch replaced, because it
// added a "Final Challenge " header and the relic phrase on top of what was
// already there.
//
// THE REPAIR. Write ONE TERM PER LINE in a compact "TINT NOUN have/need"
// panel shape -- "SAPPHIRE 12/18", "BOSSES 1/4", "GEMS 3/5" -- separated by
// '\r', the explicit-break character DecalFont_DrawMultiLine already
// recognises elsewhere in this codebase. Every term keeps the exact same
// BLOCKING predicate the pre-repair single-line version used (a satisfied
// term is omitted, never a blocking one dropped to save width), so the panel
// never claims fewer requirements than the door actually enforces.
//
// Pure, engine-free and allocation-free, exactly like ap_goal_line.h, so
// tools/test-oxide-garage-advert.c can drive every shape -- including the
// widest Final Challenge + relic + Boss + Gem combination -- and prove each
// rendered line against the safe width model without an engine, a socket or
// a seed. ap_hooks.c's AP_OxideGateAdvert is the thin glue that resolves the
// real ctr_cfg/AP_GateCount*/AP_ReqOwned values and calls
// AP_OxideGarageAdvertFormat below, the same split AP_GoalAdvert already uses
// over AP_GoalLineFormat.
// ---------------------------------------------------------------------------

// ctr_req.type values this panel understands (mirrors ap_seedcfg.h's ctr_req
// exactly; restated here as plain ints, not an include, so this header stays
// engine-free).
#define AP_OXIDE_REQ_NONE     0
#define AP_OXIDE_REQ_TROPHY   1
#define AP_OXIDE_REQ_KEY      2
#define AP_OXIDE_REQ_TOKEN    3
#define AP_OXIDE_REQ_RELIC    4
#define AP_OXIDE_REQ_GEM      5
#define AP_OXIDE_REQ_TOKEN_NC 6 // token, no colour tint
#define AP_OXIDE_REQ_ANYRELIC 7
#define AP_OXIDE_REQ_ANYGEM   8

// ctr_cfg.oxide_final_unlock relic-goal modes (mirrors AP_RelicGoalMet's
// table, restated here for the same engine-free reason).
#define AP_OXIDE_RELIC_MODE_SAPPHIRE   0
#define AP_OXIDE_RELIC_MODE_GOLD       1
#define AP_OXIDE_RELIC_MODE_PLATINUM   2
#define AP_OXIDE_RELIC_MODE_BEST_TIER  3
#define AP_OXIDE_RELIC_MODE_TOTAL      4

// Count the explicit panel lines consumed by DecalFont_DrawMultiLine. CTR's
// formatter treats '\r' as a hard line break. A non-empty panel therefore has
// one line plus one for every '\r'.
static inline int AP_OxideGarageAdvertLineCount(const char *panel)
{
	int lines = 0;

	if (panel == 0 || panel[0] == '\0')
		return 0;
	lines = 1;
	for (; *panel != '\0'; panel++)
		if (*panel == '\r')
			lines++;
	return lines;
}

// Keep the challenge title and its locked-door panel together as one block.
// `preferredTitleY` is retail's title position. The returned title Y preserves
// it when the block fits and otherwise shifts the whole block upward just far
// enough that the final panel line remains inside `viewportBottom`.
//
// The caller passes the actual region font metrics from
// data.font_charPixHeight, not duplicated layout guesses. This stays pure so
// the host harness can pin the NTSC-U values (FONT_BIG 17, FONT_SMALL 8) over
// every possible formatter line count.
static inline int AP_OxideGarageBlockTitleY(int viewportBottom,
	int preferredTitleY, int titleHeight, int panelLineHeight, int panelLines)
{
	int blockBottom;

	if (titleHeight < 0)
		titleHeight = 0;
	if (panelLineHeight < 0)
		panelLineHeight = 0;
	if (panelLines < 0)
		panelLines = 0;

	blockBottom = preferredTitleY + titleHeight + panelLineHeight * panelLines;
	if (blockBottom > viewportBottom)
		preferredTitleY -= blockBottom - viewportBottom;
	return preferredTitleY;
}

// Compact "<TINT ><NOUN> <owned>/<need>" line for one resolved requirement,
// e.g. "SAPPHIRE 12/18" or "KEYS 2/4". Returns 1 when a line was written, 0
// when the requirement resolves to nothing to say (type NONE or count <= 0),
// matching AP_ReqPhrase's/AP_ReqPhraseSatisfied's "nothing resolved" rule.
static inline int AP_OxideReqTermFormat(int type, int count, int colour,
                                        int owned, char *out, int cap)
{
	static const char *COLOURS[5] = {"RED", "GREEN", "BLUE", "YELLOW",
	                                 "PURPLE"};
	const char *noun;
	const char *tint = "";

	if (out == 0 || cap <= 0)
		return 0;
	if (type == AP_OXIDE_REQ_NONE || count <= 0)
		return 0;

	switch (type)
	{
	case AP_OXIDE_REQ_TROPHY:
		noun = "TROPHIES";
		break;
	case AP_OXIDE_REQ_KEY:
		noun = "KEYS";
		break;
	case AP_OXIDE_REQ_TOKEN:
		noun = "TOKENS";
		if (colour >= 0 && colour <= 4)
			tint = COLOURS[colour];
		break;
	case AP_OXIDE_REQ_TOKEN_NC:
		noun = "TOKENS";
		break;
	case AP_OXIDE_REQ_RELIC:
		noun = "RELICS";
		tint = (colour == 1) ? "GOLD" : (colour == 2) ? "PLATINUM" : "SAPPHIRE";
		break;
	case AP_OXIDE_REQ_GEM:
		noun = "GEMS";
		if (colour >= 0 && colour <= 4)
			tint = COLOURS[colour];
		break;
	case AP_OXIDE_REQ_ANYRELIC:
		noun = "RELICS";
		break;
	case AP_OXIDE_REQ_ANYGEM:
		noun = "GEMS";
		break;
	default:
		return 0;
	}

	if (tint[0] != '\0')
		snprintf(out, (size_t)cap, "%s %s %d/%d", tint, noun, owned, count);
	else
		snprintf(out, (size_t)cap, "%s %d/%d", noun, owned, count);
	return 1;
}

// Is a resolved requirement already satisfied? Same "nothing resolved counts
// as satisfied" rule as AP_ReqPhraseSatisfied, so a term can never be listed
// as blocking while unresolved.
static inline int AP_OxideReqTermSatisfied(int type, int count, int owned)
{
	if (type == AP_OXIDE_REQ_NONE || count <= 0)
		return 1;
	return owned >= count;
}

// Compact "<TINT> <owned>/<need>" line for the Final Challenge relic goal,
// e.g. "SAPPHIRE 12/18". Mirrors AP_RelicGoalMet's mode table case for case
// -- including which tally each mode compares -- so the panel and
// AP_OxideFinalOpen cannot disagree about what is being counted. `n` is
// already resolved (the vanilla-fallback-of-18 belongs to the caller, exactly
// as it did in the pre-repair prose version).
static inline int AP_OxideRelicTermFormat(int mode, int n, int sapphire,
                                          int gold, int platinum, char *out,
                                          int cap)
{
	int best = sapphire;

	if (out == 0 || cap <= 0)
		return 0;
	if (gold > best)
		best = gold;
	if (platinum > best)
		best = platinum;

	switch (mode)
	{
	case AP_OXIDE_RELIC_MODE_GOLD:
		snprintf(out, (size_t)cap, "GOLD %d/%d", gold, n);
		break;
	case AP_OXIDE_RELIC_MODE_PLATINUM:
		snprintf(out, (size_t)cap, "PLATINUM %d/%d", platinum, n);
		break;
	case AP_OXIDE_RELIC_MODE_BEST_TIER:
		snprintf(out, (size_t)cap, "BEST RELIC %d/%d", best, n);
		break;
	case AP_OXIDE_RELIC_MODE_TOTAL:
		snprintf(out, (size_t)cap, "RELICS %d/%d", sapphire + gold + platinum, n);
		break;
	case AP_OXIDE_RELIC_MODE_SAPPHIRE:
	default:
		snprintf(out, (size_t)cap, "SAPPHIRE %d/%d", sapphire, n);
		break;
	}
	return 1;
}

// Appends `text` at `used` within a `cap`-sized buffer and returns the new
// cursor, clamped so a truncated append cannot walk past the end. Local twin
// of ap_hooks.c's AP_AdvertAppend so this header stays free of that file's
// engine-facing declarations.
static inline int AP_OxideAdvertAppend(char *out, int cap, int used,
                                       const char *text)
{
	int add;

	if (out == 0 || cap <= 0 || used >= cap - 1)
		return (cap > 0) ? cap - 1 : 0;
	add = snprintf(out + used, (size_t)(cap - used), "%s", text);
	if (add < 0)
		return used;
	used += add;
	return (used > cap - 1) ? cap - 1 : used;
}

// Builds the full multi-line garage panel into `out` (lines separated by
// '\r') and returns 1, or returns 0 only when the caller passed a null/empty
// buffer. `st` is the SAME AP_OxideGarageEvaluate decision the gate itself
// uses, so the panel and the door it describes cannot disagree.
//
// Every argument is a plain already-resolved value (door requirement type/
// count/colour/owned, the relic goal's mode/threshold/tallies, and the
// companion Boss/Gem goal/won counts) -- the same "gather real numbers, hand
// them to pure formatting logic" split ap_goal_line.h's AP_GoalLineFormat
// already uses, which is what lets this run without ctr_cfg, AP_GateCount or
// any other engine symbol.
static inline int AP_OxideGarageAdvertFormat(
	AP_OxideGarageState st, int doorType, int doorCount, int doorColour,
	int doorOwned, int finalRelicMet, int relicMode, int relicN, int sapphire,
	int gold, int platinum, int goalBosses, int bossesWon, int goalGems,
	int gemsHeld, char *out, int cap)
{
	char part[64]; // headroom above any realistic term's actual width; see
	               // tools/test-oxide-garage-advert.c for the proven widths
	int used = 0;
	int lines = 0;

	if (out == 0 || cap <= 0)
		return 0;

	if (st.encounter == AP_OXIDE_ENCOUNTER_NONE)
	{
		snprintf(out, (size_t)cap, "N. Oxide's garage stays shut");
		return 1;
	}

	out[0] = '\0';

	if (st.encounter == AP_OXIDE_ENCOUNTER_FINAL)
	{
		used = AP_OxideAdvertAppend(out, cap, used, "FINAL CHALLENGE");
		lines = 1;
	}

	if (!AP_OxideReqTermSatisfied(doorType, doorCount, doorOwned) &&
	    AP_OxideReqTermFormat(doorType, doorCount, doorColour, doorOwned,
	                          part, (int)sizeof part))
	{
		if (lines)
			used = AP_OxideAdvertAppend(out, cap, used, "\r");
		used = AP_OxideAdvertAppend(out, cap, used, part);
		lines++;
	}

	if (st.needsRelics && !finalRelicMet &&
	    AP_OxideRelicTermFormat(relicMode, relicN, sapphire, gold, platinum,
	                            part, (int)sizeof part))
	{
		if (lines)
			used = AP_OxideAdvertAppend(out, cap, used, "\r");
		used = AP_OxideAdvertAppend(out, cap, used, part);
		lines++;
	}

	if (st.needsCompanions && goalBosses > 0 && bossesWon < goalBosses)
	{
		snprintf(part, sizeof part, "BOSSES %d/%d", bossesWon, goalBosses);
		if (lines)
			used = AP_OxideAdvertAppend(out, cap, used, "\r");
		used = AP_OxideAdvertAppend(out, cap, used, part);
		lines++;
	}

	if (st.needsCompanions && goalGems > 0 && gemsHeld < goalGems)
	{
		snprintf(part, sizeof part, "GEMS %d/%d", gemsHeld, goalGems);
		if (lines)
			used = AP_OxideAdvertAppend(out, cap, used, "\r");
		used = AP_OxideAdvertAppend(out, cap, used, part);
		lines++;
	}

	// Nothing read as blocking. The gate and this formatter share their
	// inputs, so that means the door is open and the caller is not drawing
	// this anyway; fall back to the plain door term rather than an empty
	// panel.
	if (lines == 0)
		return AP_OxideReqTermFormat(doorType, doorCount, doorColour,
		                             doorOwned, out, cap);

	return 1;
}

#endif // AP_OXIDE_GARAGE_ADVERT_H
