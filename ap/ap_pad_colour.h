#ifndef AP_PAD_COLOUR_H
#define AP_PAD_COLOUR_H

// One colour per pad state, shared by the warp pad beam, the hub map dot and
// the Adventure tracker's node (alpha2, decided 2026-11-04: the
// "Familiar" palette of the one colour language proposal, no symbols yet).
//
// Freestanding on purpose: AH_WarpPad.c, AH_Map.c and ap_tracker.c all read
// this table, and tools/test-pad-colour.c pins it on the host, so the three
// surfaces cannot drift apart. Only pad-state roles live here. Item colours
// (reward models, AP markers, AP boxes, feed text) are not pad states and
// keep their own tables.

#include <stdint.h>

// Display state of a pad or garage node. Derived from the pad state codes of
// ap/ap_pad_state.h (1..5), plus two display-only refinements that never feed
// back into a gate: a closed pad whose racer has not arrived, and a beaten
// boss garage (the tracker's AP_TRACKER_NODE_BEATEN, 6).
enum AP_PadLook
{
	AP_PAD_LOOK_NONE = 0,       // no AP state: the retail look stays
	AP_PAD_LOOK_CLOSED,         // pad state 1, requirement unmet
	AP_PAD_LOOK_OPEN,           // pad state 2, a check is available
	AP_PAD_LOOK_STAGE2_OPEN,    // pad state 4, stage 2 met, checks remain
	AP_PAD_LOOK_STAGE2_WAITING, // pad state 3, Trophy won, stage 2 unmet
	AP_PAD_LOOK_RACER_LOCKED,   // pad state 1 with its racer not received
	AP_PAD_LOOK_DONE,           // pad state 5, nothing left
	AP_PAD_LOOK_BOSS_BEATEN,    // tracker garage node only
	AP_PAD_LOOK_COUNT
};

// Pad state code of a beaten boss garage node (ap_tracker_boss_node_logic.h).
#define AP_PAD_LOOK_STATE_BEATEN 6

// Pad state (and the racer fact) to display state. Done stays Done whatever
// lock is unmet; a missing racer shows as racer-locked even when other
// requirements are missing too (the tracker's text still lists every one).
static inline int AP_PadLookFromState(int padState, int racerMissing)
{
	switch (padState)
	{
	case 1: return racerMissing ? AP_PAD_LOOK_RACER_LOCKED : AP_PAD_LOOK_CLOSED;
	case 2: return AP_PAD_LOOK_OPEN;
	case 3: return AP_PAD_LOOK_STAGE2_WAITING;
	case 4: return AP_PAD_LOOK_STAGE2_OPEN;
	case 5: return AP_PAD_LOOK_DONE;
	case AP_PAD_LOOK_STATE_BEATEN: return AP_PAD_LOOK_BOSS_BEATEN;
	default: return AP_PAD_LOOK_NONE;
	}
}

// The Familiar palette, sRGB 0xRRGGBB, as the proposal's table gives it.
// AP_PAD_LOOK_NONE has no colour of its own (0); callers keep the retail look.
static inline uint32_t AP_PadLookRGB(int look)
{
	switch (look)
	{
	case AP_PAD_LOOK_CLOSED: return 0xDA4230u;
	case AP_PAD_LOOK_OPEN: return 0x70E860u;
	case AP_PAD_LOOK_STAGE2_OPEN: return 0x93ADFAu;
	case AP_PAD_LOOK_STAGE2_WAITING: return 0xF49228u;
	case AP_PAD_LOOK_RACER_LOCKED: return 0xBA4E22u;
	case AP_PAD_LOOK_DONE: return 0x9AA4AEu;
	case AP_PAD_LOOK_BOSS_BEATEN: return 0xF2C248u;
	default: return 0u;
	}
}

static inline unsigned AP_PadLookR(uint32_t rgb) { return (rgb >> 16) & 0xffu; }
static inline unsigned AP_PadLookG(uint32_t rgb) { return (rgb >> 8) & 0xffu; }
static inline unsigned AP_PadLookB(uint32_t rgb) { return rgb & 0xffu; }

// ── Warp pad beam ──
// Which pads birth a beam is unchanged (AH_WarpPad.c): an open pad, and a pad
// kept open for a fruit replay (AP_PadReplayable). This only answers which
// colour a beam that exists wears. Only the two actionable states, open and
// stage 2 open, recolour it; a beam kept for a replay (stage 2 waiting, Done)
// or a racer-locked pad keeps the retail light blue, so no new beam appears and
// none is dressed up as a state it does not signal.
//
// The beam is untextured: the renderer lerps each vertex colour toward the
// instance's far colour (colorRGBA) by alphaScale/4096 (GTE DPCT), and the
// result is drawn additively. The retail beam keeps its own light blue with
// colorRGBA 0 at alphaScale 0xC00. A state beam replaces the hue fully
// (alphaScale 0x1000) with the state colour scaled to the retail beam's
// brightness, so it reads as the same column of light in a new colour.
#define AP_PAD_BEAM_ALPHA 0x1000
// Scale of the state colour, in 1/256, against the retail beam's strength.
#define AP_PAD_BEAM_LEVEL 160

static inline int AP_PadLookHasBeamColour(int look)
{
	return look == AP_PAD_LOOK_OPEN || look == AP_PAD_LOOK_STAGE2_OPEN;
}

// Instance colorRGBA packing: (R << 20) | (G << 12) | (B << 4), the layout
// RenderBucket_SetFarColorFromInstance reads. 0 for a look with no beam
// colour, which leaves the retail beam untouched.
static inline uint32_t AP_PadLookBeamTint(int look)
{
	uint32_t rgb;
	if (!AP_PadLookHasBeamColour(look))
		return 0u;
	rgb = AP_PadLookRGB(look);
	return ((AP_PadLookR(rgb) * AP_PAD_BEAM_LEVEL / 256u) << 20) |
	       ((AP_PadLookG(rgb) * AP_PAD_BEAM_LEVEL / 256u) << 12) |
	       ((AP_PadLookB(rgb) * AP_PAD_BEAM_LEVEL / 256u) << 4);
}

// ── Hub map dot ──
// The map draws its pad icon as a textured quad whose vertex colours modulate
// the icon (0x80 = the texture unchanged, so a vertex value of half the target
// gives the target on the white icon). Words are 0x00BBGGRR, the layout of
// data.colors. `dim` is the darker half of the existing Map Flash flicker
// (open: fast, stage 2 open: slow); it keeps the same hue at 70%.
static inline uint32_t AP_PadLookMapVertex(int look, int dim)
{
	uint32_t rgb = AP_PadLookRGB(look);
	unsigned r = (AP_PadLookR(rgb) + 1u) / 2u, g = (AP_PadLookG(rgb) + 1u) / 2u, b = (AP_PadLookB(rgb) + 1u) / 2u;
	if (dim)
	{
		r = r * 7u / 10u;
		g = g * 7u / 10u;
		b = b * 7u / 10u;
	}
	return (b << 16) | (g << 8) | r;
}

#endif
