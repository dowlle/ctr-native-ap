// cc -m32 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_NATIVE -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DBUILD=926 -I include -I . tools/test-custom-selector.c
// platform/native_custom_track_library.c platform/native_custom_records.c -lm -o /tmp/test-custom-selector
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
/* Every string drawn this tick, for the reason and hint checks. */
static char drawn[4096];
static int drawnHas(const char *text) { return strstr(drawn, text) != NULL; }
void DecalFont_DrawLineOT(char *text, int x, int y, s16 font, int flags, uint32_t *ot)
{
    (void)ot;
    if (strlen(drawn) + strlen(text) + 2 < sizeof drawn) { strcat(drawn, text); strcat(drawn, "\n"); }
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
static int checkedMode = -1;
int CustomOffline_CheckStructure(const struct CustomOfflineRequest *r, int mode, char *e, size_t n)
{ (void)r; (void)e; (void)n; checkedMode = mode; return 1; }
int CustomOffline_RetainPackage(const struct CustomOfflineRequest *r, struct CustomPackageOwned **out)
{ (void)r; *out = (struct CustomPackageOwned *)1; return 1; }
int CustomPackage_GetRaceLaps(const struct CustomPackageOwned *p, unsigned int *laps, char *e, size_t n)
{ (void)p; (void)e; (void)n; *laps = 7; return 1; }
int CustomOffline_PackageLaps(const struct CustomPackageOwned *p, unsigned int *laps, char *e, size_t n)
{ return CustomPackage_GetRaceLaps(p, laps, e, n); }
void CustomPackage_Free(struct CustomPackageOwned **p) { *p = NULL; }
int CustomOffline_RuntimeLaps(void) { return 7; }
static int beganHost = -1, beganMode, ghostOnFile, ghostLoads;
int CustomOffline_BeginRuntime(struct CustomOfflineRequest **r, int host, int seed, int mode)
{
    if (!r || !*r || seed) return 0;
    launched++; *r = NULL; beganHost = host; beganMode = mode; return 1;
}
int CustomOffline_RuntimeMode(void) { return beganMode; }
static unsigned char highMem[0x3e00];
void *MEMPACK_AllocHighMem(int size) { CHECK(size == 0x3e00); memset(highMem, 0xee, sizeof highMem); return highMem; }
int MainRaceTrack_CustomLoadGhost(struct GhostHeader *dst)
{
    ghostLoads++;
    if (!ghostOnFile) return 0;
    dst->version = -4; dst->characterID = COCO_BANDICOOT; dst->size = 0x100; dst->timeElapsedInRace = 90000;
    return 1;
}
char *RECTMENU_DrawTime(int time) { static char t[16]; snprintf(t, sizeof t, "T%d", time); return t; }
#include "../game/230/MM_CustomTrackSelect.c"

static int tick(int buttons)
{
    while (s_arcadeMotion) { sdata->buttonTapPerPlayer[0] = 0; MM_CustomTrackSelect_Tick(&D230.menuTrackSelect); }
    sdata->buttonTapPerPlayer[0] = buttons;
    drawn[0] = 0;
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
    tracker.gameMode1 = TIME_TRIAL | ARCADE_MODE; /* not a mode the pages know */
    CHECK(tick(BTN_RIGHT) == 0);
    tracker.gameMode1 = BATTLE_MODE;
    CHECK(tick(BTN_RIGHT) == 0);
    tracker.gameMode1=ARCADE_MODE;D230.trackSel_transitionState=IN_MENU;
    CHECK(beganMode == CTR_OFFLINE_MODE_ARCADE && checkedMode == CTR_OFFLINE_MODE_ARCADE);
    CHECK(!MM_CustomTrackSelect_TimeTrialStart()); /* Arcade never takes the Time Trial exit. */

    /* Installed Time Trial-only content is listed, dimmed, and says why. */
    entries[0].local.arcade=0; entries[0].local.timeTrial=1;
    strcpy(entries[0].local.arcadeReason,"No AI paths");
    strcpy(entries[1].local.uuid,"same-track");strcpy(entries[2].local.uuid,"same-track");
    strcpy(entries[1].version,"1.9.0");strcpy(entries[2].version,"1.10.0");
    CHECK(CustomTrackLibrary_Replace(&s_contentLibrary,entries,3));
    MM_CustomTrackSelect_Init();tick(0);
    CHECK(s_arcadeLibrary.count==2);
    CHECK(!strcmp(s_arcadeLibrary.entries[0].title,"Track 00") && !strcmp(s_arcadeLibrary.entries[1].version,"1.10.0"));
    D230.trackSel_transitionState=IN_MENU;
    tick(BTN_RIGHT); CHECK(s_arcadePage==1);
    s_arcadeRows[0]=0; tick(0);
    CHECK(drawnHas("No AI paths. Use\nTime Trial.\n") || drawnHas("No AI paths.\nUse Time\nTrial.\n"));
    { int before = prepared; tick(BTN_CROSS_one); CHECK(prepared == before && !s_arcadePrepareBusy); }
    CHECK(!strcmp(s_arcadeMessage, "No AI paths. Use Time Trial."));
    CHECK(!drawnHas("..."));

    /* A revision that can race beats a newer one that cannot. */
    entries[2].local.arcade=0; entries[2].local.timeTrial=1; strcpy(entries[2].local.arcadeReason,"No AI paths");
    CHECK(CustomTrackLibrary_Replace(&s_contentLibrary,entries,3));
    MM_CustomTrackSelect_Init();tick(0);
    CHECK(s_arcadeLibrary.count==2 && !strcmp(s_arcadeLibrary.entries[1].version,"1.9.0"));
    entries[2].local.arcade=1; entries[2].local.arcadeReason[0]=0;

    /* Time Trial: the same pages, the Time Trial rule, the Time Trial runtime. */
    tracker.gameMode1=TIME_TRIAL; tracker.numLaps=3;
    entries[1].local.timeTrial=0; strcpy(entries[1].local.timeTrialReason,"No lap checkpoints");
    entries[2].local.timeTrial=0; strcpy(entries[2].local.timeTrialReason,"No lap checkpoints");
    strcpy(entries[0].local.uuid,"0f8fad5b-d9cb-469f-a165-70867728950e");
    CHECK(CustomTrackLibrary_Replace(&s_contentLibrary,entries,3));
    MM_CustomTrackSelect_Init();D230.trackSel_transitionState=IN_MENU;tick(0);
    CHECK(s_arcadeLibrary.count==2);
    tick(BTN_RIGHT); CHECK(s_arcadePage==1);
    s_arcadeRows[0]=0; tick(0);
    CHECK(drawnHas("Installed\n")); /* Time Trial runs Track 00 (no records file) */
    launched=0; prepared=0; beganMode=0;
    tick(BTN_CROSS_one); tick(0); CHECK(s_arcadeConfirm && checkedMode == CTR_OFFLINE_MODE_TIME_TRIAL);
    tick(BTN_CROSS_one);
    CHECK(launched == 1 && beganMode == CTR_OFFLINE_MODE_TIME_TRIAL && tracker.currLEV == 6 && tracker.numLaps == 7);
    CHECK(D230.trackSel_transitionState == EXITING_MENU && D230.trackSel_StartRaceAfterFadeOut == 1);
    /* Leaving the menu: the saved ghost comes from custom-records/, the memory
       card's ghost selection is skipped, and the load is queued. */
    ghostOnFile = 1; ghostLoads = 0; sdata->boolReplayHumanGhost = 0; data.characterIDs[1] = CRASH_BANDICOOT;
    CHECK(MM_CustomTrackSelect_TimeTrialStart());
    CHECK(ghostLoads == 1 && sdata->boolReplayHumanGhost == 1 && data.characterIDs[1] == COCO_BANDICOOT);
    CHECK(sdata->ptrDesiredMenu == &data.menuQueueLoadTrack && sdata->ptrGhostTapePlaying == (void *)highMem);
    CHECK(!MM_CustomTrackSelect_TimeTrialStart()); /* once per launch */
    /* Without a ghost file: an empty playing ghost, no replay, P2 model id 0. */
    MM_CustomTrackSelect_Init();D230.trackSel_transitionState=IN_MENU;tick(0);tick(BTN_RIGHT);s_arcadeRows[0]=0;
    tick(BTN_CROSS_one); tick(0); tick(BTN_CROSS_one); CHECK(launched == 2);
    ghostOnFile = 0; sdata->boolReplayHumanGhost = 1;
    CHECK(MM_CustomTrackSelect_TimeTrialStart());
    CHECK(sdata->boolReplayHumanGhost == 0 && sdata->ptrGhostTapePlaying->characterID == 0 && data.characterIDs[1] == 0);
    /* The track that cannot run a Time Trial says why and does not start. */
    MM_CustomTrackSelect_Init();D230.trackSel_transitionState=IN_MENU;tick(0);tick(BTN_RIGHT);
    s_arcadeRows[0]=1; tick(0);
    CHECK(drawnHas("No lap\ncheckpoints\n"));
    CHECK(!drawnHas("..."));
    { int before = prepared; tick(BTN_CROSS_one); CHECK(prepared == before); }
    /* A retail Time Trial exit is left to the retail ghost selection. */
    CHECK(!MM_CustomTrackSelect_TimeTrialStart());
    tracker.gameMode1=ARCADE_MODE;
    CustomTrackLibrary_Free(&s_contentLibrary);
    CustomTrackLibrary_Free(&s_arcadeLibrary);
    printf("custom selector: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
