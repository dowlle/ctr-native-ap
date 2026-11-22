#ifndef AP_COOP_LOGIC_H
#define AP_COOP_LOGIC_H

/* Two seats require a supported retail event outside a cup. */
static inline int AP_CoopSeatsTwo(int enabled, int controllers, int adventure,
                                  int supportedEvent, int retail, int cup)
{
	return enabled && controllers >= 2 && adventure && supportedEvent && retail && !cup;
}

/* Unknown ranks never beat a known rank. Inputs and output are zero-based. */
static inline int AP_CoopBetterRank(int first, int second)
{
	if (first < 0) return second;
	if (second < 0) return first;
	return first < second ? first : second;
}

static inline int AP_CoopWorseRank(int first, int second)
{
	if (first < 0 || second < 0) return -1;
	return first > second ? first : second;
}

static inline int AP_CoopHardWon(int first, int second)
{
	return (first == 0 && second == 1) || (first == 1 && second == 0);
}

static inline int AP_CoopFinishReady(int coop, int humans, int finished)
{
	return finished >= (coop ? humans : humans - 1);
}

#endif
