#ifndef AP_EDITSTAT_RANK_LOGIC_H
#define AP_EDITSTAT_RANK_LOGIC_H

// ---------------------------------------------------------------------------
// Editable stats as a choice between the rank values (owner ruling, 2026-10-29).
//
// Per stat the player picks one of the five rank values (VERY LOW, LOW, MEDIUM,
// HIGH, VERY HIGH), the same ladder Progressive Stats drives (ap_stat_ladder.h).
// There are no free values. The package on the wire is unchanged: a delta over
// the racer's vanilla value, so a stored package from an older client still
// reads, and snaps to the nearest rank value when it is used or edited.
//
// Freestanding on purpose: tools/test-editstat-rank.c compiles exactly the code
// production runs.
// ---------------------------------------------------------------------------

#define AP_EDITRANK_COUNT 5

// Index of the rank value nearest to `current`. A tie goes to the lower rank,
// and equal rank values resolve to the lowest of them.
static inline int AP_EditRank_Nearest(const int values[AP_EDITRANK_COUNT], int current)
{
	int best = 0, bestDist = -1, i;

	for (i = 0; i < AP_EDITRANK_COUNT; i++)
	{
		int d = current - values[i];
		if (d < 0)
			d = -d;
		if (bestDist < 0 || d < bestDist)
		{
			best = i;
			bestDist = d;
		}
	}
	return best;
}

// The value a stat takes after one press: the nearest rank value moved one
// distinct rank in `dir` (+1 or -1), or that nearest value when already at the
// bottom or the top. Rank values are ascending; a flat stretch counts as one.
static inline int AP_EditRank_Step(const int values[AP_EDITRANK_COUNT], int current, int dir)
{
	int at = AP_EditRank_Nearest(values, current);
	int i = at;

	if (dir > 0)
	{
		while (i < AP_EDITRANK_COUNT - 1 && values[i] <= values[at])
			i++;
		return values[i] > values[at] ? values[i] : values[at];
	}
	if (dir < 0)
	{
		while (i > 0 && values[i] >= values[at])
			i--;
		return values[i] < values[at] ? values[i] : values[at];
	}
	return values[at];
}

// What a stat is driven at: its value snapped to the nearest rank value.
static inline int AP_EditRank_Snap(const int values[AP_EDITRANK_COUNT], int current)
{
	return values[AP_EditRank_Nearest(values, current)];
}

#endif
