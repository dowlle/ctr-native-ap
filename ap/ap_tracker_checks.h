#ifndef AP_TRACKER_CHECKS_H
#define AP_TRACKER_CHECKS_H

#include <stdint.h>
#include "ap_content_plan.h"

/* Read-only check progress. Shared routes contribute one location, never one
 * check per race. The sanity parser already deduplicates a cup's pad codes. */
typedef struct {
    int total, done, in_logic, out_logic, unknown;
} AP_TrackerCheckCount;

typedef struct {
    int (*checked)(int64_t);
    int (*logic)(int64_t); /* 0 out, 1 in, 2 unknown */
} AP_TrackerCheckSource;

static inline void AP_TrackerCheckAdd(AP_TrackerCheckCount *row, int64_t code,
                                    AP_TrackerCheckSource source)
{
    int state;
    if (code <= 0) return;
    ++row->total;
    if (source.checked(code)) { ++row->done; return; }
    state = source.logic(code);
    if (state == 0) ++row->out_logic;
    else if (state == 1) ++row->in_logic;
    else ++row->unknown;
}

/* Boss venues have their own track record, independent of standalone pads.
 * counts[] is the LEV object inventory, not the selected check count: use the
 * positive location slots, including holes left by exclusions. */
static inline void AP_TrackerCheckSanity(AP_TrackerCheckCount rows[3],
                                       const ctr_content_sanity *sanity,
                                       AP_TrackerCheckSource source)
{
    int family, i;
    if (!sanity) return;
    for (family = 0; family < 3; ++family)
        for (i = 0; i < 80; ++i)
            AP_TrackerCheckAdd(&rows[family], sanity->locations[family][i], source);
}

#endif
