// cc -m32 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_NATIVE -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DBUILD=926 -I include -I . tools/test-custom-selector.c
// platform/native_custom_track_library.c platform/native_custom_records.c -lm -o /tmp/test-custom-selector
/* Exercise the Arcade custom pages with renderer and worker boundaries stubbed.
   Built as is for the authoring builds, and with CTR_AP through
   test-custom-selector-player.c for the player client. This proves navigation
   and the handoff to the offline loader on host slot 6, not rendered
   acceptance. */
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
static unsigned int s_contentStoreGeneration;
static int busy, allowed = 1, prepared, launched, reloads, ticksSeen, checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static void MM_CustomLibrary_Tick(int reload) { reloads += reload; ticksSeen++; }
static int MM_CustomLibrary_Busy(void) { return busy; }
int AP_CustomOfflineLaunchAllowed(void) { return allowed; }
static const char *roomNote = "";
const char *AP_CustomOfflineRoomNote(void) { return roomNote; }
static int rawPad, textResult, textActive;
static char *textBuffer;
int Platform_InputRawGamepadButtons(void) { return rawPad; }
void NativeText_Begin(char *buf, int cap, int x, int y, int w, int h, int password)
{ (void)cap; (void)x; (void)y; (void)w; (void)h; (void)password; textBuffer = buf; textActive = 1; textResult = 0; }
void NativeText_End(void) { textActive = 0; textResult = 0; }
int NativeText_Active(void) { return textActive; }
int NativeText_Result(void) { return textResult; }
void NativeText_Resolve(int result) { textResult = result; }
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

/* What a finished store scan leaves behind. */
static void publish(const struct CustomTrackLibraryEntry *rows, size_t count)
{
    CHECK(CustomTrackLibrary_Replace(&s_contentLibrary, rows, count));
    s_contentStoreGeneration++;
}

static int tick(int buttons)
{
    while (s_arcadeMotion) { sdata->buttonTapPerPlayer[0] = 0; MM_CustomTrackSelect_Tick(&D230.menuTrackSelect); }
    sdata->buttonTapPerPlayer[0] = buttons;
    drawn[0] = 0;
    return MM_CustomTrackSelect_Tick(&D230.menuTrackSelect);
}

/* Empty, one, 18 and 100 tracks: pages, wrap, and the Vanilla page in front. */
static void scale_scenarios(void)
{
    static struct CustomTrackLibraryEntry rows[100];
    const size_t sizes[] = {0, 1, 18, 100};
    memset(rows, 0, sizeof rows);
    for (size_t i = 0; i < 100; i++)
    {
        snprintf(rows[i].manifestSha256, 65, "%064zx", i + 500);
        snprintf(rows[i].title, sizeof rows[i].title, "Scale %03zu", i);
        strcpy(rows[i].version, "1.0.0");
        rows[i].installed = 1; rows[i].local.arcade = 1; rows[i].local.laps = 3;
    }
    for (size_t s = 0; s < 4; s++)
    {
        size_t count = sizes[s], pages = count ? (count + 7) / 8 + 1 : 2;
        publish(rows, count);
        memset(s_arcadeRows, 0, sizeof s_arcadeRows);
        memset(s_arcadeLibrary.rememberedPin, 0, sizeof s_arcadeLibrary.rememberedPin);
        MM_CustomTrackSelect_Init();
        D230.trackSel_transitionState = IN_MENU;
        CHECK(tick(0) == 0 && s_arcadePage == 0); /* Vanilla first, every size. */
        CHECK(s_arcadeLibrary.visibleCount == count);
        CHECK(tick(BTN_LEFT) == 1 && s_arcadePage == pages - 1);
        tick(0);
        CHECK(drawnHas(count ? "Ready\n" : "No tracks."));
        CHECK(tick(BTN_RIGHT) == 0 && s_arcadePage == 0); /* Pages wrap. */
        if (count)
        {
            tick(BTN_RIGHT);
            size_t rowsOnPage = CustomTrackLibrary_PageRowCount(&s_arcadeLibrary, 0);
            tick(BTN_UP); CHECK(s_arcadeRows[0] == rowsOnPage - 1); /* Rows wrap. */
            tick(BTN_DOWN); CHECK(s_arcadeRows[0] == 0);
            tick(BTN_LEFT);
        }
    }
}

