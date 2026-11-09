#ifndef AP_AI_DIFFICULTY_H
#define AP_AI_DIFFICULTY_H

// Shared by the title Options row and the adventure hub selector. Values are
// raw engine difficulty; zero retains the trophy/loss-based vanilla scaling.
static const int s_aiDiffValues[] = {0, 0x50, 0xA0, 0xF0, 0x140, 0x280};
static const char *s_aiDiffNames[] = {"VANILLA", "EASY", "MEDIUM", "HARD", "SUPER HARD", "ULTRA HARD"};
#define AI_DIFF_COUNT ((int)(sizeof(s_aiDiffValues) / sizeof(s_aiDiffValues[0])))

static inline int AiDiff_NearestIndex(int value)
{
	int best = 0;
	long long bestDist = -1;
	for (int i = 0; i < AI_DIFF_COUNT; i++)
	{
		long long d = (long long)value - s_aiDiffValues[i];
		if (d < 0) d = -d;
		if (bestDist < 0 || d < bestDist)
		{
			bestDist = d;
			best = i;
		}
	}
	return best;
}

static inline const char *AiDiff_Label(int value)
{
	return s_aiDiffNames[AiDiff_NearestIndex(value)];
}

static inline void AiDiff_Step(int *value, int dir)
{
	int i = AiDiff_NearestIndex(*value) + dir;
	if (i < 0) i = 0;
	if (i >= AI_DIFF_COUNT) i = AI_DIFF_COUNT - 1;
	*value = s_aiDiffValues[i];
}

#endif
