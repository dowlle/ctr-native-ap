/* "Import track files" (xdelta importer, 0.3.0). Included once: by
   MM_ConfigMenu.c in the player client (MM_IMPORT_PLAYER), hosted by the
   Custom Tracks Import tab,
   by MM_CustomManager.c in the authoring builds (decision 35, 2026-10-28).
   Select opens it on Options > Custom Content (authoring builds) or Custom
   Tracks (player client). The player picks a patch through the
   system dialog, from assets/tracks/import/ (Steam Deck Game Mode), or drops
   it on the client window; the patch is read on a worker against the mounted
   disc, and the review list shows one line per found item. Confirm installs
   the selected race tracks in the shared package store. Rooms, Play Solo
   and Arcade resolve that same installed revision. */
#include <platform/native_xdelta_import.h>
#include <platform/native_xdelta_pick.h>
#include <platform/native_disc_image.h>

#ifndef MM_IMPORT_PLAYER
#define IMPORT_WHERE CTR_XDELTA_INSTALL_PACKAGES
#define IMPORT_HINT "Pick or drop an xdelta patch. Its race tracks become packages."
#else
#define IMPORT_WHERE CTR_XDELTA_INSTALL_PACKAGES
#define IMPORT_HINT "Pick or drop an xdelta patch to install tracks."
#endif

#ifdef MM_IMPORT_PLAYER
/* The importer's Play Solo descriptors (ruling of 2026-11-02): each installed
   track with one goes into solo/tracks, so the Play Solo Custom Tracks page
   lists it at once. A track without laps goes into solo/needs-laps: the page
   lists it too and asks the player for its laps first. */
static int MM_CustomImport_KeepSoloDescriptors(void)
{
    static char json[16384];
    char contentId[65], revision[65];
    int i, needsLaps, count = XdeltaImport_InstalledDescriptorCount(), ok = 1;
    for (i = 0; i < count; i++)
        if (XdeltaImport_InstalledDescriptor(i, contentId, revision, json, sizeof json, &needsLaps))
        {
            if (needsLaps)
                ok &= AP_Solo_RecordTrackNeedsLaps(contentId, revision, json);
            else
                ok &= AP_Solo_RecordTrackDescriptor(contentId, revision, json);
        }
        else ok = 0;
    return ok && count > 0;
}
#endif

enum { IMPORT_CHOOSE, IMPORT_PICKING, IMPORT_SCANNING, IMPORT_REVIEW, IMPORT_INSTALLING, IMPORT_RESULT };
static int s_importOpen, s_importStage, s_importFileCount, s_importProgress;
static size_t s_importRow;
static char s_importFiles[CTR_XDELTA_FOLDER_MAX][CTR_XDELTA_NAME_MAX];
static struct XdeltaImportSummary s_importSummary;
static unsigned char s_importSelected[CTR_XDELTA_ITEM_MAX];
static char s_importMessage[256], s_importPath[4096];
/* The picker's per-file states (hashed on a worker), "Import all valid
   tracks" (scan, then install without the review step) and the name edit. */
static struct XdeltaPatchState s_importStates[CTR_XDELTA_FOLDER_MAX];
static int s_importStatesWait, s_importAuto, s_importEditing, s_importPadPrev;
static char s_importEditText[129], s_importEditBackup[129];

static int MM_CustomImport_Active(void) { return s_importOpen; }

static void MM_CustomImport_Rescan(void)
{
    s_importFileCount = XdeltaImport_ListFolder(NativeAssets_GetAssetDir(), s_importFiles, CTR_XDELTA_FOLDER_MAX);
    if (s_importRow > (size_t)s_importFileCount) s_importRow = 0;
    memset(s_importStates, 0, sizeof s_importStates);
    s_importStatesWait = s_importFileCount > 0 &&
                         XdeltaImport_StartPatchStates(NativeAssets_GetAssetDir(), s_importFiles, s_importFileCount);
}

/* What a folder file has done before, for the list tag and the detail panel. */
static const char *MM_CustomImport_StateTag(int row)
{
    if (row < 0 || row >= s_importFileCount || s_importStatesWait) return "";
    switch (s_importStates[row].state)
    {
    case XDELTA_STATE_IMPORTED: return "Done";
    case XDELTA_STATE_PARTLY: return "Part";
    case XDELTA_STATE_FAILED: return "Fail";
    }
    return "";
}

