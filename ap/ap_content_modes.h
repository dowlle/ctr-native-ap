#ifndef AP_CONTENT_MODES_H
#define AP_CONTENT_MODES_H
// Track-mode decisions for content-plan pads (#133 PR 3), pure C so the host
// harness tools/test-content-modes.c runs them without the engine. The pad
// selector, pad state, glow and race results all read these, and every gate
// question goes through ap_content_mode_open (ap_content_plan.h), so a row
// the selector offers is a mode the race-start gate admits.
#include "ap_content_plan.h"

typedef int (*ap_content_checked_fn)(int64_t code);

// Unchecked codes a mode still owns on this pad.
static inline int ap_content_mode_pending(const ctr_content_pad *pad, int mode, ap_content_checked_fn checked)
{
    int64_t codes[3];
    int n = ap_content_mode_codes(pad, mode, codes), i, left = 0;
    for (i = 0; i < n; ++i)
        if (codes[i] > 0 && !checked(codes[i])) ++left;
    return left;
}

// One selector row per mode, in the pad's order. A locked row is drawn grey
// and cannot be chosen; its missing requirement is shown under the pad name.
typedef struct ap_content_selector {
    int count, cursor;
    int mode[AP_CONTENT_MODE_COUNT], open[AP_CONTENT_MODE_COUNT], pending[AP_CONTENT_MODE_COUNT];
} ap_content_selector;

// The cursor starts on the first open row with a check left, else on the
// first open row. Returns the number of open rows.
static inline int ap_content_selector_build(const ctr_content_pad *pad, const int *counts,
                                            ap_content_checked_fn checked, ap_content_selector *out)
{
    int i, open = 0;
    out->count = 0;
    out->cursor = -1;
    for (i = 0; pad && i < pad->mode_count && i < AP_CONTENT_MODE_COUNT; ++i) {
        const int mode = pad->modes[i];
        out->mode[i] = mode;
        out->open[i] = ap_content_mode_open(pad, mode, counts);
        out->pending[i] = ap_content_mode_pending(pad, mode, checked);
        out->count++;
        if (out->open[i]) {
            ++open;
            if (out->cursor < 0 || (out->pending[i] && !out->pending[out->cursor])) out->cursor = i;
        }
    }
    return open;
}

// Pad state codes of AP_PadState for a content-plan track pad: 1 locked,
// 2 a check is left in an open mode, 3 checks are left only behind the
// second-stage gate, 4 everything is checked (replayable).
static inline int ap_content_track_pad_state(const ctr_content_pad *pad, const int *counts,
                                             ap_content_checked_fn checked)
{
    int i, lockedLeft = 0;
    if (!ap_content_pad_open(pad, counts)) return 1;
    for (i = 0; i < pad->mode_count && i < AP_CONTENT_MODE_COUNT; ++i) {
        const int left = ap_content_mode_pending(pad, pad->modes[i], checked);
        if (!left) continue;
        if (ap_content_mode_open(pad, pad->modes[i], counts)) return 2;
        lockedLeft = 1;
    }
    return lockedLeft ? 3 : 4;
}

// Relic tiers a finish reaches, bit t for Sapphire, Gold, Platinum: the time
// (after the all-crates bonus) is at or under the tier's target, as in the
// retail award loop (game/223.c). Targets are in race ticks (960 per second).
static inline unsigned ap_content_relic_tiers(int raceTime, const int targets[3])
{
    unsigned mask = 0;
    int t;
    for (t = 0; t < 3; ++t)
        if (targets[t] > 0 && raceTime <= targets[t]) mask |= 1u << t;
    return mask;
}

// A CTR Challenge pays on first place with all three letters; a Trophy Race
// on first place. A Relic Race pays only its tiers (ap_content_relic_tiers).
static inline int ap_content_race_award(int mode, int rank, int letters)
{
    if (mode == AP_CONTENT_MODE_TROPHY) return rank == 0;
    if (mode == AP_CONTENT_MODE_CTR) return rank == 0 && letters == 3;
    return 0;
}

static inline const char *ap_content_mode_label(int mode)
{
    return mode == AP_CONTENT_MODE_TROPHY ? "Trophy" : mode == AP_CONTENT_MODE_CTR ? "CTR" :
           mode == AP_CONTENT_MODE_RELIC ? "Relic" : "Race";
}

// Pad display identities of content-plan checks (AP_LookupLocationCode):
// Trophy or Crystal completion 50000 + pad, CTR 50200 + pad, relic tiers
// 50400 + pad * 3 + tier, crystal pickups 60000 + pad * 20 + n.
#define AP_CONTENT_BIT_TROPHY 50000
#define AP_CONTENT_BIT_CTR 50200
#define AP_CONTENT_BIT_RELIC 50400
static inline int ap_content_bit_decode(int bit, int *physical, int *mode, int *tier)
{
    if (bit >= AP_CONTENT_BIT_TROPHY && bit <= AP_CONTENT_BIT_TROPHY + 104) {
        *physical = bit - AP_CONTENT_BIT_TROPHY; *mode = AP_CONTENT_MODE_TROPHY; *tier = 0; return 1;
    }
    if (bit >= AP_CONTENT_BIT_CTR && bit <= AP_CONTENT_BIT_CTR + 104) {
        *physical = bit - AP_CONTENT_BIT_CTR; *mode = AP_CONTENT_MODE_CTR; *tier = 0; return 1;
    }
    if (bit >= AP_CONTENT_BIT_RELIC && bit < AP_CONTENT_BIT_RELIC + 105 * 3) {
        *physical = (bit - AP_CONTENT_BIT_RELIC) / 3; *mode = AP_CONTENT_MODE_RELIC;
        *tier = (bit - AP_CONTENT_BIT_RELIC) % 3; return 1;
    }
    return 0;
}

#endif
