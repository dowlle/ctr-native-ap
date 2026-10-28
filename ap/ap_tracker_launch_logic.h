#ifndef AP_TRACKER_LAUNCH_LOGIC_H
#define AP_TRACKER_LAUNCH_LOGIC_H

// A start from the in-game tracker (#375, 0.3.0). Freestanding so
// tools/test-pad-activity.py pins it out of engine.
//
// The tracker never loads a race. It records which physical pad or garage
// the player chose, resumes the hub, loads the pad's hub first when the
// player is in another one, and then lets that pad or garage run its own
// entry as if the kart had arrived there: same gates, same menu (the chosen
// mode answers it), same cup setup, boss intro, custom loading and hub
// return. If the pad does not take the kart within a short time, or the
// player ends up anywhere else, the start is dropped and the kart stays
// where it is.

enum
{
	AP_TL_IDLE = 0,
	AP_TL_LOADING = 1, // waiting for a safe hub, the pad's hub load, or both
	AP_TL_ARRIVE = 2,  // in the hub: the pad or garage may take the kart
	AP_TL_TAKEN = 3    // the pad holds the kart; its menu takes `mode`
};

enum
{
	AP_TL_BEGIN_REFUSED = 0,
	AP_TL_BEGIN_HERE = 1,    // same hub: resume, the pad takes the kart
	AP_TL_BEGIN_LOAD_HUB = 2 // load the pad's hub first, once the hub is safe
};

enum
{
	AP_TL_DROP_NONE = 0,
	AP_TL_DROP_TIMEOUT = 1,  // the pad did not take the kart
	AP_TL_DROP_ELSEWHERE = 2, // a different level is running
	AP_TL_DROP_GATE = 3,     // the shared gate closed on the way
	AP_TL_DROP_REFUSED = 4,  // the pad released the kart
	AP_TL_DROP_MODE = 5      // the pad does not offer the chosen mode now
};

// Frames the arriving pad gets to take the kart (one second at 60 Hz). The
// pad takes it on its first tick in practice; this only bounds a stuck start.
#define AP_TL_ARRIVE_FRAMES 60
// Frames a pad may hold the kart before its menu asks for the mode (it asks
// on warp frame 61). Bounds a pad that released the kart on its own route.
#define AP_TL_TAKEN_FRAMES 240
// Frames a whole start may take, waiting for a safe hub, the hub load and the
// pad, before it is dropped. Also the bound on a hub load.
#define AP_TL_LOAD_FRAMES 1800

typedef struct
{
	int stage;
	int hub;      // 0..4, levelID - GEM_STONE_VALLEY
	int physical; // physical pad, -1 for a garage
	int dest;     // the pad's destination, -1 for a garage
	int activity; // AP_PADACT_*
	int mode;     // the pad menu's answer, -1 when the pad asks nothing
	int frames;
	int total;    // frames since the start began, any stage
	int needLoad; // the hub load is still to be requested
	int drop;     // last drop reason, for the log
} ap_tl_request;

static inline void AP_TLReset(ap_tl_request *r)
{
	r->stage = AP_TL_IDLE;
	r->hub = r->physical = r->dest = r->activity = r->mode = -1;
	r->frames = r->total = r->needLoad = 0;
}

static inline int AP_TLBegin(ap_tl_request *r, int currentHub, int hub, int physical, int dest,
                             int activity, int mode)
{
	if (r->stage != AP_TL_IDLE || hub < 0 || hub > 4 || currentHub < 0 || currentHub > 4)
		return AP_TL_BEGIN_REFUSED;
	r->hub = hub;
	r->physical = physical;
	r->dest = dest;
	r->activity = activity;
	r->mode = mode;
	r->frames = r->total = 0;
	r->drop = AP_TL_DROP_NONE;
	// Even in the same hub the start waits in LOADING until the hub is safe
	// for a request (the pause menu closes over several frames).
	r->stage = AP_TL_LOADING;
	r->needLoad = hub != currentHub;
	return hub == currentHub ? AP_TL_BEGIN_HERE : AP_TL_BEGIN_LOAD_HUB;
}

static inline void AP_TLDrop(ap_tl_request *r, int why)
{
	AP_TLReset(r);
	r->drop = why;
}

