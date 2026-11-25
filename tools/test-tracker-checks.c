// cc -m32 -std=gnu99 -ffunction-sections -fdata-sections -Wl,--gc-sections -Wno-unused-function -DCTR_AP -DCTR_NATIVE -DBUILD=926 -Iinclude -I. -Iap -Iexternals/SDL/include tools/test-tracker-checks.c -o /tmp/test-tracker-checks -lm
/* Exercise the production gatherer, including the garage path that used to
 * omit Reach 10 Wumpa entirely. Optional capture uses the same draw function. */
#include <assert.h>
#include <SDL3/SDL.h>
#include <common.h>
#include <platform/native_gpu.h>
#include <namespace_Decal.h>
#include "ap/ap_hooks.h"
#include "ap/ap_net.h"
#include "ap/ap_capability.h"
#include "ap/ap_goal_logic.h"
#include "ap/ap_boss_venue.h"
#include "ap/ap_trial_letters.h"
#include "ap/ap_charseat.h"
#include "ap/ap_hit_encounter.h"
int AP_ContentLetterItemReceived(int64_t code);
#define AP_GOAL_BIT_OXIDE_FIRST 114
#include "ap/ap_tracker.c"

struct sData sdata_static;
struct Data data;
ctr_seed_config ctr_cfg;
static ctr_content_pad pads[2];
static ctr_content_bosses bosses;
static ctr_content_checks families;
static ctr_content_sanity sanity;
static int active=1;
static int64_t checked_code;
static int64_t absent_code;
static int64_t pending_sanity_code;
static int64_t stat_gated_code;
static int cup_sanity_route;
int ap_net_location_exists(long long code) { return code>0 && code!=absent_code; }
int ap_net_location_checked(long long code) { return code == checked_code; }
static int shared_pending, cup_pickups_pending;
int AP_ContentPadSharedChecks(const ctr_content_pad *pad, unsigned modes, ap_pad_shared_check *out) {
    if (pad == &pads[1]) {
        assert(modes == 0 && !out);
        return shared_pending;
    }
    assert(pad == &pads[0] && modes == AP_CONTENT_PLAIN_MODES);
    if (shared_pending)
        out[0] = (ap_pad_shared_check){35016002, AP_PAD_SHARED_ITEMSANITY};
    return shared_pending;
}
int AP_ContentPadPickupsLeft(const ctr_content_pad *pad) {
    assert(pad == &pads[1]);
    return cup_pickups_pending;
}
int AP_ContentLetterItemReceived(int64_t code) { (void)code; return 0; }
int AP_ContentCupLegSharedChecks(const ctr_content_pad *pad, int leg, ap_pad_shared_check *out) {
    assert(pad == &pads[1]);
    if (!shared_pending || leg != 2) return 0;
    out[0] = (ap_pad_shared_check){35016002, AP_PAD_SHARED_ITEMSANITY};
    return 1;
}

int ap_content_plan_active(void) { return active; }
const ctr_content_pad *ap_content_plan_pad(int physical) {
    if (physical==7) return &pads[0];
    if (physical==100 || physical==1000) return &pads[1];
    return NULL;
}
const ctr_content_bosses *ap_content_plan_bosses(void) { return &bosses; }
const ctr_content_checks *ap_content_plan_checks(void) { return &families; }
const ctr_content_package *ap_content_plan_package(int i) { (void)i; return NULL; }
const ctr_content_sanity *ap_content_plan_sanity(const char *id) {
    return !strcmp(id,"retail:7") ? &sanity : NULL;
}
const ctr_content_sanity *ap_content_plan_sanity_boss(int boss) { return boss==0 ? &sanity : NULL; }
int AP_ContentCodeChecked(int64_t code) { return code==checked_code; }
int AP_LocationLogicState(long long code) { return code%3; }
int AP_ContentPadOpen(const ctr_content_pad *pad) { return pad == &pads[1]; }
int AP_BossGarageOpen(int boss) { return boss == 0; }
int AP_BossGateAdvert(int boss, char *out, int cap) {
    if (boss == 4)
        return snprintf(out, (size_t)cap, "FINAL CHALLENGE\rNEEDS 4 RELICS\rNEEDS 2 GEMS");
    assert(boss == 2);
    return snprintf(out, (size_t)cap, "Requires: 8 Trophies (have 2)");
}
int AP_GateCount(int index) { (void)index; return 0; }
int ap_content_plan_check_boost(int64_t code) { (void)code; return 0; }
int ap_content_plan_check_requires_stats(int64_t code) { return code == stat_gated_code; }
const int64_t *ap_content_plan_tracker_locations(int node, int *count) {
    static const int64_t codes[] = {103, 106, 108, 111, 112};
    static int64_t sanity_codes[] = {103, 0};
    if (pending_sanity_code && (node == 7 || node == 100)) {
        sanity_codes[1] = pending_sanity_code;
        *count = node == 7 || cup_sanity_route ? 2 : 1;
        return sanity_codes;
    }
    *count = node == 7 ? (int)(sizeof codes / sizeof codes[0]) : node == 100 ? 1 : 0;
    return codes;
}
char *NativeMenu_Labels(const char *text) { return (char *)text; }

