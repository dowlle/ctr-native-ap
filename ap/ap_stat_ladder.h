#ifndef CTR_AP_STAT_LADDER_H
#define CTR_AP_STAT_LADDER_H

// Freestanding five-rank Progressive Stats ladder policy (#13).
// Four engine anchors are sorted weakest-first. The final rank continues the
// top observed step once: VERY HIGH = HIGH + (HIGH - MEDIUM). This is the
// frozen 0.2.0 balance, deliberately the smallest linear step above vanilla.
#define AP_STAT_LADDER_ANCHORS 4

static inline int AP_StatLadderValue(const int anchors[AP_STAT_LADDER_ANCHORS],
                                     int rank)
{
	int v[AP_STAT_LADDER_ANCHORS];
	int i, j, t;

	for (i = 0; i < AP_STAT_LADDER_ANCHORS; i++)
		v[i] = anchors[i];

	for (i = 1; i < AP_STAT_LADDER_ANCHORS; i++)
	{
		t = v[i];
		for (j = i - 1; j >= 0 && v[j] > t; j--)
			v[j + 1] = v[j];
		v[j + 1] = t;
	}

	if (rank < 0)
		rank = 0;
	if (rank < AP_STAT_LADDER_ANCHORS)
		return v[rank];
	return v[3] + (v[3] - v[2]);
}

#endif
