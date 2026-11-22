// Pool-scaled parser caps and production mode gates, counts 1 through 99.
// cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all -I ap -o /tmp/test-pad-scaling tools/test-pad-scaling.c
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "ap_content_plan.h"

static int checks, failures, won;
static void expect(int good, const char *why)
{
    ++checks;
    if (!good) { ++failures; printf("FAIL: %s\n", why); }
}
static int checked(int64_t code) { return won && code == 1; }

int main(void)
{
    const int baseline[15] = {16, 18, 18, 0, 4, 4, 4, 4, 4, 1, 1, 1, 1, 1, 4};
    ctr_content_item items[15] = {{0}};
    for (int mult = 1; mult <= 100; ++mult) {
        for (int i = 0; i < 15; ++i) items[i].base = baseline[i] * mult;
        for (int stage = 1; stage <= 2; ++stage) {
            const int legacy = stage == 1 ? 8 : 4;
            for (int i = 0; i < 14; ++i) {
                int scaled = (i == 0 ? 16 : i <= 3 ? 36 : i <= 8 ? 20 : 5) * mult;
                if (scaled < legacy) scaled = legacy;
                expect(ap_content_random_pad_cap(1u << i, stage, items) == (scaled > 99 ? 99 : scaled),
                       "concrete cap follows family base supply");
            }
            const unsigned masks[3] = {0x000e, 0x01f0, 0x3e00};
            const int totals[3] = {36, 20, 5};
            for (int f = 0; f < 3; ++f) {
                int scaled = totals[f] * mult;
                if (scaled < legacy) scaled = legacy;
                expect(ap_content_random_pad_cap(masks[f], stage, items) == (scaled > 99 ? 99 : scaled), "any family cap");
            }
            expect(ap_content_random_pad_cap(1u << 14, stage, items) == 4, "Keys stay at four");
        }
    }
    memset(items, 0, sizeof(items));
    items[0].base = 4;
    items[0].extra = items[0].start_additional = INT_MAX;
    expect(ap_content_random_pad_cap(1, 1, items) == 8, "old small-pool stage 1 rooms retain admission");
    expect(ap_content_random_pad_cap(1, 2, items) == 4, "old small-pool stage 2 rooms retain admission");
    for (int i = 0; i < 15; ++i) items[i].base = INT_MAX;
    expect(ap_content_random_pad_cap(0x01f0, 1, items) == 99, "large totals cannot overflow");
    expect(ap_content_random_pad_cap(0x000e, 2, items) == 99, "large Relic totals cannot overflow");

    for (int family = 0; family < 4; ++family) {
        const unsigned masks[4] = {1, 0x000e, 0x01f0, 0x3e00};
        const int first[4] = {0, 1, 4, 9};
        for (int count = 1; count <= 99; ++count) {
            ctr_content_pad pad;
            memset(&pad, 0, sizeof(pad));
            pad.occupied = pad.has_rule = pad.has_stage2 = pad.first_race_rule = 1;
            pad.rule_mask = pad.stage2_mask = masks[family];
            pad.rule_count = count - 1;
            pad.stage2_count = count;
            pad.mode_count = 2;
            pad.modes[0] = AP_CONTENT_MODE_TROPHY; pad.modes[1] = AP_CONTENT_MODE_CTR;
            pad.stages[0] = 1; pad.stages[1] = 2; pad.trophy_location = 1;
            int counts[15] = {0};
            counts[first[family]] = count - 1;
            won = 1;
            expect(!ap_content_mode_open(&pad, AP_CONTENT_MODE_CTR, counts, checked), "stage 2 closed one below");
            ++counts[first[family]];
            expect(ap_content_mode_open(&pad, AP_CONTENT_MODE_CTR, counts, checked), "stage 2 opens exactly at count");
            won = 0;
            expect(!ap_content_mode_open(&pad, AP_CONTENT_MODE_CTR, counts, checked), "first race win still required");
            pad.rule_count = count;
            --counts[first[family]];
            expect(!ap_content_pad_open(&pad, counts), "stage 1 closed one below");
            ++counts[first[family]];
            expect(ap_content_pad_open(&pad, counts), "stage 1 opens exactly at count");
        }
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
