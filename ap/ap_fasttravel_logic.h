#ifndef AP_FASTTRAVEL_LOGIC_H
#define AP_FASTTRAVEL_LOGIC_H

// Hub fast travel (A3-16, 0.3.0-alpha3). Freestanding so
// tools/test-fast-travel.py pins it out of engine.
//
// A TRAVEL row on the adventure hub pause menu opens a five-row selector, one
// row per hub. A hub the player could not walk to stays locked (grey, womp):
// the rule is the hub's door Keys, the same table the tracker activity list
// uses, so the menu, the tracker and the apworld's region logic never
// disagree. A confirmed row resumes the hub, reloads the destination hub
// (the same-hub reload the character swap already uses, so every destination
// takes one identical path) and the level's own birth then places the kart.
//
// The landing anchor is the hub's save screen: the SpawnType2 anchor and the
// STATIC_SAVEOBJ instance retail already keeps per hub. The kart lands
// AP_FT_LAND_DIST world units out along the screen's approach direction, the
// trigger instance's local X axis, which is where AH_SaveObj_ThTick aims the
// save camera and therefore open floor by level design. The screen wakes at
// sqrt(0x8ffff) = 767 world units (3D distance, kart instance to trigger),
// so 1024 out on the ground plane clears it with 256 units to spare: the
// screen never opens by itself on arrival.

#include "ap_pad_activity_logic.h" // AP_PadActivityHubKeys: the one hub-door Key table

#define AP_FT_TRIGGER_R2 0x90000 // AH_SaveObj wakes at dist^2 <= 0x8ffff
#define AP_FT_LAND_DIST 1024
#define AP_FT_HUBS 5

enum
{
	AP_FT_IDLE = 0,
	AP_FT_WAIT = 1 // waiting for a safe hub, the destination's load and the spawn
};

enum
{
	AP_FT_DROP_NONE = 0,
	AP_FT_DROP_TIMEOUT = 1, // the whole trip took too long
	AP_FT_DROP_GATE = 2,    // the hub's Keys went away on the way (a reconnect)
	AP_FT_DROP_NOSPAWN = 3, // the destination loaded but had no save screen spawn
	AP_FT_DROP_YIELD = 4,   // the birth's door or boss-scene spawn kept its entry
	AP_FT_DROP_SEED = 5     // the seed went away (disconnect, slot switch)
};

// Bounds the whole trip, as AP_TL_LOAD_FRAMES bounds a tracker start.
#define AP_FT_TIMEOUT_FRAMES 1800
// Frames the destination hub may sit idle without the spawn firing before the
// trip is dropped (the kart then stands at the hub's normal spawn). Long
// enough for the load to start on a slow machine, short enough to notice.
#define AP_FT_NOSPAWN_FRAMES 90

typedef struct
{
	int stage;  // AP_FT_*
	int hub;    // destination, 0..4 (levelID - GEM_STONE_VALLEY)
	int asked;  // the destination hub's load was requested
	int frames; // total frames waiting
	int idle;   // frames idle in the destination without the spawn firing
} ap_ft_request;

static inline void AP_FTReset(ap_ft_request *r)
{
	r->stage = AP_FT_IDLE;
	r->hub = -1;
	r->asked = r->frames = r->idle = 0;
}

// A hub the player could not walk to stays closed: its door Keys are the
// apworld's region entrances (world.json), shared with the tracker through
// AP_PadActivityHubKeys.
static inline int AP_FT_HubOpen(int hub, int keysHave)
{
	return hub >= 0 && hub < AP_FT_HUBS && keysHave >= AP_PadActivityHubKeys(hub, -1);
}

// dirX, dirZ: the save trigger's local X axis in 12-bit fixed point
// (MATH_Cos(rotY), -MATH_Sin(rotY) of its InstDef rotation). pos: the
// trigger's world position. out: the landing, AP_FT_LAND_DIST out along the
// approach direction. y passes through; the engine ground-snap after this
// resolves the real floor.
static inline void AP_FT_Landing(const int pos[3], int dirX, int dirZ, int out[3])
{
	out[0] = pos[0] + (dirX * AP_FT_LAND_DIST >> 12);
	out[1] = pos[1];
	out[2] = pos[2] + (dirZ * AP_FT_LAND_DIST >> 12);
}

// The kart faces the save screen it landed in front of. Kart forward is
// (sin(rotY), cos(rotY)) in x,z; facing from the landing to the screen is
// the approach direction turned 90 degrees back.
static inline int AP_FT_Facing(int rotY)
{
	return (rotY - 0x400) & 0xfff;
}

// The menu and the trip agree on the same rule at both ends. A tracker start
// in flight refuses the trip and vice versa (one hub move at a time).
static inline int AP_FTBegin(ap_ft_request *r, int hub, int keysHave, int otherTravelPending)
{
	if (r->stage != AP_FT_IDLE || otherTravelPending || !AP_FT_HubOpen(hub, keysHave))
		return 0;
	r->stage = AP_FT_WAIT;
	r->hub = hub;
	r->asked = r->frames = r->idle = 0;
	return 1;
}

// Once per frame. levelHub is the running hub (0..4) or -1; hold is a load in
// flight or a hub that is not yet safe for a request; keysNow is the received
// Key count now; spawned is the destination's birth having placed the kart,
// yielded is the birth having kept its door or boss-scene spawn instead, and
// seedLive is the AP configuration still owning the game. *wantLoad: request
// the destination hub now. Returns a drop reason, or AP_FT_DROP_NONE while the
// trip is alive or idle.
static inline int AP_FTFrame(ap_ft_request *r, int levelHub, int hold, int keysNow, int seedLive, int spawned, int yielded, int *wantLoad)
{
	int drop;
	*wantLoad = 0;
	if (r->stage == AP_FT_IDLE)
		return AP_FT_DROP_NONE;
	// The birth placed the kart: the trip is done, whatever else is true.
	if (spawned)
	{
		AP_FTReset(r);
		return AP_FT_DROP_NONE;
	}
	// The destination's birth kept its door or boss-scene spawn: the trip ends
	// there, at the normal spawn, with the scene intact.
	if (yielded)
	{
		drop = AP_FT_DROP_YIELD;
		goto drop;
	}
	// No seed, no trip: a disconnect or slot switch mid-wait cancels.
	if (!seedLive)
	{
		drop = AP_FT_DROP_SEED;
		goto drop;
	}
	if (++r->frames > AP_FT_TIMEOUT_FRAMES)
	{
		drop = AP_FT_DROP_TIMEOUT;
		goto drop;
	}
	// A gate that closed on the way (a reconnect that removed a Key) cancels
	// the trip instead of landing the player out of logic.
	if (!AP_FT_HubOpen(r->hub, keysNow))
	{
		drop = AP_FT_DROP_GATE;
		goto drop;
	}
	if (!r->asked)
	{
		if (hold)
			return AP_FT_DROP_NONE;
		r->asked = 1;
		*wantLoad = 1;
		return AP_FT_DROP_NONE;
	}
	// The load was asked for. The destination idling without the spawn firing
	// means the hub had no save screen to anchor to; keep the normal spawn and
	// end the trip rather than wait forever.
	if (!hold && levelHub == r->hub && ++r->idle > AP_FT_NOSPAWN_FRAMES)
	{
		drop = AP_FT_DROP_NOSPAWN;
		goto drop;
	}
	return AP_FT_DROP_NONE;
drop:
	AP_FTReset(r);
	return drop;
}

#endif
