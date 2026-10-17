#ifndef AP_CONTENT_PLAN_H
#define AP_CONTENT_PLAN_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Standalone track entries with their Trophy Race, CTR Challenge and Relic
 * Race modes (#133 PR 3), per-track check families (#133 PR 3b), Cortex
 * Vortex as a track and Relic Race Perfect checks (#133 PR 3c), and Crystal
 * Challenge entries. Stored records own
 * their text; callers copy a record before crossing a load boundary. A
 * physical pad is never recovered by inverting its borrowed engine level. */
#define AP_CONTENT_MODE_TROPHY 0
#define AP_CONTENT_MODE_CTR 1
#define AP_CONTENT_MODE_RELIC 2
#define AP_CONTENT_MODE_COUNT 3
typedef struct ctr_content_pad {
    int physical, hub, keys, occupied;
    /* Manual stage-1 rule (#463), the resolved `manual_pad_requirements_v1`
     * record for this pad. rule_mask has bit i set for item 35010000 + i; the
     * rule is met when the received counts of the masked items sum to at least
     * rule_count (one bit is a concrete item, several are an Any rule over the
     * items the seed has as base items). has_rule 0 is a free pad: only the hub
     * Key floor (`keys`) applies. */
    int has_rule, rule_count;
    unsigned rule_mask;
    int retail_id; /* -1 custom or Cortex Vortex, otherwise engine track/arena LevelID */
    /* Cortex Vortex (#133 PR 3c): the bundled pair under its own identity,
     * raced on AP_CONTENT_CORTEX_HOST with no package row and no custom slot
     * (package_index -1, custom_slot 0). */
    int cortex;
    int custom_slot, package_index, laps;
    int crystal, target, time_limit_ticks, crystal_count;
    int64_t crystal_locations[20];
    int named_block; /* -1 generic slot ids, otherwise 0..9999 (#306) */
    /* Crystal Challenge completion code, or the Trophy Race code of a track
     * entry with a Trophy mode; 0 for a track entry without one. */
    int64_t trophy_location;
    /* Track modes in the pad's resolved order (#133 PR 3): modes[0] opens with
     * the pad (stage 1); a later mode is stage 2 when stages[i] == 2, and then
     * also needs the pad's second-stage rule (stage2_mask/stage2_count, same
     * encoding as rule_mask). mode_count is 0 for a Crystal Challenge. */
    int mode_count, modes[AP_CONTENT_MODE_COUNT], stages[AP_CONTENT_MODE_COUNT];
    int has_stage2, stage2_count;
    unsigned stage2_mask;
    int64_t ctr_location;         /* CTR Token Challenge, 0 without the mode */
    int64_t relic_locations[3];   /* Sapphire, Gold, Platinum Time Trial */
    /* Per-track check families (#133 PR 3b), 0 when the seed or track has
     * none. rung_locations: the podium rungs of the Trophy Race in SLOT_ORDER
     * (held 1st, 3rd, 5th, finish on podium, finish any). wumpa_location: the
     * track's own Reach 10 Wumpa check (`wumpa: per_track`). wumpa_modes: bit
     * per mode with a Wumpa route (Trophy and CTR, on a track that can reach
     * ten fruit), also for `wumpa: global`. letter_locations: the C/T/R
     * letter checks of the CTR Challenge (Lettersanity modes 1 and 2).
     * letter_items: the C/T/R letter items the seed pools for it (modes 2 and
     * 3); the CTR Challenge then pays only with the pooled letters received. */
    int64_t rung_locations[5];
    int64_t wumpa_location;
    unsigned wumpa_modes;
    int64_t letter_locations[3];
    int64_t letter_items[3];
    /* Relic Race Perfect (#133 PR 3c, #453): the check for breaking every time
     * crate in this pad's Relic Race; 0 when the seed or entry has none. */
    int64_t perfect_location;
    char entry_id[65], track_id[65], display_name[129];
    char room_title[129]; /* what the room and Universal Tracker call this track */
} ctr_content_pad;

/* Open admission (#133 PR 2): any complete descriptor, not only compiled
 * revisions. Bytes are located and verified on load, and a claimed capability
 * the bytes do not have refuses the package (ap_content_capability.h). */
#define AP_CONTENT_MAX_PACKAGES 32
/* Descriptor capability flags (worlds/ctr/custom_packages.py CAPABILITIES). */
#define AP_CONTENT_CAP_TIME_TRIAL 0x01u
#define AP_CONTENT_CAP_ARCADE 0x02u
#define AP_CONTENT_CAP_RELIC 0x04u
#define AP_CONTENT_CAP_CTR_TOKENS 0x08u
#define AP_CONTENT_CAP_WUMPA 0x10u
#define AP_CONTENT_CAP_ALL 0x1fu
#define AP_CONTENT_RETAIL_LAPS 3 /* retail tracks race their authored three laps */
/* Engine hosts of non-retail track entries: a custom package borrows Roo's
 * Tubes; Cortex Vortex keeps the Oxide Station slot its bundled pair is
 * served on by the legacy pad track and the Oxide 2 venue. */
#define AP_CONTENT_CUSTOM_HOST 6
#define AP_CONTENT_CORTEX_HOST 13
#define AP_CONTENT_CORTEX_LAPS 3
typedef struct ctr_content_package {
    char id[65], content_id[65], uuid[37], revision[65];
    char display_name[129], author[129], lev_sha256[65], vrm_sha256[65];
    uint32_t lev_bytes, vrm_bytes;
    int laps;              /* authored laps, 1..7 */
    unsigned capabilities; /* AP_CONTENT_CAP_* bits the descriptor claims */
    int has_relic_targets; /* relic targets in ticks (960 per second) */
    int relic_targets[3];  /* sapphire, gold, platinum */
} ctr_content_package;

typedef struct ctr_content_item {
    int base, extra, start_from_pool, start_additional, remaining, receipt_cap;
} ctr_content_item;

/* The plan's per-track check settings (#133 PR 3b): `lettersanity` 0 off,
 * 1 locations, 2 locations and items, 3 items; `wumpa` 0 off, 1 global,
 * 2 per track, with the global check's code. `relic_perfect` 1 when Relic
 * Race Perfect checks are on (#133 PR 3c). */
typedef struct ctr_content_checks {
    int podium, lettersanity, letters_per_track, wumpa;
    int64_t wumpa_global;
    int relic_perfect;
} ctr_content_checks;

/* The engine LevelID a track entry races on. */
static inline int ap_content_pad_host(const struct ctr_content_pad *pad)
{
    return pad->retail_id >= 0 ? pad->retail_id : pad->cortex ? AP_CONTENT_CORTEX_HOST : AP_CONTENT_CUSTOM_HOST;
}

/* The single pad-entry evaluator for the content-plan profile. Display, pad
 * state, capture, the final load gate and the tracker launch all call it, so
 * the answers cannot diverge. `counts` is indexed by item offset (the 15
 * AP_IDX_* tallies). */
static inline int ap_content_rule_owned(const ctr_content_pad *pad, const int *counts)
{
    int i, owned = 0;
    for (i = 0; i < 15; ++i)
        if (pad->rule_mask & (1u << i)) owned += counts[i];
    return owned;
}
static inline int ap_content_pad_open(const ctr_content_pad *pad, const int *counts)
{
    return pad && pad->occupied && counts[14] >= pad->keys &&
           (!pad->has_rule || ap_content_rule_owned(pad, counts) >= pad->rule_count);
}
/* Position of a mode in the pad's order, or -1 when the entry lacks it. */
static inline int ap_content_mode_slot(const ctr_content_pad *pad, int mode)
{
    int i;
    for (i = 0; pad && i < pad->mode_count && i < AP_CONTENT_MODE_COUNT; ++i)
        if (pad->modes[i] == mode) return i;
    return -1;
}
static inline int ap_content_stage2_owned(const ctr_content_pad *pad, const int *counts)
{
    int i, owned = 0;
    for (i = 0; i < 15; ++i)
        if (pad->stage2_mask & (1u << i)) owned += counts[i];
    return owned;
}
/* The single mode gate: the pad is open, and a stage-2 mode's second rule is
 * met. No mode needs a win in another one (Wayfinder ticket 16). Pad entry,
 * the mode selector, the race-start gate and the tracker all call this. */
static inline int ap_content_mode_open(const ctr_content_pad *pad, int mode, const int *counts)
{
    const int i = ap_content_mode_slot(pad, mode);
    if (i < 0 || !ap_content_pad_open(pad, counts)) return 0;
    return pad->stages[i] == 1 || (pad->has_stage2 && ap_content_stage2_owned(pad, counts) >= pad->stage2_count);
}
/* The check codes a mode owns on this pad (Trophy 1, CTR 1, Relic 3); 0 when
 * the entry lacks the mode. */
static inline int ap_content_mode_codes(const ctr_content_pad *pad, int mode, int64_t out[3])
{
    if (ap_content_mode_slot(pad, mode) < 0) return 0;
    if (mode == AP_CONTENT_MODE_TROPHY) { out[0] = pad->trophy_location; return 1; }
    if (mode == AP_CONTENT_MODE_CTR) { out[0] = pad->ctr_location; return 1; }
    out[0] = pad->relic_locations[0]; out[1] = pad->relic_locations[1]; out[2] = pad->relic_locations[2];
    return 3;
}

/* Parsed != admitted. Net admission must bind the exact room location union
 * and identity before any gameplay consumer can obtain a pad or package. */
int ap_content_plan_present(void);
int ap_content_plan_active(void);
uint64_t ap_content_plan_epoch(void);
const ctr_content_pad *ap_content_plan_pad(int physical);
const ctr_content_package *ap_content_plan_package(int index);
/* 1 when the active plan has a Cortex Vortex track entry (#133 PR 3c). */
int ap_content_plan_cortex(void);
const ctr_content_item *ap_content_plan_item(int64_t item);
const ctr_content_checks *ap_content_plan_checks(void);
int ap_content_plan_goal(const int received_gems[5]);
int ap_content_plan_bind(const char *seed, int team, int slot,
                         const int64_t *locations, size_t count);

#ifdef __cplusplus
}
#endif
#endif
