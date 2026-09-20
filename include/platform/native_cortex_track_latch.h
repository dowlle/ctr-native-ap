#ifndef NATIVE_CORTEX_TRACK_LATCH_H
#define NATIVE_CORTEX_TRACK_LATCH_H

// Cortex Vortex pad-track serving state (schema 15), freestanding.
//
// Cortex Vortex as a pad track loads host LevelID 13, the same engine level as
// Oxide Station. Nothing in the level load says which of the two is meant, so
// the entry sites that resolve the virtual destination 110 (a warp pad or a Gem
// Cup leg) SELECT the next load explicitly, and every level request consumes
// that selection. The resulting "current" answer is the native serving state
// the contract names: independent of bossID and of the Oxide 2 venue.
//
// Rules, in the order MainRaceTrack_RequestLoad applies them:
//   1. An explicit selection wins: the request serves Cortex Vortex exactly
//      when the selection said so AND the level is 13.
//   2. With no selection, a request for any level other than 13 ends the
//      state. That covers hub returns, menus, the garage and every other track.
//   3. With no selection, a request for level 13 keeps the state. That is the
//      retry path (QueueLoadTrack, character swap reloads) re-requesting the
//      level it is already on.
//   4. A repeat of the same request with no selection changes nothing, so a
//      caller that requests the hub twice cannot erase `prevServed`.
// `prevServed` answers "was the level being left Cortex Vortex", which the hub
// needs to put the kart back at the pad hosting 110 rather than at the pad
// hosting Oxide Station.
//
// Oxide Station entry paths (its own pad, a Gem Cup leg of 13, Oxide 1 and
// Oxide 2) either select 0 explicitly or arrive from a hub load that already
// ended the state under rule 2. The Oxide boss races are also excluded by the
// identity predicate below, which refuses while the Adventure Boss flag is set.

struct CortexTrackLatch
{
	int pending;       // -1 no selection, 0 retail, 1 Cortex Vortex
	int current;       // the load in flight / on screen serves Cortex Vortex
	int prevServed;    // the level being left served Cortex Vortex
	int lastRequested; // last requested level, for rule 4
};

#define CORTEX_TRACK_HOST_LEVEL 13
#define CORTEX_TRACK_DEST       110

static inline void CortexTrackLatch_Reset(struct CortexTrackLatch *l)
{
	l->pending = -1;
	l->current = 0;
	l->prevServed = 0;
	l->lastRequested = -1;
}

static inline void CortexTrackLatch_Select(struct CortexTrackLatch *l, int cortex)
{
	l->pending = cortex ? 1 : 0;
}

static inline void CortexTrackLatch_OnRequest(struct CortexTrackLatch *l, int levelID)
{
	if (l->pending < 0 && levelID == l->lastRequested)
		return; // rule 4
	l->prevServed = l->current;
	if (l->pending >= 0)
		l->current = (l->pending == 1 && levelID == CORTEX_TRACK_HOST_LEVEL);
	else if (levelID != CORTEX_TRACK_HOST_LEVEL)
		l->current = 0;
	l->pending = -1;
	l->lastRequested = levelID;
}

// THE identity predicate. Every LevelID-13 consumer asks this before reading an
// Oxide Station identity. It does not depend on the content being verified: a
// race that the entry site admitted as Cortex Vortex must never credit Oxide
// Station, even if the bytes were refused later.
static inline int CortexTrackLatch_Identity(const struct CortexTrackLatch *l, int levelID,
                                            int adventureBossActive)
{
	return l->current && levelID == CORTEX_TRACK_HOST_LEVEL && !adventureBossActive;
}

// Destination -> engine level for a load request, and the selection to make.
static inline int CortexTrackLatch_ResolveDest(int dest, int *outCortex)
{
	if (outCortex)
		*outCortex = dest == CORTEX_TRACK_DEST;
	return dest == CORTEX_TRACK_DEST ? CORTEX_TRACK_HOST_LEVEL : dest;
}

#endif // NATIVE_CORTEX_TRACK_LATCH_H