static void expect(AP_TrackerCheckCount *r,int total,int done,int in,int out,int unknown) {
    assert(r->total==total && r->done==done && r->in_logic==in && r->out_logic==out && r->unknown==unknown);
}
static void capture(const char *prefix, const char *name) {
    char path[1024];
    snprintf(path,sizeof path,"%s-%s.rgba",prefix,name);
    FILE *f=fopen(path,"wb"); assert(f);
    AP_TrackerRect(0,0,TRACKER_W,TRACKER_H,AP_TrackerRGBA(20,27,35,255));
    AP_TrackerDrawChecks();
    assert(fwrite(ap_tracker.canvas,4,TRACKER_W*TRACKER_H,f)==TRACKER_W*TRACKER_H);
    fclose(f);
}

static void wumpa_mark_test(void) {
    ap_tracker.canvas = calloc(TRACKER_W * TRACKER_H, 4);
    assert(ap_tracker.canvas);
    /* Drawing with no room location must leave even the tick area untouched. */
    absent_code = 100;
    AP_TrackerDrawWumpaMark(0,0,100,0);
    AP_TrackerDrawWumpaMark(0,0,0,1);
    for (int i=0;i<TRACKER_W*TRACKER_H;i++) assert(!ap_tracker.canvas[i]);
    absent_code = 0;
    checked_code = 100;
    AP_TrackerDrawWumpaMark(0,0,100,1);
    int pixels=0;
    for (int i=0;i<TRACKER_W*TRACKER_H;i++) pixels += ap_tracker.canvas[i] != 0;
    assert(pixels);
    free(ap_tracker.canvas);
    ap_tracker.canvas=NULL;
}

