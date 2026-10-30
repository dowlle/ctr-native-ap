/* "Import track files" (xdelta importer, 0.3.0). Included by
   MM_CustomManager.c in the Track Manager builds (CTR_CUSTOM_PACKAGES) and by
   MM_ConfigMenu.c in the player client (decision 35, 2026-10-28). Select
   opens it on Options > Custom Content. The player picks a patch through the
   system dialog, from assets/tracks/import/ (Steam Deck Game Mode), or drops
   it on the client window; the patch is read on a worker against the mounted
   disc, and the review list shows one line per found item. Confirm installs
   the selected race tracks where rooms and solo seeds find them
   (assets/tracks/<track key>/original) and, in the Track Manager builds, as
   packages too. */
#include <platform/native_xdelta_import.h>
#include <platform/native_xdelta_pick.h>
#include <platform/native_disc_image.h>

#ifdef CTR_CUSTOM_PACKAGES
#define IMPORT_WHERE (CTR_XDELTA_INSTALL_PACKAGES | CTR_XDELTA_INSTALL_ROOM)
#define IMPORT_HINT "Pick or drop an xdelta patch. Its race tracks become packages."
#else
#define IMPORT_WHERE CTR_XDELTA_INSTALL_ROOM
#define IMPORT_HINT "Pick or drop an xdelta patch for rooms and solo seeds."
#endif

enum { IMPORT_CHOOSE, IMPORT_PICKING, IMPORT_SCANNING, IMPORT_REVIEW, IMPORT_INSTALLING, IMPORT_RESULT };
static int s_importOpen, s_importStage, s_importFileCount, s_importProgress;
static size_t s_importRow;
static char s_importFiles[CTR_XDELTA_FOLDER_MAX][CTR_XDELTA_NAME_MAX];
static struct XdeltaImportSummary s_importSummary;
static unsigned char s_importSelected[CTR_XDELTA_ITEM_MAX];
static char s_importMessage[256], s_importPath[4096];

static int MM_CustomImport_Active(void) { return s_importOpen; }

static void MM_CustomImport_Rescan(void)
{
    s_importFileCount = XdeltaImport_ListFolder(NativeAssets_GetAssetDir(), s_importFiles, CTR_XDELTA_FOLDER_MAX);
    if (s_importRow > (size_t)s_importFileCount) s_importRow = 0;
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
    if (!disc || !*disc) { snprintf(s_importMessage, sizeof s_importMessage, "No game disc is mounted."); return; }
    if (XdeltaImport_StartScan(s_importPath, disc, NativeAssets_GetAssetDir()))
    {
        s_importStage = IMPORT_SCANNING;
        s_importProgress = 0;
        s_importMessage[0] = 0;
    }
    else snprintf(s_importMessage, sizeof s_importMessage, "Could not start the import.");
}

