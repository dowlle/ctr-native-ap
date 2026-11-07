#ifndef AP_PAD_BIRTH_H
#define AP_PAD_BIRTH_H

// What a plan pad's birth draws, as one comparable number (A3-12).
//
// AH_WarpPad_ThTick rebuilds a pad in the hub when something changes, by the
// same AH_WarpPad_BuildInstances the level load runs. It used to decide that
// from the pad state alone. The state is not everything the birth reads: a
// Locked pad (state 1) floats its hub Key floor until the floor is met, then
// its own stage 1 rule, and stays state 1 the whole time. So a received Key
// that met the floor left the floor's "Key xN" icon on the pad until the hub
// was loaded again (tester report, Gem Stone Valley cup pads after a Wumpa
// Shop Key). The key below holds every fact the birth branches on, and both
// the birth and the rebuild check read it, so a rebuilt pad is the pad a
// fresh birth draws for the same state.
//
// Freestanding on purpose: tools/test-pad-rebuild-production.py runs it
// against the production birth text on the host.

#include "ap_content_plan.h"

// Which requirement a closed plan pad floats.
enum
{
	AP_PAD_GATE_FLOOR = 0,  // the hub Key floor, "Key xN"
	AP_PAD_GATE_RULE = 1,   // its stage 1 rule, once the floor is met
	AP_PAD_GATE_STAGE2 = 2  // its stage 2 rule, closed again after the first race
};

// The requirement layer, as AH_WarpPad_BuildInstances picks it. `keys` is the
// received Key count, `state` the pad's AP_PadState.
static inline int ap_pad_gate_layer(const ctr_content_pad *pad, int keys, int state)
{
	if (pad && state == 3)
		return AP_PAD_GATE_STAGE2;
	if (pad && pad->has_rule && keys >= pad->keys)
		return AP_PAD_GATE_RULE;
	return AP_PAD_GATE_FLOOR;
}

// The birth key. `counts` are the 15 received tallies (AP_IDX_*), `look` the
// pad's AP_PadLook and `replayable` its AP_PadReplayable. Packs the state
// (3 bits), whether the pad can be entered for a replay, the look (4 bits),
// the requirement layer (2 bits) and whether the birth draws it open. A Done
// pad draws only its grey X and, when it can be replayed, the beam, so the
// layer and the open bit stay 0 there.
static inline int ap_pad_birth_key(const ctr_content_pad *pad, const int *counts, int state, int look,
                                   int replayable)
{
	int key = (state & 7) | (replayable ? 8 : 0) | ((look & 15) << 4);
	int layer, owned, needed;
	if (state == 5 || !pad)
		return key;
	layer = ap_pad_gate_layer(pad, counts[14], state);
	if (layer == AP_PAD_GATE_STAGE2)
	{
		owned = pad->has_stage2 ? ap_content_stage2_owned(pad, counts) : 0;
		needed = pad->stage2_count;
	}
	else if (layer == AP_PAD_GATE_RULE)
	{
		owned = ap_content_rule_owned(pad, counts);
		needed = pad->rule_count;
	}
	else
	{
		owned = counts[14];
		needed = pad->keys;
	}
	return key | (layer << 8) | ((owned >= needed || replayable) ? 0x400 : 0);
}

#endif