static void cup_card_test(void) {
    AP_TrackerNode node = {.kind = 2, .physical = 100};
    ap_tracker.canvas = calloc(TRACKER_W * TRACKER_H, 4);
    assert(ap_tracker.canvas);
    pads[1].cup = 1;
    pads[1].trophy_location = checked_code = 100;
    for (int i=0;i<AP_CONTENT_CUP_LEGS;i++) pads[1].legs[i].track_pad=-1;
    for (int mask=0;mask<4;mask++) {
        shared_pending = mask & 1;
        cup_pickups_pending = (mask >> 1) & 1;
        AP_TrackerRect(0,0,376,126,0);
        AP_TrackerDrawContentNode(&node,0,0);
        int green=0, white=0;
        for (int y=62;y<83;y++) for (int x=312;x<338;x++) {
            uint32_t pixel = ap_tracker.canvas[y*TRACKER_W+x];
            green += pixel == TRACKER_GREEN;
            white += pixel == TRACKER_WHITE;
        }
        assert(mask ? (!green && white) : (green && !white));
    }
    shared_pending = cup_pickups_pending = 0;
    memset(&pads[1],0,sizeof pads[1]);
    free(ap_tracker.canvas);
    ap_tracker.canvas=NULL;
}
static void pictures(const char *font, const char *prefix) {
    char *strings[]={"ROO'S TUBES"}; sdata->lngStrings=strings;
    FILE *f=fopen(font,"rb"); assert(f);
    for(int face=0;face<2;face++) for(int ch=0;ch<96;ch++) {
        AP_TrackerSprite *g=&ap_tracker.glyphs[face][ch];
        assert(fread(&g->w,4,1,f)==1 && fread(&g->h,4,1,f)==1);
        assert(g->w>=0 && g->w<=128 && g->h>=0 && g->h<=128);
        int count=g->w*g->h; g->pixels=malloc(count*4);
        assert(fread(g->pixels,4,count,f)==(size_t)count);
    }
    fclose(f); ap_tracker.canvas=calloc(TRACKER_W*TRACKER_H,4);
    AP_TrackerNode *n=&ap_tracker.nodes[0]; memset(n,0,sizeof *n);
    strcpy(n->title,"ROO'S TUBES"); n->physical=7;
    pads[0].wumpa_modes=1; pads[0].wumpa_location=100;
    sanity.locations[0][0]=102; sanity.locations[0][1]=103; sanity.locations[0][2]=104;
    sanity.locations[1][0]=105; sanity.locations[1][1]=106;
    sanity.locations[2][0]=107; checked_code=102;
    ap_tracker.hub=1; ap_tracker.checks_page=0;
    capture(prefix,"track");
    n->kind=3; n->physical=-1; strcpy(n->title,"RIPPER ROO"); strcpy(n->pad,"ROO'S TUBES");
    bosses.wumpa[0]=100; capture(prefix,"boss");
    n->kind=2; n->physical=100; strcpy(n->title,"RED GEM CUP");
    pads[1].legs[2].retail_id=7;
    ap_tracker.checks_page=2; capture(prefix,"cup-leg");
    n->kind=0; n->physical=7; strcpy(n->title,"ROO'S TUBES");
    ap_tracker.checks_page=0; memset(&sanity,0,sizeof sanity); checked_code=100;
    capture(prefix,"done");
    families.wumpa=0; pads[0].wumpa_modes=0;
    capture(prefix,"off");
    shared_pending=1; capture(prefix,"shared-use"); shared_pending=0;
    /* Production reward rows with explicit mixed server/logic state. */
    pads[0].trophy_location=201; pads[0].ctr_location=202;
    for(int i=0;i<3;i++) pads[0].relic_locations[i]=203+i;
    for(int i=0;i<5;i++) pads[0].rung_locations[i]=206+i;
    pads[0].perfect_location=211; pads[0].lap_location=212;
    checked_code=201; ap_tracker.checks_detail=0;
    capture(prefix,"rewards-race-1");
    ap_tracker.checks_detail=1; capture(prefix,"rewards-race-2");
    n->physical=100; n->kind=2; strcpy(n->title,"RED GEM CUP");
    pads[1].trophy_location=300; pads[1].sweep_location=301; pads[1].gem_cup=100;
    for(int i=0;i<4;i++) {
        pads[1].legs[i].victory=302+i;
        snprintf(pads[1].legs[i].room_title,sizeof pads[1].legs[i].room_title,"CUSTOM TRACK %d",i+1);
    }
    pads[1].legs[2].lap=306;
    pads[1].legs[2].rungs[0]=307;
    ap_tracker.checks_page=2; ap_tracker.checks_detail=0; checked_code=303;
    capture(prefix,"rewards-cup");
    n->physical=-1; n->kind=3; ap_tracker.hub=0; strcpy(n->title,"N. OXIDE"); n->pad[0]=0;
    ap_tracker.checks_page=1; bosses.lap[5]=309;
    capture(prefix,"rewards-final");
    checked_code=35011105; capture(prefix,"rewards-final-done");
    absent_code=35011105; bosses.lap[5]=0;
    capture(prefix,"rewards-final-absent");
}
static void partial_and_pickup_tests(void) {
    AP_TrackerNode n = {.physical = 7};
    AP_TrackerRewards rows;
    pads[0].occupied = 1;
    pads[0].physical = 7;
    pads[0].rung_locations[0] = 103;
    pads[0].box_count = 2;
    pads[0].box_spots = 1;
    pads[0].box_number[0] = 13;
    pads[0].box_number[1] = 21;
    pads[0].box_locations[0] = 106;
    pads[0].box_locations[1] = 108;
    pads[0].letter_locations[0] = 111;
    pads[0].letter_glyphs[0] = 'X';
    pads[0].crystal_count = 1;
    pads[0].crystal_locations[0] = 112;
    checked_code = 106;
    AP_TrackerRewardsBuild(&n, 0, &rows);
    assert(rows.count == 5);
    assert(!strcmp(rows.rows[1].name, "CRYSTAL 1"));
    assert(!strcmp(rows.rows[2].name, "LETTER X"));
    assert(!strcmp(rows.rows[3].name, "ITEM BOX 13"));
    assert(!strcmp(rows.rows[4].name, "ITEM BOX 21"));
    AP_TrackerCheckCount total = AP_TrackerNodeChecks(&n);
    assert(AP_TrackerCheckNodeState(total, 0) == AP_TRACKER_NODE_IN);
    assert(AP_TrackerCheckNodeState(total, 1) == AP_TRACKER_NODE_PARTIAL);

    strcpy(pads[1].room_title, "YELLOW GEM CUP");
    pads[1].cup = 1;
    pads[1].physical = 100;
    pads[1].trophy_location = 115;
    for (int i = 0; i < AP_CONTENT_CUP_LEGS; ++i) pads[1].legs[i].track_pad = -1;
    pads[1].legs[2].track_pad = 7;
    strcpy(pads[1].legs[2].track_id, "retail:7");
    char route[128] = "";
    AP_TrackerCupRoute(&pads[0], route, sizeof route);
    assert(!strcmp(route, "SOME SHARED CHECKS VIA YELLOW GEM CUP, RACE 3"));
    pads[1].trophy_location = 114;
    AP_TrackerCupRoute(&pads[0], route, sizeof route);
    assert(!strcmp(route, "SHARED CHECKS IN LOGIC VIA ANOTHER RACE"));
    assert(AP_TrackerCupBoxesInLogic(&pads[1]) == 0);
    checked_code = 0;
    assert(AP_TrackerCupBoxesInLogic(&pads[1]) == 1);

    /* R2 Coco Park: the standalone pad is locked, the shared race rewards
     * are done, and the only remaining in-logic check is a crate or fruit. */
    strcpy(pads[1].room_title, "PURPLE GEM CUP");
    pads[1].legs[2].track_pad = -1;
    pads[1].legs[0].track_pad = 7;
    pads[0].wumpa_location = 0;
    checked_code = 103;
    const int slots[] = {15, 2, 9};
    for (int family = 0; family < 3; ++family) {
        pending_sanity_code = sanity.locations[family][slots[family]] = 118 + 3 * family;
        cup_sanity_route = 1;
        pads[1].trophy_location = 115;
        assert(!AP_ContentPadOpen(&pads[0]));
        total = AP_TrackerNodeChecks(&n);
        expect(&total, 2, 1, 1, 0, 0);
        assert(AP_TrackerCheckNodeState(total, 0) == AP_TRACKER_NODE_IN);
        strcpy(route, "THIS PAD'S RACE MODES");
        AP_TrackerCupRoute(&pads[0], route, sizeof route);
        assert(!strcmp(route, "SOME SHARED CHECKS VIA PURPLE GEM CUP, RACE 1"));
        ctr_cfg.stats_mode = AP_CAP_MODE_PER_CHARACTER;
        stat_gated_code = pending_sanity_code;
        AP_TrackerCupRoute(&pads[0], route, sizeof route);
        assert(!strcmp(route, "SHARED CHECKS IN LOGIC VIA ANOTHER RACE"));
        ctr_cfg.stats_mode = 0;
        stat_gated_code = 0;
        cup_sanity_route = 0;
        AP_TrackerCupRoute(&pads[0], route, sizeof route);
        assert(!strcmp(route, "SHARED CHECKS IN LOGIC VIA ANOTHER RACE"));
        sanity.locations[family][slots[family]] = 0;
    }
    pending_sanity_code = 0;
    strcpy(route, "THIS PAD'S RACE MODES");
    AP_TrackerCupRoute(&pads[0], route, sizeof route);
    assert(!strcmp(route, "THIS PAD'S RACE MODES"));

    memset(pads, 0, sizeof pads);
    strcpy(pads[0].track_id, "retail:7");
    checked_code = 0;
    puts("tracker parity: cup route, partial colour, sparse box labels, crystals, letters and cup box logic pass");
}

