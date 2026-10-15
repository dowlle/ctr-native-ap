#ifndef AP_CONTENT_PLAN_H
#define AP_CONTENT_PLAN_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* First executable profile: independent standalone Trophy entries. Stored
 * records own their text; callers copy a record before crossing a load boundary.
 * A physical pad is never recovered by inverting its borrowed engine level. */
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
    int retail_id; /* -1 custom, otherwise 0..17 */
    int custom_slot, package_index, laps;
    int named_block; /* -1 generic slot ids, otherwise 0..9999 (#306) */
    int64_t trophy_location;
    char entry_id[65], track_id[65], display_name[129];
    char room_title[129]; /* what the room and Universal Tracker call this track */
} ctr_content_pad;

typedef struct ctr_content_package {
    char id[65], content_id[65], uuid[37], revision[65];
    char display_name[129], author[129], lev_sha256[65], vrm_sha256[65];
    uint32_t lev_bytes, vrm_bytes;
} ctr_content_package;

typedef struct ctr_content_item {
    int base, extra, start_from_pool, start_additional, remaining, receipt_cap;
} ctr_content_item;

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

/* Parsed != admitted. Net admission must bind the exact room location union
 * and identity before any gameplay consumer can obtain a pad or package. */
int ap_content_plan_present(void);
int ap_content_plan_active(void);
uint64_t ap_content_plan_epoch(void);
const ctr_content_pad *ap_content_plan_pad(int physical);
const ctr_content_package *ap_content_plan_package(int index);
const ctr_content_item *ap_content_plan_item(int64_t item);
int ap_content_plan_goal(const int received_gems[5]);
int ap_content_plan_bind(const char *seed, int team, int slot,
                         const int64_t *locations, size_t count);

#ifdef __cplusplus
}
#endif
#endif
