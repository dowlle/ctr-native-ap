#ifndef AP_PAD_LOGIC_LOOK_H
#define AP_PAD_LOGIC_LOOK_H

// Green only for checks in logic (A3-37, ruled 2026-11-07 08:07 CET: "Only
// checks in logic"). The open and stage 2 open looks (green and blue-violet
// beam, map dot, tracker node) mean "at least one open check behind this pad
// is in logic now". A pad whose open checks are all out of logic shows the
// stage 2 waiting look instead: static orange dot, and the retail light blue
// beam on the pad that can still be entered. An existing look is reused, so
// the beam, map and tracker need no new colour.
//
// Display only. The pad state (ap_pad_state.h) is untouched: no gate, entry
// route, count or glow reads this.
//
// A check the client has no logic for is counted as in logic, which keeps the
// look it had before this rule. The exported win_logic block covers every win
// check, and every other pad location only when pad_approach_hints is on
// (SCHEMA.md, version 2), so a pad that still holds a box, a Wumpa check, a
// letter or a podium rung on a seed without that option stays green.
//
// Freestanding so tools/test-pad-logic-look.c can pin it on the host.

#include "ap_pad_colour.h"
#include "ap_win_logic.h"

// How one open location stands against the exported logic.
enum
{
	AP_PAD_LOGIC_OUT = 0,    // the block has an entry and the rule is not met now
	AP_PAD_LOGIC_IN = 1,     // the block has an entry and the rule is met now
	AP_PAD_LOGIC_UNKNOWN = 2 // no usable entry: no block, an invalid one, or no row
};

// One location against the parsed block. Unknown when there is no valid block,
// the inputs cannot be trusted (tally overflow) or the block has no row for the
// code: the client never guesses logic.
static inline int AP_PadLogicStateOf(const AP_WinLogic *logic, const AP_WinLogicEnv *env, long code)
{
	int idx;
	if (!logic || !env || logic->state != AP_WL_VALID || env->unreliable)
		return AP_PAD_LOGIC_UNKNOWN;
	idx = AP_WinLogicFindCheck(logic, code);
	if (idx < 0)
		return AP_PAD_LOGIC_UNKNOWN;
	return AP_WinLogicInLogic(logic, env, idx, 0) ? AP_PAD_LOGIC_IN : AP_PAD_LOGIC_OUT;
}

// `look` is the look the pad's state gives. `open` is how many open locations
// the pad lists, `inLogic` and `unknown` how many of them stand as above.
static inline int AP_PadLookByLogic(int look, int open, int inLogic, int unknown)
{
	if (look != AP_PAD_LOOK_OPEN && look != AP_PAD_LOOK_STAGE2_OPEN)
		return look;
	if (open <= 0 || inLogic > 0 || unknown > 0)
		return look;
	return AP_PAD_LOOK_STAGE2_WAITING;
}

#endif