static void MM_CustomImport_StateText(int row, char *out, size_t size)
{
    const struct XdeltaPatchState *st;
    if (row < 0 || row >= s_importFileCount) { out[0] = 0; return; }
    st = &s_importStates[row];
    if (s_importStatesWait) snprintf(out, size, "Checking what this patch did before...");
    else if (st->state == XDELTA_STATE_IMPORTED)
        snprintf(out, size, "Imported: %d track%s.", st->imported, st->imported == 1 ? "" : "s");
    else if (st->state == XDELTA_STATE_PARTLY)
        snprintf(out, size, "Partly imported: %d of %d tracks.%s%s", st->imported, st->offered, st->reason[0] ? " Last error: " : "", st->reason);
    else if (st->state == XDELTA_STATE_FAILED)
        snprintf(out, size, "Failed: %s", st->reason[0] ? st->reason : "unknown reason");
    else snprintf(out, size, "Not imported yet.");
}

static void MM_CustomImport_Open(void)
{
    s_importOpen = 1;
    s_importStage = IMPORT_CHOOSE;
    s_importRow = 0;
    s_importMessage[0] = 0;
    MM_CustomImport_Rescan();
}

static void MM_CustomImport_StartScan(const char *path);

/* A patch dropped on the window opens the import with it, from the page
   that calls this (Custom Content), and starts it on the choose step. The
   caller leaves a drop waiting while another content job runs. A drop while
   this import is busy (dialog, scan, review, install) is turned away with a
   note, so Back never starts a patch dropped meanwhile. */
static int MM_CustomImport_TakeDrop(void)
{
    char path[4096];
    int state;
    state = XdeltaPick_TakeDrop(path, sizeof path);
    if (!state) return 0;
    if (s_importOpen && s_importStage != IMPORT_CHOOSE)
    {
        if (s_importStage != IMPORT_RESULT)
            snprintf(s_importMessage, sizeof s_importMessage, "Finish or cancel this import first.");
        return 0;
    }
    if (!s_importOpen) MM_CustomImport_Open();
    if (state == 1) MM_CustomImport_StartScan(path);
    else snprintf(s_importMessage, sizeof s_importMessage, "Drop an .xdelta or .vcdiff patch.");
    OtherFX_Play(1, 1);
    return 1;
}

static void MM_CustomImport_StartScan(const char *path)
{
    const char *disc = NativeDiscImage_GetPath();
    snprintf(s_importPath, sizeof s_importPath, "%s", path);
    if (!disc || !*disc) { s_importAuto = 0; snprintf(s_importMessage, sizeof s_importMessage, "No game disc is mounted."); return; }
    if (XdeltaImport_StartScan(s_importPath, disc, NativeAssets_GetAssetDir()))
    {
        s_importStage = IMPORT_SCANNING;
        s_importProgress = 0;
        s_importMessage[0] = 0;
    }
    else { s_importAuto = 0; snprintf(s_importMessage, sizeof s_importMessage, "Could not start the import."); }
}

/* Start the install of what is selected. Returns 0 with the reason in the message. */
static int MM_CustomImport_StartInstallSelected(void)
{
    unsigned i, chosen = 0;
    for (i = 0; i < s_importSummary.count; i++) chosen += s_importSelected[i] != 0;
    if (!chosen) { snprintf(s_importMessage, sizeof s_importMessage, "Nothing selected."); return 0; }
    if (XdeltaImport_StartInstall(s_importSelected, s_importSummary.count, NativeAssets_GetAssetDir(), IMPORT_WHERE))
    {
        s_importStage = IMPORT_INSTALLING;
        return 1;
    }
    snprintf(s_importMessage, sizeof s_importMessage, "Could not start the install.");
    return 0;
}

/* Tracks the scan listed but could not offer (not a race track, same track
   twice, failed checks); the review list names each reason. */
static unsigned MM_CustomImport_SkippedTracks(void)
{
    unsigned i, n = 0;
    for (i = 0; i < s_importSummary.count; i++) n += s_importSummary.items[i].bigfileIndex >= 0 && !s_importSummary.items[i].offered;
    return n;
}

