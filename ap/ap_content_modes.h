#ifndef AP_CONTENT_MODES_H
#define AP_CONTENT_MODES_H
// Track-mode decisions for content-plan pads (#133 PR 3), pure C so the host
// harness tools/test-content-modes.c runs them without the engine. The pad
// selector, pad state, glow and race results all read these, and every gate
// question goes through ap_content_mode_open (ap_content_plan.h), so a row
// the selector offers is a mode the race-start gate admits.
#include "ap_content_plan.h"


// Per-track check families (#133 PR 3b) a mode reaches on this pad, with their
// display slot: podium rungs (slots 0..4) on the Trophy Race, the track's own
// Reach 10 Wumpa check (slot 5) on every mode with a Wumpa route, and the
// letter checks (slots 6..8) on the CTR Challenge, and the Relic Race Perfect
// check (slot 9, #133 PR 3c) on the Relic Race. The `wumpa: global` check
// belongs to no pad and keeps no pad open, as on the legacy path.
#define AP_CONTENT_FAMILY_SLOTS 10
#define AP_CONTENT_FAMILY_PERFECT 9
// Content-plan podium identity of a pad's Trophy Race: logical podium track
// AP_CONTENT_PODIUM_LOGICAL_BASE + physical pad, resolved through the pad's
// own rung codes (ap_hooks_race.c AP_ContentPodiumRungs), never the engine
// host's row.
#define AP_CONTENT_PODIUM_LOGICAL_BASE 1000
static inline int ap_content_mode_family_codes(const ctr_content_pad *pad, int mode,
                                               int64_t out[AP_CONTENT_FAMILY_SLOTS], int slots[AP_CONTENT_FAMILY_SLOTS])
{
    int n = 0, k;
    if (!pad || ap_content_mode_slot(pad, mode) < 0) return 0;
    for (k = 0; mode == AP_CONTENT_MODE_TROPHY && k < 5; ++k)
        if (pad->rung_locations[k] > 0) { out[n] = pad->rung_locations[k]; slots[n++] = k; }
    if (pad->wumpa_location > 0 && (pad->wumpa_modes & (1u << mode))) { out[n] = pad->wumpa_location; slots[n++] = 5; }
    for (k = 0; mode == AP_CONTENT_MODE_CTR && k < 3; ++k)
        if (pad->letter_locations[k] > 0) { out[n] = pad->letter_locations[k]; slots[n++] = 6 + k; }
    if (mode == AP_CONTENT_MODE_RELIC && pad->perfect_location > 0) {
        out[n] = pad->perfect_location; slots[n++] = AP_CONTENT_FAMILY_PERFECT;
    }
    return n;
}

// Item Box checks (#133 PR 4e) still unchecked on this pad. A box sits at a
// place, not at a race type, so every mode of the pad reaches all of them.
static inline int ap_content_box_pending(const ctr_content_pad *pad, ap_content_checked_fn checked)
{
    int b, left = 0;
    for (b = 0; pad && b < pad->box_count && b < 15; ++b)
        if (pad->box_locations[b] > 0 && !checked(pad->box_locations[b])) ++left;
    return left;
}

// Unchecked codes a mode still owns on this pad, its family checks and the
// pad's Item Box checks included.
static inline int ap_content_mode_pending(const ctr_content_pad *pad, int mode, ap_content_checked_fn checked)
{
    int64_t codes[3], family[AP_CONTENT_FAMILY_SLOTS];
    int slots[AP_CONTENT_FAMILY_SLOTS];
    int n = ap_content_mode_codes(pad, mode, codes), i, left = 0;
    for (i = 0; i < n; ++i)
        if (codes[i] > 0 && !checked(codes[i])) ++left;
    n = ap_content_mode_family_codes(pad, mode, family, slots);
    for (i = 0; i < n; ++i)
        if (!checked(family[i])) ++left;
    if (ap_content_mode_slot(pad, mode) >= 0)
        left += ap_content_box_pending(pad, checked);
    return left;
}

// The Reach 10 Wumpa check a race in `mode` on this pad sends, or -1: the
// track's own check (`per_track`) or the one global check, only for a mode
// with a Wumpa route. Never the engine host's retail code.
static inline int64_t ap_content_wumpa_code(const ctr_content_checks *fam, const ctr_content_pad *pad, int mode)
{
    if (!fam || !pad || mode < 0 || mode >= AP_CONTENT_MODE_COUNT || !(pad->wumpa_modes & (1u << mode))) return -1;
    if (fam->wumpa == 2) return pad->wumpa_location > 0 ? pad->wumpa_location : -1;
    if (fam->wumpa == 1) return fam->wumpa_global > 0 ? fam->wumpa_global : -1;
    return -1;
}

