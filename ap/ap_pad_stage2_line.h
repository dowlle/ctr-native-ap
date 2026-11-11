#ifndef AP_PAD_STAGE2_LINE_H
#define AP_PAD_STAGE2_LINE_H

// The stage 2 line under a pad's name (A4-59).
//
// Standing at a pad shows its name. A pad with a second stage also shows one
// small line under the name with what stage 2 asks for and how many the player
// holds, `Stage 2: 1/2 Gold Relics`, from the first approach on. Before
// A4-59 the second stage only showed after the pad's first race was won, as
// the floating requirement model, so a player could not plan for it.
//
// The line reads only what the room already sent: the pad's `stage2_mask` and
// `stage2_count` and the received tallies, through the same counting the gate
// uses (ap_content_stage2_owned) and the same wording the tracker's activity
// rows use (ap_content_requirement_text). No slot_data change.
//
// Colours follow the A4-10 pad states: green when the requirement is met, red
// while it is not, grey when the stage 2 races have nothing left. A Done pad,
// a cup, a Crystal Challenge and a pad without a second stage show no line.
//
// Freestanding on purpose: tools/test-pad-stage2-line.c drives it on the host.

#include "ap_content_modes.h"

enum
{
	AP_S2LINE_NONE = 0,
	AP_S2LINE_UNMET = 1, // red: the player holds fewer than stage 2 asks for
	AP_S2LINE_MET = 2,   // green: the requirement is met
	AP_S2LINE_DONE = 3   // grey: the stage 2 races have no unchecked check left
};

// 1 when a mode of the pad sits behind its second stage.
static inline int ap_pad_stage2_has_mode(const ctr_content_pad *pad)
{
	int i;
	for (i = 0; pad && i < pad->mode_count && i < AP_CONTENT_MODE_COUNT; ++i)
		if (pad->stages[i] == 2)
			return 1;
	return 0;
}

// Unchecked checks only a stage 2 race can earn: the race checks of every
// stage 2 mode (CTR Token, relic tiers, a ctr_first paired Trophy) and its
// family checks (CTR letters, Relic Race Perfect, podium rungs when their race
// is behind stage 2). Checks a stage 1 race also reaches are not counted: the
// pad's Item Boxes, the track's lap check and a Wumpa check with a stage 1
// route. Same families as ap_content_mode_pending.
static inline int ap_pad_stage2_left(const ctr_content_pad *pad, ap_content_checked_fn checked)
{
	int i, j, k, n, left = 0, wumpaStage1 = 0;
	for (j = 0; pad && j < pad->mode_count && j < AP_CONTENT_MODE_COUNT; ++j)
		if (pad->stages[j] != 2 && (pad->wumpa_modes & (1u << pad->modes[j])))
			wumpaStage1 = 1;
	for (i = 0; pad && i < pad->mode_count && i < AP_CONTENT_MODE_COUNT; ++i)
	{
		int64_t codes[3] = {0, 0, 0}, family[AP_CONTENT_FAMILY_SLOTS];
		int slots[AP_CONTENT_FAMILY_SLOTS];
		int64_t paired;
		if (pad->stages[i] != 2)
			continue;
		n = ap_content_mode_codes(pad, pad->modes[i], codes);
		for (k = 0; k < n; ++k)
			left += codes[k] > 0 && !checked(codes[k]);
		paired = ap_content_mode_paired_trophy(pad, pad->modes[i]);
		left += paired > 0 && !checked(paired);
		n = ap_content_mode_family_codes(pad, pad->modes[i], family, slots);
		for (k = 0; k < n; ++k)
		{
			if (slots[k] == AP_CONTENT_FAMILY_LAP || (slots[k] == 5 && wumpaStage1))
				continue;
			left += family[k] > 0 && !checked(family[k]);
		}
	}
	return left;
}

// Which line the pad shows. `padState` is AP_PadState (5 Done), `owned` the
// received count toward stage 2 (ap_content_stage2_owned), `left` the
// ap_pad_stage2_left count.
static inline int ap_pad_stage2_line_state(const ctr_content_pad *pad, int padState, int owned, int left)
{
	if (!pad || !pad->occupied || pad->cup || pad->crystal || !pad->has_stage2 || pad->stage2_count <= 0)
		return AP_S2LINE_NONE;
	if (padState == 5 || !ap_pad_stage2_has_mode(pad))
		return AP_S2LINE_NONE;
	if (left <= 0)
		return AP_S2LINE_DONE;
	return owned >= pad->stage2_count ? AP_S2LINE_MET : AP_S2LINE_UNMET;
}

// The line's text; returns its length, 0 for AP_S2LINE_NONE. Held count first,
// capped at the need, as `1/2 Gold Relics`: the HUD font has no parentheses,
// and the short form stays clear of the minimap corner.
static inline int ap_pad_stage2_line_text(const ctr_content_pad *pad, int state, int owned, char *out, size_t cap)
{
	char what[40];
	if (!out || cap == 0)
		return 0;
	out[0] = 0;
	if (state == AP_S2LINE_NONE || !pad)
		return 0;
	ap_content_requirement_text(pad->stage2_mask, pad->stage2_count, what, sizeof what);
	if (state == AP_S2LINE_DONE)
		return snprintf(out, cap, "Stage 2 done: %s", what);
	if (owned > pad->stage2_count)
		owned = pad->stage2_count;
	if (owned < 0)
		owned = 0;
	return snprintf(out, cap, "Stage 2: %d/%s", owned, what);
}

// How far the name and the lines above it move up, in pixels: the block under
// the name holds `linesBelow` small lines and the first one fits where the
// AP BOXES LEFT line always sat. One line keeps the old layout exactly.
static inline int ap_pad_title_block_lift(int linesBelow, int smallLineHeight)
{
	return linesBelow > 1 ? (linesBelow - 1) * smallLineHeight : 0;
}

#endif