static void activity_reason_tests(void) {
    AP_TrackerNode node = {.physical = 7};
    ctr_content_pad pad = {.keys = 4, .has_rule = 1, .rule_count = 2, .rule_mask = 1};
    char line[160];
    ap_tracker.hub = 3;
    AP_TrackerActivityReason(&node, &pad, AP_PADACT_LOCK_HUB, line, sizeof line);
    assert(!strcmp(line, "NEEDS 4 KEYS"));
    AP_TrackerActivityReason(&node, &pad, AP_PADACT_LOCK_PAD, line, sizeof line);
    assert(!strcmp(line, "NEEDS 2 Trophies"));
    strcpy(ap_tracker.acts.stage2, "1 Red Gem");
    AP_TrackerActivityReason(&node, &pad, AP_PADACT_LOCK_STAGE2, line, sizeof line);
    assert(!strcmp(line, "NEEDS 1 Red Gem"));
    node.kind = 3;
    AP_TrackerActivityReason(&node, NULL, AP_PADACT_LOCK_BOSS, line, sizeof line);
    assert(!strcmp(line, "Requires: 8 Trophies (have 2)"));
    ap_tracker.hub = 0;
    int mask = AP_PADACT_BIT(AP_PADACT_LOCK_HUB) | AP_PADACT_BIT(AP_PADACT_LOCK_BOSS);
    assert(AP_TrackerActivityDetails(&node, NULL, mask, line, sizeof line) == 4);
    assert(!strcmp(line, "NEEDS 1 KEY\rFINAL CHALLENGE\rNEEDS 4 RELICS\rNEEDS 2 GEMS"));
}

