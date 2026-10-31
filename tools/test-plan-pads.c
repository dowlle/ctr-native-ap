// One generator row 6: what a plan pad does in the client, from its pure
// decisions. The race type menu (rows, cursor, always, productive, the Options
// row), the pad that closes again after its first race, the AP BOXES LEFT line,
// the glow's open modes and the DeathLink row's shown value. The engine half
// (RECTMENU, the warp, the models) is pinned by test-plan-pads-production.py;
// only a played room proves it.
// cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all -DCTR_AP -I ap -I . -I include -o /tmp/test-plan-pads tools/test-plan-pads.c
#include <stdio.h>
#include <string.h>
#include "ap_content_modes.h"
#include "ap_pad_state.h"
#include "ap_race_attempt_logic.h"

static int failures, total;
static void expect(int ok, const char *what)
{
    total++;
    if (!ok) { failures++; printf("FAIL: %s\n", what); }
}

static int64_t done[32];
static int done_count;
static void check(int64_t code) { done[done_count++] = code; }
static int checked(int64_t code)
{
    for (int i = 0; i < done_count; ++i) if (done[i] == code) return 1;
    return 0;
}

enum { TROPHY = 1, CTR = 2, SAPPHIRE = 3, GOLD = 4, PLATINUM = 5, RUNG = 6, BOX = 7, WUMPA = 8, LETTER = 9 };

// A classic pad: Trophy Race, then CTR and Relic behind stage 2 (1 Trophy).
static ctr_content_pad classic(void)
{
    ctr_content_pad p;
    memset(&p, 0, sizeof p);
    p.occupied = 1; p.first_race_rule = 1; p.mode_count = 3; p.racer_lock = -1;
    p.modes[0] = AP_CONTENT_MODE_TROPHY; p.modes[1] = AP_CONTENT_MODE_CTR; p.modes[2] = AP_CONTENT_MODE_RELIC;
    p.stages[0] = 1; p.stages[1] = p.stages[2] = 2;
    p.has_stage2 = 1; p.stage2_mask = 1u; p.stage2_count = 1;
    p.trophy_location = TROPHY; p.ctr_location = CTR;
    p.relic_locations[0] = SAPPHIRE; p.relic_locations[1] = GOLD; p.relic_locations[2] = PLATINUM;
    return p;
}

