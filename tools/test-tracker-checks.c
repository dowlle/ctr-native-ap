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
    if(argc==3) pictures(argv[1],argv[2]);
    return 0;
}