// Lettersanity on a content-plan CTR Challenge, in the legacy encoding of
// ap_lettersanity.h: a letter's location code or -1 when it has none.
static inline void ap_content_letter_codes(const ctr_content_pad *pad, long out[3])
{
    int l;
    for (l = 0; l < 3; ++l) out[l] = pad && pad->letter_locations[l] > 0 ? (long)pad->letter_locations[l] : -1;
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
        out->open[i] = ap_content_mode_open(pad, mode, counts, checked);
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
        if (ap_content_mode_open(pad, pad->modes[i], counts, checked)) return 2;
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
// 50400 + pad * 3 + tier, crystal pickups 60000 + pad * 20 + n, and the
// per-track families 56000 + pad * 16 + family slot (0..4 rungs, 5 Wumpa,
// 6..8 letters, #133 PR 3b; 9 Relic Race Perfect, #133 PR 3c).
#define AP_CONTENT_BIT_TROPHY 50000
#define AP_CONTENT_BIT_CTR 50200
#define AP_CONTENT_BIT_RELIC 50400
#define AP_CONTENT_BIT_FAMILY 56000
#define AP_CONTENT_FAMILY_STRIDE 16

// The family check behind a family display bit, or -1.
static inline int64_t ap_content_family_bit_code(const ctr_content_pad *pad, int slot)
{
    if (!pad || slot < 0 || slot >= AP_CONTENT_FAMILY_SLOTS) return -1;
    if (slot < 5) return pad->rung_locations[slot] > 0 ? pad->rung_locations[slot] : -1;
    if (slot == 5) return pad->wumpa_location > 0 ? pad->wumpa_location : -1;
    if (slot == AP_CONTENT_FAMILY_PERFECT) return pad->perfect_location > 0 ? pad->perfect_location : -1;
    return pad->letter_locations[slot - 6] > 0 ? pad->letter_locations[slot - 6] : -1;
}
// Glow prize slot of a family check under by_reward_type: rungs and Wumpa
// ride the race slot (0), letters the token slot (2), the Relic Race Perfect
// the relic slot (1) beside the tiers, as on retail pads (#439).
static inline int ap_content_family_reward_group(int slot)
{
    return slot == AP_CONTENT_FAMILY_PERFECT ? 1 : slot >= 6 ? 2 : 0;
}
static inline int ap_content_family_bit_decode(int bit, int *physical, int *slot)
{
    if (bit < AP_CONTENT_BIT_FAMILY || bit >= AP_CONTENT_BIT_FAMILY + 111 * AP_CONTENT_FAMILY_STRIDE) return 0;
    *physical = (bit - AP_CONTENT_BIT_FAMILY) / AP_CONTENT_FAMILY_STRIDE;
    *slot = (bit - AP_CONTENT_BIT_FAMILY) % AP_CONTENT_FAMILY_STRIDE;
    return *slot < AP_CONTENT_FAMILY_SLOTS;
}
// Item Box checks (#133 PR 4e): display bit 58000 + pad * 16 + slot (0..14).
// Under by_reward_type they ride the race slot, as on retail pads (ruling
// 2026-09-30).
#define AP_CONTENT_BIT_BOX 58000
#define AP_CONTENT_BOX_STRIDE 16
static inline int ap_content_box_bit_decode(int bit, int *physical, int *slot)
{
    if (bit < AP_CONTENT_BIT_BOX || bit >= AP_CONTENT_BIT_BOX + 111 * AP_CONTENT_BOX_STRIDE) return 0;
    *physical = (bit - AP_CONTENT_BIT_BOX) / AP_CONTENT_BOX_STRIDE;
    *slot = (bit - AP_CONTENT_BIT_BOX) % AP_CONTENT_BOX_STRIDE;
    return *slot < 15;
}
static inline int64_t ap_content_box_bit_code(const ctr_content_pad *pad, int slot)
{
    if (!pad || slot < 0 || slot >= 15 || slot >= pad->box_count) return -1;
    return pad->box_locations[slot] > 0 ? pad->box_locations[slot] : -1;
}
// Cups (#133 PR 5): a cup pad's Win rides the Trophy display bit; its sweep
// and its legs' Cup Leg Victory checks take 62200 + pad * 8 + slot (0 the
// sweep, 1..4 the legs). They ride the race slot under by_reward_type.
#define AP_CONTENT_BIT_CUP 62200
#define AP_CONTENT_CUP_STRIDE 8
static inline int ap_content_cup_bit_decode(int bit, int *physical, int *slot)
{
    if (bit < AP_CONTENT_BIT_CUP || bit >= AP_CONTENT_BIT_CUP + 111 * AP_CONTENT_CUP_STRIDE) return 0;
    *physical = (bit - AP_CONTENT_BIT_CUP) / AP_CONTENT_CUP_STRIDE;
    *slot = (bit - AP_CONTENT_BIT_CUP) % AP_CONTENT_CUP_STRIDE;
    return *slot <= AP_CONTENT_CUP_LEGS;
}
static inline int64_t ap_content_cup_bit_code(const ctr_content_pad *pad, int slot)
{
    int64_t code;
    if (!pad || !pad->cup || slot < 0 || slot > AP_CONTENT_CUP_LEGS) return -1;
    code = slot ? pad->legs[slot - 1].victory : pad->sweep_location;
    return code > 0 ? code : -1;
}
// The cup's distinct check codes: Win, sweep, then each leg's Cup Leg Victory
// once (a repeated leg track shares its check). Returns the count (<= 6);
// slots[i] is the display slot (-1 the Win, 0 the sweep, 1..4 a leg).
static inline int ap_content_cup_codes(const ctr_content_pad *pad, int64_t out[6], int slots[6])
{
    int n = 0, k, j, seen;
    if (!pad || !pad->cup) return 0;
    if (pad->trophy_location > 0) { out[n] = pad->trophy_location; slots[n++] = -1; }
    if (pad->sweep_location > 0) { out[n] = pad->sweep_location; slots[n++] = 0; }
    for (k = 0; k < AP_CONTENT_CUP_LEGS; ++k) {
        if (pad->legs[k].victory <= 0) continue;
        for (j = 0, seen = 0; j < n; ++j) seen |= out[j] == pad->legs[k].victory;
        if (!seen) { out[n] = pad->legs[k].victory; slots[n++] = k + 1; }
    }
    return n;
}
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