static void MM_CustomImport_Proc(uint32_t *ot, struct GamepadBuffer *pad)
{
    int tapped = pad->buttonsTapped;
    char line[256];
    size_t count = 0, first;
    if (s_importStage == IMPORT_PICKING)
    {
        char picked[4096];
        int state = XdeltaPick_Poll(picked, sizeof picked);
        if (state == 1) MM_CustomImport_StartScan(picked);
        else if (state == 2 || state == -1)
        {
            s_importStage = IMPORT_CHOOSE;
            snprintf(s_importMessage, sizeof s_importMessage, "No file chosen.");
        }
        else if (tapped & (BTN_TRIANGLE | BTN_START))
        {
            /* The dialog may never show (Steam Deck Game Mode): stop waiting. */
            XdeltaPick_Abandon();
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
        }
        else if (state == -2)
        {
            s_importStage = IMPORT_RESULT;
            snprintf(s_importMessage, sizeof s_importMessage, "%s", error);
            XdeltaImport_DiscardScan();
        }
        if (tapped & (BTN_TRIANGLE | BTN_START))
        {
            XdeltaImport_DiscardScan();
            s_importStage = IMPORT_CHOOSE;
            snprintf(s_importMessage, sizeof s_importMessage, "Import cancelled.");
            OtherFX_Play(2, 1);
        }
        tapped = 0;
    }
    if (s_importStage == IMPORT_INSTALLING)
    {
        int installed = 0;
        char error[512] = {0};
        int state = XdeltaImport_PollInstall(&installed, error, sizeof error);
        if (state == 1 || state == -2)
        {
            s_importStage = IMPORT_RESULT;
#ifdef CTR_CUSTOM_PACKAGES
            s_contentStoreWanted = 1;
#else
            /* Current Seed shows the new files at once, also after a
               failure part way through the list. */
            if (!AP_SeedContentInstallBusy()) AP_CustomContentRescan();
#endif
            if (state == 1) snprintf(s_importMessage, sizeof s_importMessage, "Installed %d track%s. Files verified.", installed, installed == 1 ? "" : "s");
            else snprintf(s_importMessage, sizeof s_importMessage, "%s", error);
        }
        tapped = 0;
    }

    if (s_importStage == IMPORT_CHOOSE && MM_CustomImport_TakeDrop()) tapped = 0;
    if (s_importStage == IMPORT_CHOOSE)
    {
        count = 1 + (size_t)s_importFileCount;
        if (tapped & (BTN_TRIANGLE | BTN_START)) { s_importOpen = 0; OtherFX_Play(2, 1); return; }
        if (tapped & BTN_SQUARE) { MM_CustomImport_Rescan(); count = 1 + (size_t)s_importFileCount; s_importMessage[0] = 0; }
        if (tapped & (BTN_CROSS | BTN_CIRCLE))
        {
            if (s_importRow == 0)
            {
                if (XdeltaPick_Start()) s_importStage = IMPORT_PICKING;
                else snprintf(s_importMessage, sizeof s_importMessage, "No file dialog. Use the folder.");
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
        if (tapped & (BTN_CROSS | BTN_CIRCLE))
        {
            unsigned chosen = 0;
            for (unsigned i = 0; i < count; i++) chosen += s_importSelected[i] != 0;
            if (!chosen) snprintf(s_importMessage, sizeof s_importMessage, "Nothing selected.");
            else if (XdeltaImport_StartInstall(s_importSelected, (unsigned)count, NativeAssets_GetAssetDir(), IMPORT_WHERE))
                s_importStage = IMPORT_INSTALLING;
            else snprintf(s_importMessage, sizeof s_importMessage, "Could not start the install.");
            OtherFX_Play(1, 1);
        }
    }
    else if (s_importStage == IMPORT_RESULT)
    {
        if (tapped & (BTN_CROSS | BTN_CIRCLE | BTN_TRIANGLE | BTN_START))
        {
            s_importOpen = 0;
#ifdef CTR_CUSTOM_PACKAGES
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
            MM_CustomText_Draw(row == 0 ? "Choose a file" : s_importFiles[row - 1], 32, box.y + 2, 244, 1,
                               row == s_importRow ? ORANGE : WHITE, ot);
            if (row == s_importRow) CTR_Box_DrawClearBox(&box, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
        }
        MM_CustomText_Draw(s_importStage == IMPORT_PICKING ? "Waiting for the file dialog. Back: stop waiting" :
                           s_importMessage[0] ? s_importMessage :
                           IMPORT_HINT,
                           305, 70, 176, 6, WHITE, ot);
        MM_CustomText_Draw("[ Rescan folder", 305, 159, 176, 1, ORANGE, ot);
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
            MM_CustomText_Draw(item->offered ? "" : item->reason, 305, 112, 176, 2, WHITE, ot);
        }
        MM_CustomText_Draw(s_importStage == IMPORT_INSTALLING ? "Installing..." :
                           s_importMessage[0] ? s_importMessage : s_importSummary.descriptorNote[0] ? s_importSummary.descriptorNote :
                           "[ Select   X Install", 305, 144, 176, 2, ORANGE, ot);
    }
    else
    {
        MM_CustomText_Draw(s_importMessage, 32, 84, 452, 3, WHITE, ot);
    }
    if (s_importStage == IMPORT_REVIEW || s_importStage == IMPORT_INSTALLING)
    {
        unsigned offered = 0;
        for (unsigned i = 0; i < s_importSummary.count; i++) offered += s_importSummary.items[i].offered != 0;
        snprintf(line, sizeof line, "%u/%u  %u track%s  Back: Cancel", count ? (unsigned)s_importRow + 1 : 0,
                 (unsigned)count, offered, offered == 1 ? "" : "s");
    }
#ifdef CTR_CUSTOM_PACKAGES
#define IMPORT_DONE "X: Installed tracks"
#else
#define IMPORT_DONE "X: Custom Content"
#endif
    else snprintf(line, sizeof line, "%s", s_importStage == IMPORT_RESULT ? IMPORT_DONE :
                  s_importStage == IMPORT_SCANNING ? "Back: Cancel" : "Folder: assets/tracks/import");
    MM_CustomText_Draw(line, 256, 196, 456, 1, JUSTIFY_CENTER | WHITE, ot);
    {
        RECT listBox = {20, 64, 268, 108}, detailBox = {297, 64, 190, 108};
        RECTMENU_DrawInnerRect(&listBox, 0, ot);
        RECTMENU_DrawInnerRect(&detailBox, 0, ot);
    }
}
