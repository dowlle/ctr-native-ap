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

enum {
    AP_TRACKER_NODE_EMPTY, AP_TRACKER_NODE_DONE, AP_TRACKER_NODE_OUT,
    AP_TRACKER_NODE_IN, AP_TRACKER_NODE_PARTIAL, AP_TRACKER_NODE_UNKNOWN
};

static inline int AP_TrackerCheckNodeState(AP_TrackerCheckCount checks, int partial)
{
    if (!checks.total) return AP_TRACKER_NODE_EMPTY;
    if (checks.done == checks.total) return AP_TRACKER_NODE_DONE;
    if (checks.in_logic) {
        if (!partial || checks.done + checks.in_logic == checks.total)
            return AP_TRACKER_NODE_IN;
        if (checks.out_logic) return AP_TRACKER_NODE_PARTIAL;
        return AP_TRACKER_NODE_UNKNOWN;
    }
    return checks.unknown ? AP_TRACKER_NODE_UNKNOWN : AP_TRACKER_NODE_OUT;
}

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
