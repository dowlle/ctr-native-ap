// ap_pad_hints_logic.h -- Pad Approach Hints (#124), the pure decisions.
//
// Ruling of 2026-07-20 (R1), built for 0.3.0 on 2026-11-02: "Auto-hint
// should become a separate YAML option. They should only trigger when you
// approach a pad (so when the name is shown in-game)." Off by default
// (ctr_options.pad_approach_hints, absent = off).
//
// When a pad shows its name, the client hints the locations its glow lists
// with openOnly, filtered by their exported location logic: unchecked checks
// of the modes that are open now and whose item gates are satisfied. A pad that
// is closed (its requirement or racer lock not met), empty, or whose custom
// track is not ready hints nothing (agent decision, conservative: a locked
// pad's checks are not in logic yet). Each location is hinted once ever: the
// latch of hinted codes lives in the room's data storage
// (ap_pad_hint_latch.h), so a reconnect sends nothing again. A pad whose
// second stage opens later hints only the checks that opened.
//
// Header-only and engine-free so tools/test-pad-approach-hints.cpp can run it.
#ifndef AP_PAD_HINTS_LOGIC_H
#define AP_PAD_HINTS_LOGIC_H

#include <stdint.h>
#include "ap_win_logic.h"

// An open pad can still contain checks gated by boost, letters or other
// inventory. Use the exported rule for each code before creating a hint.
static inline int AP_PadHint_InLogic(const AP_WinLogic *logic, const AP_WinLogicEnv *env, int64_t code)
{
	return logic && env && logic->state == AP_WL_VALID && !env->unreliable &&
	       AP_WinLogicInLogic(logic, env, AP_WinLogicFindCheck(logic, (long)code), 0);
}

// Whether a pad may hint now. option: the seed's pad_approach_hints. ready:
// connected, admitted, and the latch read from the room. plan: a content-plan
// room. occupied: the pad has an entry. contentReady: its custom track (if
// any) is installed. open: AP_ContentPadOpen, requirement and racer lock met.
static inline int AP_PadHint_Eligible(int option, int ready, int plan, int occupied, int contentReady, int open)
{
	return option && ready && plan && occupied && contentReady && open;
}

// Copies the codes to hint from the pad's open, unchecked codes: positive,
// not latched, each once. latched(code, ctx) answers the room latch. Returns
// how many were written to out (at most cap).
static inline int AP_PadHint_Select(const int64_t *open, int n, int (*latched)(int64_t, void *), void *ctx,
                                    int64_t *out, int cap)
{
	int count = 0, i, k, dup;
	if (!open || !out || cap <= 0) return 0;
	for (i = 0; i < n && count < cap; ++i)
	{
		if (open[i] <= 0 || (latched && latched(open[i], ctx))) continue;
		for (dup = 0, k = 0; k < count; ++k)
			if (out[k] == open[i]) dup = 1;
		if (!dup) out[count++] = open[i];
	}
	return count;
}

#endif
