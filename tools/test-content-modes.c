// Track-mode decisions for content-plan pads (#133 PR 3): the mode gate, the
// selector rows, pad state, relic tiers, race awards and pad display bits.
// cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all -I ap -I . -I include -o /tmp/test-content-modes tools/test-content-modes.c
#include <stdio.h>
#include <string.h>
#include "ap_content_modes.h"

static int checks, failures;
static void expect(int good, const char *why)
{
    ++checks;
    if (!good) { ++failures; printf("FAIL: %s\n", why); }
}

static int64_t checked_codes[16];
static int checked_count;
static int checked(int64_t code)
{
    int i;
    for (i = 0; i < checked_count; ++i)
        if (checked_codes[i] == code) return 1;
    return 0;
}

// A retail-style pad with all three modes in the given order, a free stage 1
// and a stage-2 rule of `need` Gold Relics.
static ctr_content_pad pad3(int first, int second, int third, int need)
{
    ctr_content_pad p;
    memset(&p, 0, sizeof p);
    p.physical = 3; p.hub = 26; p.keys = 0; p.occupied = 1;
    p.mode_count = 3; p.modes[0] = first; p.modes[1] = second; p.modes[2] = third;
    p.stages[0] = 1; p.stages[1] = p.stages[2] = 2;
    p.has_stage2 = 1; p.stage2_count = need; p.stage2_mask = 1u << 2; // Gold Relic
    p.trophy_location = 35011000; p.ctr_location = 35012300;
    p.relic_locations[0] = 35012000; p.relic_locations[1] = 35012100; p.relic_locations[2] = 35012200;
    return p;
}

