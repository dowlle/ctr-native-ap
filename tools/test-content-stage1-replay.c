// cc -std=c99 -Wall -Wextra -Werror -I ap -I . -I include -o /tmp/test-content-stage1-replay tools/test-content-stage1-replay.c && /tmp/test-content-stage1-replay
#include <stdio.h>
#include <string.h>
#include "ap_content_modes.h"
#include "ap_pad_activity_logic.h"
static int won;
static int checked(int64_t code) { return won && code == 100; }
static int failures;
#define CHECK(s, x) do { if (!(x)) { printf("FAIL %s\n", s); failures++; } } while (0)
int main(void)
{
    ctr_content_pad p;
    ap_rs_rows rows;
    ap_padact_facts f;
    ap_padact_list list;
    int counts[200] = {0};
    memset(&p, 0, sizeof p);
    p.occupied = 1;
    p.mode_count = 3;
    p.modes[0] = AP_CONTENT_MODE_TROPHY;
    p.modes[1] = AP_CONTENT_MODE_CTR;
    p.modes[2] = AP_CONTENT_MODE_RELIC;
    p.stages[0] = 1;
    p.stages[1] = p.stages[2] = 2;
    p.trophy_location = 100;
    p.ctr_location = 101;
    p.relic_locations[0] = 102;
    p.first_race_rule = 1;
    p.has_stage2 = 1;
    p.stage2_mask = 1;
    p.stage2_count = 2;
    CHECK("an unwon race is not a replay", !ap_content_first_mode_replayable(&p, checked));
    won = 1;
    CHECK("stage one replay ignores missing stage two", ap_content_first_mode_replayable(&p, checked));
    CHECK("the pad still advertises stage two", ap_content_track_pad_state(&p, counts, checked, 0) == 3);
    ap_content_menu_rows(&p, counts, checked, 0, &rows);
    CHECK("only the grey Trophy replay is offered", rows.count == 1 && rows.mode[0] == AP_CONTENT_MODE_TROPHY && !rows.productive[0]);
    CHECK("later races remain closed", !ap_content_mode_open(&p, AP_CONTENT_MODE_CTR, counts, checked) && !ap_content_mode_open(&p, AP_CONTENT_MODE_RELIC, counts, checked));
    memset(&f, 0, sizeof f);
    f.node = AP_PADACT_NODE_CONTENT;
    f.occupied = f.contentReady = f.stage1Met = f.padReqMet = f.racerMet = f.replayable = f.firstRaceWon = 1;
    f.padState = 3;
    f.modeCount = 2;
    f.contentMode[0] = AP_CONTENT_MODE_TROPHY;
    f.contentMode[1] = AP_CONTENT_MODE_CTR;
    f.contentOpen[0] = 1;
    f.contentStage2[1] = 1;
    AP_PadActivityDecide(&f, &list);
    CHECK("tracker offers the same replay", list.reason[0] == AP_PADACT_OPEN && !list.left[0]);
    CHECK("tracker keeps stage two closed", list.reason[1] == AP_PADACT_LOCK_STAGE2);
    f.racerMet = f.stage1Met = 0;
    AP_PadActivityDecide(&f, &list);
    CHECK("racer lock still blocks replay", list.reason[0] == AP_PADACT_LOCK_RACER);
    p.ctr_first = 1;
    p.mode_count = 2;
    p.modes[0] = AP_CONTENT_MODE_CTR;
    p.modes[1] = AP_CONTENT_MODE_RELIC;
    CHECK("ctr_first replays its won first CTR race", ap_content_first_mode_replayable(&p, checked));
    p.crystal = 1;
    CHECK("Crystal Challenges are unaffected", !ap_content_first_mode_replayable(&p, checked));
    p.crystal = 0; p.cup = 1;
    CHECK("cups are unaffected", !ap_content_first_mode_replayable(&p, checked));
    printf("%s stage-one replay\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
