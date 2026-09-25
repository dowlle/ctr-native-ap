// cc -m32 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_NATIVE -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DBUILD=926 -I include -I . tools/test-custom-selector.c
// platform/native_custom_track_library.c -lm -o /tmp/test-custom-selector
/* Exercise the Arcade custom pages (box authoring build) with renderer and
   worker boundaries stubbed. This proves navigation and the handoff to the
   offline loader on host slot 6, not rendered acceptance. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <common.h>
#include <platform/native_custom_track_library.h>
#include <platform/native_custom_offline.h>
#include <platform/native_assets.h>
#include <platform/native_custom_track_manager.h>

struct sData sdata_static;
struct Data data;
#define DecalFont_DrawLineOT HarnessUnusedDrawLineOT
#include "../game/DecalFont.c"
#undef DecalFont_DrawLineOT
int MATH_Sin(u32 angle) { return (int)(4096 * sin((angle & 4095) * 6.283185307179586 / 4096)); }
int MATH_Cos(u32 angle) { return MATH_Sin(angle + 1024); }
#include "../game/230/MM_CustomText.c"

struct OverlayDATA_230 D230;
static struct GameTracker tracker;
static struct DB buffer;
static struct CustomTrackLibrary s_contentLibrary;
static int busy, allowed = 1, prepared, launched, checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static void MM_CustomLibrary_Tick(int reload) { (void)reload; }
static int MM_CustomLibrary_Busy(void) { return busy; }
int AP_CustomOfflineLaunchAllowed(void) { return allowed; }
const char *NativeAssets_GetAssetDir(void) { return "/unused"; }
int OtherFX_Play(u32 sound, int flags) { (void)sound; (void)flags; return 0; }
void RECTMENU_ClearInput(void) { memset(sdata->buttonTapPerPlayer, 0, sizeof sdata->buttonTapPerPlayer); }
void MM_TrackSelect_Video_State(int state) { (void)state; }
void DecalFont_DrawLineOT(char *text, int x, int y, s16 font, int flags, uint32_t *ot)
{
    (void)ot;
    int width = DecalFont_GetLineWidth(text, font);
    if (flags & JUSTIFY_CENTER) x -= width / 2;
    /* Retail curved rows intentionally enter/leave beyond the left/top edge.
       Detail and footer text must fit; every title fits its 240px row inset. */
    CHECK(x + width <= 512);
    if (font == FONT_SMALL) CHECK(x >= 0 && y >= 0 && y + 8 <= 216);
    else if (!(flags & JUSTIFY_CENTER)) CHECK(width <= 240);
}
static void box_check(const RECT *box)
{
    CHECK(box->x + box->w <= 512);
}
void RECTMENU_DrawInnerRect(RECT *r, int x, uint32_t *ot) { (void)x; (void)ot; box_check(r); }
void CTR_Box_DrawClearBox(const RECT *r, const Color *c, int t, uint32_t *ot)
{ (void)c; (void)t; (void)ot; box_check(r); }
void CTR_Box_DrawSolidBox(RECT *r, Color c, uint32_t *ot) { (void)c; (void)ot; box_check(r); }
int CustomOffline_StartPrepare(const char *assets, const char *pin)
{ (void)assets; (void)pin; prepared++; return 1; }
int CustomOffline_PollPrepare(struct CustomOfflineRequest **out, char *e, size_t n)
{ (void)e; (void)n; *out = (struct CustomOfflineRequest *)1; return 1; }
void CustomOffline_Free(struct CustomOfflineRequest **out) { *out = NULL; }
int CustomOffline_CheckStructure(const struct CustomOfflineRequest *r, char *e, size_t n)
{ (void)r; (void)e; (void)n; return 1; }
int CustomOffline_RetainPackage(const struct CustomOfflineRequest *r, struct CustomPackageOwned **out)
{ (void)r; *out = (struct CustomPackageOwned *)1; return 1; }
int CustomPackage_GetRaceLaps(const struct CustomPackageOwned *p, unsigned int *laps, char *e, size_t n)
{ (void)p; (void)e; (void)n; *laps = 7; return 1; }
int CustomOffline_PackageLaps(const struct CustomPackageOwned *p, unsigned int *laps, char *e, size_t n)
{ return CustomPackage_GetRaceLaps(p, laps, e, n); }
void CustomPackage_Free(struct CustomPackageOwned **p) { *p = NULL; }
int CustomOffline_RuntimeLaps(void) { return 7; }
static int beganHost = -1;
int CustomOffline_BeginRuntime(struct CustomOfflineRequest **r, int host, int seed)
{
    if (!r || !*r || seed) return 0;
    launched++; *r = NULL; beganHost = host; return 1;
}
#include "../game/230/MM_CustomTrackSelect.c"

