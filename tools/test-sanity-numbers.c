// cc -m32 -std=c11 -Wall -Wextra -DCTR_AP -DCTR_NATIVE -DCTR_CUSTOM_TRACKS -DBUILD=926 -Iap -I. -Iinclude tools/test-sanity-numbers.c -o /tmp/test-sanity-numbers -lm
// Production binding, draw loop and config. Only the engine projection/font
// endpoints and room/network inputs are replaced; no game or server is run.
#define _GNU_SOURCE
#include <common.h>
#include <platform/native_config.h>
#include <platform/native_custom_race_context.h>
#include <ctr_menu_pages.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include "ap_content_plan.h"
#include "ap_coop.h"

struct sData sdata_static;
struct Data data;
#include "../platform/native_config.c"

static struct GameTracker gt;
static struct DB frame_buffer;
static union { u32 align; unsigned char bytes[65536 + 32]; } arena;
static size_t arena_budget = 65536;
static struct CustomTrackRaceContext context;
static ctr_content_sanity record;
static ctr_content_pad pad;
static uint64_t epoch = 1;
static long checked;
static int logic = 1, route = 1, draws, projections, reads;
static int test_depth = 450, sx = 250, sy = 100;
static unsigned flags;
static char labels[128][4];
static SVECTOR last_world;

int ctr_cfg_active(void) { return 1; }
int ap_content_plan_active(void) { return 1; }
uint64_t ap_content_plan_epoch(void) { return epoch; }
const ctr_content_sanity *ap_content_plan_sanity(const char *id) {
    return strcmp(id, context.trackID) ? NULL : &record;
}
const ctr_content_sanity *ap_content_plan_sanity_boss(int boss) { (void)boss; return &record; }
int ap_content_plan_sanity_pad(int p, int f, int64_t *out, int cap) {
    (void)p; (void)f; (void)out; (void)cap; return 0;
}
static const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int level) { (void)level; return &context; }
static const ctr_content_pad *AP_ContentCupPad(void) { return &pad; }
static const ctr_content_pad *AP_StandaloneRoutePad(const struct CustomTrackRaceContext *c) { (void)c; return route ? &pad : NULL; }
static int CustomTrack_ServingLoad(int a, int b, int c) { (void)a; (void)b; (void)c; return 1; }
static int ap_net_location_checked(long code) { ++reads; return code == checked; }
static int AP_ContentCodeChecked(int64_t code) { return code == checked; }
static int AP_LocationLogicState(long long code) { (void)code; return logic; }
static void AP_AppendLog(const char *s) { (void)s; }
static void AP_FeedNoticeLine(const char *s) { (void)s; }
static int AP_EmitClassCheck(long code, int a, int b, int c, int d, const char *fmt, ...) {
    (void)code; (void)a; (void)b; (void)c; (void)d; (void)fmt; return 1;
}

static void test_load(const SVECTOR *v) { last_world = *v; ++projections; }
static void test_screen(s16 *out) { out[0] = sx; out[1] = sy; }
static void test_font(char *text, int x, int y, s16 font, int style) {
    struct PrimMem *prim = &gt.backBuffer->primMem;
    size_t bytes = strlen(text) * sizeof(POLY_GT4);
    assert((uintptr_t)prim->cursor <= (uintptr_t)prim->guardEnd);
    assert(bytes <= (uintptr_t)prim->guardEnd - (uintptr_t)prim->cursor);
    memset(prim->cursor, 0x5a, bytes);
    prim->cursor = (unsigned char *)prim->cursor + bytes;
    assert(font == FONT_SMALL && data.font_charPixWidth[font] == (test_depth < 450 ? 13 : test_depth < 900 ? 11 : 9));
    assert(x == sx + (draws % 2) && y == sy + (draws % 2));
    assert(style == (JUSTIFY_CENTER | (draws % 2 ? BLACK : WHITE)));
    assert(draws / 2 < 128);
    strcpy(labels[draws++ / 2], text);
}
#define CTR_GteLoadSV0 test_load
#define CTR_GteStoreSXY test_screen
#undef gte_rtps
#undef gte_stsz
#undef gte_stflg
#undef gte_SetRotMatrix
#undef gte_SetTransMatrix
#define gte_rtps() ((void)0)
#define gte_stsz(p) (*(p) = test_depth)
#define gte_stflg(p) (*(p) = flags)
#define gte_SetRotMatrix(p) ((void)(p))
#define gte_SetTransMatrix(p) ((void)(p))
#define DecalFont_DrawLine test_font
#include "../ap/ap_hooks_sanity.c"

