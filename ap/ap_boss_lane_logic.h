#ifndef AP_BOSS_LANE_LOGIC_H
#define AP_BOSS_LANE_LOGIC_H

// ============================================================================
// Where a boss lands when it changes lane on a custom track.
//
// FREESTANDING, like ap_navrec_identity_logic.h: no engine headers, no
// config, no logging. game/BOTS.c calls it from the boss lane switch;
// tools/test-boss-lane-ahead.c drives the same functions over synthetic lanes
// and, when the files are present locally, over real track nav data.
//
// The boss asks for a lane change on a timer (PickupBots.c) and takes it at
// the next nav frame whose pathChangeOpcode targets the wanted lane: its nav
// frame becomes NavFrame[target lane][opcode & 0x3ff]. Retail boss tracks
// point that link two to five frames ahead of the boss (never behind it,
// except one lap-wrap link on Papu's Pyramid). Some custom tracks point it at
// the same frame index, or one or two behind: Out of Time has 441 of its 907
// boss switch links landing behind the boss (A2-21). The boss then steers to
// a target beside or behind itself at every lane change.
//
// Ruling (2026-11-05): on custom tracks, when the boss switches lane it
// takes the first frame of the target lane at or after the linked one that
// lies ahead of its own position. Retail tracks keep the linked frame
// unchanged: AP_BossLane_Applies is false for every retail load, so the
// engine never calls the search there.
// ============================================================================

// Does the forward correction apply to this load? navrecOutcome is the
// AP_NAVREC_LOAD_* answer BOTS.c already settles for the load (0 = retail).
// A non-retail answer means custom bytes are served, or the load was admitted
// as the Cortex Vortex pad track (an intent, which is not a boss race). It is
// the same "is this the slot's own retail track" decision the recorded-lane
// policy uses, so a retail boss track always answers 0.
static inline int AP_BossLane_Applies(int navrecOutcome)
{
	return navrecOutcome != 0;
}

// How far past the linked frame the search may step. Retail links sit two to
// five frames ahead and Out of Time needs at most two steps. Without a bound
// the search could carry a boss that sits near the centre of a tight bend
// most of the way round it; a link with no frame ahead within this many steps
// is left as the track gives it, which is today's behaviour.
#define AP_BOSS_LANE_MAX_STEP 16

// Frame i of a lane laid out like struct NavFrame: s16 x at +0, s16 z at +4,
// stride bytes apart.
static inline void AP_BossLane_FrameXZ(const void *frames, int stride, int i, int *x, int *z)
{
	const unsigned char *p = (const unsigned char *)frames + (long)i * stride;
	*x = *(const short *)(p + 0);
	*z = *(const short *)(p + 4);
}

// Is frame j of the lane still ahead of the boss at (bx, bz)? Measured
// along the lane's own direction at j (j to j+1, wrapping at numPoints): the
// boss has not yet passed frame j on that lane. The boss's own heading is not
// used, because at a sharp turn the old lane and the new one point different
// ways and the heading would call frames ahead on the new lane "behind".
static inline int AP_BossLane_FrameAhead(const void *frames, int stride, int numPoints, int j, int bx, int bz)
{
	int jx, jz, kx, kz;
	AP_BossLane_FrameXZ(frames, stride, j, &jx, &jz);
	AP_BossLane_FrameXZ(frames, stride, (j + 1) % numPoints, &kx, &kz);
	long long dot = (long long)(jx - bx) * (kx - jx) + (long long)(jz - bz) * (kz - jz);
	return dot > 0;
}

// The frame index the boss should take on the target lane: the linked frame
// if it is already ahead of the boss, else the first frame after it that is,
// within AP_BOSS_LANE_MAX_STEP steps. No frame ahead in that window, or input
// out of shape, returns the linked frame unchanged.
static inline int AP_BossLane_AheadIndex(const void *frames, int stride, int numPoints, int linked, int bx, int bz)
{
	if (frames == 0 || stride <= 0 || numPoints < 2 || linked < 0 || linked >= numPoints)
		return linked;

	for (int step = 0; step <= AP_BOSS_LANE_MAX_STEP && step < numPoints; step++)
	{
		int j = (linked + step) % numPoints;
		if (AP_BossLane_FrameAhead(frames, stride, numPoints, j, bx, bz))
			return j;
	}
	return linked;
}

#endif // AP_BOSS_LANE_LOGIC_H
