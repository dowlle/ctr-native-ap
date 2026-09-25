/* Custom pages at the normal track selector in Arcade and Time Trial, box
   authoring build only. Included by MM_TrackSelect.c inside
   CTR_CUSTOM_PACKAGES. Custom pages use owned library metadata, never indices
   into retail track/ghost tables.

   One custom track at a time: Cross prepares the chosen installed package,
   the second Cross hands it to the offline loader (native_custom_offline.h)
   and starts a one-player Arcade single race or a Time Trial under the track's
   own identity, CTR_CUSTOM_LEVEL_ID (native_custom_identity.h). The engine
   reads the package's LEV and VRM through one arcade BIGFILE subfile group;
   which group that is, is a loading detail below and never the track's
   identity. Leaving the race ends the custom runtime; Retry keeps it.

   Every installed track is listed. One that cannot run in this mode is drawn
   dimmed and says why (native_custom_content_verify.h). A Time Trial reads
   and writes its best times and ghost in custom-records/
   (native_custom_records.h), never the memory card. */
#include <platform/native_custom_track_library.h>
#include <platform/native_custom_offline.h>
#include <platform/native_assets.h>
#include <platform/native_custom_revision.h>

#include <platform/native_custom_identity.h>
#include <platform/native_custom_records.h>

/* The BIGFILE subfile group the package's bytes are served through (arcade
   group 6). A loading mechanism only: difficulty, music, sound, banner, high
   scores, AI recordings, logs and box keys all use the custom identity, see
   the audit in native_custom_identity.h and tools/test-custom-identity.c. */
#define MM_CUSTOM_ARCADE_HOST_LEVEL 6

static struct CustomTrackLibrary s_arcadeLibrary;
static size_t s_arcadePage;
static size_t s_arcadeRows[CTR_LIBRARY_MAX / CTR_LIBRARY_PAGE_ROWS + 1];
static int s_arcadeScanWasBusy;
static int s_arcadePrepareBusy;
static int s_arcadeConfirm;
static int s_arcadeMotion, s_arcadeMotionDirection;
static struct CustomOfflineRequest *s_arcadeRequest;
static char s_arcadeMessage[512];
static int s_customTimeTrialPending;
/* Best race time of the highlighted track (Time Trial), cached per row. */
static char s_recordsPin[65];
static char s_recordsLine[32];

/* CTR_OFFLINE_MODE_ARCADE or _TIME_TRIAL, 0 where custom pages do not show. */
static int MM_CustomTrackSelect_Mode(void)
{
    u32 mode1 = sdata->gGT->gameMode1;
    if ((mode1 & (BATTLE_MODE | ADVENTURE_MODE | ADVENTURE_CUP)) || (sdata->gGT->gameMode2 & CUP_ANY_KIND)) return 0;
    if ((mode1 & (ARCADE_MODE | TIME_TRIAL)) == ARCADE_MODE) return CTR_OFFLINE_MODE_ARCADE;
    if ((mode1 & (ARCADE_MODE | TIME_TRIAL)) == TIME_TRIAL) return CTR_OFFLINE_MODE_TIME_TRIAL;
    return 0;
}

static int MM_CustomTrackSelect_Enabled(void)
{
    return MM_CustomTrackSelect_Mode() != 0;
}

/* Whether an installed track can run in this page's mode, and if not, a
   sentence for the detail panel (three 13-character lines at most). */
static int MM_CustomTrackSelect_Eligible(const struct CustomTrackLibraryEntry *entry, char *why, size_t whySize)
{
    int timeTrial = MM_CustomTrackSelect_Mode() == CTR_OFFLINE_MODE_TIME_TRIAL;
    if (why && whySize) why[0] = 0;
    if (!entry || !entry->installed) return 0;
    if (timeTrial ? entry->local.timeTrial : entry->local.arcade) return 1;
    if (why && whySize)
    {
        if (!timeTrial && entry->local.timeTrial)
            snprintf(why, whySize, "%s. Use Time Trial.", entry->local.arcadeReason);
        else
            snprintf(why, whySize, "%s", timeTrial ? entry->local.timeTrialReason : entry->local.arcadeReason);
    }
    return 0;
}