static void reward_tests(void) {
    AP_TrackerNode n={0}; AP_TrackerRewards r;
    n.physical=7; ap_tracker.checks_page=0;
    pads[0].trophy_location=201; pads[0].ctr_location=202;
    for(int i=0;i<3;i++) pads[0].relic_locations[i]=203+i;
    for(int i=0;i<5;i++) pads[0].rung_locations[i]=206+i;
    pads[0].perfect_location=211; pads[0].lap_location=212;
    AP_TrackerRewardsBuild(&n,ap_tracker.checks_page,&r); assert(r.count==12 && AP_TrackerChecksDetails(&n)==3);
    assert(!strcmp(r.rows[5].name,"HELD 1ST") && !strcmp(r.rows[10].name,"RELIC RACE PERFECT"));
    absent_code=211; AP_TrackerRewardsBuild(&n,ap_tracker.checks_page,&r); assert(r.count==11); absent_code=0;
    /* Older supported plans omit optional families. No invented rows. */
    memset(pads[0].rung_locations,0,sizeof pads[0].rung_locations);
    pads[0].perfect_location=pads[0].lap_location=0;
    AP_TrackerRewardsBuild(&n,ap_tracker.checks_page,&r); assert(r.count==5 && AP_TrackerChecksDetails(&n)==2);
    n.kind=2; n.physical=100; pads[1].trophy_location=300; pads[1].sweep_location=301;
    for(int i=0;i<4;i++) { pads[1].legs[i].victory=302+i; pads[1].legs[i].track_pad=-1; }
    strcpy(pads[1].legs[2].room_title,"DISTINCT ROOM TITLE");
    pads[1].legs[2].rungs[0]=310; pads[1].legs[2].lap=311;
    ap_tracker.checks_page=2;
    AP_TrackerRewardsBuild(&n,ap_tracker.checks_page,&r); assert(r.count==8);
    assert(r.rows[0].code==300 && r.rows[1].code==301);
    assert(r.rows[4].code==304 && strstr(r.rows[4].name,"LEG 3 WIN: DISTINCT ROOM TITLE"));
    /* Pre-title-field rooms retain their retail title in the leg reward. */
    char *strings[]={"OLD ROOM RETAIL TITLE"}; sdata->lngStrings=strings;
    pads[1].legs[0].retail_id=7;
    AP_TrackerRewardsBuild(&n,ap_tracker.checks_page,&r); assert(strstr(r.rows[2].name,"OLD ROOM RETAIL TITLE"));
    pads[1].legs[0].retail_id=-1;
    ap_tracker.checks_page=1; AP_TrackerRewardsBuild(&n,ap_tracker.checks_page,&r); assert(r.count==6);
    /* Shared track rungs are deduplicated by code, never by their label. */
    pads[1].legs[2].track_pad=7; pads[0].rung_locations[0]=310;
    ap_tracker.checks_page=2; AP_TrackerRewardsBuild(&n,ap_tracker.checks_page,&r); assert(r.count==8);
    n.kind=3; n.physical=-1; ap_tracker.hub=1; bosses.cup[0]=1;
    AP_TrackerRewardsBuild(&n,ap_tracker.checks_page,&r); assert(r.count==8);
    /* A real boss cup (garage pad) whose leg track has a standalone Trophy
     * Race with enabled rungs: the race's held and finish rows belong to that
     * race alone. Cup win, leg lap and the rest stay. Ordinary cup control. */
    {
        int old_phys=pads[1].physical, i, held=0;
        pads[1].legs[2].rungs[0]=0; pads[1].legs[2].lap=330;
        pads[0].rung_locations[0]=331; pads[0].rung_locations[3]=332;
        n.kind=2; n.physical=100; ap_tracker.checks_page=2;
        AP_TrackerRewardsBuild(&n,ap_tracker.checks_page,&r);
        for(i=0;i<r.count;i++) held|=r.rows[i].code==331 || r.rows[i].code==332;
        assert(held);
        pads[1].physical=AP_CONTENT_GARAGE_CUP_PAD; pads[1].legs[2].rungs[1]=333;
        AP_TrackerRewardsBuild(&n,ap_tracker.checks_page,&r);
        assert(r.rows[0].code==300 && r.rows[1].code==301);
        for(i=0;i<r.count;i++)
            assert(r.rows[i].code!=331 && r.rows[i].code!=332 && r.rows[i].code!=333);
        assert(r.rows[r.count-1].code==330 && !strcmp(r.rows[r.count-1].name,"COMPLETE ONE LAP"));
        pads[1].physical=old_phys; pads[1].legs[2].rungs[1]=0; pads[1].legs[2].lap=311;
        pads[1].legs[2].rungs[0]=310; pads[0].rung_locations[0]=310; pads[0].rung_locations[3]=0;
        n.kind=3; n.physical=-1;
    }
    bosses.cup[0]=0; bosses.lap[0]=320;
    AP_TrackerRewardsBuild(&n,ap_tracker.checks_page,&r); assert(r.count==2 && r.rows[0].code==35011100);
    ap_tracker.hub=0; ap_tracker.checks_page=1; bosses.lap[5]=321;
    AP_TrackerRewardsBuild(&n,ap_tracker.checks_page,&r); assert(r.count==2 && r.rows[0].code==35011105);
    assert(strstr(r.rows[0].name,"FINAL CHALLENGE"));
    absent_code=35011105; bosses.lap[5]=0;
    AP_TrackerRewardsBuild(&n,ap_tracker.checks_page,&r); assert(!r.count && AP_TrackerChecksDetails(&n)==1); absent_code=0;
    /* These are display reads; checked always wins over location logic. */
    AP_TrackerCheckCount c={0}; AP_TrackerCheckSource src={AP_ContentCodeChecked,AP_TrackerChecksLogic};
    checked_code=321; AP_TrackerCheckAdd(&c,321,src); expect(&c,1,1,0,0,0);
    memset(pads[0].rung_locations,0,sizeof pads[0].rung_locations);
    pads[0].trophy_location=pads[0].ctr_location=0;
    memset(pads[0].relic_locations,0,sizeof pads[0].relic_locations);
    memset(pads[1].legs[2].rungs,0,sizeof pads[1].legs[2].rungs);
    pads[1].legs[2].lap=0; pads[1].legs[2].track_pad=-1;
    pads[1].trophy_location=pads[1].sweep_location=0;
    for(int i=0;i<4;i++) pads[1].legs[i].victory=0;
    memset(bosses.lap,0,sizeof bosses.lap);
    puts("tracker rewards: named race families, absent membership, old optional fields, distinct cup legs, shared codes, boss cups and Final Challenge pass");
}
int main(int argc,char **argv) {
    AP_TrackerNode node={0}; AP_TrackerCheckCount rows[AP_TRACKER_PICKUP_ROWS + AP_TRACKER_SHARED_ROWS];
    char title[160],route[256];
    wumpa_mark_test();
    cup_card_test();
    node.physical=7; strcpy(node.title,"ROO'S TUBES");
    strcpy(pads[0].track_id,"retail:7"); strcpy(pads[0].room_title,"ROO'S TUBES");
    pads[0].wumpa_modes=1; pads[0].wumpa_location=100;
    /* Sparse selected ordinals, deliberately unrelated to the LEV totals. */
    sanity.counts[0]=80;
    sanity.locations[0][0]=102; sanity.locations[0][2]=103; sanity.locations[0][79]=104;
    sanity.locations[1][1]=105; sanity.locations[2][31]=106;
    checked_code=102;
    AP_TrackerChecksBuild(&node,ap_tracker.checks_page,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,0,1,0,0); expect(&rows[1],3,1,1,0,1);
    expect(&rows[2],1,0,0,1,0); expect(&rows[3],1,0,1,0,0);
    checked_code=100;
    AP_TrackerChecksBuild(&node,ap_tracker.checks_page,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,1,0,0,0);
    families.wumpa=1; families.wumpa_global=107;
    AP_TrackerChecksBuild(&node,ap_tracker.checks_page,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,0,0,0,1);
    pads[0].wumpa_modes=0;
    shared_pending=1;
    AP_TrackerChecksBuild(&node,ap_tracker.checks_page,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],0,0,0,0,0);
    expect(&rows[4],1,0,0,0,1);
    shared_pending=0;
    families.wumpa=2;
    /* Cup-only track, no standalone pad. Every leg gets its own counts. */
    node.physical=100; pads[1].cup=1;
    for(int i=0;i<4;i++) { pads[1].legs[i].track_pad=-1; pads[1].legs[i].retail_id=-1; }
    strcpy(pads[1].legs[2].track_id,"retail:7"); pads[1].legs[2].wumpa=100;
    ap_tracker.checks_page=2;
    shared_pending=1;
    AP_TrackerChecksBuild(&node,ap_tracker.checks_page,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,1,0,0,0); expect(&rows[1],3,0,1,1,1);
    expect(&rows[4],1,0,0,0,1);
    assert(strstr(route,"RACE 3 OF 4") && AP_TrackerChecksPages(&node)==4);
    /* A cup-only custom leg keeps the accepted room title, not its package name. */
    strcpy(pads[1].legs[2].room_title,"CRASH COVE (CUSTOM)");
    AP_TrackerChecksBuild(&node,ap_tracker.checks_page,rows,title,sizeof title,route,sizeof route);
    assert(!strcmp(title,"CRASH COVE (CUSTOM)"));
    pads[1].legs[2].room_title[0]=0;
    ap_tracker.checks_page=1;
    AP_TrackerChecksBuild(&node,ap_tracker.checks_page,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],0,0,0,0,0); expect(&rows[1],0,0,0,0,0);
    expect(&rows[4],0,0,0,0,0);
    shared_pending=0;
    /* Ripper Roo's garage has no physical track pad. */
    node.kind=3; node.physical=-1; ap_tracker.hub=1; ap_tracker.checks_page=0;
    strcpy(node.title,"RIPPER ROO"); bosses.wumpa[0]=100;
    AP_TrackerChecksBuild(&node,ap_tracker.checks_page,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,1,0,0,0); expect(&rows[1],3,0,1,1,1);
    assert(strstr(route,"BOSS RACE"));
    bosses.cup[0]=1; ap_tracker.checks_page=2;
    AP_TrackerChecksBuild(&node,ap_tracker.checks_page,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,1,0,0,0); assert(AP_TrackerChecksPages(&node)==4);
    bosses.cup[0]=0; ap_tracker.hub=0; ap_tracker.checks_page=1; bosses.wumpa[5]=108;
    AP_TrackerChecksBuild(&node,ap_tracker.checks_page,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,0,0,1,0); assert(strstr(route,"REMATCH"));
    /* Zero locations and null records are absent rather than complete. */
    memset(&sanity,0,sizeof sanity); bosses.wumpa[5]=0;
    AP_TrackerChecksBuild(&node,ap_tracker.checks_page,rows,title,sizeof title,route,sizeof route);
    for(int i=0;i<4;i++) expect(&rows[i],0,0,0,0,0);
    puts("tracker checks: production track, cup-only legs, boss, boss cup, rematch, sparse ordinals, done and unknown pass");
    reward_tests();
    if(argc==3) pictures(argv[1],argv[2]);
    else {
        partial_and_pickup_tests();
        activity_reason_tests();
    }
    return 0;
}
