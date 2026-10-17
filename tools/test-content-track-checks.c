// Per-track check families on content-plan pads (#133 PR 3b): podium rungs,
// Reach 10 Wumpa and Lettersanity letters. Pins the pure decisions the hooks
// read: which family checks a mode reaches, pad pending and state, the Wumpa
// code a race sends, the family display bits and the Lettersanity answers on
// the pad's own letter codes. Relic Race Perfect (#133 PR 3c): the family
// slot of the Relic Race, the pad staying open for it, its glow prize slot,
// and the engine host of a Cortex Vortex pad.
// cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all -I ap -I . -I include -o /tmp/test-content-track-checks tools/test-content-track-checks.c
#include <stdio.h>
#include <string.h>
#include "ap_content_modes.h"
#include "ap_lettersanity.h"

static int checks, failures;
static void expect(int good, const char *why)
{
    ++checks;
    if (!good) { ++failures; printf("FAIL: %s\n", why); }
}

static int64_t checked_codes[32];
static int checked_count;
static int checked(int64_t code)
{
    int i;
    for (i = 0; i < checked_count; ++i)
        if (checked_codes[i] == code) return 1;
    return 0;
}
static void check(int64_t code) { checked_codes[checked_count++] = code; }

// Crash Cove with all three modes, free, every family on: four rungs (held
// 5th off), per-track Wumpa on Trophy and CTR, Lettersanity mode 2 with the C
// and R letters drawn.
static ctr_content_pad crash_cove(void)
{
    ctr_content_pad p;
    memset(&p, 0, sizeof p);
    p.physical = 3; p.hub = 26; p.occupied = 1;
    p.mode_count = 3; p.modes[0] = AP_CONTENT_MODE_TROPHY; p.modes[1] = AP_CONTENT_MODE_CTR; p.modes[2] = AP_CONTENT_MODE_RELIC;
    p.stages[0] = p.stages[1] = p.stages[2] = 1;
    p.trophy_location = 35011000; p.ctr_location = 35012300;
    p.relic_locations[0] = 35012000; p.relic_locations[1] = 35012100; p.relic_locations[2] = 35012200;
    p.rung_locations[0] = 35015100; p.rung_locations[1] = 35015101; p.rung_locations[3] = 35015103; p.rung_locations[4] = 35015002;
    p.wumpa_location = 35016104; p.wumpa_modes = 3u;
    p.letter_locations[0] = 35012500; p.letter_locations[2] = 35012502;
    p.letter_items[0] = 35010139; p.letter_items[2] = 35010141;
    return p;
}

