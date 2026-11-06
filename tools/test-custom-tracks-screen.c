// cc -m32 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_NATIVE -DCTR_AP -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DMM_IMPORT_PLAYER -DMM_CUSTOM_TRACKS_SCREEN -DBUILD=926 -I include -I . tools/test-custom-tracks-screen.c -o /tmp/test-custom-tracks-screen
/* Options > Custom Tracks (0.3.0-alpha2, game/230/MM_CustomTracks.c), the
   production screen driven frame by frame with stub workers, stub room and
   the retail font widths.

   Checks: the landing tab (This Seed only when the seed misses tracks), the
   controller map (tab strip, list, page controls, actions, views; Back never
   installs), every action of the old Custom Content and Track Library pages
   reachable (the proposal's migration table), Library union and filters,
   versions, search cancel, export and relic times only inside a track's
   details, This Seed rows and Install missing overview, the hosted importer
   and its follow-up, a dropped patch, and that every frame's text stays in
   its panel, inside the menu, without overlaps, also with 640-character
   names and long errors.

   With CT_SHOT_DIR set, named frames are written there as text records for
   the screenshot renderer (draw position, width, colour, text). */
#include <stdio.h>
#include "menu-label-stub.h"
#include <stdlib.h>
#include <string.h>
#include <common.h>
#include <platform/native_input.h>
#include <platform/native_assets.h>
#include <platform/native_custom_track_library.h>
#include <platform/native_custom_track_manager.h>
#include <platform/native_saphi_catalogue.h>
#include <platform/native_custom_revision.h>
#include "../ap/ap_solo.h"
#include "../ap/ap_seed_content.h"
#include "../ap/ap_custom_track_download.h"
struct sData sdata_static;
struct Data data;
#define DecalFont_DrawLineOT UnusedDraw
#include "../game/DecalFont.c"
#undef DecalFont_DrawLineOT
#include "../game/230/MM_CustomText.c"
#define CONN_PAD_COMMIT (RAW_BTN_CROSS | RAW_BTN_CIRCLE)
#define CONN_PAD_CANCEL (RAW_BTN_TRIANGLE | RAW_BTN_START)

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
#define CHECKM(x, m) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s (%s)\n", __LINE__, #x, m); } } while (0)

/* ---- Options menu state the screen shares (MM_ConfigMenu.c) ---- */
static int s_customContentSection = 7;
static struct CustomTrackLibrary s_contentLibrary;
static int s_contentSearchEditing, s_contentSearchPadPrev, s_contentStoreWanted, s_contentLibraryTab;
static char s_contentSearchBackup[CTR_LIBRARY_QUERY_MAX + 1], s_contentInstallPin[65], s_contentStoreMessage[512];
static struct CustomSaphiCatalogue *s_saphiCatalogue;
static int s_saphiRefreshBusy, s_saphiLoaded, s_saphiDownloading;
static char s_saphiMessage[512], s_saphiQuery[CTR_LIBRARY_QUERY_MAX + 1];
static size_t s_saphiSelection[2];
static int libraryBusy;
static void MM_CustomLibrary_Tick(int reload) { (void)reload; }
static int MM_CustomLibrary_Busy(void) { return libraryBusy || s_saphiDownloading; }

/* ---- platform ---- */
static struct GameTracker gt;
static int textResult, textBegins, clipboardSets, urlOpens;
static char lastURL[160];
int Platform_InputRawGamepadButtons(void) { return 0; }
int NativeText_Result(void) { return textResult; }
void NativeText_Resolve(int result) { textResult = result; }
void NativeText_End(void) { textResult = 0; }
void NativeText_Begin(char *b, int c, int x, int y, int w, int h, int p) { (void)b; (void)c; (void)x; (void)y; (void)w; (void)h; (void)p; textBegins++; }
const char *NativeAssets_GetAssetDir(void) { return "/test"; }
int OtherFX_Play(u32 a, int b) { (void)a; (void)b; return 0; }
int Platform_OpenURL(const char *url) { urlOpens++; snprintf(lastURL, sizeof lastURL, "%s", url); return 1; }
int Platform_SetClipboardText(const char *text) { (void)text; clipboardSets++; return 1; }

/* ---- Saphi catalogue ---- */
#define CAT_ROWS 12
static struct CustomSaphiRevision cat[CAT_ROWS];
static int catCount, refreshResult = -1, refreshStarts, installResult = -1, installStarts;
int CustomSaphi_StartRefresh(void) { refreshStarts++; return 1; }
int CustomSaphi_PollRefresh(struct CustomSaphiCatalogue **out, char *error, size_t n)
{ if (refreshResult == 1) *out = (void *)2; if (refreshResult == -2) snprintf(error, n, "Offline"); return refreshResult; }
void CustomSaphi_Free(struct CustomSaphiCatalogue **p) { *p = NULL; }
size_t CustomSaphi_Count(const struct CustomSaphiCatalogue *p) { return p ? (size_t)catCount : 0; }
const struct CustomSaphiRevision *CustomSaphi_Row(const struct CustomSaphiCatalogue *p, size_t i) { (void)p; return &cat[i]; }
int CustomSaphi_StartInstall(const struct CustomSaphiRevision *r, const char *a) { (void)r; (void)a; installStarts++; return 1; }
int CustomSaphi_PollInstall(char pin[65], char *e, size_t n) { (void)e; (void)n; if (installResult == 1) strcpy(pin, "fixture"); return installResult; }

/* ---- Play Solo ---- */
static int publishOK = 1, soloActive;
int AP_Solo_PublishInstalledStore(const char *pin, char *error, size_t n) { (void)pin; if (!publishOK) snprintf(error, n, "Index unavailable"); return publishOK; }
int AP_Solo_RecordTrackNeedsLaps(const char *a, const char *b, const char *c) { (void)a; (void)b; (void)c; return 1; }
int AP_Solo_RecordTrackDescriptor(const char *a, const char *b, const char *c) { (void)a; (void)b; (void)c; return 1; }
int AP_Solo_SessionActive(void) { return soloActive; }