static void draw(int expected) {
    int old_reads = reads;
    size_t i;
    gt.backBuffer = &frame_buffer;
    memset(arena.bytes, 0xa5, sizeof arena.bytes);
    frame_buffer.primMem.cursor = arena.bytes;
    frame_buffer.primMem.guardEnd = arena.bytes + arena_budget;
    draws = projections = 0;
    AP_SanityDrawNumbers();
    if (draws != 2 * expected) fprintf(stderr, "draw: expected %d labels, got %d, projections=%d size=%d open=%d ready=%d load=%d config=%d route=%p\n", expected, draws / 2, projections, ap_sanity_size, ap_sanity_objects[0].open, ap_sanity_ready, sdata->Loading.stage, g_config.sanityNumbers, (void *)AP_SanityRoute(&gt));
    assert(draws == 2 * expected && reads == old_reads);
    assert(data.font_charPixWidth[FONT_SMALL] == 7);
    for (i = arena_budget; i < arena_budget + 32; ++i) assert(arena.bytes[i] == 0xa5);
}

static void label_budget(size_t bytes, int expected) {
    SVECTOR world = {0};
    size_t i;
    memset(arena.bytes, 0xa5, sizeof arena.bytes);
    frame_buffer.primMem.cursor = arena.bytes;
    frame_buffer.primMem.guardEnd = arena.bytes + bytes;
    draws = 0;
    AP_WorldNumber_Draw(&world, "80");
    assert(draws == expected * 2);
    assert((unsigned char *)frame_buffer.primMem.cursor == arena.bytes + (expected ? 4 * sizeof(POLY_GT4) : 0));
    assert(data.font_charPixWidth[FONT_SMALL] == 7);
    for (i = bytes; i < bytes + 32; ++i) assert(arena.bytes[i] == 0xa5);
}