/* "Best 1:23.45" for the highlighted track in Time Trial, read from
   custom-records/ once per highlighted revision. */
static const char *MM_CustomTrackSelect_BestTime(const struct CustomTrackLibraryEntry *entry)
{
    struct CustomRecordKey key;
    struct CustomRecordTimes times;
    if (!entry || MM_CustomTrackSelect_Mode() != CTR_OFFLINE_MODE_TIME_TRIAL) return "";
    if (strcmp(s_recordsPin, entry->manifestSha256))
    {
        memcpy(s_recordsPin, entry->manifestSha256, sizeof s_recordsPin);
        s_recordsLine[0] = 0;
        if (CustomRecords_MakeKey(&key, entry->local.uuid, entry->local.levSha256, entry->local.vrmSha256,
                                  entry->local.laps) &&
            CustomRecords_LoadTimes(CTR_RECORDS_DIR, &key, &times) && times.entry[1].time < 0x8c640)
            snprintf(s_recordsLine, sizeof s_recordsLine, "Best %s", RECTMENU_DrawTime(times.entry[1].time));
    }
    return s_recordsLine;
}

/* MM_TrackSelect.c, leaving the menu with a Time Trial chosen: a custom
   track loads its saved ghost from custom-records/ into the retail playing
   buffer and goes straight to the load, skipping the memory card's ghost
   selection. Returns 0 for retail tracks. */
static int MM_CustomTrackSelect_TimeTrialStart(void)
{
    if (!s_customTimeTrialPending) return 0;
    s_customTimeTrialPending = 0;
    if (CustomOffline_RuntimeMode() != CTR_OFFLINE_MODE_TIME_TRIAL) return 0;
    sdata->ptrGhostTapePlaying = MEMPACK_AllocHighMem(0x3e00);
    memset(sdata->ptrGhostTapePlaying, 0, 0x28);
    sdata->boolReplayHumanGhost = MainRaceTrack_CustomLoadGhost(sdata->ptrGhostTapePlaying);
    /* The ghost's character is P2 (the Time Trial pack loads its model). */
    data.characterIDs[1] = sdata->ptrGhostTapePlaying->characterID;
    sdata->ptrDesiredMenu = &data.menuQueueLoadTrack;
    sdata->errorMessagePosIndex = 0;
    return 1;
}

static void MM_CustomTrackSelect_Init(void)
{
    if (!MM_CustomTrackSelect_Enabled()) return;
    /* Vanilla always opens first; row positions on custom pages survive Back. */
    s_arcadePage = 0;
    s_arcadeMotion = 0;
    s_arcadeConfirm = 0;
    s_arcadeMessage[0] = 0;
    s_recordsPin[0] = 0;
    s_customTimeTrialPending = 0;
    CustomOffline_Free(&s_arcadeRequest);
    MM_CustomLibrary_Tick(1);
    s_arcadeScanWasBusy = 1;
}

static void MM_CustomTrackSelect_Text(const char *text, int x, int y, size_t width,
                                     size_t lines, int color, uint32_t *ot)
{
    MM_CustomText_Draw(text, x, y, (int)width, lines, color, ot);
}

/* Called after the retail transition state advances, before retail indexing.
   Returning 1 owns this frame; Vanilla returns to the unchanged retail proc. */