/* ---- the room ---- */
static int seedActive, seedSelected, seedBusy, seedStarts, seedRescans, registryRetries, verifies, legacyStarts;
static int planResult = AP_SEED_PLAN_OK, legacyState = AP_CT_DOWNLOAD_IDLE;
static struct AP_SeedContentRow room[AP_SEED_ROWS_MAX];
static int roomCount;
static char seedName[64] = "";
int AP_SeedContentActive(void) { return seedActive; }
int AP_SeedContentRows(struct AP_SeedContentRow *rows, int max) { int i; for (i = 0; i < roomCount && i < max; i++) rows[i] = room[i]; return i; }
int AP_SeedContentInstallPreview(int *plan, int *count, uint64_t *bytes, char *reason, size_t reasonSize)
{
    return ap_seed_install_plan(room, roomCount, seedBusy, plan, count, bytes, reason, reasonSize) == AP_SEED_PLAN_OK && planResult == AP_SEED_PLAN_OK
           ? AP_SEED_PLAN_OK : (snprintf(reason, reasonSize, "%s", planResult != AP_SEED_PLAN_OK ? "The room changed." : reason), AP_SEED_PLAN_ALL_READY);
}
int AP_SeedContentInstallStart(char *m, size_t n) { seedStarts++; snprintf(m, n, "Installing 1 track..."); return 1; }
int AP_SeedContentInstallTick(char *m, size_t n) { (void)m; (void)n; return seedBusy; }
int AP_SeedContentInstallBusy(void) { return seedBusy; }
void AP_SeedContentRegistryRetry(void) { registryRetries++; }
static struct CustomTrackManagerPackage pkg = {"baby-t-park", "11111111-2222-3333-4444-555555555555", "1.0.2", "Baby T Park",
    "Pancho", "https://www.projectsaphi.com/tracks/101", "", "", "", "", "", "", 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static struct CustomTrackManagerStatus pkgStatus;
const struct CustomTrackManagerPackage *AP_CustomContentPackage(void) { return &pkg; }
const struct CustomTrackManagerStatus *AP_CustomContentStatus(void) { return &pkgStatus; }
int AP_CustomContentSeedSelected(void) { return seedSelected; }
int AP_CustomContentRequired(void) { return 0; }
void AP_CustomContentRescan(void) { seedRescans++; }
void AP_CustomContentVerify(void) { verifies++; }
int AP_CustomContentDownloadStart(void) { legacyStarts++; legacyState = AP_CT_DOWNLOAD_RUNNING; return 1; }
int AP_CustomContentDownloadStatus(char *d, int n) { snprintf(d, (size_t)n, "Downloading 40%%"); return legacyState; }
int ap_net_seed_name(char *buf, int n) { snprintf(buf, (size_t)n, "%s", seedName); return 1; }
int ap_net_slot_name(char *buf, int n) { snprintf(buf, (size_t)n, "AppieCTR"); return 1; }

/* ---- release-profile package files ---- */
static int relicSet, relicTicks[3], relicSaves, relicClears, yamlSaves;
const char *CustomTrackManager_StateText(int s) { return s == CTR_CT_MANAGER_READY ? "Ready" : "Not installed"; }
int CustomTrackManager_RelicTicksFromCentis(int c) { return c < 1 ? -1 : (c * 96 + 9) / 10; }
int CustomTrackManager_RelicCentisFromTicks(int t) { return t * 10 / 96; }
int CustomTrackManager_RelicTimesValid(const int t[3]) { return t[0] > t[1] && t[1] > t[2] && t[2] > 0; }
int CustomTrackManager_LoadRelicTimes(const char *a, const struct CustomTrackManagerPackage *p, const struct CustomTrackManagerStatus *s, struct CustomTrackRelicTimes *o)
{ (void)a; (void)p; (void)s; o->set = relicSet; memcpy(o->ticks, relicTicks, sizeof relicTicks); return relicSet; }
int CustomTrackManager_SaveRelicTimes(const char *a, const struct CustomTrackManagerPackage *p, const struct CustomTrackManagerStatus *s, const int t[3])
{ (void)a; (void)p; (void)s; relicSaves++; relicSet = 1; memcpy(relicTicks, t, sizeof relicTicks); return 1; }
int CustomTrackManager_ClearRelicTimes(const char *a, const struct CustomTrackManagerPackage *p) { (void)a; (void)p; relicClears++; relicSet = 0; return 1; }
int CustomTrackManager_RenderYaml(const struct CustomTrackManagerPackage *p, const struct CustomTrackManagerStatus *s, const struct CustomTrackRelicTimes *r, char *d, size_t n)
{ (void)p; (void)r; if (s->state != CTR_CT_MANAGER_READY) return 0; snprintf(d, n, "custom_tracks: []"); return 1; }
int CustomTrackManager_SaveYaml(const char *a, const struct CustomTrackManagerPackage *p, const struct CustomTrackManagerStatus *s, const struct CustomTrackRelicTimes *r, struct CustomTrackManagerStatus *o)
{ (void)a; (void)p; (void)r; if (s->state != CTR_CT_MANAGER_READY) return 0; yamlSaves++;
  snprintf(o->yamlPath, sizeof o->yamlPath, "/home/deck/CTR-Native-AP-SteamDeck/Testing/client/assets/tracks/custom_tracks.generated.yaml"); return 1; }

/* ---- xdelta importer worker, dialog and disc ---- */
#include <platform/native_xdelta_import.h>
static int pickState = -1, scanState = -1, installState = -1, scanStarts, xdInstallStarts, discards, folderCount, dropState;
static struct XdeltaImportSummary fakeSummary;
int XdeltaImport_ListFolder(const char *a, char names[][CTR_XDELTA_NAME_MAX], int max)
{ (void)a; for (int i = 0; i < folderCount && i < max; i++) snprintf(names[i], CTR_XDELTA_NAME_MAX, i ? "patch-two.xdelta" : "CRASH-30ANNIVERSARY_ADVENTURE_V2.xdelta"); return folderCount < max ? folderCount : max; }
const char *NativeDiscImage_GetPath(void) { return "/disc.bin"; }
int XdeltaPick_Start(void) { pickState = 0; return 1; }
void XdeltaPick_Abandon(void) { pickState = -1; }
int XdeltaPick_Poll(char *path, size_t size) { int s = pickState; if (s == 1) snprintf(path, size, "/patches/Pack.xdelta"); if (s == 1 || s == 2) pickState = -1; return s; }
int XdeltaImport_StartScan(const char *p, const char *d, const char *a) { (void)p; (void)d; (void)a; scanStarts++; scanState = 0; return 1; }
int XdeltaImport_PollScan(int *progress, struct XdeltaImportSummary *s, char *e, size_t n)
{ *progress = 37; if (scanState == 1) *s = fakeSummary; if (scanState == -2) snprintf(e, n, "Patch does not match this disc"); return scanState; }
void XdeltaImport_DiscardScan(void) { discards++; scanState = -1; }
int XdeltaImport_StartInstall(const unsigned char *sel, unsigned count, const char *a, int where) { (void)sel; (void)count; (void)a; (void)where; xdInstallStarts++; installState = 0; return 1; }
int XdeltaPick_TakeDrop(char *path, size_t size) { int s = dropState; if (s == 1) snprintf(path, size, "/drop/LRG2.xdelta"); dropState = 0; return s; }
int XdeltaImport_PollInstall(int *installed, char *e, size_t n) { (void)e; (void)n; int s = installState; if (s == 1) { *installed = 2; installState = -1; } return s; }
static struct XdeltaPatchState fakeStates[CTR_XDELTA_FOLDER_MAX];
static int stateStarts, setTitleCalls, setTitleOk = 1;
static char lastTitle[129];
int XdeltaImport_StartPatchStates(const char *a, char names[][CTR_XDELTA_NAME_MAX], int count) { (void)a; (void)names; (void)count; stateStarts++; return 1; }
int XdeltaImport_PollPatchStates(struct XdeltaPatchState *out, int max) { memcpy(out, fakeStates, sizeof(fakeStates[0]) * (size_t)max); return 1; }
int XdeltaImport_SetItemTitle(unsigned item, const char *title) { (void)item; setTitleCalls++; snprintf(lastTitle, sizeof lastTitle, "%s", title); return setTitleOk; }
int XdeltaImport_InstalledDescriptorCount(void) { return 1; }
int XdeltaImport_InstalledDescriptor(int i, char c[65], char r[65], char *j, size_t n, int *laps) { (void)i; strcpy(c, "x"); strcpy(r, "1"); snprintf(j, n, "{}"); *laps = 0; return 1; }
void XdeltaImport_InstalledDescriptorNote(char *note, size_t size) { snprintf(note, size, "%s", ""); }

/* ---- drawing, with bounds and overlap checks ---- */
struct Box { int x, y, w, h, flags; char text[200]; };
static struct Box boxes[160];
static int nboxes, npanels, panelsDrawn;
static RECT panels[4], highlights[16];
static int nhigh;
static char drawn[16384];
static const char *frameName = "";
void DecalFont_DrawLineOT(char *s, int x, int y, s16 f, int flags, uint32_t *ot)
{
    int w = DecalFont_GetLineWidth(s, f);
    (void)ot;
    CHECKM(panelsDrawn == 0, s); /* panels are drawn last, behind the text */
    if (flags & JUSTIFY_CENTER) x -= w / 2;
    else if (flags & JUSTIFY_RIGHT) x -= w;
    CHECKM(x >= 24 && x + w <= 488 && y >= 20 && y + 8 <= 206, s);
    if (nboxes < 160)
    {
        struct Box *b = &boxes[nboxes++];
        b->x = x; b->y = y; b->w = w; b->h = f == FONT_BIG ? 17 : 8; b->flags = flags & ~(JUSTIFY_CENTER | JUSTIFY_RIGHT);
        snprintf(b->text, sizeof b->text, "%s", s);
    }
    if (strlen(drawn) + strlen(s) + 2 < sizeof drawn) { strcat(drawn, s); strcat(drawn, "\n"); }
}
void RECTMENU_DrawInnerRect(RECT *r, int t, uint32_t *ot)
{
    (void)t; (void)ot;
    CHECK(r->x >= 0x10 && r->x + r->w <= 0x1F0 && r->y >= 0x2E && r->y + r->h <= 0xD0);
    if (npanels < 4) panels[npanels++] = *r;
    panelsDrawn = 1;
}
void CTR_Box_DrawClearBox(const RECT *r, const Color *c, int t, uint32_t *ot)
{
    (void)c; (void)t; (void)ot;
    CHECK(panelsDrawn == 0);
    CHECK(r->x >= 0x10 && r->x + r->w <= 0x1F0);
    if (nhigh < 16) highlights[nhigh++] = *r;
}

#include "../game/230/MM_CustomImport.c"
#include "../game/230/MM_CustomManager.c"
#include "../game/230/MM_CustomTracks.c"

static void layout_checks(void)
{
    int i, j;
    for (i = 0; i < nboxes; i++)
    {
        const struct Box *a = &boxes[i];
        /* Text in a panel's band stays inside that panel. */
        for (j = 0; j < npanels && !MM_CustomImport_Active() && s_importStage != IMPORT_RESULT; j++)
        {
            const RECT *p = &panels[j];
            if (a->y >= p->y && a->y < p->y + p->h && a->x >= p->x && a->x < p->x + p->w)
                CHECKM(a->x + a->w <= p->x + p->w - 4 && a->y + a->h <= p->y + p->h, a->text);
        }
        for (j = i + 1; j < nboxes; j++)
        {
            const struct Box *b = &boxes[j];
            const int hit = a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
            if (hit) printf("  %s: \"%s\" overlaps \"%s\"\n", frameName, a->text, b->text);
            CHECKM(!hit, frameName);
        }
    }
}

static void dump(const char *name)
{
    const char *dir = getenv("CT_SHOT_DIR");
    char path[600];
    FILE *f;
    int i;
    if (!dir || !name) return;
    snprintf(path, sizeof path, "%s/%s.txt", dir, name);
    f = fopen(path, "w");
    if (!f) return;
    for (i = 0; i < npanels; i++) fprintf(f, "P\t%d\t%d\t%d\t%d\n", panels[i].x, panels[i].y, panels[i].w, panels[i].h);
    for (i = 0; i < nhigh; i++) fprintf(f, "H\t%d\t%d\t%d\t%d\n", highlights[i].x, highlights[i].y, highlights[i].w, highlights[i].h);
    for (i = 0; i < nboxes; i++)
    {
        const int c = boxes[i].flags;
        fprintf(f, "T\t%d\t%d\t%d\t%d\t%s\t%s\n", boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h,
                c == ORANGE ? "orange" : c == RED ? "red" : c == GRAY ? "gray" : "white", boxes[i].text);
    }
    fclose(f);
}

static int consumed; /* the Options menu would not leave on this frame's Back */
static void frame(int buttons, const char *name)
{
    struct GamepadBuffer pad = {0};
    pad.buttonsTapped = buttons;
    nboxes = 0; npanels = 0; panelsDrawn = 0; nhigh = 0; drawn[0] = 0;
    frameName = name ? name : "";
    consumed = MM_CustomTracks_ConsumesBack(s_customContentSection);
    gt.timer++;
    MM_ConfigProc_CustomTracks(NULL, NULL, &pad);
    layout_checks();
    dump(name);
}
/* Leave Options and come back: a fresh visit. */
static void revisit(void) { gt.timer += 100; }

static void row(int i, const char *title, const char *rev, int scan, int source)
{
    memset(&room[i], 0, sizeof room[i]);
    snprintf(room[i].title, sizeof room[i].title, "%s", title);
    snprintf(room[i].author, sizeof room[i].author, "Pancho");
    snprintf(room[i].revision, sizeof room[i].revision, "%s", rev);
    snprintf(room[i].folder, sizeof room[i].folder, "track-%d", i);
    room[i].lev_bytes = 1200000; room[i].vrm_bytes = 300000;
    room[i].scan = scan; room[i].source = source; room[i].index = i; room[i].locator.bigfile_index = -1;
}

static struct CustomTrackLibraryEntry lib[8];
static void entry(int i, const char *title, const char *version, int trackID, int arcade, int xdelta)
{
    memset(&lib[i], 0, sizeof lib[i]);
    snprintf(lib[i].title, sizeof lib[i].title, "%s", title);
    snprintf(lib[i].version, sizeof lib[i].version, "%s", version);
    snprintf(lib[i].author, sizeof lib[i].author, "Author %d", i);
    snprintf(lib[i].local.uuid, sizeof lib[i].local.uuid, "uuid-%d", trackID ? trackID : 900 + i);
    lib[i].installed = 1; lib[i].local.trackID = trackID; lib[i].local.arcade = arcade; lib[i].local.timeTrial = 1;
    lib[i].local.xdelta = xdelta; lib[i].local.levID = trackID * 10; lib[i].local.vrmID = trackID * 10 + 1;
    if (!arcade) snprintf(lib[i].local.arcadeReason, sizeof lib[i].local.arcadeReason, "Grid: 3 of 8");
}
static void catrow(int i, const char *title, const char *version, int trackID)
{
    memset(&cat[i], 0, sizeof cat[i]);
    snprintf(cat[i].title, sizeof cat[i].title, "%s", title);
    snprintf(cat[i].author, sizeof cat[i].author, "Saphi author");
    snprintf(cat[i].version, sizeof cat[i].version, "%s", version);
    cat[i].trackID = trackID; cat[i].modeTags = 1; cat[i].lev.id = trackID * 10; cat[i].vrm.id = trackID * 10 + 1;
}
/* Drawn text with line breaks as spaces, so wrapped text still matches. */
static int has(const char *s)
{
    static char flat[sizeof drawn];
    size_t i;
    for (i = 0; drawn[i] && i + 1 < sizeof flat; i++) flat[i] = drawn[i] == '\n' ? ' ' : drawn[i];
    flat[i] = 0;
    return strstr(flat, s) != NULL;
}
/* Move the action cursor to the action with this label. */
static int select_action(const char *label)
{
    int k;
    for (k = 0; k < CT_ACTIONS_MAX + 1; k++)
    {
        char want[64];
        snprintf(want, sizeof want, "%s\n", label);
        frame(0, NULL);
        /* the selected row is drawn orange; find it by trying each position */
        if (s_ctTab == CT_TAB_LIBRARY && s_ctItemCount)
        {
            struct CT_Action a[CT_ACTIONS_MAX]; struct CT_LibView v;
            int n; CT_View(&s_ctItems[s_ctListSel[CT_TAB_LIBRARY]], &v); n = CT_LibraryActions(&s_ctItems[s_ctListSel[CT_TAB_LIBRARY]], &v, a);
            if (s_ctActionSel < n && !strcmp(a[s_ctActionSel].label, label)) return 1;
        }
        else if (s_ctTab == CT_TAB_SEED && s_ctSeedCount)
        {
            struct CT_Action a[CT_ACTIONS_MAX];
            int n = CT_SeedActions(&s_ctSeedRows[s_ctListSel[CT_TAB_SEED]], a);
            if (s_ctActionSel < n && !strcmp(a[s_ctActionSel].label, label)) return 1;
        }
        frame(BTN_DOWN, NULL);
    }
    return 0;
}
static void select_track(const char *title)
{
    int k;
    for (k = 0; k < 40; k++)
    {
        struct CT_LibView v;
        frame(0, NULL);
        CT_View(&s_ctItems[s_ctListSel[CT_TAB_LIBRARY]], &v);
        if (!strcmp(v.title, title)) return;
        frame(BTN_DOWN, NULL);
    }
    CHECKM(0, title);
}

int main(void)
{
    static char longTitle[641];
    data.font_charPixWidth[FONT_SMALL] = 13; data.font_charPixWidth[FONT_BIG] = 17;
    data.font_puncPixWidth[FONT_SMALL] = 7; data.font_puncPixWidth[FONT_BIG] = 11;
    data.font_buttonPixWidth[FONT_SMALL] = 2; data.font_buttonPixWidth[FONT_BIG] = 16;
    sdata->gGT = &gt;
    pkgStatus.state = CTR_CT_MANAGER_READY;
    strcpy(pkgStatus.verifiedPackageSha256, "abc");
    strcpy(pkgStatus.detail, "Ready. Files verified.");

    /* A store with four tracks (Baby T Park installed twice), a catalogue of five. */
    entry(0, "Baby T Park", "1.0.2", 101, 1, 0);
    entry(1, "Baby T Park", "1.0.0", 101, 1, 0);
    entry(2, "Arabian Heights Night", "1.0.0", 120, 0, 0);
    entry(3, "Nightmare Fueled Slot 7", "1.0.0", 0, 1, 1);
    entry(4, "Coco Park 2025", "1.0.0", 130, 1, 0);
    s_contentLibrary.entries = lib; s_contentLibrary.count = 5;
    catrow(0, "Baby T Park", "1.0.2", 101);
    catrow(1, "Coco Park 2025", "1.1.0", 130);
    catrow(2, "Coco Park 2025", "1.0.0", 130);
    catrow(3, "Android Alley Remix", "2.0.0", 140);
    catrow(4, "Arcane Garden", "1.0.0", 150);
    strcpy(cat[4].disabledReason, "Files not hosted on Saphi");
    catCount = 5;

    /* 0. The first visit loads the catalogue once. */
    revisit(); frame(0, NULL); CHECK(refreshStarts == 1 && s_saphiRefreshBusy && s_saphiLoaded);
    refreshResult = 1; frame(0, NULL); CHECK(!s_saphiRefreshBusy && s_saphiCatalogue == (void *)2);
    refreshResult = -1; revisit(); frame(0, NULL); CHECK(refreshStarts == 1);

    /* 1. No seed: lands on Library, tab strip focused; Back there is Options'. */
    revisit(); frame(0, "library-tabs");
    CHECK(s_ctTab == CT_TAB_LIBRARY && s_ctFocus == CT_FOCUS_TABS && !consumed);
    frame(0, NULL); CHECK(!consumed);
    CHECK(has("CUSTOM TRACKS") && has("Library") && has("This Seed") && has("Import"));
    /* Union: Baby T Park once, Coco Park once (installed), Android Alley and Arcane Garden from Saphi. */
    CHECK(s_ctItemCount == 6);
    CHECK(!strcmp(CT_ItemTitle(&s_ctItems[0]), "Android Alley Remix") && has("Nightmare Fueled"));
    frame(BTN_RIGHT, NULL); CHECK(s_ctTab == CT_TAB_SEED);
    frame(BTN_R1, NULL); CHECK(s_ctTab == CT_TAB_IMPORT);
    frame(BTN_RIGHT, NULL); CHECK(s_ctTab == CT_TAB_LIBRARY);
    frame(BTN_L1, NULL); CHECK(s_ctTab == CT_TAB_IMPORT);
    frame(BTN_LEFT, NULL); frame(BTN_LEFT, NULL); CHECK(s_ctTab == CT_TAB_LIBRARY);

    /* 2. Enter the list; Back goes to page controls, then tabs, then Options. */
    frame(BTN_DOWN, "library-list"); CHECK(s_ctFocus == CT_FOCUS_LIST);
    CHECK(has("Filter: All") && has("1/6"));
    frame(BTN_DOWN, NULL); CHECK(s_ctListSel[0] == 1);
    frame(BTN_TRIANGLE, NULL); CHECK(consumed && s_ctFocus == CT_FOCUS_CONTROLS);
    frame(0, "library-controls"); CHECK(has("Refresh catalogue") && has("Rescan local files") && has("Search: none"));
    frame(BTN_TRIANGLE, NULL); CHECK(consumed && s_ctFocus == CT_FOCUS_TABS);
    frame(BTN_TRIANGLE, NULL); CHECK(!consumed);

    /* 3. Filters. */
    frame(BTN_CROSS, NULL); frame(BTN_TRIANGLE, NULL); CHECK(s_ctFocus == CT_FOCUS_CONTROLS);
    frame(BTN_RIGHT, NULL); frame(0, NULL); CHECK(s_ctFilter == CT_FILTER_INSTALLED && s_ctItemCount == 4);
    CHECK(!has("Android Alley Remix"));
    frame(BTN_RIGHT, NULL); frame(0, "library-browse"); CHECK(s_ctFilter == CT_FILTER_BROWSE && s_ctItemCount == 4);
    CHECK(!has("Nightmare Fueled"));
    frame(BTN_RIGHT, NULL); CHECK(s_ctFilter == CT_FILTER_ALL);
    /* Refresh catalogue and Rescan local files are separate controls. */
    frame(BTN_DOWN, NULL); frame(BTN_DOWN, NULL); CHECK(s_ctControlSel[0] == 2);
    frame(BTN_CROSS, NULL); CHECK(refreshStarts == 2 && s_saphiRefreshBusy);
    refreshResult = -2; frame(0, NULL); CHECK(!s_saphiRefreshBusy && s_saphiCatalogue == (void *)2);
    frame(0, "library-offline"); CHECK(has("offline"));
    refreshResult = -1;
    s_contentStoreWanted = 0;
    frame(BTN_DOWN, NULL); frame(BTN_CROSS, NULL); CHECK(s_contentStoreWanted == 1 && refreshStarts == 2);
    s_contentStoreWanted = 0; s_ctMessage[0] = 0;
    /* Search: the keyboard opens; cancel restores the old query. */
    frame(BTN_UP, NULL); frame(BTN_UP, NULL); CHECK(s_ctControlSel[0] == 1);
    strcpy(s_saphiQuery, "coco");
    frame(BTN_CROSS, NULL); CHECK(s_contentSearchEditing && textBegins == 1);
    frame(0, NULL); CHECK(consumed);
    strcpy(s_saphiQuery, "zzz"); textResult = 2; frame(0, NULL);
    CHECK(!s_contentSearchEditing && !strcmp(s_saphiQuery, "coco"));
    frame(0, NULL); CHECK(s_ctItemCount == 1);
    s_saphiQuery[0] = 0;
    frame(BTN_SQUARE, NULL); CHECK(s_ctFocus == CT_FOCUS_LIST);

    /* 4. Actions on an installed Saphi track with an update. */
    select_track("Coco Park 2025");
    frame(0, "library-detail-update"); CHECK(has("Update available") || has("update"));
    frame(BTN_CROSS, "library-actions"); CHECK(s_ctFocus == CT_FOCUS_ACTIONS);
    CHECK(has("Update to v1.1.0") && has("Versions") && has("Open source page") && has("Export YAML") && has("Relic times"));
    frame(BTN_CROSS, NULL); CHECK(installStarts == 1 && s_saphiDownloading);
    frame(0, NULL); CHECK(has("Downloading..."));
    installResult = 1; frame(0, NULL); CHECK(!s_saphiDownloading && s_contentStoreWanted);
    installResult = -1; s_contentStoreWanted = 0;
    /* Versions keeps the place: Back returns to the actions. */
    CHECK(select_action("Versions")); frame(BTN_CROSS, "library-versions");
    CHECK(s_ctSub == CT_SUB_VERSIONS && has("v1.1.0") && has("v1.0.0") && has("Installed"));
    frame(BTN_DOWN, NULL); CHECK(s_ctSubSel == 1);
    frame(BTN_CROSS, NULL); CHECK(installStarts == 1); /* 1.0.0 is installed: recheck */
    frame(BTN_TRIANGLE, NULL); CHECK(s_ctFocus == CT_FOCUS_ACTIONS && s_ctSub == CT_SUB_NONE && s_ctListSel[0] >= 0);
    CHECK(select_action("Open source page")); frame(BTN_CROSS, NULL);
    CHECK(urlOpens == 1 && !strcmp(lastURL, "https://www.projectsaphi.com/tracks/130"));
    /* Export is only for the release profile: reason shown, nothing exported. */
    CHECK(select_action("Export YAML")); frame(BTN_CROSS, "library-export-unavailable");
    CHECK(clipboardSets == 0 && s_ctSub == CT_SUB_NONE && has("Only Baby T Park"));
    frame(BTN_TRIANGLE, NULL); CHECK(s_ctFocus == CT_FOCUS_LIST);

    /* 5. A disabled catalogue revision stays listed with its reason. */
    select_track("Arcane Garden");
    frame(0, "library-disabled"); CHECK(has("Files not hosted"));
    frame(BTN_CROSS, NULL); frame(BTN_CROSS, NULL); CHECK(installStarts == 1 && has("Files not hosted"));
    frame(BTN_TRIANGLE, NULL);

    /* 6. An imported track: no source page, Recheck instead of Download. */
    select_track("Nightmare Fueled Slot 7");
    frame(0, NULL); CHECK(has("Imported"));
    frame(BTN_CROSS, NULL); CHECK(has("Recheck files"));
    CHECK(select_action("Open source page")); frame(BTN_CROSS, NULL); CHECK(urlOpens == 1 && has("Imported from a patch"));
    frame(BTN_TRIANGLE, NULL);

    /* 7. Baby T Park: the release profile. Verify, export, relic times. */
    select_track("Baby T Park");
    frame(BTN_CROSS, "library-package-actions"); CHECK(has("Recheck files") && has("Verify / Setup"));
    CHECK(select_action("Verify / Setup")); frame(BTN_CROSS, NULL); CHECK(verifies == 1);
    CHECK(select_action("Export YAML")); frame(BTN_CROSS, NULL); CHECK(s_ctSub == CT_SUB_EXPORT && consumed == 1);
    frame(BTN_CROSS, NULL); CHECK(clipboardSets == 1 && has("YAML copied"));
    frame(BTN_DOWN, NULL); frame(BTN_CROSS, "library-export-saved"); CHECK(yamlSaves == 1 && has("Saved to:"));
    frame(BTN_TRIANGLE, NULL); CHECK(s_ctFocus == CT_FOCUS_ACTIONS);
    CHECK(select_action("Relic times")); frame(BTN_CROSS, NULL); CHECK(s_ctSub == CT_SUB_RELIC);
    frame(BTN_UP, NULL); frame(BTN_RIGHT, NULL); frame(BTN_UP, "library-relic");
    CHECK(has("Sapphire") && has("Save") && has("Cancel") && has("Clear relic times"));
    /* Back cancels: nothing saved. */
    frame(BTN_TRIANGLE, NULL); CHECK(relicSaves == 0 && s_ctFocus == CT_FOCUS_ACTIONS && consumed);
    /* Confirm on a digit only moves to Save; Save then refuses invalid times. */
    frame(BTN_CROSS, NULL); CHECK(s_ctSub == CT_SUB_RELIC);
    frame(BTN_UP, NULL); frame(BTN_CROSS, NULL); CHECK(relicSaves == 0 && s_ctSubSel == CT_RELIC_SAVE);
    frame(BTN_CROSS, NULL); CHECK(relicSaves == 0 && has("Enter all three"));
    s_ctRelicCentis[0] = 7000; s_ctRelicCentis[1] = 8000; s_ctRelicCentis[2] = 9000;
    frame(BTN_CROSS, NULL); CHECK(relicSaves == 0 && has("slower"));
    s_ctRelicCentis[0] = 9000; s_ctRelicCentis[1] = 8000; s_ctRelicCentis[2] = 7000;
    frame(BTN_CROSS, NULL); CHECK(relicSaves == 1 && relicSet);
    frame(BTN_RIGHT, NULL); frame(BTN_RIGHT, NULL); CHECK(s_ctSubSel == CT_RELIC_CLEAR);
    frame(BTN_CROSS, NULL); CHECK(relicClears == 0 && has("Confirm again"));
    frame(BTN_CROSS, NULL); CHECK(relicClears == 1 && has("no Relic Race"));
    frame(BTN_LEFT, NULL); frame(BTN_CROSS, NULL); CHECK(s_ctSub == CT_SUB_NONE); /* Cancel row */
    /* A package that changes under the editor never saves. */
    CHECK(select_action("Relic times")); frame(BTN_CROSS, NULL);
    strcpy(pkgStatus.verifiedPackageSha256, "def"); frame(0, NULL);
    CHECK(s_ctSub == CT_SUB_NONE && relicSaves == 1);
    frame(0, NULL); CHECK(has("package changed"));
    strcpy(pkgStatus.verifiedPackageSha256, "abc");
    frame(BTN_TRIANGLE, NULL); frame(BTN_TRIANGLE, NULL); frame(BTN_TRIANGLE, NULL); CHECK(s_ctFocus == CT_FOCUS_TABS);

    /* 8. The release profile when no other row lists it (offline, not installed). */
    s_contentLibrary.count = 0; catCount = 0;
    frame(0, NULL); CHECK(s_ctItemCount == 1 && has("Baby T Park"));
    frame(BTN_CROSS, NULL); frame(BTN_CROSS, NULL); CHECK(s_ctFocus == CT_FOCUS_ACTIONS && has("Download v1.0.2"));
    frame(BTN_CROSS, NULL); CHECK(legacyStarts == 1 && s_ctLegacyDownload);
    legacyState = AP_CT_DOWNLOAD_SUCCEEDED; frame(0, NULL); CHECK(!s_ctLegacyDownload && verifies == 2);
    legacyState = AP_CT_DOWNLOAD_IDLE;
    CHECK(select_action("Open source page")); frame(BTN_CROSS, NULL); CHECK(!strcmp(lastURL, pkg.sourceUrl));
    frame(BTN_TRIANGLE, NULL); frame(BTN_TRIANGLE, NULL); frame(BTN_TRIANGLE, NULL);
    pkgStatus.state = CTR_CT_MANAGER_NOT_INSTALLED; s_ctFilter = CT_FILTER_INSTALLED;
    frame(0, "library-empty"); CHECK(s_ctItemCount == 0 && has("No installed tracks"));
    s_ctFilter = CT_FILTER_ALL; pkgStatus.state = CTR_CT_MANAGER_READY;
    s_contentLibrary.count = 5; catCount = 5;

    /* 9. Long names and long errors stay in their panels. */
    memset(longTitle, 'W', 640); longTitle[300] = ' '; longTitle[640] = 0;
    snprintf(lib[4].title, sizeof lib[4].title, "%s", longTitle);
    strcpy(lib[2].local.arcadeReason, "Grid: 3 of 8");
    frame(BTN_CROSS, NULL);
    s_ctListSel[0] = 0;
    for (int k = 0; k < 6; k++) frame(k ? BTN_DOWN : 0, k == 5 ? "library-long-name" : NULL);
    snprintf(s_saphiMessage, sizeof s_saphiMessage, "Files installed, but Play Solo publication failed: %s",
             "the descriptor index could not be written because the folder is read only on this device");
    frame(0, "library-long-error"); CHECK(has("Files installed"));
    s_saphiMessage[0] = 0;
    snprintf(lib[4].title, sizeof lib[4].title, "Coco Park 2025");
    frame(BTN_TRIANGLE, NULL); frame(BTN_TRIANGLE, NULL);

    /* 10. This Seed: no seed loaded. */
    frame(BTN_RIGHT, NULL); frame(0, "seed-none"); CHECK(s_ctTab == CT_TAB_SEED && has("No seed loaded"));
    frame(BTN_DOWN, NULL); CHECK(s_ctFocus == CT_FOCUS_CONTROLS);
    frame(BTN_DOWN, NULL); frame(BTN_DOWN, NULL); frame(BTN_CROSS, NULL); frame(0, "seed-none-identity"); CHECK(s_ctSub == CT_SUB_INFO && has("No room"));
    frame(BTN_TRIANGLE, NULL); CHECK(s_ctFocus == CT_FOCUS_CONTROLS);
    frame(BTN_TRIANGLE, NULL); frame(BTN_LEFT, NULL);

    /* A connected retail-only seed is loaded even with no custom rows. */
    strcpy(seedName, "AP_RetailOnly");
    s_ctTab = CT_TAB_SEED; s_ctFocus = CT_FOCUS_TABS;
    frame(0, "seed-retail-only");
    CHECK(!has("No seed loaded") && has("no custom tracks") && has("AP_RetailOnly"));
    soloActive = 1; seedName[0] = 0;
    frame(0, "seed-solo-retail-only");
    CHECK(!has("No seed loaded") && has("Play Solo"));
    soloActive = 0;

    /* Refresh failure stays visible with local/package rows present. */
    s_ctTab = CT_TAB_LIBRARY; s_ctFocus = CT_FOCUS_TABS;
    s_saphiRefreshFailed = 1;
    frame(0, "library-offline"); CHECK(has("Saphi refresh failed. Retry Refresh.") && has("refresh."));
    s_ctFocus = CT_FOCUS_LIST;
    frame(0, NULL); CHECK(has("Saphi refresh failed"));
    s_saphiRefreshFailed = 0;
    s_ctFocus = CT_FOCUS_TABS; s_saphiDownloading = 1;
    strcpy(s_saphiMessage, "Downloading 40%");
    frame(0, "library-downloading"); CHECK(has("Downloading 40%"));
    s_saphiDownloading = 0; s_saphiMessage[0] = 0;
    s_ctFocus = CT_FOCUS_LIST;
    pkgStatus.state = CTR_CT_MANAGER_MANIFEST_MISSING;
    select_track("Baby T Park");
    frame(0, NULL); CHECK(has("Installed") && !has("Needs setup"));
    lib[0].installed = lib[1].installed = 0;
    select_track("Baby T Park");
    frame(0, "library-needs-setup"); CHECK(has("Needs setup"));
    lib[0].installed = lib[1].installed = 1;
    pkgStatus.state = CTR_CT_MANAGER_READY;
    s_ctTab = CT_TAB_IMPORT; s_ctFocus = CT_FOCUS_TABS;
    frame(0, "import-help"); CHECK(!has("...") && has("Open to choose") && has("review."));
    s_ctTab = CT_TAB_LIBRARY; s_ctFocus = CT_FOCUS_TABS;

    /* 11. A room that misses tracks: the screen opens on This Seed. */
    seedActive = 1; strcpy(seedName, "AP_93412345678901234567");
    row(0, "Baby T Park", "1.0.2", AP_SEED_SCAN_READY, AP_SEED_SOURCE_SAPHI);
    row(1, "Android Alley Remix", "2.0.0", AP_SEED_SCAN_NOT_INSTALLED, AP_SEED_SOURCE_SAPHI);
    row(2, "Coco Park 2025", "1.1.0", AP_SEED_SCAN_MISMATCH, AP_SEED_SOURCE_SAPHI);
    row(3, "Nightmare Fueled Slot 7", "1.0.0", AP_SEED_SCAN_NOT_INSTALLED, AP_SEED_SOURCE_MANUAL);
    strcpy(room[3].locator.patch_name, "nightmarefueled.xdelta"); strcpy(room[3].locator.patch_sha256, "0123456789abcdef");
    room[3].locator.bigfile_index = 57; room[3].registry = AP_SEED_REGISTRY_NOT_LISTED;
    row(4, "Cortex Vortex", "", AP_SEED_SCAN_READY, AP_SEED_SOURCE_BUNDLED); room[4].index = -1;
    roomCount = 5;
    revisit(); frame(0, "seed-tabs"); CHECK(s_ctTab == CT_TAB_SEED && s_ctFocus == CT_FOCUS_TABS);
    CHECK(has("Missing") && has("Mismatch") && has("Ready 2/5"));
    frame(BTN_DOWN, NULL); frame(BTN_DOWN, "seed-missing"); CHECK(has("Needs v2.0.0"));
    frame(BTN_DOWN, NULL); frame(BTN_DOWN, "seed-manual"); CHECK(has("No download"));
    frame(BTN_DOWN, "seed-bundled"); CHECK(has("Comes with the client"));
    /* Track actions. */
    frame(BTN_UP, NULL); frame(BTN_CROSS, "seed-actions");
    CHECK(has("Resolve track") && has("Verify / Setup") && has("Open source page") && has("Required files") && has("Installed copies"));
    CHECK(select_action("Required files")); frame(BTN_CROSS, NULL); frame(0, "seed-required-files");
    CHECK(s_ctSub == CT_SUB_INFO && has("nightmarefueled") && has("slot 57"));
    frame(BTN_TRIANGLE, NULL); CHECK(s_ctFocus == CT_FOCUS_ACTIONS);
    CHECK(select_action("Installed copies")); frame(BTN_CROSS, NULL); frame(0, "seed-copies");
    CHECK(has("imported") && has("seed's version"));
    frame(BTN_TRIANGLE, NULL);
    /* Resolve a manual track: a rescan, then the instructions. */
    CHECK(select_action("Resolve track")); { int before = seedRescans; frame(BTN_CROSS, NULL); CHECK(seedRescans == before + 1 && s_ctSub == CT_SUB_INFO); }
    frame(BTN_TRIANGLE, NULL);
    frame(BTN_TRIANGLE, NULL); CHECK(s_ctFocus == CT_FOCUS_LIST);
    /* Resolve a downloadable track: the Install missing overview. */
    s_ctListSel[1] = 1; frame(BTN_CROSS, NULL);
    CHECK(select_action("Resolve track")); frame(BTN_CROSS, NULL); CHECK(s_ctSub == CT_SUB_INSTALL);
    frame(BTN_TRIANGLE, NULL); frame(0, NULL); CHECK(seedStarts == 0 && s_ctFocus == CT_FOCUS_CONTROLS && has("Nothing was installed"));
    /* Page controls: Install missing shows the overview; Confirm installs once. */
    s_ctControlSel[1] = 0;
    frame(BTN_CROSS, "seed-install-overview"); CHECK(s_ctSub == CT_SUB_INSTALL && has("Android") && has("Install missing: 1 track"));
    frame(BTN_CROSS, NULL); CHECK(seedStarts == 1 && s_ctSub == CT_SUB_NONE);
    seedBusy = 1; frame(0, "seed-installing"); CHECK(has("Installing..."));
    { int before = registryRetries; frame(BTN_DOWN, NULL); frame(BTN_CROSS, NULL); frame(0, NULL); CHECK(registryRetries == before && has("Wait for the install")); }
    frame(BTN_TRIANGLE, NULL); CHECK(s_ctFocus == CT_FOCUS_TABS);
    frame(BTN_RIGHT, NULL); frame(BTN_CROSS, NULL); CHECK(!MM_CustomImport_Active()); /* no import during an install */
    seedBusy = 0; frame(BTN_LEFT, NULL); CHECK(s_ctTab == CT_TAB_SEED);
    frame(BTN_CROSS, NULL); frame(BTN_TRIANGLE, NULL); s_ctControlSel[1] = 1;
    { int before = registryRetries; frame(BTN_CROSS, NULL); frame(0, NULL); CHECK(registryRetries == before + 1 && has("Rescanned. Ready: 2 / 5.")); }
    frame(BTN_DOWN, NULL); frame(BTN_CROSS, NULL); frame(0, "seed-identity"); CHECK(has("Seed AP_934") && has("Slot AppieCTR"));
    frame(BTN_TRIANGLE, NULL); frame(BTN_TRIANGLE, NULL);
    /* All ready: the screen opens on Library. */
    for (int k = 0; k < roomCount; k++) room[k].scan = AP_SEED_SCAN_READY;
    revisit(); frame(0, NULL); CHECK(s_ctTab == CT_TAB_LIBRARY);
    room[1].scan = AP_SEED_SCAN_NOT_INSTALLED; room[2].scan = AP_SEED_SCAN_MISMATCH;

    /* 12. A room without a content plan that names the release profile. */
    seedActive = 0; seedSelected = 1; pkgStatus.state = CTR_CT_MANAGER_NOT_INSTALLED;
    revisit(); frame(0, "seed-legacy"); CHECK(s_ctTab == CT_TAB_SEED && s_ctSeedCount == 1 && has("Missing"));
    frame(BTN_DOWN, NULL); frame(BTN_CROSS, NULL); frame(BTN_CROSS, NULL); CHECK(legacyStarts == 2);
    legacyState = AP_CT_DOWNLOAD_FAILED; frame(0, NULL); CHECK(!s_ctLegacyDownload);
    legacyState = AP_CT_DOWNLOAD_IDLE;
    CHECK(select_action("Open source page")); frame(BTN_CROSS, NULL); CHECK(!strcmp(lastURL, pkg.sourceUrl));
    seedSelected = 0; seedActive = 1; pkgStatus.state = CTR_CT_MANAGER_READY;
    frame(BTN_TRIANGLE, NULL); frame(BTN_TRIANGLE, NULL); frame(BTN_TRIANGLE, NULL);

    /* 13. Import: the tab hosts the importer; Back on its first step returns to the tabs. */
    revisit(); frame(0, NULL); s_ctTab = CT_TAB_IMPORT; s_ctFocus = CT_FOCUS_TABS;
    folderCount = 2;
    frame(0, "import-tabs"); CHECK(has("xdelta") && !MM_CustomImport_Active());
    frame(BTN_DOWN, NULL); CHECK(MM_CustomImport_Active() && consumed == 0);
    frame(0, "import-choose"); CHECK(consumed && has("Choose a file") && has("CRASH-30"));
    frame(BTN_TRIANGLE, NULL); CHECK(!MM_CustomImport_Active() && s_ctFocus == CT_FOCUS_TABS);
    /* A full import, then the follow-up choices. */
    frame(BTN_CROSS, NULL); frame(BTN_DOWN, NULL); frame(BTN_CROSS, NULL); CHECK(scanStarts == 1);
    fakeSummary.count = 2;
    fakeSummary.items[0].offered = 1; strcpy(fakeSummary.items[0].title, "Lego Adventure track 3"); strcpy(fakeSummary.items[0].tags, "Race TT Arcade");
    fakeSummary.items[1].offered = 0; strcpy(fakeSummary.items[1].title, "Code changes"); strcpy(fakeSummary.items[1].reason, "Not a track");
    scanState = 1; frame(0, NULL); frame(0, "import-review");
    frame(BTN_CROSS, NULL); CHECK(xdInstallStarts == 1);
    installState = 1; frame(0, "import-result"); CHECK(has("Installed 1 track") || has("Installed 2 tracks"));
    frame(BTN_CROSS, NULL); frame(0, "import-done"); CHECK(s_ctSub == CT_SUB_IMPORT_DONE && has("Show in Library") && has("Go to This Seed"));
    CHECK(s_contentStoreWanted);
    frame(BTN_DOWN, NULL); { int before = seedRescans; frame(BTN_CROSS, NULL); CHECK(s_ctTab == CT_TAB_SEED && seedRescans == before + 1); }
    /* A failed scan reports the error and still offers the next steps. */
    s_ctTab = CT_TAB_IMPORT; s_ctFocus = CT_FOCUS_TABS;
    frame(BTN_CROSS, NULL); frame(BTN_DOWN, NULL); frame(BTN_CROSS, NULL); scanState = -2; frame(0, "import-failed");
    CHECK(has("does not match"));
    frame(BTN_CROSS, NULL); frame(BTN_CROSS, NULL); CHECK(s_ctTab == CT_TAB_LIBRARY && s_ctFilter == CT_FILTER_INSTALLED);

    /* 14. A patch dropped on the window opens Import with it; Select is a shortcut. */
    s_ctFilter = CT_FILTER_ALL; s_ctTab = CT_TAB_LIBRARY; s_ctFocus = CT_FOCUS_LIST; scanState = -1;
    dropState = 1; frame(0, NULL); CHECK(s_ctTab == CT_TAB_IMPORT && MM_CustomImport_Active() && scanStarts == 3);
    frame(BTN_TRIANGLE, NULL); frame(BTN_TRIANGLE, NULL); CHECK(!MM_CustomImport_Active());
    s_ctTab = CT_TAB_LIBRARY; s_ctFocus = CT_FOCUS_LIST;
    frame(BTN_SELECT, NULL); CHECK(s_ctTab == CT_TAB_IMPORT && MM_CustomImport_Active());
    frame(BTN_TRIANGLE, NULL);


    /* 15. A2-19: the picker shows what each patch did before, names can be typed, Import all is an explicit review row. */
    revisit(); frame(0, NULL); s_ctTab = CT_TAB_IMPORT; s_ctFocus = CT_FOCUS_TABS; scanState = -1; installState = -1;
    folderCount = 2;
    fakeStates[0].state = XDELTA_STATE_IMPORTED; fakeStates[0].imported = 14; fakeStates[0].offered = 14;
    fakeStates[1].state = XDELTA_STATE_FAILED; strcpy(fakeStates[1].reason, "Not an xdelta patch");
    frame(BTN_CROSS, NULL); frame(BTN_DOWN, NULL);
    CHECK(stateStarts > 0);
    frame(0, "import-picker-done"); CHECK(has("Done") && has("Fail") && has("Imported: 14 tracks"));
    frame(BTN_DOWN, NULL); frame(0, "import-picker-failed"); CHECK(has("Failed: Not an xdelta patch") && has("patch-two.xdel") && has("Rescan"));
    {
        const int before = xdInstallStarts, scans = scanStarts;
        frame(BTN_R1, NULL); CHECK(scanStarts == scans);
        frame(BTN_CIRCLE, NULL); CHECK(scanStarts == scans + 1);
        fakeSummary.count = 2;
        scanState = 1; frame(0, NULL);
        CHECK(xdInstallStarts == before && s_importStage == IMPORT_REVIEW);
        s_importRow = fakeSummary.count; frame(BTN_CIRCLE, NULL);
        CHECK(xdInstallStarts == before + 1);
        installState = 1; frame(0, "import-all-result");
        CHECK(has("Installed 2 tracks") && has("Not imported: 1"));
        frame(BTN_CROSS, NULL); frame(BTN_TRIANGLE, NULL);
    }
    /* Start on scan completion cancels before an install can start. */
    s_ctTab = CT_TAB_IMPORT; s_ctFocus = CT_FOCUS_TABS; scanState = -1;
    frame(BTN_CROSS, NULL); frame(BTN_DOWN, NULL);
    {
        const int before = xdInstallStarts;
        frame(BTN_CROSS, NULL); scanState = 1; frame(BTN_START, NULL);
        CHECK(xdInstallStarts == before && s_importStage == IMPORT_CHOOSE);
        frame(BTN_TRIANGLE, NULL); frame(BTN_TRIANGLE, NULL);
    }
    /* Back on the frame a normal scan finishes returns to the picker, as before. */
    s_ctTab = CT_TAB_IMPORT; s_ctFocus = CT_FOCUS_TABS; scanState = -1;
    frame(BTN_CROSS, NULL); frame(BTN_DOWN, NULL); frame(BTN_CROSS, NULL);
    scanState = 1; frame(BTN_TRIANGLE, NULL); CHECK(s_importStage == IMPORT_CHOOSE);
    scanState = -1;
    frame(BTN_TRIANGLE, NULL);
    /* Review: type a name, a refused name, a cancelled name, then Import all. */
    s_ctTab = CT_TAB_IMPORT; s_ctFocus = CT_FOCUS_TABS; scanState = -1;
    frame(BTN_CROSS, NULL); frame(BTN_DOWN, NULL); frame(BTN_CROSS, NULL);
    scanState = 1; frame(0, NULL); frame(0, "import-review-names");
    CHECK(has("Placeholder name") || has("Name from") || has("Name of"));
    {
        const int begins = textBegins, calls = setTitleCalls, before = xdInstallStarts;
        frame(BTN_L1, NULL); CHECK(textBegins == begins);
        frame(BTN_SELECT, NULL); CHECK(textBegins == begins + 1);
        strcpy(s_importEditText, "Sunset Raceway"); textResult = 1; frame(0, "import-name-commit");
        CHECK(setTitleCalls == calls + 1 && !strcmp(lastTitle, "Sunset Raceway") && has("Sunset Raceway") && has("typed by you"));
        setTitleOk = 0; frame(BTN_SELECT, NULL); strcpy(s_importEditText, "Bad"); textResult = 1; frame(0, "import-name-refused");
        CHECK(has("Name refused")); setTitleOk = 1;
        frame(BTN_SELECT, NULL); frame(0, "import-name-editing"); CHECK(has("Name: ") && has("Keep this name"));
        textResult = 2; frame(0, NULL); CHECK(setTitleCalls == calls + 2);
        frame(BTN_R1, NULL); CHECK(xdInstallStarts == before);
        s_importRow = fakeSummary.count; frame(BTN_CROSS, NULL); CHECK(xdInstallStarts == before + 1);
        installState = 1; frame(0, NULL); frame(BTN_CROSS, NULL); frame(BTN_TRIANGLE, NULL);
    }
    scanState = -1; installState = -1;

    printf("custom tracks screen: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