int main(void)
{
    int64_t codes[AP_CONTENT_FAMILY_SLOTS];
    int slots[AP_CONTENT_FAMILY_SLOTS];
    int counts[15];
    ctr_content_pad p = crash_cove();
    ctr_content_checks fam;
    int n, i, physical, slot;

    memset(counts, 0, sizeof counts);
    memset(&fam, 0, sizeof fam);

    // Family codes per mode.
    n = ap_content_mode_family_codes(&p, AP_CONTENT_MODE_TROPHY, codes, slots);
    expect(n == 5 && codes[0] == 35015100 && slots[2] == 3 && codes[3] == 35015002 && codes[4] == 35016104 && slots[4] == 5,
           "Trophy Race reaches its four rungs and the Wumpa check, in slot order");
    n = ap_content_mode_family_codes(&p, AP_CONTENT_MODE_CTR, codes, slots);
    expect(n == 3 && codes[0] == 35016104 && codes[1] == 35012500 && slots[1] == 6 && codes[2] == 35012502 && slots[2] == 8,
           "CTR Challenge reaches the Wumpa check and its two drawn letters");
    expect(ap_content_mode_family_codes(&p, AP_CONTENT_MODE_RELIC, codes, slots) == 0, "a Relic Race reaches no family check");
    {
        ctr_content_pad relic = p;
        relic.mode_count = 1; relic.modes[0] = AP_CONTENT_MODE_RELIC;
        expect(ap_content_mode_family_codes(&relic, AP_CONTENT_MODE_TROPHY, codes, slots) == 0,
               "an absent mode reaches nothing even when codes are set");
    }

    // Pending and pad state: family checks keep a mode open until checked.
    checked_count = 0;
    check(35011000); check(35012300); check(35012000); check(35012100); check(35012200);
    expect(ap_content_mode_pending(&p, AP_CONTENT_MODE_TROPHY, checked) == 5, "rungs and Wumpa pending after the win");
    expect(ap_content_track_pad_state(&p, counts, checked) == 2, "pad stays open for its family checks");
    check(35015100); check(35015101); check(35015103); check(35015002);
    expect(ap_content_mode_pending(&p, AP_CONTENT_MODE_TROPHY, checked) == 1 &&
           ap_content_mode_pending(&p, AP_CONTENT_MODE_CTR, checked) == 3, "Wumpa pending in both modes that reach it");
    check(35016104); check(35012500);
    expect(ap_content_track_pad_state(&p, counts, checked) == 2, "one letter left keeps the pad open");
    check(35012502);
    expect(ap_content_track_pad_state(&p, counts, checked) == 4, "everything checked: replayable");
    {
        ctr_content_pad later = p;
        later.stages[1] = 2; later.has_stage2 = 1; later.stage2_mask = 1u; later.stage2_count = 2;
        checked_count = 5;
        check(35015100); check(35015101); check(35015103); check(35015002); check(35016104);
        expect(ap_content_track_pad_state(&later, counts, checked) == 3,
               "letters left only behind the second-stage gate: state 3");
    }
    {
        ctr_content_pad off = crash_cove();
        memset(off.rung_locations, 0, sizeof off.rung_locations);
        off.wumpa_location = 0; off.wumpa_modes = 0;
        memset(off.letter_locations, 0, sizeof off.letter_locations);
        memset(off.letter_items, 0, sizeof off.letter_items);
        checked_count = 0;
        check(35011000); check(35012300); check(35012000); check(35012100); check(35012200);
        expect(ap_content_track_pad_state(&off, counts, checked) == 4, "families off: the 3a pad state");
    }

    // Wumpa code a race sends.
    fam.wumpa = 2;
    expect(ap_content_wumpa_code(&fam, &p, AP_CONTENT_MODE_TROPHY) == 35016104 &&
           ap_content_wumpa_code(&fam, &p, AP_CONTENT_MODE_CTR) == 35016104, "per_track: the track's own check from Trophy and CTR");
    expect(ap_content_wumpa_code(&fam, &p, AP_CONTENT_MODE_RELIC) == -1, "per_track: a Relic Race sends nothing");
    fam.wumpa = 1; fam.wumpa_global = 35016100;
    {
        ctr_content_pad g = p;
        g.wumpa_location = 0;
        expect(ap_content_wumpa_code(&fam, &g, AP_CONTENT_MODE_CTR) == 35016100, "global: the one global check");
        g.wumpa_modes = 1u;
        expect(ap_content_wumpa_code(&fam, &g, AP_CONTENT_MODE_CTR) == -1, "global: only modes with a Wumpa route");
        expect(ap_content_mode_family_codes(&g, AP_CONTENT_MODE_TROPHY, codes, slots) == 4,
               "the global check keeps no pad open");
        g.wumpa_modes = 0;
        expect(ap_content_wumpa_code(&fam, &g, AP_CONTENT_MODE_TROPHY) == -1, "global: a track without fruit sends nothing");
    }
    fam.wumpa = 0;
    expect(ap_content_wumpa_code(&fam, &p, AP_CONTENT_MODE_TROPHY) == -1, "off: nothing");
    expect(ap_content_wumpa_code(NULL, &p, AP_CONTENT_MODE_TROPHY) == -1 && ap_content_wumpa_code(&fam, NULL, 0) == -1 &&
           ap_content_wumpa_code(&fam, &p, 7) == -1, "bad input: nothing");

    // Display bits round trip.
    for (slot = 0; slot < AP_CONTENT_FAMILY_SLOTS; ++slot)
    {
        const int bit = AP_CONTENT_BIT_FAMILY + p.physical * AP_CONTENT_FAMILY_STRIDE + slot;
        const int64_t want = slot < 5 ? p.rung_locations[slot] : slot == 5 ? p.wumpa_location :
                             slot == AP_CONTENT_FAMILY_PERFECT ? p.perfect_location : p.letter_locations[slot - 6];
        expect(ap_content_family_bit_decode(bit, &physical, &i) && physical == 3 && i == slot &&
               ap_content_family_bit_code(&p, i) == (want > 0 ? want : -1), "family bit decodes to its code");
        expect(!ap_content_bit_decode(bit, &physical, &i, &n), "family bits are outside the mode bit ranges");
    }
    expect(!ap_content_family_bit_decode(AP_CONTENT_BIT_FAMILY + 3 * AP_CONTENT_FAMILY_STRIDE + 10, &physical, &slot) &&
           !ap_content_family_bit_decode(AP_CONTENT_BIT_FAMILY - 1, &physical, &slot) &&
           !ap_content_family_bit_decode(AP_CONTENT_BIT_FAMILY + 111 * AP_CONTENT_FAMILY_STRIDE, &physical, &slot),
           "unused and out-of-range family bits decode to nothing");
    expect(AP_CONTENT_BIT_FAMILY + 111 * AP_CONTENT_FAMILY_STRIDE <= 60000, "family bits stay below the crystal pickups");

    // Lettersanity on the pad's own codes (ap_lettersanity.h encoding).
    {
        long lc[3];
        unsigned char none[3] = {0, 0, 0}, cOnly[3] = {1, 0, 0}, both[3] = {1, 0, 1}, all[3] = {1, 1, 1};
        ap_content_letter_codes(&p, lc);
        expect(lc[0] == 35012500 && lc[1] == -1 && lc[2] == 35012502, "letter codes: absent is -1");
        // Mode 2: an undrawn letter is transparent, a drawn one needs its item.
        expect(!AP_LetterAvailablePure(1, 2, lc[1], 1), "mode 2: undrawn T cannot be picked up");
        expect(!AP_LetterAvailablePure(1, 2, lc[0], 0) && AP_LetterAvailablePure(1, 2, lc[0], 1), "mode 2: C after its item");
        expect(AP_LettersRequiredCountPure(1, 2, lc) == 2, "mode 2: two letters win the token");
        expect(!AP_LetterTokenEarnedPure(1, 2, 1, 2, lc, cOnly) && AP_LetterTokenEarnedPure(1, 2, 1, 2, lc, both) &&
               !AP_LetterTokenEarnedPure(0, 2, 1, 2, lc, both) && !AP_LetterTokenEarnedPure(1, 1, 1, 2, lc, both),
               "mode 2: a win with both drawn letters and both items");
        // Mode 3: every letter needs its item; no letter checks.
        {
            ctr_content_pad m3 = crash_cove();
            memset(m3.letter_locations, 0, sizeof m3.letter_locations);
            m3.letter_items[1] = 35010140;
            ap_content_letter_codes(&m3, lc);
            expect(lc[0] == -1 && lc[1] == -1 && lc[2] == -1, "mode 3: no letter checks");
            expect(!AP_LetterAvailablePure(1, 3, lc[1], 0) && AP_LetterAvailablePure(1, 3, lc[1], 1), "mode 3: T after its item");
            expect(AP_LettersRequiredCountPure(1, 3, lc) == 3 && !AP_LetterTokenEarnedPure(1, 3, 1, 3, lc, both) &&
                   AP_LetterTokenEarnedPure(1, 3, 1, 3, lc, all), "mode 3: three letters and all three items");
        }
        // Modes 0 and 1: the retail letters and token rule.
        ap_content_letter_codes(&p, lc);
        expect(AP_LetterAvailablePure(1, 1, lc[1], 0) && AP_LetterAvailablePure(1, 0, lc[0], 0), "modes 0 and 1: letters stay vanilla");
        expect(AP_LetterTokenEarnedPure(1, 3, 1, 1, lc, none) && !AP_LetterTokenEarnedPure(1, 2, 1, 1, lc, none) &&
               AP_LetterTokenEarnedPure(1, 3, 1, 0, lc, none), "modes 0 and 1: a win with all three letters");
    }

    // Relic Race Perfect (#133 PR 3c): the Relic Race's own family check.
    {
        ctr_content_pad r = crash_cove();
        r.perfect_location = 35012400;
        n = ap_content_mode_family_codes(&r, AP_CONTENT_MODE_RELIC, codes, slots);
        expect(n == 1 && codes[0] == 35012400 && slots[0] == AP_CONTENT_FAMILY_PERFECT, "a Relic Race reaches its perfect");
        n = ap_content_mode_family_codes(&r, AP_CONTENT_MODE_TROPHY, codes, slots);
        expect(n == 5, "the perfect is not a Trophy Race check");
        {
            ctr_content_pad noRelic = r;
            noRelic.mode_count = 2;
            expect(ap_content_mode_family_codes(&noRelic, AP_CONTENT_MODE_RELIC, codes, slots) == 0,
                   "no Relic mode reaches no perfect");
        }
        checked_count = 0;
        check(35011000); check(35012300); check(35012000); check(35012100); check(35012200);
        check(35015100); check(35015101); check(35015103); check(35015002); check(35016104); check(35012500); check(35012502);
        expect(ap_content_mode_pending(&r, AP_CONTENT_MODE_RELIC, checked) == 1 && ap_content_track_pad_state(&r, counts, checked) == 2,
               "every relic tier checked: the unchecked perfect keeps the pad open (#453)");
        {
            ctr_content_pad later = r;
            later.stages[2] = 2; later.has_stage2 = 1; later.stage2_mask = 1u; later.stage2_count = 2;
            expect(ap_content_track_pad_state(&later, counts, checked) == 3, "a perfect behind the second stage: state 3");
        }
        check(35012400);
        expect(ap_content_track_pad_state(&r, counts, checked) == 4, "the perfect checked: replayable");
        expect(ap_content_family_bit_code(&r, AP_CONTENT_FAMILY_PERFECT) == 35012400 &&
               ap_content_family_bit_code(&p, AP_CONTENT_FAMILY_PERFECT) == -1, "perfect display bit resolves to its code");
        expect(ap_content_family_reward_group(AP_CONTENT_FAMILY_PERFECT) == 1 && ap_content_family_reward_group(0) == 0 &&
               ap_content_family_reward_group(5) == 0 && ap_content_family_reward_group(6) == 2 &&
               ap_content_family_reward_group(8) == 2, "glow prize slots: perfect relic, rungs and Wumpa race, letters token");
        r.retail_id = 3; expect(ap_content_pad_host(&r) == 3, "a retail pad races its own LevelID");
        r.retail_id = -1; expect(ap_content_pad_host(&r) == AP_CONTENT_CUSTOM_HOST, "a custom pad borrows host 6");
        r.cortex = 1; expect(ap_content_pad_host(&r) == 13, "Cortex Vortex races on host 13");
    }

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
