#ifndef AP_COOP_LOGIC_H
#define AP_COOP_LOGIC_H

/* The first slice seats two humans only in retail, standalone Trophy Races.
 * Keep this policy independent of the engine so every refusal is testable. */
enum AP_CoopRefusal
{
	AP_COOP_OK, AP_COOP_NEEDS_HUB, AP_COOP_IN_CUP, AP_COOP_NEEDS_PAD
};

static inline int AP_CoopToggleRefusal(int hub, int cup, int idle, int controllers, int enabling)
{
	if (cup) return AP_COOP_IN_CUP;
	if (!hub || !idle) return AP_COOP_NEEDS_HUB;
	if (enabling && controllers < 2) return AP_COOP_NEEDS_PAD;
	return AP_COOP_OK;
}

static inline int AP_CoopSeatsTwo(int enabled, int controllers, int adventure,
                                  int trophy, int retail, int cup)
{
	return enabled && controllers >= 2 && adventure && trophy && retail && !cup;
}

/* Unknown ranks never beat a known rank. Inputs and output are zero-based. */
static inline int AP_CoopBetterRank(int first, int second)
{
	if (first < 0) return second;
	if (second < 0) return first;
	return first < second ? first : second;
}

static inline int AP_CoopLenientWon(int first, int second)
{
	return AP_CoopBetterRank(first, second) == 0;
}

static inline int AP_CoopFinishReady(int coop, int humans, int finished)
{
	return finished >= (coop ? humans : humans - 1);
}

#endif
