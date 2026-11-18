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
int ap_net_location_exists(long long code) { return code>0 && code!=absent_code; }

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
static void reward_tests(void) {
    AP_TrackerNode n={0}; AP_TrackerRewards r;
    n.physical=7; ap_tracker.checks_page=0;
    pads[0].trophy_location=201; pads[0].ctr_location=202;
    for(int i=0;i<3;i++) pads[0].relic_locations[i]=203+i;
    for(int i=0;i<5;i++) pads[0].rung_locations[i]=206+i;
    pads[0].perfect_location=211; pads[0].lap_location=212;
    AP_TrackerRewardsBuild(&n,&r); assert(r.count==12 && AP_TrackerChecksDetails(&n)==3);
    assert(!strcmp(r.rows[5].name,"HELD 1ST") && !strcmp(r.rows[10].name,"RELIC RACE PERFECT"));
    absent_code=211; AP_TrackerRewardsBuild(&n,&r); assert(r.count==11); absent_code=0;
    /* Older supported plans omit optional families. No invented rows. */
    memset(pads[0].rung_locations,0,sizeof pads[0].rung_locations);
    pads[0].perfect_location=pads[0].lap_location=0;
    AP_TrackerRewardsBuild(&n,&r); assert(r.count==5 && AP_TrackerChecksDetails(&n)==2);
    n.kind=2; n.physical=100; pads[1].trophy_location=300; pads[1].sweep_location=301;
    for(int i=0;i<4;i++) { pads[1].legs[i].victory=302+i; pads[1].legs[i].track_pad=-1; }
    strcpy(pads[1].legs[2].room_title,"DISTINCT ROOM TITLE");
    pads[1].legs[2].rungs[0]=310; pads[1].legs[2].lap=311;
    ap_tracker.checks_page=2;
    AP_TrackerRewardsBuild(&n,&r); assert(r.count==8);
    assert(r.rows[0].code==300 && r.rows[1].code==301);
    assert(r.rows[4].code==304 && strstr(r.rows[4].name,"LEG 3 WIN: DISTINCT ROOM TITLE"));
    /* Pre-title-field rooms retain their retail title in the leg reward. */
    char *strings[]={"OLD ROOM RETAIL TITLE"}; sdata->lngStrings=strings;
    pads[1].legs[0].retail_id=7;
    AP_TrackerRewardsBuild(&n,&r); assert(strstr(r.rows[2].name,"OLD ROOM RETAIL TITLE"));
    pads[1].legs[0].retail_id=-1;
    ap_tracker.checks_page=1; AP_TrackerRewardsBuild(&n,&r); assert(r.count==6);
    /* Shared track rungs are deduplicated by code, never by their label. */
    pads[1].legs[2].track_pad=7; pads[0].rung_locations[0]=310;
    ap_tracker.checks_page=2; AP_TrackerRewardsBuild(&n,&r); assert(r.count==8);
    n.kind=3; n.physical=-1; ap_tracker.hub=1; bosses.cup[0]=1;
    AP_TrackerRewardsBuild(&n,&r); assert(r.count==8);
    /* A real boss cup (garage pad) whose leg track has a standalone Trophy
     * Race with enabled rungs: the race's held and finish rows belong to that
     * race alone. Cup win, leg lap and the rest stay. Ordinary cup control. */
    {
        int old_phys=pads[1].physical, i, held=0;
        pads[1].legs[2].rungs[0]=0; pads[1].legs[2].lap=330;
        pads[0].rung_locations[0]=331; pads[0].rung_locations[3]=332;
        n.kind=2; n.physical=100; ap_tracker.checks_page=2;
        AP_TrackerRewardsBuild(&n,&r);
        for(i=0;i<r.count;i++) held|=r.rows[i].code==331 || r.rows[i].code==332;
        assert(held);
        pads[1].physical=AP_CONTENT_GARAGE_CUP_PAD; pads[1].legs[2].rungs[1]=333;
        AP_TrackerRewardsBuild(&n,&r);
        assert(r.rows[0].code==300 && r.rows[1].code==301);
        for(i=0;i<r.count;i++)
            assert(r.rows[i].code!=331 && r.rows[i].code!=332 && r.rows[i].code!=333);
        assert(r.rows[r.count-1].code==330 && !strcmp(r.rows[r.count-1].name,"COMPLETE ONE LAP"));
        pads[1].physical=old_phys; pads[1].legs[2].rungs[1]=0; pads[1].legs[2].lap=311;
        pads[1].legs[2].rungs[0]=310; pads[0].rung_locations[0]=310; pads[0].rung_locations[3]=0;
        n.kind=3; n.physical=-1;
    }
    bosses.cup[0]=0; bosses.lap[0]=320;
    AP_TrackerRewardsBuild(&n,&r); assert(r.count==2 && r.rows[0].code==35011100);
    ap_tracker.hub=0; ap_tracker.checks_page=1; bosses.lap[5]=321;
    AP_TrackerRewardsBuild(&n,&r); assert(r.count==2 && r.rows[0].code==35011105);
    assert(strstr(r.rows[0].name,"FINAL CHALLENGE"));
    absent_code=35011105; bosses.lap[5]=0;
    AP_TrackerRewardsBuild(&n,&r); assert(!r.count && AP_TrackerChecksDetails(&n)==1); absent_code=0;
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
    AP_TrackerNode node={0}; AP_TrackerCheckCount rows[4]; char title[160],route[256];
    node.physical=7; strcpy(node.title,"ROO'S TUBES");
    strcpy(pads[0].track_id,"retail:7"); strcpy(pads[0].room_title,"ROO'S TUBES");
    pads[0].wumpa_modes=1; pads[0].wumpa_location=100;
    /* Sparse selected ordinals, deliberately unrelated to the LEV totals. */
    sanity.counts[0]=80;
    sanity.locations[0][0]=102; sanity.locations[0][2]=103; sanity.locations[0][79]=104;
    sanity.locations[1][1]=105; sanity.locations[2][31]=106;
    checked_code=102;
    AP_TrackerChecksBuild(&node,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,0,1,0,0); expect(&rows[1],3,1,1,0,1);
    expect(&rows[2],1,0,0,1,0); expect(&rows[3],1,0,1,0,0);
    checked_code=100;
    AP_TrackerChecksBuild(&node,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,1,0,0,0);
    families.wumpa=1; families.wumpa_global=107;
    AP_TrackerChecksBuild(&node,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,0,0,0,1);
    pads[0].wumpa_modes=0;
    AP_TrackerChecksBuild(&node,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],0,0,0,0,0);
    families.wumpa=2;
    /* Cup-only track, no standalone pad. Every leg gets its own counts. */
    node.physical=100; pads[1].cup=1;
    for(int i=0;i<4;i++) { pads[1].legs[i].track_pad=-1; pads[1].legs[i].retail_id=-1; }
    strcpy(pads[1].legs[2].track_id,"retail:7"); pads[1].legs[2].wumpa=100;
    ap_tracker.checks_page=2;
    AP_TrackerChecksBuild(&node,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,1,0,0,0); expect(&rows[1],3,0,1,1,1);
    assert(strstr(route,"LEG 3 OF 4") && AP_TrackerChecksPages(&node)==4);
    /* A cup-only custom leg keeps the accepted room title, not its package name. */
    strcpy(pads[1].legs[2].room_title,"CRASH COVE (CUSTOM)");
    AP_TrackerChecksBuild(&node,rows,title,sizeof title,route,sizeof route);
    assert(!strcmp(title,"CRASH COVE (CUSTOM)"));
    pads[1].legs[2].room_title[0]=0;
    ap_tracker.checks_page=1;
    AP_TrackerChecksBuild(&node,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],0,0,0,0,0); expect(&rows[1],0,0,0,0,0);
    /* Ripper Roo's garage has no physical track pad. */
    node.kind=3; node.physical=-1; ap_tracker.hub=1; ap_tracker.checks_page=0;
    strcpy(node.title,"RIPPER ROO"); bosses.wumpa[0]=100;
    AP_TrackerChecksBuild(&node,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,1,0,0,0); expect(&rows[1],3,0,1,1,1);
    assert(strstr(route,"BOSS RACE"));
    bosses.cup[0]=1; ap_tracker.checks_page=2;
    AP_TrackerChecksBuild(&node,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,1,0,0,0); assert(AP_TrackerChecksPages(&node)==4);
    bosses.cup[0]=0; ap_tracker.hub=0; ap_tracker.checks_page=1; bosses.wumpa[5]=108;
    AP_TrackerChecksBuild(&node,rows,title,sizeof title,route,sizeof route);
    expect(&rows[0],1,0,0,1,0); assert(strstr(route,"REMATCH"));
    /* Zero locations and null records are absent rather than complete. */
    memset(&sanity,0,sizeof sanity); bosses.wumpa[5]=0;
    AP_TrackerChecksBuild(&node,rows,title,sizeof title,route,sizeof route);
    for(int i=0;i<4;i++) expect(&rows[i],0,0,0,0,0);
    puts("tracker checks: production track, cup-only legs, boss, boss cup, rematch, sparse ordinals, done and unknown pass");
    reward_tests();
    if(argc==3) pictures(argv[1],argv[2]);
    return 0;
}