static int MM_CustomTrackSelect_Tick(struct RectMenu *menu)
{
    struct GameTracker *gGT = sdata->gGT;
    uint32_t *ot = gGT->backBuffer->otMem.uiOT;
    size_t pages, rows, row, first;
    const struct CustomTrackLibraryEntry *entry = NULL;
    int tapped = sdata->buttonTapPerPlayer[0];
    char line[256], label[16];
    if (!MM_CustomTrackSelect_Enabled()) return 0;

    if (!s_arcadePrepareBusy) MM_CustomLibrary_Tick(0);
    if (s_arcadeScanWasBusy && !MM_CustomLibrary_Busy())
    {
        /* Copy entries, not the manager's query/Current Seed filter. Every
           installed track shows, one row per track: the newest revision that
           can run in this mode, else the newest revision. */
        struct CustomTrackLibraryEntry *eligible = calloc(s_contentLibrary.count ? s_contentLibrary.count : 1, sizeof *eligible);
        size_t count = 0;
        if (eligible)
        {
            for (size_t i=0;i<s_contentLibrary.count;i++)
            {
                const struct CustomTrackLibraryEntry *item=&s_contentLibrary.entries[i];
                if (!item->installed) continue;
                size_t at=0;
                for (;at<count;at++)
                    if (item->local.uuid[0] && !strcmp(item->local.uuid,eligible[at].local.uuid)) break;
                if (at==count) eligible[count++]=*item;
                else
                {
                    int itemOk = MM_CustomTrackSelect_Eligible(item, NULL, 0);
                    int heldOk = MM_CustomTrackSelect_Eligible(&eligible[at], NULL, 0);
                    if (itemOk != heldOk ? itemOk : CustomRevision_Compare(item->version,eligible[at].version)>0)
                        eligible[at]=*item;
                }
            }
            CustomTrackLibrary_Replace(&s_arcadeLibrary, eligible, count);
            free(eligible);
        }
        s_arcadeScanWasBusy = 0;
    }
    pages = CustomTrackLibrary_PageCount(&s_arcadeLibrary) + 1;
    if (s_arcadePage >= pages) s_arcadePage = pages - 1;
    if (D230.trackSel_transitionState != IN_MENU || D230.trackSel_boolOpenLapBox ||
        D230.trackSel_changeTrack_frameCount || s_arcadeMotion) tapped = 0;

    if (s_arcadePrepareBusy)
    {
        int result = CustomOffline_PollPrepare(&s_arcadeRequest, s_arcadeMessage, sizeof s_arcadeMessage);
        if (result != 0)
        {
            s_arcadePrepareBusy = 0;
            s_arcadeConfirm = result == 1;
            if (result == 1 && !CustomOffline_CheckStructure(s_arcadeRequest, MM_CustomTrackSelect_Mode(),
                                                             s_arcadeMessage, sizeof s_arcadeMessage))
            {
                s_arcadeConfirm = 0;
                CustomOffline_Free(&s_arcadeRequest);
            }
        }
        tapped = 0;
    }
    if (!s_arcadePrepareBusy && !s_arcadeConfirm && (tapped & (BTN_LEFT | BTN_RIGHT)))
    {
        s_arcadePage = (s_arcadePage + ((tapped & BTN_LEFT) ? pages - 1 : 1)) % pages;
        s_arcadeMessage[0] = 0;
        MM_TrackSelect_Video_State(1);
        OtherFX_Play(0, 1);
        RECTMENU_ClearInput();
        tapped = 0;
    }
    if (!s_arcadePage)
    {
        if (D230.trackSel_transitionState == IN_MENU && !D230.trackSel_boolOpenLapBox)
        {
            DecalFont_DrawLineOT("<  Vanilla  >", 394, 178, FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
            snprintf(line, sizeof line, "Page 1/%u", (unsigned)pages);
            DecalFont_DrawLineOT(line, 394, 190, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
        }
        /* Drain an in-flight store scan before leaving this menu. */
        if (MM_CustomLibrary_Busy()) RECTMENU_ClearInput();
        return 0;
    }

    rows = CustomTrackLibrary_PageRowCount(&s_arcadeLibrary, s_arcadePage - 1);
    row = s_arcadeRows[s_arcadePage - 1];
    if (row >= rows) row = 0;
    first = (s_arcadePage - 1) * CTR_LIBRARY_PAGE_ROWS;
    if (s_arcadeConfirm)
    {
        if (tapped & (BTN_TRIANGLE | BTN_SQUARE_one))
        {
            s_arcadeConfirm = 0;
            CustomOffline_Free(&s_arcadeRequest);
            OtherFX_Play(2, 1);
        }
        else if (tapped & (BTN_CROSS_one | BTN_CIRCLE))
        {
            const int host = MM_CUSTOM_ARCADE_HOST_LEVEL;
            const int mode = MM_CustomTrackSelect_Mode();
            if (AP_CustomOfflineLaunchAllowed() && gGT->numPlyrNextGame == 1 &&
                CustomOffline_BeginRuntime(&s_arcadeRequest, host, 0, mode))
            {
                /* Preserve the players, difficulty and characters chosen in
                   Arcade. Only the selected content and authored laps change. */
                gGT->currLEV = host;
                gGT->numLaps = CustomOffline_RuntimeLaps();
                D230.trackSel_StartRaceAfterFadeOut = 1;
                D230.trackSel_transitionState = EXITING_MENU;
                s_customTimeTrialPending = mode == CTR_OFFLINE_MODE_TIME_TRIAL;
                fprintf(stderr, "[CustomSelector] custom track %d (bytes via group %d) mode=%s players=%d difficulty=%d laps=%d\n",
                    CTR_CUSTOM_LEVEL_ID, host, mode == CTR_OFFLINE_MODE_TIME_TRIAL ? "time trial" : "arcade",
                    gGT->numPlyrNextGame, gGT->arcadeDifficulty, gGT->numLaps);
                OtherFX_Play(1, 1);
            }
            else
            {
                snprintf(s_arcadeMessage, sizeof s_arcadeMessage, "Could not start this track.");
                CustomOffline_Free(&s_arcadeRequest);
            }
            s_arcadeConfirm = 0;
        }
    }
    else if (!s_arcadePrepareBusy)
    {
        if (rows && (tapped & (BTN_UP | BTN_DOWN)))
        {
            row = (row + ((tapped & BTN_UP) ? rows - 1 : 1)) % rows;
            s_arcadeMotion = rows > 1 ? 3 : 0;
            s_arcadeMotionDirection = (tapped & BTN_UP) ? -1 : 1;
            s_arcadeMessage[0] = 0;
            OtherFX_Play(0, 1);
        }
        if (!MM_CustomLibrary_Busy() && (tapped & (BTN_TRIANGLE | BTN_SQUARE_one)))
        {
            D230.trackSel_StartRaceAfterFadeOut = 0;
            D230.trackSel_transitionState = EXITING_MENU;
            OtherFX_Play(2, 1);
        }
        entry = rows ? CustomTrackLibrary_Row(&s_arcadeLibrary, first + row) : NULL;
        if (entry && !MM_CustomLibrary_Busy() && (tapped & (BTN_CROSS_one | BTN_CIRCLE)))
        {
            char why[80];
            if (!entry->installed)
                snprintf(s_arcadeMessage, sizeof s_arcadeMessage, "Install this track in Options / Custom Content.");
            else if (!MM_CustomTrackSelect_Eligible(entry, why, sizeof why))
                snprintf(s_arcadeMessage, sizeof s_arcadeMessage, "%s", why);
            else if (!AP_CustomOfflineLaunchAllowed())
                snprintf(s_arcadeMessage, sizeof s_arcadeMessage, "Custom tracks need a client with no Archipelago seed.");
            else if (gGT->numPlyrNextGame != 1)
                snprintf(s_arcadeMessage, sizeof s_arcadeMessage, "Custom tracks are one player only. Choose one player.");
            else
            {
                s_arcadePrepareBusy = CustomOffline_StartPrepare(NativeAssets_GetAssetDir(), entry->manifestSha256);
                snprintf(s_arcadeMessage, sizeof s_arcadeMessage, "%s", s_arcadePrepareBusy ?
                    "Checking selected track..." : "Could not prepare this revision.");
            }
        }
    }
    s_arcadeRows[s_arcadePage - 1] = row;
    entry = rows ? CustomTrackLibrary_Row(&s_arcadeLibrary, first + row) : NULL;
    RECTMENU_ClearInput();

    /* Match the retail selector: curved big-font rows, translucent boxes,
       three-frame scrolling and the same entrance/exit transition offsets. */
    int dx = D230.transitionMeta_trackSel[1].currX;
    int dy = D230.transitionMeta_trackSel[1].currY;
    CustomTrackLibrary_PageLabel(&s_arcadeLibrary, s_arcadePage - 1, label, sizeof label);
    snprintf(line, sizeof line, "< Custom %s >", label);
    MM_CustomText_Draw(line, 394 + dx, 10 + dy, 192, 1, JUSTIFY_CENTER | ORANGE, ot);
    snprintf(line, sizeof line, "Page %u/%u", (unsigned)s_arcadePage + 1, (unsigned)pages);
    MM_CustomText_Draw(line, 394 + dx, 24 + dy, 192, 1, JUSTIFY_CENTER | WHITE, ot);
    for (size_t i = 0; i < rows; i++)
    {
        const struct CustomTrackLibraryEntry *item = CustomTrackLibrary_Row(&s_arcadeLibrary, first + i);
        int offset = (int)i - (int)row;
        if (offset > (int)rows / 2) offset -= (int)rows;
        if (offset < -(int)rows / 2) offset += (int)rows;
        if (offset == 4) offset = -4;
        int angle = offset * 0x73 + s_arcadeMotionDirection * s_arcadeMotion * 0x73 / 3;
        RECT box = {
            D230.transitionMeta_trackSel[0].currX + (MATH_Cos(angle) * 25 >> 9) - 180,
            D230.transitionMeta_trackSel[0].currY + (MATH_Sin(angle) * 200 >> 12) + 96,
            256, 25
        };
        /* Keep the retail curve, but do not draw a row through the footer. */
        if (box.y + box.h > 176) continue;
        MM_CustomText_DrawFont(item->title, box.x + 8, box.y + 5, 240, 1, FONT_BIG,
                               MM_CustomTrackSelect_Eligible(item, NULL, 0) ? ORANGE : ORANGE_DARKENED, ot);
        if (i == row && !s_arcadeMotion)
        {
            RECT highlight = {box.x + 6, box.y + 4, box.w - 12, box.h - 8};
            CTR_Box_DrawClearBox(&highlight, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
        }
        RECTMENU_DrawInnerRect(&box, 0, ot);
    }
    if (s_arcadeMotion) s_arcadeMotion--;
    DecalFont_DrawLineOT("SELECT", 394 + dx, 43 + dy, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
    DecalFont_DrawLineOT("LEVEL", 394 + dx, 61 + dy, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
    if (entry)
    {
        snprintf(line, sizeof line, "v%.20s", entry->version);
        MM_CustomTrackSelect_Text(entry->title, 309 + dx, 85 + dy, 174, 2, ORANGE, ot);
        MM_CustomTrackSelect_Text(line, 309 + dx, 112 + dy, 174, 1, WHITE, ot);
        
        if (s_arcadeConfirm)
        {
            struct CustomPackageOwned *package = NULL;
            unsigned int laps = 0;
            if (CustomOffline_RetainPackage(s_arcadeRequest, &package))
            {
                CustomOffline_PackageLaps(package, &laps, NULL, 0);
                CustomPackage_Free(&package);
            }
            snprintf(line, sizeof line, "%u laps", laps);
            MM_CustomTrackSelect_Text(line, 309 + dx, 124 + dy, 174, 1, WHITE, ot);
            MM_CustomTrackSelect_Text("Start race", 309 + dx, 143 + dy, 174, 1, ORANGE, ot);
        }
        else
        {
            char why[80];
            const char *best = MM_CustomTrackSelect_BestTime(entry);
            int ok = MM_CustomTrackSelect_Eligible(entry, why, sizeof why);
            MM_CustomTrackSelect_Text(s_arcadeMessage[0] ? s_arcadeMessage : !entry->installed ? "Not installed" :
                !ok ? why : best[0] ? best : "Installed", 309 + dx, 129 + dy, 174, 3, WHITE, ot);
        }
    }
    else MM_CustomTrackSelect_Text(MM_CustomLibrary_Busy() ? "Loading tracks..." :
        "No tracks. Manage content in Options.", 309 + dx, 88 + dy, 174, 5, WHITE, ot);
    RECT details = {301 + dx, 82 + dy, 188, 87};
    RECTMENU_DrawInnerRect(&details, 0, ot);
    if (D230.trackSel_transitionState == IN_MENU)
    {
        MM_CustomText_Draw(s_arcadeConfirm ? "Cross: Start  Back: Track list" :
            "Left/Right: Pages  Up/Down: Tracks", 256, 181, 464, 1, JUSTIFY_CENTER | WHITE, ot);
        if (!s_arcadeConfirm)
            MM_CustomText_Draw("Cross: Select  Back: Characters", 256, 194, 464, 1, JUSTIFY_CENTER | WHITE, ot);
    }
    return 1;
}
