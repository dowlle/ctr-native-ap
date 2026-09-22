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
    int retail_id; /* -1 custom, otherwise 0..17 */
    int custom_slot, package_index, laps;
    int64_t trophy_location;
    char entry_id[65], track_id[65], display_name[129];
} ctr_content_pad;

typedef struct ctr_content_package {
    char id[65], content_id[65], uuid[37], revision[65];
    char display_name[129], author[129], lev_sha256[65], vrm_sha256[65];
    uint32_t lev_bytes, vrm_bytes;
} ctr_content_package;

typedef struct ctr_content_item {
    int base, extra, start_from_pool, start_additional, remaining, receipt_cap;
} ctr_content_item;

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