// Once per frame. `levelHub` is the running hub (0..4) or -1 for any other
// level; `loading` is a level load in flight or a hub that is not yet safe
// for a request (pause menu still closing, a cutscene, a hub swap); `gateOpen`
// is the shared gate's answer for the chosen row, asked again now. Returns a
// drop reason, or AP_TL_DROP_NONE while the start is alive or idle.
static inline int AP_TLFrame(ap_tl_request *r, int levelHub, int loading, int gateOpen)
{
	if (r->stage == AP_TL_IDLE)
		return AP_TL_DROP_NONE;
	if (r->stage != AP_TL_TAKEN && ++r->total > AP_TL_LOAD_FRAMES)
	{
		AP_TLDrop(r, AP_TL_DROP_TIMEOUT);
		return AP_TL_DROP_TIMEOUT;
	}
	if (r->stage == AP_TL_TAKEN)
	{
		if (!loading && ++r->frames > AP_TL_TAKEN_FRAMES)
		{
			AP_TLDrop(r, AP_TL_DROP_TIMEOUT);
			return AP_TL_DROP_TIMEOUT;
		}
		return AP_TL_DROP_NONE;
	}
	if (r->stage == AP_TL_LOADING)
	{
		if (!gateOpen && !loading)
		{
			AP_TLDrop(r, AP_TL_DROP_GATE);
			return AP_TL_DROP_GATE;
		}
		if (r->needLoad || loading || levelHub != r->hub)
			return AP_TL_DROP_NONE;
		r->stage = AP_TL_ARRIVE;
		r->frames = 0;
	}
	if (loading)
		return AP_TL_DROP_NONE;
	if (levelHub != r->hub)
	{
		AP_TLDrop(r, AP_TL_DROP_ELSEWHERE);
		return AP_TL_DROP_ELSEWHERE;
	}
	if (!gateOpen)
	{
		AP_TLDrop(r, AP_TL_DROP_GATE);
		return AP_TL_DROP_GATE;
	}
	if (++r->frames > AP_TL_ARRIVE_FRAMES)
	{
		AP_TLDrop(r, AP_TL_DROP_TIMEOUT);
		return AP_TL_DROP_TIMEOUT;
	}
	return AP_TL_DROP_NONE;
}

// The hub is safe for a request: ask for the pad's hub once. Returns 1 when
// the caller should request the load now.
static inline int AP_TLWantsLoad(ap_tl_request *r)
{
	if (r->stage != AP_TL_LOADING || !r->needLoad)
		return 0;
	r->needLoad = 0;
	return 1;
}

// The pad (physical, dest) asks whether the kart arrives at it this frame.
static inline int AP_TLPadArrives(const ap_tl_request *r, int levelHub, int physical, int dest)
{
	return r->stage == AP_TL_ARRIVE && r->physical >= 0 && levelHub == r->hub &&
	       r->physical == physical && r->dest == dest;
}

static inline int AP_TLGarageArrives(const ap_tl_request *r, int levelHub)
{
	return r->stage == AP_TL_ARRIVE && r->physical < 0 && levelHub == r->hub;
}

// The pad captured the kart. A pad without a menu is done with the start
// here; one with a menu keeps it until it asks for the mode.
static inline void AP_TLTaken(ap_tl_request *r)
{
	if (r->stage != AP_TL_ARRIVE)
		return;
	if (r->mode < 0)
		AP_TLReset(r);
	else
	{
		r->stage = AP_TL_TAKEN;
		r->frames = 0;
	}
}

// The pad's menu asks for the chosen mode. Returns it and ends the start, or
// -1 when no tracker start holds this pad.
static inline int AP_TLTakeMode(ap_tl_request *r, int physical, int dest)
{
	int mode;
	if (r->stage != AP_TL_TAKEN || r->physical != physical || r->dest != dest)
		return -1;
	mode = r->mode;
	AP_TLReset(r);
	return mode;
}

// Is `mode` one of the rows the pad's own menu built? A start whose mode the
// pad no longer offers is refused, never swapped for another mode.
static inline int AP_TLModeOffered(const int *modes, int count, int mode)
{
	int i;
	for (i = 0; i < count; i++)
		if (modes[i] == mode)
			return 1;
	return 0;
}

#endif
