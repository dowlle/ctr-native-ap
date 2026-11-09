// cc -std=c11 -Wall -Wextra -DCTR_AP -Iap tools/test-sanity.c -o /tmp/test-sanity
// Exercises the production runtime against a synthetic LEV instance table.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ap_content_plan.h"
#define CTR_CUSTOM_TRACKS
#define PU_RANDOM_CRATE 8
#define PU_FRUIT_CRATE 7
#define PU_WUMPA_FRUIT 2
#define ACTION_BOT 1
#define ADVENTURE_MODE 1
#define ADVENTURE_ARENA 2
#define ARCADE_MODE 4
#define BATTLE_MODE 8
#define CRYSTAL_CHALLENGE 16
#define MAIN_MENU 32
#define RELIC_RACE 64
#define TIME_TRIAL 256
#define IS_BOSS_RACE(m) ((m) & 128)
struct Model { int id; };
struct Instance { unsigned colorRGBA; short alphaScale; };
struct InstDef { struct Model *model; struct Instance *ptrInstance; };
struct Driver { int actionsFlagSet, driverID; };
struct GameTracker { int levelID, gameMode1, boolDemoMode, bossID; struct Driver *drivers[2]; };
struct CustomTrackRaceContext { uint64_t seedEpoch; int cup, leg, retail; char trackID[65]; };
static struct GameTracker gt;
static struct { struct GameTracker *gGT; } globals = { &gt }, *sdata = &globals;
static ctr_content_sanity record;
static ctr_content_pad pad;
static struct CustomTrackRaceContext context;
static uint64_t epoch = 1;
static int sent, checked, route_valid = 1, notices;
int ctr_cfg_active(void) { return 1; }
int ap_content_plan_active(void) { return 1; }
uint64_t ap_content_plan_epoch(void) { return epoch; }
const ctr_content_sanity *ap_content_plan_sanity(const char *id) { return strcmp(id, "retail:3") ? NULL : &record; }
const ctr_content_sanity *ap_content_plan_sanity_boss(int boss) { return boss == 0 ? &record : NULL; }
int ap_content_plan_sanity_pad(int physical, int family, int64_t *out, int capacity) {
    (void)physical; (void)family; if (capacity && out) out[0] = 123; return capacity > 0;
}
static const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int level) { (void)level; return &context; }
static const ctr_content_pad *AP_ContentCupPad(void) { return &pad; }
static const ctr_content_pad *AP_StandaloneRoutePad(const struct CustomTrackRaceContext *c) { (void)c; return route_valid ? &pad : NULL; }
static int CustomTrack_ServingLoad(int a, int b, int c) { (void)a; (void)b; (void)c; return 1; }
static int ap_net_location_checked(long code) { (void)code; return checked; }
static int ap_net_location_exists(long code) { return code > 0; }
static int AP_ContentCodeChecked(int64_t code) { (void)code; return checked; }
static void AP_AppendLog(const char *line) { (void)line; }
static void AP_FeedNoticeLine(const char *line) { assert(strstr(line, "CHECKED")); ++notices; }
static int AP_EmitClassCheck(long code, int ledger, int bit, int tag, int toast, const char *fmt, ...) {
    assert(code > 0 && !ledger && bit == -1 && tag == -1 && !toast && fmt); ++sent; return 1;
}
#include "ap_hooks_sanity.c"
int main(void) {
    struct Model models[] = {{8}, {8}, {7}, {2}};
    struct Instance objects[4] = {{0}};
    struct InstDef defs[4];
    struct Driver player = {0, 0}, bot = {ACTION_BOT, 1}, second = {0, 1};
    int i;
    gt.gameMode1 = ADVENTURE_MODE; gt.drivers[0] = &player;
    context.seedEpoch = 1; context.retail = 1; strcpy(context.trackID, "retail:3");
    record.counts[0] = 2; record.counts[1] = record.counts[2] = 1;
    record.locations[0][1] = 101; record.locations[1][0] = 102; record.locations[2][0] = 103;
    for (i = 0; i < 4; ++i) { defs[i].model = &models[i]; defs[i].ptrInstance = &objects[i]; }
    AP_SanityLevelInstances(defs, 4); AP_SanityTick(&gt);
    assert(objects[0].colorRGBA == 0 && objects[1].colorRGBA != 0);
    assert(AP_SanityPickup(&objects[3], &bot) == 0 && !sent);
    assert(AP_SanityPickup(&objects[1], &bot) == 1 && !sent);
    AP_SanityPickup(&objects[1], &second); assert(!sent);
    AP_SanityPickup(&objects[1], &player); assert(sent == 1 && objects[1].colorRGBA == 0);
    AP_SanityPickup(&objects[1], &player); assert(sent == 1);
    AP_SanityPickup(&objects[3], &player); assert(sent == 2);
    assert(AP_SanityPickup(&objects[3], &bot) == 1);
    AP_SanityLevelInstances(defs, 4); AP_SanityPickup(&objects[1], &player); assert(sent == 2);
    gt.gameMode1 |= RELIC_RACE; AP_SanityPickup(&objects[2], &player); assert(sent == 2);
    gt.gameMode1 = ADVENTURE_MODE | TIME_TRIAL; AP_SanityPickup(&objects[2], &player); assert(sent == 2);
    gt.gameMode1 = ADVENTURE_MODE; route_valid = 0; AP_SanityPickup(&objects[2], &player); assert(sent == 2);
    route_valid = 1; AP_SanityPickup(&objects[2], &player); assert(sent == 3);
    for (i = 0; i < 121; ++i) AP_SanityTick(&gt);
    assert(notices == 3);
    ++epoch; context.seedEpoch = epoch; record.counts[0] = 3;
    AP_SanityLevelInstances(defs, 4); AP_SanityPickup(&objects[2], &player); assert(sent == 3 && ap_sanity_ready == -1);
    AP_SanityLevelInstances(defs, 513); AP_SanityPickup(&objects[2], &player); assert(sent == 3);
    puts("sanity runtime: ordinal pick, AI, local player, dedup, reload, race mode, route, feed and mismatch pass");
    return 0;
}
