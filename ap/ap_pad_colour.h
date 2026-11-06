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
// kept open for a fruit replay (AP_PadReplayable), Done included (AP_PadDoneBeam).
// This only answers which
// colour a beam that exists wears. Only the two actionable states, open and
// stage 2 open, recolour it; a beam kept for a replay (stage 2 waiting, Done)
// or a racer-locked pad keeps the retail light blue, so no new beam appears and
// none is dressed up as a state it does not signal.
//
// The beam (STATIC_BEAM, model 123) and the two rising spiral wisps
// (STATIC_BOTTOMRING, model 124, two instances) are textured and drawn
// additively (texture page blend mode 1). Their vertex colours run from black
// (the fade at the top and edges) through deep blue to near white, and the
// renderer lerps each vertex colour toward the instance's far colour
// (colorRGBA) by alphaScale/4096 (GTE DPCT). The retail beam keeps colorRGBA 0
// at alphaScale 0xC00, so it shows 25% of its vertex colours.
//
// A state beam keeps that shape: it is drawn from copies of the models whose
// colours carry the state hue at each vertex's own brightness
// (AP_PadBeamHueWord), with colorRGBA 0 like retail (A3-4 render study,
// 2026-11-05: a flat far colour filled the whole column and read as a pale
// block).
//
// The open beam is green, and that needs three parts (A3-4b render study,
// 2026-11-05). The bright blue bands and sparkles players see are the wisps,
// not the beam, so the wisps take the hue too. And additive light over the
// blue hub sky (about #5E6FFB) can only add, so green on its own reads cyan:
// the sky's blue stays. A third copy of the beam, drawn subtractive (blend
// mode 2), takes red and blue out of what is behind the column, in the beam's
// own fade, so the green on top reads green. The beam green is a purer green
// than the map dot's #70E860, whose red and blue turn the wisps white.
//
// Stage 2 open takes the same three parts in its own blue-violet (ruled
// 2026-11-05, A2-46), so it reads apart from both the open green and the
// retail light blue. Its table colour #93ADFA, used the same way, read pale
// and close to retail; with a filter taking out only green it read magenta.
// The violet takes out red and green behind the column and adds a deep
// blue-violet, so the column reads indigo against sky and cliff. The map dot
// and the tracker keep #93ADFA.
#define AP_PAD_BEAM_GREEN 0x20FF20u
#define AP_PAD_BEAM_FILTER 0x6000FFu // channels the open filter removes
#define AP_PAD_BEAM_VIOLET 0x6A4CFFu
#define AP_PAD_BEAM_VIOLET_FILTER 0xA0FF00u // channels the stage 2 filter removes
// Both state beams at 60% of the retail strength:
// 0x1000 - 0.6 * (0x1000 - 0xC00) = 0xD9A.
#define AP_PAD_BEAM_STATE_ALPHA 0xD9A
#define AP_PAD_BEAM_FILTER_ALPHA 0xB00 // subtractive copy at 31% of its colours

static inline int AP_PadLookHasBeamColour(int look)
{
	return look == AP_PAD_LOOK_OPEN || look == AP_PAD_LOOK_STAGE2_OPEN;
}

// A Done pad shows the retail light blue beam only while the player can still
// enter it for a replay (AP_PadReplayable), so it no longer looks locked
// (ruled 2026-11-05, A2-45). A Done pad that cannot be entered keeps no beam.
// The beam keeps the retail colour: Done has no beam hue, wisp hue or filter.
static inline int AP_PadDoneBeam(int look, int replayable)
{
	return look == AP_PAD_LOOK_DONE && replayable != 0;
}

// Hue of the beam and of its two wisps (0xRRGGBB), 0 for a look that keeps
// the retail beam.
static inline uint32_t AP_PadLookBeamHue(int look)
{
	if (look == AP_PAD_LOOK_OPEN)
		return AP_PAD_BEAM_GREEN;
	if (look == AP_PAD_LOOK_STAGE2_OPEN)
		return AP_PAD_BEAM_VIOLET;
	return 0u;
}

static inline uint32_t AP_PadLookWispHue(int look)
{
	return AP_PadLookBeamHue(look);
}

// Channels the subtractive beam copy takes out, 0 for no copy.
static inline uint32_t AP_PadLookBeamFilter(int look)
{
	if (look == AP_PAD_LOOK_OPEN)
		return AP_PAD_BEAM_FILTER;
	if (look == AP_PAD_LOOK_STAGE2_OPEN)
		return AP_PAD_BEAM_VIOLET_FILTER;
	return 0u;
}

// alphaScale of a recoloured beam.
static inline int AP_PadLookBeamAlpha(int look)
{
	return AP_PadLookHasBeamColour(look) ? AP_PAD_BEAM_STATE_ALPHA : 0xC00;
}

// One model colour word (0xCCBBGGRR) recoloured to `rgb` (0xRRGGBB) at the
// word's own brightness, its strongest channel. Black stays black, so the
// fade survives; the top byte is kept.
static inline uint32_t AP_PadBeamHueWord(uint32_t word, uint32_t rgb)
{
	unsigned r = word & 0xffu, g = (word >> 8) & 0xffu, b = (word >> 16) & 0xffu;
	unsigned v = r > g ? r : g;
	if (b > v)
		v = b;
	return (word & 0xff000000u) | ((v * AP_PadLookB(rgb) / 255u) << 16) |
	       ((v * AP_PadLookG(rgb) / 255u) << 8) | (v * AP_PadLookR(rgb) / 255u);
}

// Texture page with its blend mode (bits 5 and 6) set to `abr`: 1 additive,
// 2 subtractive.
static inline uint16_t AP_PadTpageBlend(uint16_t tpage, unsigned abr)
{
	return (uint16_t)((tpage & ~0x60u) | ((abr & 3u) << 5));
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