int main(void) {
    static struct Model models[3];
    static struct Instance objects[128];
    static struct InstDef defs[128];
    struct Driver player = {0};
    char dir[] = "/tmp/test-sanity-numbers-XXXXXX";
    int i, f, n = 0;
    const char *names[CTR_MENU_PAGE_CAP];
    int rows[CTR_MENU_PAGE_CAP][CTR_MENU_PAGE_ROW_CAP], counts[CTR_MENU_PAGE_CAP];
    int pages = CTR_MenuBuildPages(g_configEntries, g_numConfigEntries, names, rows, counts);
    assert(!g_config.sanityNumbers);
    for (i = 0; i < pages; ++i) {
        int r;
        assert(counts[i] <= CTR_MENU_PAGE_VISIBLE_ROWS);
        for (r = 0; r < counts[i]; ++r)
            if (!strcmp(g_configEntries[rows[i][r]].key, "sanity_numbers")) {
                assert(r > 0 && !strcmp(g_configEntries[rows[i][r - 1]].key, "item_box_numbers"));
                assert(g_configEntries[rows[i][r]].valuePtr == &g_config.sanityNumbers);
            }
    }
    assert(mkdtemp(dir) && !chdir(dir));
    NativeConfig_Save(); g_config.sanityNumbers = true; NativeConfig_Load();
    assert(!g_config.sanityNumbers);
    g_config.sanityNumbers = true; NativeConfig_Save();
    g_config.sanityNumbers = false; NativeConfig_Load(); assert(g_config.sanityNumbers);

    sdata->gGT = &gt; sdata->Loading.stage = LOAD_IDLE;
    gt.gameMode1 = ADVENTURE_MODE; gt.numPlyrCurrGame = 1;
    gt.drivers[0] = &player; data.font_charPixWidth[FONT_SMALL] = 7;
    context.seedEpoch = epoch; context.retail = 1; strcpy(context.trackID, "retail:0");
    models[0].id = PU_RANDOM_CRATE; models[1].id = PU_FRUIT_CRATE; models[2].id = PU_WUMPA_FRUIT;
    // Full protocol budgets, deliberately interleaved like a LEV, with holes
    // in the selected subset. A compact index must never become a label.
    record.counts[0] = 80; record.counts[1] = 16; record.counts[2] = 32;
    for (i = 0; i < 80; ++i) for (f = 0; f < 3; ++f) if (i < record.counts[f]) {
        defs[n].model = &models[f]; defs[n].ptrInstance = &objects[n];
        objects[n].scale.x = 0x1000; objects[n].matrix.t[1] = 50;
        objects[n].flags = DRAW_INSTANCE;
        if (i != 0 && i != 7) record.locations[f][i] = 1000 + f * 100 + i;
        ++n;
    }
    AP_SanityLevelInstances(defs, n); AP_SanityTick(&gt); draw(122);
    assert(!strcmp(labels[0], "2") && !strcmp(labels[1], "2") && !strcmp(labels[2], "2"));
    assert(!strcmp(labels[121], "80") && last_world.vy == 150);
    assert(AP_SanityRecolourTag(&objects[3]) == AP_SanityTag(AP_PAD_LOOK_OPEN, AP_SANITY_VISIBILITY));
    logic = 0; AP_SanityTick(&gt); draw(122);
    assert(AP_SanityRecolourTag(&objects[3]) == AP_SanityTag(AP_PAD_LOOK_CLOSED, AP_SANITY_VISIBILITY));
    AP_SanityPickup(&objects[3], &player); draw(121);
    assert(!AP_SanityRecolourTag(&objects[3]));
    checked = 1101; AP_SanityTick(&gt); draw(120);
    objects[5].scale.x = 0; draw(119); objects[5].scale.x = 0x1000;
    objects[5].flags &= ~DRAW_INSTANCE; draw(119); objects[5].flags |= DRAW_INSTANCE;
    objects[5].flags |= HIDE_MODEL; draw(119); objects[5].flags &= ~HIDE_MODEL;
    g_config.sanityNumbers = false; draw(0); assert(!projections); g_config.sanityNumbers = true;
    gt.numPlyrCurrGame = 2; draw(0); gt.numPlyrCurrGame = 1;
    gt.gameMode1 |= PAUSE_ALL; draw(0); gt.gameMode1 = ADVENTURE_MODE | RELIC_RACE; draw(0);
    gt.gameMode1 = ADVENTURE_MODE; sdata->Loading.stage = 1; draw(0); sdata->Loading.stage = LOAD_IDLE;
    route = 0; draw(0); route = 1;
    test_depth = 99; draw(0); test_depth = 1500; draw(0); test_depth = 100; draw(120);
    test_depth = 900; draw(120); flags = 0x40000; draw(0); flags = 0;
    sx = 19; draw(0); sx = 493; draw(0); sx = 250;
    sy = 15; draw(0); sy = 201; draw(0); sy = 100;
    label_budget(0, 0);
    label_budget(2 * sizeof(POLY_GT4), 0); // White alone fits; the whole label does not.
    label_budget(4 * sizeof(POLY_GT4) - 1, 0);
    label_budget(4 * sizeof(POLY_GT4), 1); // Exact boundary includes both passes.
    frame_buffer.primMem.cursor = arena.bytes + 16;
    frame_buffer.primMem.guardEnd = arena.bytes + 8;
    draws = 0; { SVECTOR world = {0}; AP_WorldNumber_Draw(&world, "80"); }
    assert(!draws);
    arena_budget = 3 * sizeof(POLY_GT4); draw(1); arena_budget = 65536;
    // Different identity routes preserve ordinals, not frozen code arithmetic.
    strcpy(context.trackID, "cortex_vortex"); context.retail = 0; AP_SanityTick(&gt); draw(120);
    strcpy(context.trackID, "custom:north-pole"); AP_SanityTick(&gt); draw(120);
    context.cup = 1; strcpy(pad.legs[0].track_id, context.trackID); draw(120); context.cup = 0;
    ++epoch; context.seedEpoch = epoch; AP_SanityTick(&gt); draw(121);
    record.counts[0] = 79; AP_SanityLevelInstances(defs, n); AP_SanityTick(&gt); draw(0);
    puts("sanity numbers: ordinals, families, routes, completion, config, menu, clipping, visibility flags, two-pass arena bounds and dense binding pass");
    return 0;
}
