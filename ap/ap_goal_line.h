#ifndef AP_GOAL_LINE_H
#define AP_GOAL_LINE_H

#include <stdio.h>
#include "ap_oxide_encounter.h"

enum { AP_GOAL_MAX_LINES = 4, AP_GOAL_LINE_CAP = 64 };
typedef char AP_GoalLines[AP_GOAL_MAX_LINES][AP_GOAL_LINE_CAP];

static inline void AP_GoalCounter(char *line, const char *label, long long have, int need)
{
	int shown = have < 0 ? 0 : have > need ? need : (int)have;
	snprintf(line, AP_GOAL_LINE_CAP, "%s %d/%d", label, shown, need);
}

// The completion checklist shared by the hub pause and tracker.
static inline int AP_GoalFormatLines(AP_GoalLines lines, int oxide, int firstWon, int finalWon,
                                    int bosses, int bossesWon, int gems, int gemsHeld,
                                    int wumpa, long long banked)
{
	int n = 0;
	if (oxide == AP_OXIDE_GOAL_ANY)
		AP_GoalCounter(lines[n++], "Oxide Challenge", firstWon, 1);
	else if (oxide == AP_OXIDE_GOAL_FINAL)
		AP_GoalCounter(lines[n++], "Oxide Final", finalWon, 1);
	if (bosses > 0)
		AP_GoalCounter(lines[n++], "Boss wins", bossesWon, bosses);
	if (gems > 0)
		AP_GoalCounter(lines[n++], "Gem colours", gemsHeld, gems);
	if (wumpa > 0)
		AP_GoalCounter(lines[n++], "Wumpa", banked, wumpa);
	return n;
}

int AP_GoalAdvertLines(AP_GoalLines lines);

#endif