static void menu(void)
{
    int counts[15] = {0};
    ap_rs_rows r;
    ctr_content_pad p = classic();
    done_count = 0;
    // A race that is not open has no row.
    ap_content_menu_rows(&p, counts, checked, 0, &r);
    expect(r.count == 1 && r.mode[0] == AP_RS_TROPHY && r.productive[0] && r.cursor == 0, "only the open race has a row");
    expect(AP_RaceSelectorDecide(&r, AP_RS_SELECTOR_ALWAYS) == AP_RS_OPEN_MENU, "always opens with a single row");
    expect(AP_RaceSelectorDecide(&r, AP_RS_SELECTOR_PRODUCTIVE) == AP_RS_TROPHY, "productive loads the one race");
    // Stage 2 does not count until the first race is won.
    counts[0] = 1;
    ap_content_menu_rows(&p, counts, checked, 0, &r);
    expect(r.count == 1, "stage 2 met, first race not won: still one row");
    check(TROPHY);
    ap_content_menu_rows(&p, counts, checked, 0, &r);
    expect(r.count == 3 && !r.productive[0] && r.productive[1] && r.productive[2] && r.cursor == 1 &&
           r.productiveCount == 2, "a finished race is grey, the cursor on the first row with a check");
    expect(AP_RaceSelectorReplayMask(&r) == 1u, "the grey row can be chosen");
    expect(AP_RaceSelectorDecide(&r, AP_RS_SELECTOR_PRODUCTIVE) == AP_RS_OPEN_MENU, "two races with checks: menu");
    check(CTR);
    ap_content_menu_rows(&p, counts, checked, 0, &r);
    expect(AP_RaceSelectorDecide(&r, AP_RS_SELECTOR_PRODUCTIVE) == AP_RS_RELIC, "one race with checks: loads it");
    check(SAPPHIRE); check(GOLD); check(PLATINUM);
    ap_content_menu_rows(&p, counts, checked, 0, &r);
    expect(r.count == 3 && r.productiveCount == 0 && r.cursor == 0, "a Done pad: every row grey, cursor on the first");
    expect(AP_RaceSelectorDecide(&r, AP_RS_SELECTOR_PRODUCTIVE) == AP_RS_OPEN_MENU, "nothing left: the menu opens");
    expect(AP_RaceSelectorReplayMask(&r) == 7u, "every row a replay");
    // The pad's order, not a fixed one (pad_layout.mode_order).
    p = classic();
    p.modes[0] = AP_CONTENT_MODE_RELIC; p.modes[1] = AP_CONTENT_MODE_TROPHY; p.modes[2] = AP_CONTENT_MODE_CTR;
    done_count = 0; check(SAPPHIRE); counts[0] = 1;
    ap_content_menu_rows(&p, counts, checked, 0, &r);
    expect(r.count == 3 && r.mode[0] == AP_RS_RELIC && r.mode[1] == AP_RS_TROPHY && r.mode[2] == AP_RS_CTR, "rows in the pad's order");
    // ctr_first: no Trophy row, first place pays the Trophy check.
    p = classic();
    p.ctr_first = 1; p.mode_count = 2; p.modes[0] = AP_CONTENT_MODE_CTR; p.modes[1] = AP_CONTENT_MODE_RELIC;
    counts[0] = 0; done_count = 0;
    ap_content_menu_rows(&p, counts, checked, 0, &r);
    expect(r.count == 1 && r.mode[0] == AP_RS_CTR, "ctr_first: the CTR Challenge opens with the pad");
    check(TROPHY); counts[0] = 1;
    ap_content_menu_rows(&p, counts, checked, 0, &r);
    expect(r.count == 2 && r.productive[0] && r.productive[1], "first place without the letters: the CTR Token stays open for a replay");
    // Lettersanity's rule on the first race: the CTR Challenge moved back by the
    // generator stays behind the first race here.
    p = classic();
    p.modes[0] = AP_CONTENT_MODE_RELIC; p.modes[1] = AP_CONTENT_MODE_CTR; p.modes[2] = AP_CONTENT_MODE_TROPHY;
    counts[0] = 1; done_count = 0;
    ap_content_menu_rows(&p, counts, checked, 0, &r);
    expect(r.count == 1 && r.mode[0] == AP_RS_RELIC, "letter items: the first race is the moved one");
    // A single race pad.
    p = classic();
    p.mode_count = 1; p.has_stage2 = 0;
    done_count = 0;
    ap_content_menu_rows(&p, counts, checked, 0, &r);
    expect(r.count == 1 && AP_RaceSelectorDecide(&r, AP_RS_SELECTOR_ALWAYS) == AP_RS_OPEN_MENU, "a single race pad still opens under always");
    // A Hit Character or Itemsanity use counts for races with other racers.
    p = classic();
    done_count = 0; check(TROPHY); check(CTR); check(SAPPHIRE); check(GOLD); check(PLATINUM);
    ap_content_menu_rows(&p, counts, checked, 1, &r);
    expect(r.productive[0] && r.productive[1] && !r.productive[2] && r.productiveCount == 2, "a Hit or weapon use has a check; a Relic Race has none");
    // No row: the pad releases the kart.
    r.count = 0;
    expect(AP_RaceSelectorDecide(&r, AP_RS_SELECTOR_ALWAYS) == AP_RS_NO_ROUTE, "no row at all: refused");
    // The Options row over the seed.
    expect(AP_RaceSelectorEffective(-1, AP_RS_SELECTOR_PRODUCTIVE) == AP_RS_SELECTOR_PRODUCTIVE, "SEED follows the plan");
    expect(AP_RaceSelectorEffective(AP_RS_SELECTOR_ALWAYS, AP_RS_SELECTOR_PRODUCTIVE) == AP_RS_SELECTOR_ALWAYS, "ALWAYS wins");
    expect(AP_RaceSelectorEffective(AP_RS_SELECTOR_PRODUCTIVE, AP_RS_SELECTOR_ALWAYS) == AP_RS_SELECTOR_PRODUCTIVE, "PRODUCTIVE wins");
    expect(AP_RaceSelectorEffective(7, AP_RS_SELECTOR_ALWAYS) == AP_RS_SELECTOR_ALWAYS, "a hand-edited value follows the plan");
}