int main(void)
{
    int counts[15];
    ctr_content_pad p = pad3(AP_CONTENT_MODE_TROPHY, AP_CONTENT_MODE_CTR, AP_CONTENT_MODE_RELIC, 2);
    ap_content_selector sel;
    int64_t codes[3];

    memset(counts, 0, sizeof counts);
    checked_count = 0;

    // Gates: stage 1 opens with the pad; stage 2 needs its own rule, never a
    // win in another mode.
    expect(ap_content_mode_open(&p, AP_CONTENT_MODE_TROPHY, counts), "stage-1 mode opens with the pad");
    expect(!ap_content_mode_open(&p, AP_CONTENT_MODE_CTR, counts), "stage-2 CTR closed without its rule");
    expect(!ap_content_mode_open(&p, AP_CONTENT_MODE_RELIC, counts), "stage-2 Relic closed without its rule");
    counts[2] = 1;
    expect(!ap_content_mode_open(&p, AP_CONTENT_MODE_CTR, counts), "one of two Gold Relics is not enough");
    counts[2] = 2;
    expect(ap_content_mode_open(&p, AP_CONTENT_MODE_CTR, counts) && ap_content_mode_open(&p, AP_CONTENT_MODE_RELIC, counts),
           "both later modes open on the stage-2 rule alone (Trophy unchecked)");
    expect(ap_content_stage2_owned(&p, counts) == 2, "stage-2 owned count");
    p.keys = 2;
    expect(!ap_content_mode_open(&p, AP_CONTENT_MODE_TROPHY, counts) && !ap_content_mode_open(&p, AP_CONTENT_MODE_CTR, counts),
           "the hub Key floor closes every mode");
    counts[14] = 2;
    expect(ap_content_mode_open(&p, AP_CONTENT_MODE_TROPHY, counts), "Keys reopen the pad");
    p.has_rule = 1; p.rule_mask = 1u; p.rule_count = 3; // manual stage-1 rule: 3 Trophies
    expect(!ap_content_mode_open(&p, AP_CONTENT_MODE_RELIC, counts), "a manual stage-1 rule also closes stage 2");
    counts[0] = 3;
    expect(ap_content_mode_open(&p, AP_CONTENT_MODE_RELIC, counts), "manual rule and stage-2 rule both met");
    p.has_rule = 0; p.keys = 0;

    // Codes per mode, and an absent mode owns none.
    expect(ap_content_mode_codes(&p, AP_CONTENT_MODE_RELIC, codes) == 3 && codes[2] == 35012200, "relic owns three codes");
    expect(ap_content_mode_codes(&p, AP_CONTENT_MODE_CTR, codes) == 1 && codes[0] == 35012300, "CTR owns one code");
    // Relic tier checks follow the item counts (one generator row 2): the race
    // keeps its route when the seed holds some tiers or none.
    {
        ctr_content_pad cut = p;
        expect(ap_content_mode_route(&cut, AP_CONTENT_MODE_RELIC) == 35012000, "a route without relic_route is the first code");
        cut.relic_route = 35012000;
        cut.relic_locations[0] = 0;
        expect(ap_content_mode_route(&cut, AP_CONTENT_MODE_RELIC) == 35012000, "a missing Sapphire check keeps the route");
        cut.relic_locations[1] = cut.relic_locations[2] = 0;
        expect(ap_content_mode_route(&cut, AP_CONTENT_MODE_RELIC) == 35012000, "no tier check keeps the route");
        expect(ap_content_mode_route(&cut, AP_CONTENT_MODE_CTR) == 35012300, "other modes route on their own code");
    }
    {
        ctr_content_pad relicOnly = pad3(AP_CONTENT_MODE_RELIC, 0, 0, 0);
        relicOnly.mode_count = 1; relicOnly.has_stage2 = 0; relicOnly.trophy_location = 0; relicOnly.ctr_location = 0;
        expect(ap_content_mode_codes(&relicOnly, AP_CONTENT_MODE_TROPHY, codes) == 0, "absent Trophy owns no code");
        expect(ap_content_mode_open(&relicOnly, AP_CONTENT_MODE_RELIC, counts) &&
               !ap_content_mode_open(&relicOnly, AP_CONTENT_MODE_TROPHY, counts), "a Relic-only entry opens Relic only");
    }

    // Single stage: every mode uses the entrance gate.
    {
        ctr_content_pad single = pad3(AP_CONTENT_MODE_CTR, AP_CONTENT_MODE_TROPHY, AP_CONTENT_MODE_RELIC, 0);
        int none[15];
        memset(none, 0, sizeof none);
        single.has_stage2 = 0; single.stages[1] = single.stages[2] = 1;
        expect(ap_content_mode_open(&single, AP_CONTENT_MODE_RELIC, none) && ap_content_mode_open(&single, AP_CONTENT_MODE_TROPHY, none),
               "single stage opens every mode with the pad");
    }

    // Selector: rows in pad order, locked rows marked, cursor on the first open
    // row with a check left.
    memset(counts, 0, sizeof counts);
    p = pad3(AP_CONTENT_MODE_RELIC, AP_CONTENT_MODE_TROPHY, AP_CONTENT_MODE_CTR, 1);
    expect(ap_content_selector_build(&p, counts, checked, &sel) == 1, "one open row before stage 2");
    expect(sel.count == 3 && sel.mode[0] == AP_CONTENT_MODE_RELIC && sel.mode[1] == AP_CONTENT_MODE_TROPHY &&
           sel.mode[2] == AP_CONTENT_MODE_CTR, "rows follow the pad's mode order");
    expect(sel.open[0] && !sel.open[1] && !sel.open[2] && sel.cursor == 0, "locked rows and cursor");
    expect(sel.pending[0] == 3 && sel.pending[1] == 1 && sel.pending[2] == 1, "pending counts per row");
    counts[2] = 1;
    checked_codes[checked_count++] = 35012000;
    checked_codes[checked_count++] = 35012100;
    checked_codes[checked_count++] = 35012200;
    expect(ap_content_selector_build(&p, counts, checked, &sel) == 3, "all rows open with the rule");
    expect(sel.cursor == 1 && sel.pending[0] == 0, "cursor skips a fully checked open row");
    checked_codes[checked_count++] = 35011000;
    checked_codes[checked_count++] = 35012300;
    expect(ap_content_selector_build(&p, counts, checked, &sel) == 3 && sel.cursor == 0,
           "everything checked: cursor on the first open row, still replayable");

    // Pad state.
    checked_count = 0;
    memset(counts, 0, sizeof counts);
    p = pad3(AP_CONTENT_MODE_TROPHY, AP_CONTENT_MODE_CTR, AP_CONTENT_MODE_RELIC, 1);
    expect(ap_content_track_pad_state(&p, counts, checked) == 2, "raceable with the Trophy open");
    checked_codes[checked_count++] = 35011000;
    expect(ap_content_track_pad_state(&p, counts, checked) == 3, "only stage-2 checks left: re-locked");
    counts[2] = 1;
    expect(ap_content_track_pad_state(&p, counts, checked) == 2, "stage 2 opens: raceable");
    checked_codes[checked_count++] = 35012300;
    checked_codes[checked_count++] = 35012000;
    checked_codes[checked_count++] = 35012100;
    expect(ap_content_track_pad_state(&p, counts, checked) == 2, "one relic tier left");
    checked_codes[checked_count++] = 35012200;
    expect(ap_content_track_pad_state(&p, counts, checked) == 4, "all checked: replayable");
    p.keys = 1;
    expect(ap_content_track_pad_state(&p, counts, checked) == 1, "hub Keys missing: locked");

    // Relic tiers: at or under the target, each tier independently.
    {
        const int targets[3] = {96000, 86400, 76800};
        const int none[3] = {-1, -1, -1};
        expect(ap_content_relic_tiers(96001, targets) == 0, "slower than Sapphire earns nothing");
        expect(ap_content_relic_tiers(96000, targets) == 1, "exactly Sapphire");
        expect(ap_content_relic_tiers(86400, targets) == 3, "Gold also earns Sapphire");
        expect(ap_content_relic_tiers(1, targets) == 7, "Platinum earns every tier");
        expect(ap_content_relic_tiers(1, none) == 0, "missing targets earn nothing");
    }

    // Race awards.
    {
        int rank, letters;
        for (rank = 0; rank < 8; ++rank)
            for (letters = 0; letters <= 3; ++letters)
            {
                expect(ap_content_race_award(AP_CONTENT_MODE_TROPHY, rank, letters) == (rank == 0), "Trophy on first place");
                expect(ap_content_race_award(AP_CONTENT_MODE_CTR, rank, letters) == (rank == 0 && letters == 3),
                       "CTR on first place with all letters");
                expect(!ap_content_race_award(AP_CONTENT_MODE_RELIC, rank, letters), "Relic pays by time only");
            }
    }

    // Display bits round-trip, and the ranges stay clear of crystal pickups.
    {
        int physical, mode, tier, pad;
        for (pad = 0; pad <= 104; ++pad)
        {
            expect(ap_content_bit_decode(AP_CONTENT_BIT_TROPHY + pad, &physical, &mode, &tier) && physical == pad &&
                   mode == AP_CONTENT_MODE_TROPHY, "Trophy bit");
            expect(ap_content_bit_decode(AP_CONTENT_BIT_CTR + pad, &physical, &mode, &tier) && physical == pad &&
                   mode == AP_CONTENT_MODE_CTR, "CTR bit");
            for (tier = 0; tier < 3; ++tier)
            {
                int t = -1;
                expect(ap_content_bit_decode(AP_CONTENT_BIT_RELIC + pad * 3 + tier, &physical, &mode, &t) &&
                       physical == pad && mode == AP_CONTENT_MODE_RELIC && t == tier, "relic bit");
            }
        }
        expect(!ap_content_bit_decode(AP_CONTENT_BIT_TROPHY + 105, &physical, &mode, &tier), "Trophy range ends at pad 104");
        expect(!ap_content_bit_decode(AP_CONTENT_BIT_RELIC + 105 * 3, &physical, &mode, &tier), "relic range ends at pad 104");
        expect(AP_CONTENT_BIT_RELIC + 105 * 3 <= 52000, "content bits end below the crystal pseudo-bits");
        expect(!ap_content_bit_decode(60000, &physical, &mode, &tier), "crystal pickups are not mode bits");
    }

    expect(!strcmp(ap_content_mode_label(AP_CONTENT_MODE_CTR), "CTR"), "labels");
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