static void MM_CustomImport_Proc(uint32_t *ot, struct GamepadBuffer *pad)
{
    int tapped = pad->buttonsTapped;
    int rawPad = Platform_InputRawGamepadButtons();
    int rawTapped = rawPad & ~s_importPadPrev;
    char line[256];
    size_t count = 0, first;
    s_importPadPrev = rawPad;
    if (s_importStatesWait && XdeltaImport_PollPatchStates(s_importStates, CTR_XDELTA_FOLDER_MAX) != 0) s_importStatesWait = 0;
    /* Name entry (review list): the physical pad commits or cancels, as on
       the Library search; typing never reaches the pad buffer. */
    if (s_importEditing)
    {
        if (!NativeText_Result())
        {
            if (rawTapped & CONN_PAD_CANCEL) NativeText_Resolve(2);
            else if (rawTapped & CONN_PAD_COMMIT) NativeText_Resolve(1);
        }
        if (NativeText_Result())
        {
            const int committed = NativeText_Result() == 1;
            NativeText_End();
            s_importEditing = 0;
            if (committed)
            {
                char *text = s_importEditText;
                while (*text == ' ') text++;
                if (!XdeltaImport_SetItemTitle((unsigned)s_importRow, text))
                    snprintf(s_importMessage, sizeof s_importMessage, "Name refused.");
                else
                {
                    snprintf(s_importSummary.items[s_importRow].title, sizeof s_importSummary.items[s_importRow].title, "%s", text);
                    s_importSummary.items[s_importRow].nameSource = XDELTA_NAME_EDITED;
                    s_importMessage[0] = 0;
                }
            }
        }
        tapped = 0;
    }
    if (s_importStage == IMPORT_PICKING)
    {
        char picked[4096];
        int state = XdeltaPick_Poll(picked, sizeof picked);
        if (state == 1) MM_CustomImport_StartScan(picked);
        else if (state == 2 || state == -1)
        {
            s_importAuto = 0;
            s_importStage = IMPORT_CHOOSE;
            snprintf(s_importMessage, sizeof s_importMessage, "No file chosen.");
        }
        else if (tapped & (BTN_TRIANGLE | BTN_START))
        {
            /* The dialog may never show (Steam Deck Game Mode): stop waiting. */
            XdeltaPick_Abandon();
            s_importAuto = 0;
            s_importStage = IMPORT_CHOOSE;
            snprintf(s_importMessage, sizeof s_importMessage, "No file chosen.");
            OtherFX_Play(2, 1);
        }
        tapped = 0;
    }
    if (s_importStage == IMPORT_SCANNING)
    {
        char error[256] = {0};
        int state = XdeltaImport_PollScan(&s_importProgress, &s_importSummary, error, sizeof error);
        if (state == 1)
        {
            s_importStage = IMPORT_REVIEW;
            s_importRow = 0;
            for (unsigned i = 0; i < CTR_XDELTA_ITEM_MAX; i++)
                s_importSelected[i] = i < s_importSummary.count && s_importSummary.items[i].offered;
            /* "Import all valid tracks" skips the review when something is offered. */
            if (s_importAuto)
            {
                s_importAuto = 0;
                if (MM_CustomImport_StartInstallSelected()) { s_importMessage[0] = 0; OtherFX_Play(1, 1); }
            }
        }
        else if (state == -2)
        {
            s_importAuto = 0;
            s_importStage = IMPORT_RESULT;
            snprintf(s_importMessage, sizeof s_importMessage, "%s", error);
            XdeltaImport_DiscardScan();
        }
        /* Not once an install has started: "Import all valid tracks" may have started it this frame. */
        if ((s_importStage == IMPORT_SCANNING || s_importStage == IMPORT_REVIEW) && (tapped & (BTN_TRIANGLE | BTN_START)))
        {
            XdeltaImport_DiscardScan();
            s_importAuto = 0;
            s_importStage = IMPORT_CHOOSE;
            snprintf(s_importMessage, sizeof s_importMessage, "Import cancelled.");
            OtherFX_Play(2, 1);
        }
        tapped = 0;
    }
    if (s_importStage == IMPORT_INSTALLING)
    {
        int installed = 0;
        int published = 1;
        char error[512] = {0};
        int state = XdeltaImport_PollInstall(&installed, error, sizeof error);
        if (state == 1 || state == -2)
        {
            s_importStage = IMPORT_RESULT;
#ifndef MM_IMPORT_PLAYER
            s_contentStoreWanted = 1;
#else
            /* Current Seed shows the new files at once, also after a
               failure part way through the list. */
            if (!AP_SeedContentInstallBusy()) AP_CustomContentRescan();
            published = MM_CustomImport_KeepSoloDescriptors();
#endif
            if (state == -2 && installed && !published)
                snprintf(s_importMessage, sizeof s_importMessage, "%.185s Play Solo publication also failed.", error);
            else if (state == 1 && !published)
                snprintf(s_importMessage, sizeof s_importMessage, "Files installed, but Play Solo publication failed.");
            else if (state == 1)
            {
                const unsigned skipped = MM_CustomImport_SkippedTracks();
                snprintf(s_importMessage, sizeof s_importMessage, "Installed %d track%s. Files verified.", installed, installed == 1 ? "" : "s");
                if (skipped)
                {
                    size_t used = strlen(s_importMessage);
                    snprintf(s_importMessage + used, sizeof s_importMessage - used, " Not imported: %u (not a race track, the same track twice, or failed checks).", skipped);
                }
            }
            else snprintf(s_importMessage, sizeof s_importMessage, "%s", error);
#ifdef MM_IMPORT_PLAYER
            if (state == 1)
            {
                char note[160];
                XdeltaImport_InstalledDescriptorNote(note, sizeof note);
                if (note[0])
                {
                    size_t used = strlen(s_importMessage);
                    snprintf(s_importMessage + used, sizeof s_importMessage - used, " %s.", note);
                }
            }
#endif
        }
        tapped = 0;
    }

    if (s_importStage == IMPORT_CHOOSE && MM_CustomImport_TakeDrop()) tapped = 0;
    if (s_importStage == IMPORT_CHOOSE)
    {
        count = 1 + (size_t)s_importFileCount;
        if (tapped & (BTN_TRIANGLE | BTN_START)) { s_importOpen = 0; OtherFX_Play(2, 1); return; }
        if (tapped & BTN_SQUARE) { MM_CustomImport_Rescan(); count = 1 + (size_t)s_importFileCount; s_importMessage[0] = 0; }
        if (tapped & (BTN_CROSS | BTN_CIRCLE | BTN_R1))
        {
            /* R1 is "Import all valid tracks": the same pick, then no review step. */
            s_importAuto = (tapped & BTN_R1) != 0;
            if (s_importRow == 0)
            {
                if (XdeltaPick_Start()) s_importStage = IMPORT_PICKING;
                else { s_importAuto = 0; snprintf(s_importMessage, sizeof s_importMessage, "No file dialog. Use the folder."); }
            }
            else
            {
                char path[4096];
                snprintf(path, sizeof path, "%s/tracks/import/%s", NativeAssets_GetAssetDir(), s_importFiles[s_importRow - 1]);
                MM_CustomImport_StartScan(path);
            }
            OtherFX_Play(1, 1);
        }
    }
    else if (s_importStage == IMPORT_REVIEW)
    {
        count = s_importSummary.count;
        if (tapped & (BTN_TRIANGLE | BTN_START))
        {
            XdeltaImport_DiscardScan();
            s_importStage = IMPORT_CHOOSE;
            s_importRow = 0;
            s_importMessage[0] = 0;
            OtherFX_Play(2, 1);
            return;
        }
        if ((tapped & BTN_SQUARE) && s_importRow < count && s_importSummary.items[s_importRow].offered)
        {
            s_importSelected[s_importRow] = !s_importSelected[s_importRow];
            OtherFX_Play(0, 1);
        }
        if ((tapped & BTN_L1) && s_importStage == IMPORT_REVIEW && s_importRow < count && s_importSummary.items[s_importRow].offered)
        {
            {
                /* A title over 128 bytes (a pack descriptor allows more) is cut at a character boundary. */
                const char *title = s_importSummary.items[s_importRow].title;
                size_t n = strlen(title);
                if (n > sizeof s_importEditText - 1)
                {
                    n = sizeof s_importEditText - 1;
                    while (n && ((unsigned char)title[n] & 0xC0) == 0x80) n--;
                }
                memcpy(s_importEditText, title, n);
                s_importEditText[n] = 0;
            }
            snprintf(s_importEditBackup, sizeof s_importEditBackup, "%s", s_importEditText);
            NativeText_Begin(s_importEditText, (int)sizeof s_importEditText, 28, 170, 456, 12, 0);
            s_importEditing = 1;
            s_importMessage[0] = 0;
            OtherFX_Play(1, 1);
        }
        if (tapped & BTN_R1)
        {
            /* Import all valid tracks: every offered track, under the names shown. */
            for (unsigned i = 0; i < CTR_XDELTA_ITEM_MAX; i++)
                s_importSelected[i] = i < s_importSummary.count && s_importSummary.items[i].offered;
            MM_CustomImport_StartInstallSelected();
            OtherFX_Play(1, 1);
        }
        else if (tapped & (BTN_CROSS | BTN_CIRCLE))
        {
            MM_CustomImport_StartInstallSelected();
            OtherFX_Play(1, 1);
        }
    }
    else if (s_importStage == IMPORT_RESULT)
    {
        if (tapped & (BTN_CROSS | BTN_CIRCLE | BTN_TRIANGLE | BTN_START))
        {
            s_importOpen = 0;
#ifndef MM_IMPORT_PLAYER
            s_contentLibraryTab = 1;
#endif
            OtherFX_Play(2, 1);
            return;
        }
    }
    if (count && (tapped & (BTN_UP | BTN_DOWN)))
    {
        s_importRow = (s_importRow + ((tapped & BTN_UP) ? count - 1 : 1)) % count;
        if (s_importStage == IMPORT_CHOOSE) s_importMessage[0] = 0;
        OtherFX_Play(0, 1);
    }
    if (count && s_importRow >= count) s_importRow = 0;

    DecalFont_DrawLineOT("IMPORT TRACK FILES", 256, 24, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
    if (s_importStage == IMPORT_CHOOSE || s_importStage == IMPORT_PICKING)
    {
        first = (s_importRow / 7) * 7;
        for (size_t row = first; row < first + 7 && row < count; row++)
        {
            RECT box = {24, 68 + (int)(row - first) * 15, 260, 14};
            if (row == 0) MM_CustomText_Draw("Choose a file", 32, box.y + 2, 244, 1, row == s_importRow ? ORANGE : WHITE, ot);
            else
            {
                /* A short state tag, then the name (the detail panel shows it whole). */
                MM_CustomText_Draw(MM_CustomImport_StateTag((int)row - 1), 32, box.y + 2, 56, 1, row == s_importRow ? ORANGE : GRAY, ot);
                MM_CustomText_Draw(s_importFiles[row - 1], 92, box.y + 2, 184, 1, row == s_importRow ? ORANGE : WHITE, ot);
            }
            if (row == s_importRow) CTR_Box_DrawClearBox(&box, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
        }
        if (s_importStage == IMPORT_CHOOSE && s_importRow > 0)
        {
            /* The full file name and what this patch did before. */
            MM_CustomText_Draw(s_importFiles[s_importRow - 1], 305, 68, 176, 3, ORANGE, ot);
            MM_CustomImport_StateText((int)s_importRow - 1, line, sizeof line);
            MM_CustomText_Draw(line, 305, 108, 176, 3, WHITE, ot);
            if (s_importMessage[0]) MM_CustomText_Draw(s_importMessage, 305, 146, 176, 1, WHITE, ot);
        }
        else
            MM_CustomText_Draw(s_importStage == IMPORT_PICKING ? "Waiting for the file dialog. Back: stop waiting" :
                               s_importMessage[0] ? s_importMessage :
                               IMPORT_HINT " Or copy patches into assets/tracks/import.",
                               305, 70, 176, 6, WHITE, ot);
    }
    else if (s_importStage == IMPORT_SCANNING)
    {
        snprintf(line, sizeof line, "%d%%", s_importProgress);
        MM_CustomText_Draw("Reading patch", 32, 84, 244, 1, WHITE, ot);
        MM_CustomText_Draw(line, 32, 99, 244, 1, ORANGE, ot);
    }
    else if (s_importStage == IMPORT_REVIEW || s_importStage == IMPORT_INSTALLING)
    {
        const struct XdeltaImportItem *item = count ? &s_importSummary.items[s_importRow] : NULL;
        first = (s_importRow / 7) * 7;
        for (size_t row = first; row < first + 7 && row < count; row++)
        {
            const struct XdeltaImportItem *r = &s_importSummary.items[row];
            RECT box = {24, 68 + (int)(row - first) * 15, 260, 14};
            snprintf(line, sizeof line, "%s %s", r->offered ? (s_importSelected[row] ? "[x]" : "[ ]") : " - ", r->title);
            MM_CustomText_Draw(line, 32, box.y + 2, 244, 1, row == s_importRow ? ORANGE : r->offered ? WHITE : GRAY, ot);
            if (row == s_importRow) CTR_Box_DrawClearBox(&box, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
        }
        if (!count) MM_CustomText_Draw("The patch changes no track files.", 32, 84, 244, 2, WHITE, ot);
        if (item)
        {
            MM_CustomText_Draw(item->title, 305, 70, 176, 2, ORANGE, ot);
            MM_CustomText_Draw(item->offered ? item->tags : "Skipped", 305, 98, 176, 1, WHITE, ot);
            MM_CustomText_Draw(item->offered ? (item->nameSource == XDELTA_NAME_DESCRIPTOR ? "Name from the pack" :
                                                item->nameSource == XDELTA_NAME_KNOWN ? "Name of a known track" :
                                                item->nameSource == XDELTA_NAME_TESTER ? "Name from tester feedback" :
                                                item->nameSource == XDELTA_NAME_EDITED ? "Name typed by you" :
                                                "Placeholder name: L1 to change it") : item->reason, 305, 112, 176, 2, WHITE, ot);
        }
        MM_CustomText_Draw(s_importStage == IMPORT_INSTALLING ? "Installing..." :
                           s_importMessage[0] ? s_importMessage : s_importSummary.descriptorNote[0] ? s_importSummary.descriptorNote :
                           s_importSummary.history[0] ? s_importSummary.history :
                           "[ Select  L1 Name  R1 Import all  X Install", 305, 144, 176, 2, ORANGE, ot);
    }
    else
    {
        MM_CustomText_Draw(s_importMessage, 32, 84, 452, 3, WHITE, ot);
    }
    if (s_importEditing)
    {
        char name[160];
        snprintf(name, sizeof name, "Name: %s", s_importEditText);
        MM_CustomText_Draw(name, 28, 172, 456, 1, ORANGE, ot);
        snprintf(line, sizeof line, "X: Keep this name   ^: Cancel");
    }
    else if (s_importStage == IMPORT_REVIEW || s_importStage == IMPORT_INSTALLING)
    {
        unsigned offered = 0;
        for (unsigned i = 0; i < s_importSummary.count; i++) offered += s_importSummary.items[i].offered != 0;
        snprintf(line, sizeof line, "%u/%u  %u track%s  Back: Cancel", count ? (unsigned)s_importRow + 1 : 0,
                 (unsigned)count, offered, offered == 1 ? "" : "s");
    }
#ifndef MM_IMPORT_PLAYER
#define IMPORT_DONE "X: Installed tracks"
#else
#define IMPORT_DONE "X: Done"
#endif
    else snprintf(line, sizeof line, "%s", s_importStage == IMPORT_RESULT ? IMPORT_DONE :
                  s_importStage == IMPORT_SCANNING ? "Back: Cancel" : "[ Rescan   R1 Import all valid");
    MM_CustomText_Draw(line, 256, 196, 456, 1, JUSTIFY_CENTER | WHITE, ot);
    {
        RECT listBox = {20, 64, 268, 108}, detailBox = {297, 64, 190, 108};
        RECTMENU_DrawInnerRect(&listBox, 0, ot);
        RECTMENU_DrawInnerRect(&detailBox, 0, ot);
    }
}
