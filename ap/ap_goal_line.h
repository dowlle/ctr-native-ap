#ifndef AP_GOAL_LINE_H
#define AP_GOAL_LINE_H

#include "ap_oxide_encounter.h"

// ---------------------------------------------------------------------------
// The pause menu's composed-goal readout (issue #322).
//
// WHAT WAS WRONG. AP_GoalAdvert used to join as many as three conditions into
// one prose sentence -- "Goal: beat N. Oxide's Final Challenge (not yet) and
// win 4 of 4 boss races (have 0) and hold 5 of 5 Gems (have 0)" -- and
// AH_Pause.c drew the whole buffer once with DecalFont_DrawLine, which does
// not wrap. That longest shape is 112 characters, roughly 1444 px in
// FONT_SMALL, against about 460 px of safe pause-page width. A tester on the
// 1 September Alpha 7 stream could not read it and had to guess the goal from
// the visible fragment.
//
// THE APPROVED SHAPE. A compact static checklist, one line, no wrap, no
// scroll:
//
//     GOAL: OXIDE 2 - BOSS 3/4 - GEM 4/5
//
// That maximum shape measures about 436 px, leaving roughly 24 px of margin.
// Both alternatives were ruled out: wrapping the prose needs four visual
// lines, and a marquee would make the longest text travel about 984 px past
// the viewport (about 16 seconds per pass at 1 px/frame) while being harder
// to scan than a static line.
//
// LABELS AND OMISSION RULES (issue #322, exactly as ruled):
//   * OXIDE   for the any_percent goal;
//   * OXIDE 2 for the 101_percent goal;
//   * no Oxide segment at all for `optional`/`none` and for `disabled` --
//     Oxide is not a completion condition in either, so it is not on the
//     checklist;
//   * BOSS / GEM segments omitted when their configured requirement is 0;
//   * counters are progress over the CONFIGURED requirement, CAPPED at it, so
//     three wins in a two-boss goal read `BOSS 2/2` rather than `BOSS 3/2`.
//
// WHAT THIS DELIBERATELY DOES NOT SHOW. Which Oxide encounter is currently
// available, and why the garage is shut. That is the locked-garage advert's
// job (AP_BossGateAdvert), and keeping the two apart is the point: a Final-goal
// seed can correctly show unfinished Boss/Gem arms while Oxide 1 is
// nevertheless available, so a completion checklist must not imply that every
// unfinished line blocks the next visit. Under the approved encounter flow the
// enabled arms gate the selected finale, so winning that encounter completes
// the composed goal immediately and no separate Oxide status is needed.
//
// Pure, engine-free and allocation-free so tools/test-goal-line.c can drive
// every shape, including the width budget, without an engine or a seed.
// ---------------------------------------------------------------------------

// Appends `text` at *pos within a `cap`-sized buffer, always NUL-terminating.
// Hand-rolled rather than snprintf so this header stays freestanding and so
// truncation is explicit rather than a return-value convention.
static inline void AP_GoalLinePut(char *out, int cap, int *pos, const char *text)
{
	int i = *pos;
	while (*text != '\0' && i < cap - 1)
		out[i++] = *text++;
	if (cap > 0)
		out[i] = '\0';
	*pos = i;
}

// Writes a non-negative integer. Counts here are 0..54, so a small fixed
// buffer is sufficient.
static inline void AP_GoalLinePutInt(char *out, int cap, int *pos, int value)
{
	char digits[12];
	int n = 0;

	if (value < 0)
		value = 0;
	do
	{
		digits[n++] = (char)('0' + (value % 10));
		value /= 10;
	} while (value > 0 && n < (int)sizeof digits);
	while (n > 0 && *pos < cap - 1)
		out[(*pos)++] = digits[--n];
	if (cap > 0)
		out[*pos] = '\0';
}

// `have`/`need` counter segment, capped at `need`.
static inline void AP_GoalLinePutCounter(char *out, int cap, int *pos,
                                         const char *label, int have, int need)
{
	AP_GoalLinePut(out, cap, pos, label);
	AP_GoalLinePut(out, cap, pos, " ");
	AP_GoalLinePutInt(out, cap, pos, have > need ? need : have);
	AP_GoalLinePut(out, cap, pos, "/");
	AP_GoalLinePutInt(out, cap, pos, need);
}

// Builds the compact goal line into `out`. Returns 1 when at least one segment
// was written, 0 when the seed has no active goal arm at all (which the
// apworld's generate_early rejects, so it should not happen on a real seed --
// returning 0 leaves the pause page drawing nothing rather than a bare
// "GOAL:").
static inline int AP_GoalLineFormat(char *out, int cap, int goalOxide,
                                    int goalBosses, int bossesWon,
                                    int goalGems, int gemsHeld)
{
	const char *oxide = 0;
	int pos = 0;
	int written = 0;

	if (out == 0 || cap <= 0)
		return 0;
	out[0] = '\0';

	if (goalOxide == AP_OXIDE_GOAL_ANY)
		oxide = "OXIDE";
	else if (goalOxide == AP_OXIDE_GOAL_FINAL)
		oxide = "OXIDE 2";

	if (oxide == 0 && goalBosses <= 0 && goalGems <= 0)
		return 0;

	AP_GoalLinePut(out, cap, &pos, "GOAL: ");

	if (oxide != 0)
	{
		AP_GoalLinePut(out, cap, &pos, oxide);
		written = 1;
	}
	if (goalBosses > 0)
	{
		if (written)
			AP_GoalLinePut(out, cap, &pos, " - ");
		AP_GoalLinePutCounter(out, cap, &pos, "BOSS", bossesWon, goalBosses);
		written = 1;
	}
	if (goalGems > 0)
	{
		if (written)
			AP_GoalLinePut(out, cap, &pos, " - ");
		AP_GoalLinePutCounter(out, cap, &pos, "GEM", gemsHeld, goalGems);
		written = 1;
	}

	return written;
}

#endif // AP_GOAL_LINE_H
