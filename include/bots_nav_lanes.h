#ifndef CTR_BOTS_NAV_LANES_H
#define CTR_BOTS_NAV_LANES_H

#define BOTS_NAV_PATH_COUNT 3
#if defined(CTR_AP) || defined(CTR_AI_LAP_RECORDER)
#define BOTS_NAV_LANE_MAX 10
#else
#define BOTS_NAV_LANE_MAX BOTS_NAV_PATH_COUNT
#endif

// The LEV supplies three paths. A missing recorded lane tries the kart's
// original LEV path before cycling through the other two.
static inline int BOTS_NavFallbackPath(int lane, int originalPath, int offset)
{
	if (lane >= BOTS_NAV_PATH_COUNT)
		lane = originalPath;
	if (lane < 0 || lane >= BOTS_NAV_PATH_COUNT)
		lane = 0;
	return (lane + BOTS_NAV_PATH_COUNT - offset) % BOTS_NAV_PATH_COUNT;
}

#endif