static void closing(void)
{
    int counts[15] = {0};
    ctr_content_pad p = classic();
    done_count = 0;
    p.keys = 1;
    expect(ap_content_track_pad_state(&p, counts, checked, 0) == 1, "hub Keys missing: locked");
    p.keys = 0;
    expect(ap_content_track_pad_state(&p, counts, checked, 0) == 2, "open, first race not won");
    check(TROPHY);
    expect(ap_content_track_pad_state(&p, counts, checked, 0) == 3, "first race won, stage 2 not met: closed again");
    expect(ap_content_track_pad_state(&p, counts, checked, 1) == 2, "a Hit or weapon use keeps the first race open");
    counts[0] = 1;
    expect(ap_content_track_pad_state(&p, counts, checked, 0) == 4, "stage 2 met: the later races are open");
    check(CTR); check(SAPPHIRE); check(GOLD); check(PLATINUM);
    expect(ap_content_track_pad_state(&p, counts, checked, 0) == 5, "nothing left: Done");
    expect(ap_content_track_pad_state(&p, counts, checked, 1) == 4, "a use left: not Done");
    // Boxes and first-race families keep the pad open, as on an ordinary pad (#232).
    p = classic(); p.box_count = 1; p.box_locations[0] = BOX;
    counts[0] = 0; done_count = 0; check(TROPHY);
    expect(ap_content_track_pad_state(&p, counts, checked, 0) == 2, "a box left: the pad stays open");
    expect(ap_content_boxes_line(&p, checked) == 1, "and says why");
    check(BOX);
    expect(ap_content_track_pad_state(&p, counts, checked, 0) == 3, "the box broken: closed again");
    expect(ap_content_boxes_line(&p, checked) == 0, "the line goes");
    p = classic(); p.rung_locations[0] = RUNG;
    done_count = 0; check(TROPHY);
    expect(ap_content_track_pad_state(&p, counts, checked, 0) == 2, "a rung left: open");
    // ctr_first: closed when the merged race has nothing left; the CTR Token keeps it open.
    p = classic();
    p.ctr_first = 1; p.mode_count = 2; p.modes[0] = AP_CONTENT_MODE_CTR; p.modes[1] = AP_CONTENT_MODE_RELIC;
    done_count = 0; check(TROPHY);
    expect(ap_content_track_pad_state(&p, counts, checked, 0) == 2, "ctr_first: the CTR Token left, open");
    check(CTR);
    expect(ap_content_track_pad_state(&p, counts, checked, 0) == 3, "ctr_first: closed again");
    // A first race with no check of its own proves nothing, so the items decide.
    p = classic();
    p.modes[0] = AP_CONTENT_MODE_RELIC; p.modes[1] = AP_CONTENT_MODE_TROPHY; p.modes[2] = AP_CONTENT_MODE_CTR;
    memset(p.relic_locations, 0, sizeof p.relic_locations);
    done_count = 0;
    expect(ap_content_track_pad_state(&p, counts, checked, 0) == 3, "a relic with no tier check, stage 2 not met: closed");
    counts[0] = 1;
    expect(ap_content_track_pad_state(&p, counts, checked, 0) == 2, "and open when the items arrive");
}

static void boxes_line(void)
{
    ctr_content_pad p = classic();
    p.box_count = 2; p.box_locations[0] = BOX; p.box_locations[1] = BOX + 20;
    done_count = 0;
    expect(ap_content_boxes_line(&p, checked) == 0, "before the first race no line");
    check(TROPHY);
    expect(ap_content_boxes_line(&p, checked) == 2, "after it, the number");
    check(BOX);
    expect(ap_content_boxes_line(&p, checked) == 1, "a box less");
    p.ctr_first = 1; p.mode_count = 2; p.modes[0] = AP_CONTENT_MODE_CTR; p.modes[1] = AP_CONTENT_MODE_RELIC;
    done_count = 0; check(CTR);
    expect(ap_content_boxes_line(&p, checked) == 0, "ctr_first: the Trophy Race check is the first race");
    check(TROPHY);
    expect(ap_content_boxes_line(&p, checked) == 2, "ctr_first: then the number");
    p = classic(); p.crystal = 1; p.box_count = 1; p.box_locations[0] = BOX;
    done_count = 0; check(TROPHY);
    expect(ap_content_boxes_line(&p, checked) == 0, "a Crystal Challenge has no line");
    expect(ap_content_boxes_line(NULL, checked) == 0, "no pad, no line");
}

static void deathlink_row(void)
{
    APDeathLinkState s;
    // Seed off, DL Send Fall ON: the client receives as mask reset, and the row says so.
    s = AP_DeathLinkResolve(0, 0, -1, 1, -1, -1);
    expect(s.recv == 1 && AP_DeathLinkRowShown(-1, 0, s.recv) == 1, "a DL Send row turned receiving on");
    s = AP_DeathLinkResolve(3, 0, -1, -1, -1, -1);
    expect(AP_DeathLinkRowShown(-1, 3, s.recv) == -1, "the seed's own mode stays SEED");
    s = AP_DeathLinkResolve(0, 0, -1, -1, -1, -1);
    expect(s.recv == 0 && AP_DeathLinkRowShown(-1, 0, s.recv) == -1, "nothing on: SEED");
    expect(AP_DeathLinkRowShown(0, 0, 1) == 0 && AP_DeathLinkRowShown(4, 0, 1) == 4, "a forced row shows itself");
}

int main(void)
{
    menu();
    closing();
    boxes_line();
    deathlink_row();
    printf("%d checks, %d failures\n", total, failures);
    return failures != 0;
}