static void generic_scenarios(const struct CustomTrackLibraryEntry *start)
{
    static struct CustomTrackLibraryEntry entries[17];
    int before;
    memcpy(entries, start, sizeof entries);
    for (int i = 0; i < 17; i++)
    {
        entries[i].local.uuid[0] = 0; entries[i].local.arcade = 1; entries[i].local.timeTrial = 0;
        entries[i].local.arcadeReason[0] = 0;
    }
    /* A menu entry keeps the cached rows and scans nothing. */
    publish(entries, 17);
    MM_CustomTrackSelect_Init(); D230.trackSel_transitionState = IN_MENU; tick(0);
    {
        unsigned int built = s_arcadeGeneration;
        before = reloads;
        MM_CustomTrackSelect_Init(); tick(0);
        MM_CustomTrackSelect_Init(); tick(0);
        CHECK(reloads == before && s_arcadeGeneration == built);
    }
    /* L2 asks for a scan, on a custom page only. */
    tick(BTN_RIGHT); before = reloads;
    tick(BTN_L2); CHECK(reloads == before + 1);
    tick(BTN_LEFT);
    tick(BTN_L2); CHECK(reloads == before + 1);

    /* A new scan moves the rows but not the player: the pin stays selected. */
    tick(BTN_RIGHT); tick(BTN_RIGHT); /* page 2 */
    tick(BTN_DOWN); tick(BTN_DOWN); tick(0);
    {
        char pin[65];
        struct CustomTrackLibraryEntry more[18];
        const struct CustomTrackLibraryEntry *now;
        memcpy(pin, s_arcadeLibrary.rememberedPin, sizeof pin);
        memcpy(more, entries, 17 * sizeof *more);
        more[17] = entries[0];
        snprintf(more[17].manifestSha256, 65, "%064x", 99);
        strcpy(more[17].title, "AAA first in the alphabet");
        publish(more, 18);
        tick(0);
        now = CustomTrackLibrary_Row(&s_arcadeLibrary, (s_arcadePage - 1) * CTR_LIBRARY_PAGE_ROWS + s_arcadeRows[s_arcadePage - 1]);
        CHECK(now && !strcmp(now->manifestSha256, pin));
        /* The remembered revision left the store: the page stays valid. */
        publish(more, 2);
        tick(0); tick(0);
        CHECK(s_arcadePage == 1);
    }

    /* Search: live on the page's rows, by title, author and version; Triangle
       restores the old query; a query with no match says so. */
    strcpy(entries[3].author, "Some Author");
    strcpy(entries[4].version, "7.7.7");
    publish(entries, 17);
    MM_CustomTrackSelect_Init(); D230.trackSel_transitionState = IN_MENU; tick(0); tick(BTN_RIGHT);
    CHECK(s_arcadeLibrary.visibleCount == 17);
    tick(BTN_R2); CHECK(s_arcadeSearching && textActive);
    strcpy(textBuffer, "some auth"); tick(0);
    CHECK(s_arcadeLibrary.visibleCount == 1 && !strcmp(CustomTrackLibrary_Row(&s_arcadeLibrary, 0)->title, "Track 03"));
    CHECK(tick(BTN_LEFT) == 1 && s_arcadePage == 1); /* Typing never navigates. */
    rawPad = RAW_BTN_CROSS; tick(0); rawPad = 0;
    CHECK(!s_arcadeSearching && !textActive && s_arcadeLibrary.visibleCount == 1);
    tick(0); CHECK(drawnHas("Ready\n"));
    tick(BTN_R2); strcpy(textBuffer, "7.7.7"); tick(0);
    CHECK(s_arcadeLibrary.visibleCount == 1 && !strcmp(CustomTrackLibrary_Row(&s_arcadeLibrary, 0)->title, "Track 04"));
    rawPad = RAW_BTN_TRIANGLE; tick(0); rawPad = 0;
    CHECK(!s_arcadeSearching && !strcmp(s_arcadeQuery, "some auth") && s_arcadeLibrary.visibleCount == 1);
    tick(BTN_R2); strcpy(textBuffer, "nothing like it"); tick(0);
    rawPad = RAW_BTN_START; tick(0); rawPad = 0; tick(0);
    CHECK(s_arcadeLibrary.visibleCount == 0 && drawnHas("No search"));
    /* A new entry starts with the whole list. */
    MM_CustomTrackSelect_Init(); CHECK(s_arcadeLibrary.visibleCount == 17);
    D230.trackSel_transitionState = IN_MENU; tick(0);

    /* A revision that failed its checks stays visible, says why, and cannot
       start; Ready is the only state that prepares a launch. */
    {
        struct CustomTrackLibraryEntry mixed[3];
        memcpy(mixed, entries, sizeof mixed);
        mixed[0].installed = 0; mixed[0].local.arcade = 0;
        strcpy(mixed[0].disabledReason, "Hash mismatch");
        mixed[1].local.arcade = 0; strcpy(mixed[1].local.arcadeReason, "No AI paths");
        publish(mixed, 3);
        MM_CustomTrackSelect_Init(); D230.trackSel_transitionState = IN_MENU; tick(0);
        CHECK(s_arcadeLibrary.count == 3);
        tick(BTN_RIGHT);
        s_arcadeRows[0] = 0; tick(0); CHECK(drawnHas("Hash\nmismatch\n") || drawnHas("Hash mismatch\n"));
        before = prepared;
        tick(BTN_CROSS_one); CHECK(prepared == before && !s_arcadePrepareBusy && !strcmp(s_arcadeMessage, "Hash mismatch"));
        tick(BTN_DOWN); tick(BTN_CROSS_one); CHECK(prepared == before && !s_arcadePrepareBusy);
        tick(BTN_DOWN); tick(BTN_CROSS_one); CHECK(prepared == before + 1 && s_arcadePrepareBusy);
        tick(0); CHECK(s_arcadeConfirm);
        tick(BTN_TRIANGLE); CHECK(!s_arcadeConfirm);
    }

    /* Author and measured facts on the panel; a room adds its note at Start. */
    {
        struct CustomTrackLibraryEntry one = entries[0];
        strcpy(one.author, "Author");
        one.local.laps = 3; one.local.timeTrial = 1;
        publish(&one, 1);
        MM_CustomTrackSelect_Init(); D230.trackSel_transitionState = IN_MENU; tick(0);
        tick(BTN_RIGHT); tick(0);
        CHECK(drawnHas("Author\n") && drawnHas("3 laps TT\n") && drawnHas("Ready\n"));
        tick(BTN_CROSS_one); tick(0); CHECK(s_arcadeConfirm && !drawnHas("Room:"));
        roomNote = "Room: no checks here, items wait";
        tick(0); CHECK(s_arcadeConfirm && drawnHas("Room:") && drawnHas("Start race\n"));
        before = launched;
        tick(BTN_CROSS_one); CHECK(launched == before + 1 && beganHost == 6); /* A room is no refusal. */
        roomNote = "";
        D230.trackSel_transitionState = IN_MENU;
    }
    scale_scenarios();
    publish(start, 3);
    MM_CustomTrackSelect_Init(); D230.trackSel_transitionState = IN_MENU; tick(0);
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
    publish(entries, 17);
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
    publish(entries,3);
    MM_CustomTrackSelect_Init();tick(0);
    CHECK(s_arcadeLibrary.count==2);
    CHECK(!strcmp(s_arcadeLibrary.entries[0].title,"Track 00") && !strcmp(s_arcadeLibrary.entries[1].version,"1.10.0"));
    D230.trackSel_transitionState=IN_MENU;
    tick(BTN_RIGHT); CHECK(s_arcadePage==1);
    s_arcadeRows[0]=0; tick(0);
#ifndef CTR_AP
    CHECK(drawnHas("No AI paths. Use\nTime Trial.\n") || drawnHas("No AI paths.\nUse Time\nTrial.\n"));
#endif
    { int before = prepared; tick(BTN_CROSS_one); CHECK(prepared == before && !s_arcadePrepareBusy); }
#ifndef CTR_AP
    CHECK(!strcmp(s_arcadeMessage, "No AI paths. Use Time Trial."));
#endif
    CHECK(!drawnHas("..."));

    /* A revision that can race beats a newer one that cannot. */
    entries[2].local.arcade=0; entries[2].local.timeTrial=1; strcpy(entries[2].local.arcadeReason,"No AI paths");
    publish(entries,3);
    MM_CustomTrackSelect_Init();tick(0);
    CHECK(s_arcadeLibrary.count==2 && !strcmp(s_arcadeLibrary.entries[1].version,"1.9.0"));
    entries[2].local.arcade=1; entries[2].local.arcadeReason[0]=0;

    generic_scenarios(entries);
#ifdef CTR_AP
    /* The player client races Single Race only: Time Trial stays retail. */
    tracker.gameMode1=TIME_TRIAL;
    CHECK(MM_CustomTrackSelect_Mode()==0);
    { int before=ticksSeen; MM_CustomTrackSelect_Init(); CHECK(ticksSeen==before); }
    CHECK(tick(BTN_RIGHT)==0);
    /* A Time Trial fact does not send the player to a page they do not have. */
    tracker.gameMode1=ARCADE_MODE;
    {
        struct CustomTrackLibraryEntry only=entries[0];
        char why[260];
        only.local.arcade=0; only.local.timeTrial=1; strcpy(only.local.arcadeReason,"No AI paths");
        CHECK(!MM_CustomTrackSelect_Eligible(&only,why,sizeof why) && !strcmp(why,"No AI paths"));
    }
#else
    /* Time Trial: the same pages, the Time Trial rule, the Time Trial runtime. */
    tracker.gameMode1=TIME_TRIAL; tracker.numLaps=3;
    entries[1].local.timeTrial=0; strcpy(entries[1].local.timeTrialReason,"No lap checkpoints");
    entries[2].local.timeTrial=0; strcpy(entries[2].local.timeTrialReason,"No lap checkpoints");
    strcpy(entries[0].local.uuid,"0f8fad5b-d9cb-469f-a165-70867728950e");
    publish(entries,3);
    MM_CustomTrackSelect_Init();D230.trackSel_transitionState=IN_MENU;tick(0);
    CHECK(s_arcadeLibrary.count==2);
    tick(BTN_RIGHT); CHECK(s_arcadePage==1);
    s_arcadeRows[0]=0; tick(0);
    CHECK(drawnHas("Ready\n")); /* Time Trial runs Track 00 (no records file) */
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
#endif
    CustomTrackLibrary_Free(&s_contentLibrary);
    CustomTrackLibrary_Free(&s_arcadeLibrary);
    printf("custom selector: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