static int tick(int buttons)
{
    while (s_arcadeMotion) { sdata->buttonTapPerPlayer[0] = 0; MM_CustomTrackSelect_Tick(&D230.menuTrackSelect); }
    sdata->buttonTapPerPlayer[0] = buttons;
    return MM_CustomTrackSelect_Tick(&D230.menuTrackSelect);
}

int main(void)
{
    struct CustomTrackLibraryEntry entries[17] = {0};
    data.font_charPixWidth[FONT_SMALL] = 13;
    data.font_charPixWidth[FONT_BIG] = 17;
    data.font_puncPixWidth[FONT_SMALL] = 7;
    data.font_puncPixWidth[FONT_BIG] = 11;
    data.font_buttonPixWidth[FONT_SMALL] = 2;
    data.font_buttonPixWidth[FONT_BIG] = 16;
    sdata->gGT = &tracker;
    tracker.backBuffer = &buffer;
    tracker.gameMode1 = ARCADE_MODE;
    tracker.numPlyrNextGame = 1;
    tracker.arcadeDifficulty = 123;
    for (int i = 0; i < 17; i++)
    {
        snprintf(entries[i].manifestSha256, 65, "%064x", i + 1);
        snprintf(entries[i].title, sizeof entries[i].title, "Track %02d", i);
        strcpy(entries[i].version, "1.0.2");
        entries[i].installed = 1; entries[i].local.arcade = 1;
    }
    CHECK(CustomTrackLibrary_Replace(&s_contentLibrary, entries, 17));
    MM_CustomTrackSelect_Init();
    D230.trackSel_transitionState = IN_MENU;
    CHECK(tick(0) == 0 && s_arcadePage == 0);
    CHECK(tick(BTN_LEFT) == 1 && s_arcadePage == 3);
    tick(BTN_DOWN); CHECK(s_arcadeRows[2] == 0); /* One row on final page. */
    CHECK(tick(BTN_RIGHT) == 0 && s_arcadePage == 0);
    tick(BTN_RIGHT); CHECK(s_arcadePage == 1);
    tick(BTN_UP); CHECK(s_arcadeRows[0] == 7);
    tick(BTN_RIGHT); tick(BTN_DOWN); CHECK(s_arcadeRows[1] == 1);
    tick(BTN_LEFT); CHECK(s_arcadeRows[0] == 7);
    tick(BTN_CROSS_one); CHECK(prepared == 1 && s_arcadePrepareBusy);
    tick(BTN_TRIANGLE); CHECK(s_arcadeConfirm && !s_arcadePrepareBusy); /* Drain worker before Back. */
    tick(BTN_TRIANGLE); CHECK(!s_arcadeConfirm && !s_arcadeRequest);
    CHECK(D230.trackSel_transitionState == IN_MENU);
    allowed = 0; tick(BTN_CROSS_one); CHECK(prepared == 1);
    allowed = 1; tracker.numPlyrNextGame = 2;
    tick(BTN_CROSS_one); CHECK(prepared == 1);
    tracker.numPlyrNextGame = 1;
    tick(BTN_CROSS_one); tick(0); tick(BTN_CROSS_one);
    CHECK(launched == 1 && beganHost == 6 && tracker.currLEV == 6 && tracker.numLaps == 7);
    CHECK(tracker.arcadeDifficulty == 123 && tracker.numPlyrNextGame == 1);
    CHECK(D230.trackSel_transitionState == EXITING_MENU && D230.trackSel_StartRaceAfterFadeOut == 1);
    tracker.gameMode1 = TIME_TRIAL;
    CHECK(tick(BTN_RIGHT) == 0);
    tracker.gameMode1 = BATTLE_MODE;
    CHECK(tick(BTN_RIGHT) == 0);
    tracker.gameMode1=ARCADE_MODE;D230.trackSel_transitionState=IN_MENU;
    entries[0].local.arcade=0; /* Installed Time Trial-only content stays out. */
    strcpy(entries[1].local.uuid,"same-track");strcpy(entries[2].local.uuid,"same-track");
    strcpy(entries[1].version,"1.9.0");strcpy(entries[2].version,"1.10.0");
    CHECK(CustomTrackLibrary_Replace(&s_contentLibrary,entries,3));
    MM_CustomTrackSelect_Init();tick(0);
    CHECK(s_arcadeLibrary.count==1 && !strcmp(s_arcadeLibrary.entries[0].version,"1.10.0"));
    CustomTrackLibrary_Free(&s_contentLibrary);
    CustomTrackLibrary_Free(&s_arcadeLibrary);
    printf("custom selector: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
