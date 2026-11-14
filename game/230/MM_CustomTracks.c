#include <platform/native_menu.h>
/* Custom Tracks (0.3.0-alpha2, A2-4): Options > Custom Tracks in the player
   client, one screen with three tabs. It replaces the old Custom Content page
   (Current Seed tab and package page) and the Track Library page; every
   action of those pages is reachable here (migration table in the One Custom
   Tracks screen proposal, 2026-11-03).

     Library    every track in the shared track store, plus the Project Saphi
                catalogue. Filter All / Installed / Browse, Search, Refresh
                catalogue, Rescan local files. Confirm on a track opens its
                actions: download, update or recheck, versions, source page,
                and for a track with a release profile also Verify / Finish
                setup, Export YAML and Relic times (ruled 2026-11-04: export
                and relic times only inside a track's details).
     This Seed  the tracks the connected room or Play Solo seed needs, Ready,
                Missing, Wrong version and so on, against the same store.
                Install missing, Rescan, Seed identity; per track Resolve,
                Verify / Finish setup, Open source, Required files and
                Installed copies.
     Import     hosts the xdelta importer (MM_CustomImport.c) unchanged; a
                drop on the window opens it here.

   The screen opens on This Seed only when the loaded seed misses tracks,
   otherwise on Library (ruled 2026-11-04 19:38 CET). Two panels as on the
   Track Manager: list (20,64,268,108) with seven rows, details (297,64,190,
   108). Navigation (controller map in the proposal): Left / Right or L1 / R1
   change tabs on the tab strip, Down or Confirm enters the tab. In a list,
   Back goes to the tab's page controls, then to the tab strip, then to
   Options. Back is never an install or delete. Included by MM_ConfigMenu.c
   after MM_CustomManager.c; tools/test-custom-tracks-screen.c drives it. */

#include "../../ap/ap_net.h" // ap_net_seed_name, ap_net_slot_name: Seed identity
#include "../../ap/ap_track_consent.h" // the track author permission check (A4-74)

enum { CT_TAB_LIBRARY, CT_TAB_SEED, CT_TAB_IMPORT, CT_TAB_COUNT };
enum { CT_FOCUS_TABS, CT_FOCUS_LIST, CT_FOCUS_CONTROLS, CT_FOCUS_ACTIONS, CT_FOCUS_SUB };
enum { CT_SUB_NONE, CT_SUB_VERSIONS, CT_SUB_EXPORT, CT_SUB_RELIC, CT_SUB_INFO, CT_SUB_INSTALL, CT_SUB_IMPORT_DONE };
enum { CT_FILTER_ALL, CT_FILTER_INSTALLED, CT_FILTER_BROWSE, CT_FILTER_COUNT };
enum { CT_ITEM_INSTALLED, CT_ITEM_CATALOGUE, CT_ITEM_PACKAGE };

#define CT_LIST_X 32
#define CT_LIST_W 244
#define CT_ROW_Y0 68
#define CT_ROW_H 15
#define CT_ROWS 7
#define CT_DETAIL_X 305
#define CT_DETAIL_W 176
#define CT_LINE_H 12
#define CT_TAB_Y 52
#define CT_STATUS_Y 180
#define CT_HINT_Y 196
#define CT_PAGE_FRAMES 150 /* a detail text longer than its lines turns a page every 2.5 s */
#define CT_TEXT_MAX 1024
#define CT_ACTIONS_MAX 8

static const char *const s_ctTabNames[CT_TAB_COUNT] = {"Library", "This Seed", "Import"};
static const char *const s_ctFilterNames[CT_FILTER_COUNT] = {"All", "Installed", "Browse"};

static int s_ctTab, s_ctFocus, s_ctSub, s_ctFilter;
static int s_ctListSel[CT_TAB_COUNT], s_ctControlSel[CT_TAB_COUNT], s_ctActionSel, s_ctSubSel;
static int s_ctInfoTop, s_ctClearArmed;
static unsigned int s_ctLastFrame;
static char s_ctMessage[512];
static char s_ctInfoTitle[160], s_ctInfo[CT_TEXT_MAX];
static int s_ctLegacyDownload; /* the release-profile package download (old package page) runs */
static int s_ctInfoReturn; /* focus an info view returns to */
static unsigned int s_ctPageClock; /* frames since the last button: long texts start on page 1 */

/* ---- text --------------------------------------------------------------- */

/* Wrap plain text into lines of `width` pixels, breaking at spaces, and long
   words where they must. Returns the line count (at most max). */
static int CT_Wrap(const char *text, int width, char lines[][128], int max)
{
    char plain[CT_TEXT_MAX];
    const char *p = plain;
    int count = 0;
    ap_seed_plain_text(text ? text : "", plain, sizeof plain);
    while (*p && count < max)
    {
        size_t n = 0, cut = 0;
        char *line = lines[count];
        if (*p == ' ') { p++; continue; }
        while (p[n] && n + 1 < 128)
        {
            line[n] = p[n];
            line[n + 1] = 0;
            if (DecalFont_GetLineWidth(line, FONT_SMALL) > width) break;
            if (p[n] == ' ') cut = n;
            n++;
        }
        if (p[n] && cut > 0) n = cut;
        if (n == 0) n = 1;
        line[n] = 0;
        while (n > 0 && line[n - 1] == ' ') line[--n] = 0;
        p += strlen(line);
        count++;
    }
    return count;
}

/* Up to `lines` lines at (x, y). Longer text turns pages on the frame clock,
   so a full name or a long message is readable without a button. */
static void CT_DrawPaged(const char *text, int x, int y, int width, int lines, int flags, uint32_t *ot)
{
    static char wrapped[40][128];
    int total = CT_Wrap(text, width, wrapped, 40), pages, page, i;
    if (lines <= 0 || total == 0) return;
    pages = (total + lines - 1) / lines;
    page = pages > 1 ? (int)(s_ctPageClock / CT_PAGE_FRAMES) % pages : 0;
    for (i = 0; i < lines && page * lines + i < total; i++)
        DecalFont_DrawLineOT(wrapped[page * lines + i], x, y + i * CT_LINE_H, FONT_SMALL, flags, ot);
}

/* Cut one line to `width` pixels at any character, ending in "...". */
static void CT_Fit(const char *text, char *out, size_t size, int width)
{
    size_t n;
    ap_seed_plain_text(text, out, size);
    n = strlen(out);
    while (n > 0 && DecalFont_GetLineWidth(out, FONT_SMALL) > width)
    {
        n--;
        if (n + 4 > size) continue;
        memcpy(out + n, "...", 4);
    }
}

static void CT_SetMessage(const char *text)
{
    snprintf(s_ctMessage, sizeof s_ctMessage, "%s", text ? text : "");
}

static void CT_Row(int row, const char *text, int selected, int flags, uint32_t *ot)
{
    RECT box = {24, CT_ROW_Y0 + row * CT_ROW_H, 260, 14};
    char plain[640];
    ap_seed_plain_text(text, plain, sizeof plain);
    DecalFont_DrawLineBox(plain, CT_LIST_X, box.y + 2, CT_LIST_W, FONT_SMALL,
        selected ? ORANGE : flags, selected ? TEXT_MARQUEE : TEXT_ELLIPSIS, TEXT_SLOT_LIBRARY, ot);
    if (selected) CTR_Box_DrawClearBox(&box, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
}

/* A row with a right-hand state column, the title cut to the space left. */
static void CT_RowState(int row, const char *title, const char *state, int selected, int stateFlags, uint32_t *ot)
{
    RECT box = {24, CT_ROW_Y0 + row * CT_ROW_H, 260, 14};
    char plain[640];
    int stateW = DecalFont_GetLineWidth((char *)state, FONT_SMALL);
    ap_seed_plain_text(title, plain, sizeof plain);
    DecalFont_DrawLineBox(plain, CT_LIST_X, box.y + 2, CT_LIST_W - stateW - 10, FONT_SMALL,
        selected ? ORANGE : WHITE, selected ? TEXT_MARQUEE : TEXT_ELLIPSIS, TEXT_SLOT_LIBRARY, ot);
    DecalFont_DrawLineOT((char *)state, CT_LIST_X + CT_LIST_W, box.y + 2, FONT_SMALL, JUSTIFY_RIGHT | stateFlags, ot);
    if (selected) CTR_Box_DrawClearBox(&box, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
}

static void CT_Footer(const char *status, const char *hint, uint32_t *ot)
{
    char busy[80];
    if (hint && ap_custom_track_download_busy(busy, sizeof busy)) hint = busy;
    if (status && *status) MM_CustomText_Draw(status, 256, CT_STATUS_Y, 456, 1, JUSTIFY_CENTER | WHITE, ot);
    if (hint && *hint) MM_CustomText_Draw(hint, 256, CT_HINT_Y, 456, 1, JUSTIFY_CENTER | WHITE, ot);
}

/* Rows 0..count-1 shown seven at a time around `selected`. */
static int CT_First(int selected) { return selected < 0 ? 0 : (selected / CT_ROWS) * CT_ROWS; }

/* The slim scroll bar of a list with more than CT_ROWS rows (A4-36): right of
   the row highlight, inside the list panel. These lists show a whole page of
   seven rows at a time, so the thumb follows the page and the rows it draws
   (a last page can hold fewer than seven). */
static void CT_ScrollBar(int total, int first, uint32_t *ot)
{
    const int shown = total - first < CT_ROWS ? total - first : CT_ROWS;
    CTR_MenuDrawScrollBar(284, CT_ROW_Y0 - 2, CT_ROWS * CT_ROW_H, total, shown, first, ot);
}

static int CT_Step(int value, int count, int tapped)
{
    if (count <= 0) return 0;
    int next = value < count ? value : count - 1;
    if (tapped & BTN_UP) { OtherFX_Play(0, 1); next = (value + count - 1) % count; }
    else if (tapped & BTN_DOWN) { OtherFX_Play(0, 1); next = (value + 1) % count; }
    if (next != value) DecalFont_ResetLineBox(TEXT_SLOT_LIBRARY);
    return next;
}

/* ---- the release-profile package (old package page) --------------------- */

/* The Saphi track id at the end of a release profile's source URL
   (".../tracks/101"), 0 when there is none. */
static int CT_PackageTrackID(void)
{
    const struct CustomTrackManagerPackage *package = AP_CustomContentPackage();
    const char *slash;
    if (!package || !package->sourceUrl) return 0;
    slash = strrchr(package->sourceUrl, '/');
    return slash ? atoi(slash + 1) : 0;
}

/* ---- Library ------------------------------------------------------------ */

struct CT_LibItem { int kind; size_t index; };
static struct CT_LibItem s_ctItems[CTR_LIBRARY_MAX + 1];
static int s_ctItemCount;

struct CT_LibView
{
    const struct CustomTrackLibraryEntry *installed; /* the installed revision shown */
    const struct CustomSaphiRevision *source;        /* the preferred catalogue revision */
    const struct CustomTrackLibraryEntry *exact;     /* installed copy of exactly that revision */
    int older;      /* an older revision is installed: Update */
    int trackID;    /* Saphi track id, 0 when unknown */
    int package;    /* the track has the release profile (export, relic times) */
    int allowed;    /* A4-74: its author allowed in-game download (Download, Update, Versions) */
    const char *title, *version, *author;
};

/* A4-74: catalogue tracks the Library leaves out because their author has
   not allowed in-game download (counted per Saphi track, not installed). */
static int s_ctNotAllowed;

static int CT_RevisionAllowed(const struct CustomSaphiRevision *r)
{
    return r && ap_track_permission(r->trackID, r->authorID, r->saphiPermission, NULL) == AP_TRACK_PERMISSION_ALLOW;
}

/* The permission list's state for the page controls, "" when it is loaded
   and nothing is left out. */
static void CT_PermissionLine(char *out, size_t size)
{
    int source = AP_TRACK_CONSENT_SOURCE_NONE;
    const int state = ap_track_consent_status(&source, NULL, 0);
    out[0] = 0;
    if (source == AP_TRACK_CONSENT_SOURCE_NONE)
        snprintf(out, size, "%s", state == AP_TRACK_CONSENT_LOADING || state == AP_TRACK_CONSENT_IDLE ?
                 "Checking which authors allow in-game download..." :
                 "The permission list could not be loaded, so no track is offered for download.");
    else if (s_ctNotAllowed)
        snprintf(out, size, "%d track%s not listed: %s author%s not allowed in-game download%s.", s_ctNotAllowed,
                 s_ctNotAllowed == 1 ? "" : "s", s_ctNotAllowed == 1 ? "its" : "their",
                 s_ctNotAllowed == 1 ? " has" : "s have", source == AP_TRACK_CONSENT_SOURCE_CACHED ? " (saved list)" : "");
}

/* The two-line status slot of the Library detail: short, so it fits
   without paging. The actions say it in full. */
static const char *CT_PermissionShort(void)
{
    int source = AP_TRACK_CONSENT_SOURCE_NONE;
    const int state = ap_track_consent_status(&source, NULL, 0);
    if (source != AP_TRACK_CONSENT_SOURCE_NONE) return "Not allowed by author";
    return state == AP_TRACK_CONSENT_LOADING || state == AP_TRACK_CONSENT_IDLE ? "Checking permission..." : "Download off: no list";
}

/* Browse with every catalogue track left out: why, instead of an empty page. */
static const char *CT_PermissionEmpty(void)
{
    int source = AP_TRACK_CONSENT_SOURCE_NONE;
    const int state = ap_track_consent_status(&source, NULL, 0);
    if (source != AP_TRACK_CONSENT_SOURCE_NONE)
        return "Only author-allowed tracks are listed.";
    return state == AP_TRACK_CONSENT_LOADING || state == AP_TRACK_CONSENT_IDLE ?
           "Checking which authors allow in-game download..." :
           "Permission list not loaded: no downloads.";
}

/* A refresh can replace the selected revision without changing its title or
   row index. Copy its identity before the catalogue/store can be replaced. */
static void CT_LibraryMarqueeOwner(const struct CT_LibItem *item, const struct CT_LibView *view)
{
    static char previous[1024];
    char owner[1024];
    owner[0] = 0;
    if (item && item->kind == CT_ITEM_INSTALLED && view->installed)
        snprintf(owner, sizeof owner, "installed:%s", view->installed->manifestSha256);
    else if (item && item->kind == CT_ITEM_CATALOGUE && view->source)
    {
        const struct CustomSaphiRevision *r = view->source;
        snprintf(owner, sizeof owner, "source:%d:%s:%d:%u:%u:%s:%d:%u:%u:%s:%d:%u:%u:%s",
                 r->trackID, r->version, r->lev.id, r->lev.bytes, r->lev.crc32, r->lev.path,
                 r->vrm.id, r->vrm.bytes, r->vrm.crc32, r->vrm.path,
                 r->sca.id, r->sca.bytes, r->sca.crc32, r->sca.path);
    }
    else if (item && item->kind == CT_ITEM_PACKAGE)
    {
        const struct CustomTrackManagerPackage *p = AP_CustomContentPackage();
        snprintf(owner, sizeof owner, "package:%s:%s:%s:%s", p->packageUuid,
                 p->version, p->levSha256, p->vrmSha256);
    }
    if (!strcmp(previous, owner)) return;
    snprintf(previous, sizeof previous, "%s", owner);
    DecalFont_ResetLineBox(TEXT_SLOT_LIBRARY);
}

static const struct CustomSaphiRevision *CT_PreferredSource(int trackID)
{
    const struct CustomSaphiRevision *best = NULL;
    size_t i;
    if (trackID <= 0) return NULL;
    for (i = 0; i < CustomSaphi_Count(s_saphiCatalogue); i++)
    {
        const struct CustomSaphiRevision *r = CustomSaphi_Row(s_saphiCatalogue, i);
        if (r->trackID == trackID && (!best || MM_CustomSourcePreferred(r, best))) best = r;
    }
    return best;
}

static void CT_View(const struct CT_LibItem *item, struct CT_LibView *v)
{
    const struct CustomTrackManagerPackage *package = AP_CustomContentPackage();
    memset(v, 0, sizeof *v);
    if (item->kind == CT_ITEM_INSTALLED)
    {
        v->installed = &s_contentLibrary.entries[item->index];
        v->trackID = v->installed->local.trackID;
        v->source = CT_PreferredSource(v->trackID);
    }
    else if (item->kind == CT_ITEM_CATALOGUE)
    {
        v->source = CustomSaphi_Row(s_saphiCatalogue, item->index);
        v->trackID = v->source->trackID;
    }
    if (v->source) v->exact = MM_CustomSourceInstalled(v->source, &v->older);
    if (!v->installed) v->installed = v->exact;
    v->package = item->kind == CT_ITEM_PACKAGE || (v->trackID > 0 && v->trackID == CT_PackageTrackID());
    if (v->source) v->allowed = CT_RevisionAllowed(v->source);
    else if (item->kind == CT_ITEM_PACKAGE)
        v->allowed = ap_track_permission(CT_PackageTrackID(), 0, AP_TRACK_PERMISSION_UNKNOWN, NULL) == AP_TRACK_PERMISSION_ALLOW;
    else if (v->installed)
        v->allowed = ap_track_permission(v->installed->local.permissionTrackID, v->installed->local.authorID,
                                         AP_TRACK_PERMISSION_UNKNOWN, v->installed->local.trackKey) == AP_TRACK_PERMISSION_ALLOW;
    if (item->kind == CT_ITEM_PACKAGE)
    {
        v->title = package->title; v->version = package->version; v->author = package->author;
    }
    else if (item->kind == CT_ITEM_INSTALLED)
    {
        v->title = v->installed->title; v->version = v->installed->version;
        v->author = v->installed->author[0] ? v->installed->author : v->installed->local.author;
    }
    else
    {
        v->title = v->source->title; v->version = v->source->version; v->author = v->source->author;
    }
}

static const char *CT_ItemTitle(const struct CT_LibItem *item)
{
    if (item->kind == CT_ITEM_INSTALLED) return s_contentLibrary.entries[item->index].title;
    if (item->kind == CT_ITEM_CATALOGUE) return CustomSaphi_Row(s_saphiCatalogue, item->index)->title;
    return AP_CustomContentPackage()->title;
}

static int CT_ItemCompare(const void *a, const void *b)
{
    const char *x = CT_ItemTitle((const struct CT_LibItem *)a), *y = CT_ItemTitle((const struct CT_LibItem *)b);
    for (; *x && *y; x++, y++)
    {
        int cx = *x >= 'A' && *x <= 'Z' ? *x + 32 : (unsigned char)*x;
        int cy = *y >= 'A' && *y <= 'Z' ? *y + 32 : (unsigned char)*y;
        if (cx != cy) return cx - cy;
    }
    return (unsigned char)*x - (unsigned char)*y;
}

/* The Library rows for the filter and search: installed revisions folded by
   package uuid (newest shown), catalogue tracks folded by Saphi track id
   (preferred revision), and the release-profile package when neither lists
   it, so its download, setup, export and relic times stay reachable. */
static void CT_BuildLibrary(void)
{
    size_t i;
    int n = 0, packageListed = 0, packageID = CT_PackageTrackID();
    const struct CustomTrackManagerPackage *package = AP_CustomContentPackage();
    if (s_ctFilter != CT_FILTER_BROWSE)
        for (i = 0; i < s_contentLibrary.count && n < CTR_LIBRARY_MAX; i++)
        {
            const struct CustomTrackLibraryEntry *r = &s_contentLibrary.entries[i];
            int at;
            if (!r->installed && !r->scanFailed) continue;
            if (!(MM_CustomSearchMatch(r->title, s_saphiQuery) || MM_CustomSearchMatch(r->author, s_saphiQuery) ||
                  MM_CustomSearchMatch(r->version, s_saphiQuery))) continue;
            for (at = 0; at < n; at++)
                if (r->local.uuid[0] && !strcmp(r->local.uuid, s_contentLibrary.entries[s_ctItems[at].index].local.uuid)) break;
            if (at == n) { s_ctItems[n].kind = CT_ITEM_INSTALLED; s_ctItems[n++].index = i; }
            else if (CustomRevision_Compare(r->version, s_contentLibrary.entries[s_ctItems[at].index].version) > 0)
                s_ctItems[at].index = i;
        }
    s_ctNotAllowed = 0;
    {
        const int installedRows = n;
        int hidden[CTR_LIBRARY_MAX], hiddenCount = 0;
        if (s_ctFilter != CT_FILTER_INSTALLED)
            for (i = 0; i < CustomSaphi_Count(s_saphiCatalogue) && n < CTR_LIBRARY_MAX; i++)
            {
                const struct CustomSaphiRevision *r = CustomSaphi_Row(s_saphiCatalogue, i);
                int at, shown = 0;
                if (!(MM_CustomSearchMatch(r->title, s_saphiQuery) || MM_CustomSearchMatch(r->author, s_saphiQuery))) continue;
                /* A4-74: only tracks whose author said yes are offered. An
                   installed copy is still listed above, from the store. */
                if (!CT_RevisionAllowed(r))
                {
                    for (at = 0; at < hiddenCount && hidden[at] != r->trackID; at++) {}
                    if (at == hiddenCount && hiddenCount < CTR_LIBRARY_MAX) hidden[hiddenCount++] = r->trackID;
                    continue;
                }
                /* All: a track already listed as installed is not listed twice. */
                for (at = 0; at < installedRows && r->trackID > 0; at++)
                    if (s_contentLibrary.entries[s_ctItems[at].index].local.trackID == r->trackID) shown = 1;
                if (shown) continue;
                for (at = installedRows; at < n; at++)
                    if (CustomSaphi_Row(s_saphiCatalogue, s_ctItems[at].index)->trackID == r->trackID) break;
                if (at == n) { s_ctItems[n].kind = CT_ITEM_CATALOGUE; s_ctItems[n++].index = i; }
                else if (MM_CustomSourcePreferred(r, CustomSaphi_Row(s_saphiCatalogue, s_ctItems[at].index)))
                    s_ctItems[at].index = i;
            }
        /* A hidden track with an allowed revision listed, or installed, is not counted. */
        for (int h = 0; h < hiddenCount; h++)
        {
            int at, listed = 0;
            for (at = 0; at < n && !listed; at++)
                listed = (s_ctItems[at].kind == CT_ITEM_CATALOGUE &&
                          CustomSaphi_Row(s_saphiCatalogue, s_ctItems[at].index)->trackID == hidden[h]) ||
                         (s_ctItems[at].kind == CT_ITEM_INSTALLED &&
                          s_contentLibrary.entries[s_ctItems[at].index].local.trackID == hidden[h]);
            if (!listed) s_ctNotAllowed++;
        }
    }
    for (i = 0; i < (size_t)n; i++)
    {
        struct CT_LibView v;
        CT_View(&s_ctItems[i], &v);
        if (v.package) packageListed = 1;
    }
    /* A4-74: the release-profile row offers a download only when allowed;
       without one it is listed only when its files are already there. */
    if (!packageListed && packageID > 0 && s_ctFilter != CT_FILTER_BROWSE && package &&
        (ap_track_permission(packageID, 0, AP_TRACK_PERMISSION_UNKNOWN, NULL) == AP_TRACK_PERMISSION_ALLOW ||
         AP_CustomContentStatus()->state != CTR_CT_MANAGER_NOT_INSTALLED) &&
        (s_ctFilter == CT_FILTER_ALL || AP_CustomContentStatus()->state == CTR_CT_MANAGER_READY) &&
        MM_CustomSearchMatch(package->title, s_saphiQuery))
    {
        s_ctItems[n].kind = CT_ITEM_PACKAGE; s_ctItems[n++].index = 0;
    }
    if (n > 1) qsort(s_ctItems, (size_t)n, sizeof s_ctItems[0], CT_ItemCompare);
    s_ctItemCount = n;
}

static const char *CT_ModesText(const struct CT_LibView *v)
{
    if (v->installed && v->installed->installed)
        return v->installed->local.arcade ? (v->installed->local.timeTrial ? "Arcade, Time Trial" : "Arcade") :
               v->installed->local.timeTrial ? "Time Trial only" : "No races";
    if (v->source) return (v->source->modeTags & 1) ? "Arcade tagged" : (v->source->modeTags & 2) ? "Time Trial tagged" : "Other modes";
    return AP_CustomContentStatus()->state == CTR_CT_MANAGER_READY ? "Files verified" : "";
}

/* Where the shown copy comes from, for the detail panel. */
static void CT_Badges(const struct CT_LibView *v, int kind, char *out, size_t size)
{
    const struct CustomTrackManagerStatus *status = AP_CustomContentStatus();
    if ((!v->installed || !v->installed->installed) && v->package &&
        !strcmp(v->version, AP_CustomContentPackage()->version) &&
        status->state == CTR_CT_MANAGER_MANIFEST_MISSING)
        snprintf(out, size, "Needs setup");
    else if (kind == CT_ITEM_PACKAGE)
        snprintf(out, size, "%s", CustomTrackManager_StateText(status->state));
    else if (v->installed && v->installed->installed)
        snprintf(out, size, "%s", v->installed->local.xdelta ? "Imported" : "Installed");
    else if (v->installed && v->installed->scanFailed)
        snprintf(out, size, "Failed checks");
    else
        snprintf(out, size, "Not installed");
}

enum { CT_ACT_INSTALL, CT_ACT_VERSIONS, CT_ACT_SOURCE, CT_ACT_VERIFY, CT_ACT_EXPORT, CT_ACT_RELIC,
       CT_ACT_RESOLVE, CT_ACT_SEED_VERIFY, CT_ACT_FILES, CT_ACT_COPIES };
struct CT_Action { int id; char label[48]; char reason[200]; };

/* What an available action does, for the detail panel. */
static const char *CT_ActionHelp(const struct CT_Action *a)
{
    switch (a->id)
    {
    case CT_ACT_INSTALL:
        return !strncmp(a->label, "Recheck", 7) ? "Check the installed files again." :
               !strncmp(a->label, "Update", 6) ? "Download the newer version. A room keeps the version it pins." :
               "Download this version. Rooms, Play Solo and Arcade all use it.";
    case CT_ACT_VERSIONS: return "Every version on Project Saphi, with its race modes.";
    case CT_ACT_SOURCE: return "Open the track's Project Saphi page.";
    case CT_ACT_VERIFY: return "Check the files again and finish the local setup.";
    case CT_ACT_EXPORT: return "Copy or save the verified custom_tracks YAML.";
    case CT_ACT_RELIC: return "Set the Sapphire, Gold and Platinum times. Export YAML uses them.";
    case CT_ACT_RESOLVE: return "Use installed files that match, download them, or show where they go.";
    case CT_ACT_SEED_VERIFY: return "Check this track's files again.";
    case CT_ACT_FILES: return "Which files the seed needs and where they go.";
    case CT_ACT_COPIES: return "Installed versions of this track.";
    default: return "";
    }
}

/* The Saphi page of the shown track. A Current Seed install keeps trackID
   zero (no catalogue updates) but records its Saphi id for the permission
   check (A4-74); its page is known too. */
static int CT_SourceTrackID(const struct CT_LibView *v)
{
    if (v->trackID > 0) return v->trackID;
    return v->installed ? v->installed->local.permissionTrackID : 0;
}

static void CT_Add(struct CT_Action *a, int *n, int id, const char *label, const char *reason)
{
    if (*n >= CT_ACTIONS_MAX) return;
    a[*n].id = id;
    snprintf(a[*n].label, sizeof a[*n].label, "%s", label);
    snprintf(a[*n].reason, sizeof a[*n].reason, "%s", reason ? reason : "");
    (*n)++;
}

/* Why the release-profile actions (export, relic times) are unavailable, or
   "" when they can run. */
static void CT_PackageReason(const struct CT_LibView *v, int relic, char *out, size_t size)
{
    const struct CustomTrackManagerPackage *package = AP_CustomContentPackage();
    const struct CustomTrackManagerStatus *status = AP_CustomContentStatus();
    out[0] = 0;
    if (!v->package)
        snprintf(out, size, "Not available for this track yet. Only %s has an export profile.", package->title);
    else if (status->state != CTR_CT_MANAGER_READY || !status->verifiedPackageSha256[0])
        snprintf(out, size, "Needs a Ready %s v%s. Use Verify / Setup first.", package->title, package->version);
    else if (relic && !package->flagRelicCrates)
        snprintf(out, size, "This track has no relic crates.");
}

static int CT_LibraryActions(const struct CT_LibItem *item, const struct CT_LibView *v, struct CT_Action *a)
{
    int n = 0;
    char label[48], reason[200];
    reason[0] = 0;
    if (s_saphiDownloading || s_ctLegacyDownload)
    {
        snprintf(label, sizeof label, "Downloading...");
        snprintf(reason, sizeof reason, "A download is running. Wait for it to finish.");
    }
    else if (!v->allowed && (v->installed || (item->kind == CT_ITEM_PACKAGE &&
             AP_CustomContentStatus()->state != CTR_CT_MANAGER_NOT_INSTALLED)))
        snprintf(label, sizeof label, "Recheck files"); /* A4-74: an installed copy keeps working, no Update */
    else if (!v->allowed)
    {
        snprintf(label, sizeof label, "Download");
        snprintf(reason, sizeof reason, "%s", ap_track_permission_text());
    }
    else if (item->kind == CT_ITEM_PACKAGE)
        snprintf(label, sizeof label, "Download v%.24s", v->version);
    else if (v->source && v->exact)
        snprintf(label, sizeof label, "Recheck files");
    else if (v->source)
    {
        snprintf(label, sizeof label, "%s v%.24s", v->older ? "Update to" : "Download", v->source->version);
        if (v->source->disabledReason[0]) snprintf(reason, sizeof reason, "%s", v->source->disabledReason);
    }
    else snprintf(label, sizeof label, "Recheck files");
    CT_Add(a, &n, CT_ACT_INSTALL, label, reason);
    CT_Add(a, &n, CT_ACT_VERSIONS, "Versions", !v->source ? "Not in the Project Saphi catalogue." :
           !v->allowed ? ap_track_permission_text() : "");
    CT_Add(a, &n, CT_ACT_SOURCE, "Open source page",
           CT_SourceTrackID(v) > 0 || item->kind == CT_ITEM_PACKAGE ? "" :
           v->installed && v->installed->local.xdelta ? "Imported from a patch. No source page is known." :
           "No source page is known for this track.");
    if (v->package)
        CT_Add(a, &n, CT_ACT_VERIFY, "Verify / Setup", s_ctLegacyDownload ? "A download is running." : "");
    CT_PackageReason(v, 0, reason, sizeof reason);
    CT_Add(a, &n, CT_ACT_EXPORT, "Export YAML", reason);
    CT_PackageReason(v, 1, reason, sizeof reason);
    CT_Add(a, &n, CT_ACT_RELIC, "Relic times", reason);
    return n;
}

static void CT_OpenURL(const char *url)
{
    CT_SetMessage(Platform_OpenURL(url) ? "Opened the Project Saphi page." : "Could not open the source page.");
}

static void CT_OpenTrackSource(int trackID)
{
    char url[96];
    snprintf(url, sizeof url, "https://www.projectsaphi.com/tracks/%d", trackID);
    CT_OpenURL(url);
}

static void CT_StartSaphiInstall(const struct CustomSaphiRevision *r, const struct CustomTrackLibraryEntry *exact)
{
    if (exact)
    {
        s_contentStoreWanted = 1;
        snprintf(s_saphiMessage, sizeof s_saphiMessage, "Checking installed files...");
        CT_SetMessage("Checking the installed files again.");
        return;
    }
    if (r->disabledReason[0]) { CT_SetMessage(r->disabledReason); return; }
    if (!CT_RevisionAllowed(r)) { CT_SetMessage(ap_track_permission_text()); return; }
    if (MM_CustomLibrary_Busy() || AP_SeedContentInstallBusy())
    {
        CT_SetMessage("Another track job is running. Try again when it finishes.");
        return;
    }
    s_saphiDownloading = CustomSaphi_StartInstall(r, NativeAssets_GetAssetDir());
    snprintf(s_saphiMessage, sizeof s_saphiMessage, "%s",
             s_saphiDownloading ? "Downloading and checking the selected revision..." : "Could not start the download.");
    CT_SetMessage(s_saphiMessage);
}

static void CT_LegacyDownload(void)
{
    char detail[128];
    if (AP_CustomContentDownloadStart())
    {
        s_ctLegacyDownload = 1;
        CT_SetMessage("Starting verified download from Project Saphi...");
    }
    else
    {
        AP_CustomContentDownloadStatus(detail, sizeof detail);
        CT_SetMessage(detail[0] ? detail : "Could not start the download.");
    }
}

/* ---- relic times (row 10j, now inside a track's details) ----------------- */

#define CT_RELIC_DIGITS 6 /* M M : S S . C C */
#define CT_RELIC_SAVE (3 * CT_RELIC_DIGITS)
#define CT_RELIC_CANCEL (CT_RELIC_SAVE + 1)
#define CT_RELIC_CLEAR (CT_RELIC_SAVE + 2)
#define CT_RELIC_POSITIONS (CT_RELIC_SAVE + 3)
static const char *const s_ctRelicTiers[3] = {"Sapphire", "Gold", "Platinum"};
static int s_ctRelicCentis[3], s_ctRelicSet;
static char s_ctRelicFor[NATIVE_SHA256_HEX_BYTES];

static int CT_RelicDigit(int centis, int d)
{
    static const int place[CT_RELIC_DIGITS] = {60000, 6000, 1000, 100, 10, 1};
    int minutes = centis / 6000, rest = centis % 6000;
    if (d < 2) return d == 0 ? minutes / 10 : minutes % 10;
    return (rest / place[d]) % (d == 2 ? 6 : 10);
}

static int CT_RelicStep(int centis, int d, int up)
{
    static const int limit[CT_RELIC_DIGITS] = {6, 10, 6, 10, 10, 10};
    int digits[CT_RELIC_DIGITS], i;
    for (i = 0; i < CT_RELIC_DIGITS; i++) digits[i] = CT_RelicDigit(centis, i);
    digits[d] = (digits[d] + (up ? 1 : limit[d] - 1)) % limit[d];
    return (digits[0] * 10 + digits[1]) * 6000 + (digits[2] * 10 + digits[3]) * 100 + digits[4] * 10 + digits[5];
}

static void CT_RelicFormat(int centis, char *out, size_t size)
{
    snprintf(out, size, "%d%d:%d%d.%d%d", CT_RelicDigit(centis, 0), CT_RelicDigit(centis, 1), CT_RelicDigit(centis, 2),
             CT_RelicDigit(centis, 3), CT_RelicDigit(centis, 4), CT_RelicDigit(centis, 5));
}

static void CT_RelicLoad(void)
{
    struct CustomTrackRelicTimes relic;
    int t;
    CustomTrackManager_LoadRelicTimes(NativeAssets_GetAssetDir(), AP_CustomContentPackage(), AP_CustomContentStatus(), &relic);
    s_ctRelicSet = relic.set;
    for (t = 0; t < 3; t++) s_ctRelicCentis[t] = relic.set ? CustomTrackManager_RelicCentisFromTicks(relic.ticks[t]) : 0;
}

static void CT_RelicOpen(void)
{
    snprintf(s_ctRelicFor, sizeof s_ctRelicFor, "%s", AP_CustomContentStatus()->verifiedPackageSha256);
    CT_RelicLoad();
    s_ctSubSel = 0;
    s_ctClearArmed = 0;
    CT_SetMessage(s_ctRelicSet ? "Left/Right: move. Up/Down: change the digit." :
                  "No relic times yet. Export writes no Relic Race until they are saved.");
}

static void CT_RelicSave(void)
{
    const struct CustomTrackManagerPackage *package = AP_CustomContentPackage();
    const struct CustomTrackManagerStatus *status = AP_CustomContentStatus();
    int ticks[3], t;
    for (t = 0; t < 3; t++)
    {
        ticks[t] = CustomTrackManager_RelicTicksFromCentis(s_ctRelicCentis[t]);
        if (ticks[t] < 0) { CT_SetMessage("Enter all three times, 0:00.01 or longer."); return; }
    }
    if (!CustomTrackManager_RelicTimesValid(ticks))
        CT_SetMessage("Sapphire must be slower than Gold, Gold slower than Platinum.");
    else if (!CustomTrackManager_SaveRelicTimes(NativeAssets_GetAssetDir(), package, status, ticks))
        CT_SetMessage("Could not save the relic times.");
    else
    {
        s_ctRelicSet = 1;
        CT_SetMessage("Relic times saved. Export YAML includes them.");
    }
}

/* The editor owns the pad. Returns 1 when it closed. */
static int CT_RelicInput(int tapped)
{
    const struct CustomTrackManagerStatus *status = AP_CustomContentStatus();
    int pos = s_ctSubSel;
    if (status->state != CTR_CT_MANAGER_READY || strcmp(status->verifiedPackageSha256, s_ctRelicFor) != 0)
    {
        /* Times typed for other bytes are never saved. */
        CT_SetMessage("The package changed. Relic times not saved.");
        return 1;
    }
    if (tapped & (BTN_TRIANGLE | BTN_START))
    {
        OtherFX_Play(2, 1);
        CT_SetMessage("Relic times not changed.");
        return 1;
    }
    if (tapped & BTN_LEFT) { s_ctSubSel = (pos + CT_RELIC_POSITIONS - 1) % CT_RELIC_POSITIONS; s_ctClearArmed = 0; OtherFX_Play(0, 1); }
    if (tapped & BTN_RIGHT) { s_ctSubSel = (pos + 1) % CT_RELIC_POSITIONS; s_ctClearArmed = 0; OtherFX_Play(0, 1); }
    if ((tapped & (BTN_UP | BTN_DOWN)) && pos < CT_RELIC_SAVE)
    {
        s_ctRelicCentis[pos / CT_RELIC_DIGITS] = CT_RelicStep(s_ctRelicCentis[pos / CT_RELIC_DIGITS], pos % CT_RELIC_DIGITS,
                                                              (tapped & BTN_UP) != 0);
        OtherFX_Play(0, 1);
    }
    if (tapped & (BTN_CROSS | BTN_CIRCLE))
    {
        OtherFX_Play(1, 1);
        if (pos < CT_RELIC_SAVE) { s_ctSubSel = CT_RELIC_SAVE; CT_SetMessage("Confirm again on Save to keep these times."); }
        else if (pos == CT_RELIC_SAVE) CT_RelicSave();
        else if (pos == CT_RELIC_CANCEL) { CT_SetMessage("Relic times not changed."); return 1; }
        else if (!s_ctClearArmed)
        {
            s_ctClearArmed = 1;
            CT_SetMessage("Confirm again to clear. Export YAML then writes no Relic Race.");
        }
        else
        {
            s_ctClearArmed = 0;
            if (CustomTrackManager_ClearRelicTimes(NativeAssets_GetAssetDir(), AP_CustomContentPackage()))
            {
                CT_RelicLoad();
                CT_SetMessage("Relic times cleared. Export YAML writes no Relic Race.");
            }
            else CT_SetMessage("Could not clear the relic times.");
        }
    }
    return 0;
}

static void CT_RelicDraw(uint32_t *ot)
{
    int t;
    char text[32];
    for (t = 0; t < 3; t++)
    {
        const int y = CT_ROW_Y0 + t * CT_ROW_H + 2;
        const int rowSel = s_ctSubSel / CT_RELIC_DIGITS == t && s_ctSubSel < CT_RELIC_SAVE;
        DecalFont_DrawLineOT((char *)s_ctRelicTiers[t], CT_LIST_X, y, FONT_SMALL, rowSel ? ORANGE : WHITE, ot);
        CT_RelicFormat(s_ctRelicCentis[t], text, sizeof text);
        DecalFont_DrawLineOT(text, 160, y, FONT_SMALL, WHITE, ot);
        if (rowSel)
        {
            static const int column[CT_RELIC_DIGITS] = {0, 1, 3, 4, 6, 7};
            const int d = s_ctSubSel % CT_RELIC_DIGITS;
            RECT sel = {160 + DecalFont_GetLineWidthStrlen(text, column[d], FONT_SMALL) - 1, y - 2,
                        DecalFont_GetLineWidthStrlen(text + column[d], 1, FONT_SMALL) + 2, 12};
            CTR_Box_DrawClearBox(&sel, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
        }
    }
    CT_Row(3, "Save", s_ctSubSel == CT_RELIC_SAVE, ORANGE, ot);
    CT_Row(4, "Cancel", s_ctSubSel == CT_RELIC_CANCEL, ORANGE, ot);
    CT_Row(5, "Clear relic times", s_ctSubSel == CT_RELIC_CLEAR, ORANGE, ot);
}

/* ---- export (Copy YAML / Save YAML) ------------------------------------- */

static void CT_Export(int save)
{
    const struct CustomTrackManagerPackage *package = AP_CustomContentPackage();
    const struct CustomTrackManagerStatus *status = AP_CustomContentStatus();
    struct CustomTrackRelicTimes relic;
    static char yaml[4096];
    CustomTrackManager_LoadRelicTimes(NativeAssets_GetAssetDir(), package, status, &relic);
    if (!save)
    {
        if (CustomTrackManager_RenderYaml(package, status, &relic, yaml, sizeof yaml) && Platform_SetClipboardText(yaml))
            CT_SetMessage(relic.set ? "Verified custom_tracks YAML copied, with relic times." :
                                      "Verified custom_tracks YAML copied. No relic times, so no Relic Race.");
        else CT_SetMessage("Copy YAML needs a Ready package. Nothing was copied.");
    }
    else
    {
        struct CustomTrackManagerStatus exported;
        if (CustomTrackManager_SaveYaml(NativeAssets_GetAssetDir(), package, status, &relic, &exported))
            snprintf(s_ctMessage, sizeof s_ctMessage, "Saved to: %.480s", exported.yamlPath);
        else CT_SetMessage("Save YAML needs a Ready package. Nothing was saved.");
    }
}

/* ---- This Seed ---------------------------------------------------------- */

static struct AP_SeedContentRow s_ctSeedRows[AP_SEED_ROWS_MAX];
static int s_ctSeedCount, s_ctSeedLegacy;

static int CT_ScanFromStatus(int state)
{
    switch (state)
    {
    case CTR_CT_MANAGER_READY: return AP_SEED_SCAN_READY;
    case CTR_CT_MANAGER_NOT_INSTALLED: return AP_SEED_SCAN_NOT_INSTALLED;
    case CTR_CT_MANAGER_MISSING_FILES: return AP_SEED_SCAN_MISSING_FILE;
    case CTR_CT_MANAGER_HASH_MISMATCH: return AP_SEED_SCAN_MISMATCH;
    case CTR_CT_MANAGER_MANIFEST_MISSING:
    case CTR_CT_MANAGER_MANIFEST_INVALID: return AP_SEED_SCAN_SETUP;
    case CTR_CT_MANAGER_UNSUPPORTED:
    case CTR_CT_MANAGER_INCOMPATIBLE: return AP_SEED_SCAN_REFUSED;
    case CTR_CT_MANAGER_IO_ERROR: return AP_SEED_SCAN_IO_ERROR;
    default: return AP_SEED_SCAN_UNKNOWN;
    }
}

/* The loaded seed's tracks: a content-plan room's list, or the one release-
   profile package a room without a plan names (the old package page). */
static void CT_BuildSeed(void)
{
    s_ctSeedLegacy = 0;
    if (AP_SeedContentActive())
        s_ctSeedCount = AP_SeedContentRows(s_ctSeedRows, AP_SEED_ROWS_MAX);
    else if (AP_CustomContentSeedSelected())
    {
        const struct CustomTrackManagerPackage *p = AP_CustomContentPackage();
        struct AP_SeedContentRow *r = &s_ctSeedRows[0];
        memset(r, 0, sizeof *r);
        snprintf(r->title, sizeof r->title, "%s", p->title);
        snprintf(r->author, sizeof r->author, "%s", p->author);
        snprintf(r->revision, sizeof r->revision, "%s", p->version);
        snprintf(r->folder, sizeof r->folder, "%s", p->id);
        r->scan = CT_ScanFromStatus(AP_CustomContentStatus()->state);
        r->source = AP_SEED_SOURCE_SAPHI;
        r->index = -2;
        r->saphi_track_id = CT_PackageTrackID();
        if (ap_track_permission(r->saphi_track_id, 0, AP_TRACK_PERMISSION_UNKNOWN, NULL) != AP_TRACK_PERMISSION_ALLOW)
        {
            r->source = AP_SEED_SOURCE_NOT_ALLOWED; /* A4-74 */
            snprintf(r->permission, sizeof r->permission, "%s", ap_track_permission_text());
        }
        r->locator.bigfile_index = -1;
        s_ctSeedCount = 1;
        s_ctSeedLegacy = 1;
    }
    else s_ctSeedCount = 0;
}

static const char *CT_SeedStateText(const struct AP_SeedContentRow *r)
{
    switch (r->scan)
    {
    case AP_SEED_SCAN_READY: return "Ready";
    case AP_SEED_SCAN_NOT_INSTALLED:
    case AP_SEED_SCAN_MISSING_FILE: return "Missing";
    case AP_SEED_SCAN_MISMATCH: return "Mismatch";
    case AP_SEED_SCAN_SETUP: return "Setup";
    case AP_SEED_SCAN_REFUSED: return "Refused";
    case AP_SEED_SCAN_IO_ERROR: return "Error";
    default: return "Unknown";
    }
}

/* The full state for the detail panel; the list uses the short word. */
static const char *CT_SeedStateLong(const struct AP_SeedContentRow *r)
{
    switch (r->scan)
    {
    case AP_SEED_SCAN_MISMATCH: return "Wrong version";
    case AP_SEED_SCAN_SETUP: return "Finish setup";
    case AP_SEED_SCAN_REFUSED: return "Unsupported";
    case AP_SEED_SCAN_IO_ERROR: return "Read error";
    default: return CT_SeedStateText(r);
    }
}

static int CT_SeedReady(void) { return ap_seed_ready_count(s_ctSeedRows, s_ctSeedCount); }

/* Installed Library revisions with the seed track's title: an explanation,
   never a Ready claim (the seed's pins decide). */
static int CT_CopiesText(const struct AP_SeedContentRow *r, char *out, size_t size)
{
    size_t i, used = 0;
    int found = 0;
    out[0] = 0;
    for (i = 0; i < s_contentLibrary.count && used + 1 < size; i++)
    {
        const struct CustomTrackLibraryEntry *e = &s_contentLibrary.entries[i];
        const char *a = e->title, *b = r->title;
        if (!e->installed && !e->scanFailed) continue;
        for (; *a && *b; a++, b++)
        {
            int ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : (unsigned char)*a;
            int cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : (unsigned char)*b;
            if (ca != cb) break;
        }
        if (*a || *b) continue;
        used += (size_t)snprintf(out + used, size - used, "%sv%.24s %s%s", found ? ". " : "", e->version,
                                 e->installed ? (e->local.xdelta ? "imported" : "installed") : "failed checks",
                                 e->installed && !strcmp(e->version, r->revision) ? ", the seed's version" : "");
        found++;
    }
    if (!found) snprintf(out, size, "No installed copy with this name.");
    else if (used + 1 < size)
        snprintf(out + used, size - used, ". The seed checks the exact files, not the name.");
    return found;
}

static int CT_SeedActions(const struct AP_SeedContentRow *r, struct CT_Action *a)
{
    int n = 0, state = ap_seed_row_state(r);
    const int busy = AP_SeedContentInstallBusy() || s_ctLegacyDownload;
    CT_Add(a, &n, CT_ACT_RESOLVE, "Resolve track",
           r->source == AP_SEED_SOURCE_BUNDLED && state == AP_SEED_STATE_READY ? "Comes with the client. Nothing to do." :
           busy ? "An install is running. Wait for it to finish." : "");
    CT_Add(a, &n, CT_ACT_SEED_VERIFY, "Verify / Setup", busy ? "An install is running. Wait for it to finish." : "");
    {
        int trackID = r->saphi_track_id;
        size_t i;
        for (i = 0; i < s_contentLibrary.count && !trackID; i++)
            if (!strcmp(s_contentLibrary.entries[i].title, r->title)) trackID = s_contentLibrary.entries[i].local.trackID;
        for (i = 0; i < CustomSaphi_Count(s_saphiCatalogue) && !trackID; i++)
            if (!strcmp(CustomSaphi_Row(s_saphiCatalogue, i)->title, r->title)) trackID = CustomSaphi_Row(s_saphiCatalogue, i)->trackID;
        CT_Add(a, &n, CT_ACT_SOURCE, "Open source page",
               r->index == -2 || trackID > 0 ? "" : r->source == AP_SEED_SOURCE_BUNDLED ? "Comes with the client." :
               "No source page is known for this track.");
    }
    CT_Add(a, &n, CT_ACT_FILES, "Required files", "");
    CT_Add(a, &n, CT_ACT_COPIES, "Installed copies", "");
    return n;
}

static void CT_Info(const char *title, const char *text)
{
    snprintf(s_ctInfoTitle, sizeof s_ctInfoTitle, "%s", title);
    snprintf(s_ctInfo, sizeof s_ctInfo, "%s", text);
    s_ctInfoTop = 0;
    s_ctInfoReturn = s_ctFocus;
    s_ctSub = CT_SUB_INFO;
    s_ctFocus = CT_FOCUS_SUB;
}

static void CT_RequiredFiles(const struct AP_SeedContentRow *r)
{
    char text[CT_TEXT_MAX], lev[24], vrm[24], instruction[256], locator[160], folder[AP_SEED_ID_MAX];
    ap_seed_format_bytes(r->lev_bytes, lev, sizeof lev);
    ap_seed_format_bytes(r->vrm_bytes, vrm, sizeof vrm);
    ap_seed_instruction(r, instruction, sizeof instruction);
    ap_seed_locator_line(r, locator, sizeof locator);
    ap_seed_plain_text(r->folder, folder, sizeof folder);
    if (r->source == AP_SEED_SOURCE_BUNDLED)
        snprintf(text, sizeof text, "Comes with the client in tracks/%s. %s", folder, instruction);
    else if (r->lev_bytes || r->vrm_bytes)
        snprintf(text, sizeof text, "Version %s. LEV %s and VRM %s with the exact hashes the seed pins. "
                 "Folder for files added by hand: tracks/%s/original. %s. %s",
                 r->revision, lev, vrm, folder, locator, instruction);
    else
        snprintf(text, sizeof text, "Version %s. The original LEV and VRM go in tracks/%s/original. %s",
                 r->revision, folder, instruction);
    CT_Info(r->title, text);
}

static void CT_SeedIdentity(void)
{
    char seed[96] = "", slot[64] = "", text[512];
    ap_net_seed_name(seed, sizeof seed);
    ap_net_slot_name(slot, sizeof slot);
    if (!s_ctSeedCount && !ap_net_is_connected())
        snprintf(text, sizeof text, "%s", seed[0] || AP_Solo_SessionActive() ?
                 "Seed data is not loaded yet. Wait for the room connection." :
                 "No room or Play Solo seed is loaded.");
    else
        snprintf(text, sizeof text, "%s. Seed %s. Slot %s. Needs %d custom track%s, %d ready.",
                 AP_Solo_SessionActive() ? "Play Solo seed" : "Connected room", seed[0] ? seed : "unknown",
                 slot[0] ? slot : "unknown", s_ctSeedCount, s_ctSeedCount == 1 ? "" : "s", CT_SeedReady());
    CT_Info("Seed identity", text);
}

static void CT_SeedRescan(void)
{
    if (AP_SeedContentInstallBusy() || s_ctLegacyDownload)
    {
        CT_SetMessage("Wait for the install to finish, then Rescan.");
        return;
    }
    AP_SeedContentRegistryRetry();
    AP_CustomContentRescan();
    CT_BuildSeed();
    snprintf(s_ctMessage, sizeof s_ctMessage, "Rescanned. Ready: %d / %d.", CT_SeedReady(), s_ctSeedCount);
}

/* Install missing: the overview first, nothing changes before Confirm. */
static void CT_InstallMissing(void)
{
    int plan[AP_SEED_ROWS_MAX], queued = 0;
    uint64_t bytes = 0;
    char reason[200];
    if (s_ctSeedLegacy)
    {
        if (ap_seed_row_state(&s_ctSeedRows[0]) == AP_SEED_STATE_READY) CT_SetMessage("Every track this seed needs is ready.");
        else if (s_ctSeedRows[0].source == AP_SEED_SOURCE_NOT_ALLOWED) CT_RequiredFiles(&s_ctSeedRows[0]);
        else CT_LegacyDownload();
        return;
    }
    if (AP_SeedContentInstallPreview(plan, &queued, &bytes, reason, sizeof reason) == AP_SEED_PLAN_OK)
    {
        s_ctSub = CT_SUB_INSTALL;
        s_ctFocus = CT_FOCUS_SUB;
        s_ctSubSel = 0;
        CT_SetMessage("");
    }
    else CT_SetMessage(reason);
}

static void CT_Resolve(const struct AP_SeedContentRow *r)
{
    if (r->index == -2)
    {
        if (ap_seed_row_state(r) == AP_SEED_STATE_READY) CT_SetMessage("Ready. Files verified.");
        else if (r->source == AP_SEED_SOURCE_NOT_ALLOWED) CT_RequiredFiles(r);
        else CT_LegacyDownload();
        return;
    }
    /* The shared store first: a rescan finds an installed copy with the
       seed's exact files. Then the seed's download, then the instructions. */
    if (ap_seed_row_state(r) != AP_SEED_STATE_READY) CT_SeedRescan();
    {
        int i;
        for (i = 0; i < s_ctSeedCount; i++)
            if (!strcmp(s_ctSeedRows[i].title, r->title) && !strcmp(s_ctSeedRows[i].revision, r->revision)) { r = &s_ctSeedRows[i]; break; }
    }
    if (ap_seed_row_state(r) == AP_SEED_STATE_READY) CT_SetMessage("Ready. The installed files match this seed.");
    else if (ap_seed_row_downloadable(r)) CT_InstallMissing();
    else CT_RequiredFiles(r);
}

/* ---- per-frame jobs ----------------------------------------------------- */

/* A Saphi install finished since the last frame: its outcome, a failure with
   its HTTP status, replaces "Downloading and checking..." once the frame's
   fresh-visit reset has run. */
static int s_ctSaphiOutcome;
static void CT_Poll(void)
{
    static int saphiWasDownloading;
    char detail[128];
    MM_CustomLibrary_Tick(0);
    MM_CustomManager_Poll();
    if (saphiWasDownloading && !s_saphiDownloading && s_saphiMessage[0]) s_ctSaphiOutcome = 1;
    saphiWasDownloading = s_saphiDownloading;
    /* The catalogue loads once per session on the first visit, as on the
       Track Manager; Refresh catalogue loads it again. */
    if (!s_saphiLoaded && !s_saphiRefreshBusy)
    {
        s_saphiLoaded = 1;
        s_saphiRefreshBusy = CustomSaphi_StartRefresh();
        s_saphiRefreshFailed = !s_saphiRefreshBusy;
        ap_track_consent_refresh_assets(NativeAssets_GetAssetDir()); /* A4-74 */
    }
    AP_SeedContentInstallTick(s_ctMessage, sizeof s_ctMessage);
    /* A finished refresh: the page controls show the catalogue state instead. */
    if (!s_saphiRefreshBusy && !strncmp(s_ctMessage, "Refreshing", 10)) s_ctMessage[0] = 0;
    if (!MM_CustomLibrary_Busy() && !strncmp(s_ctMessage, "Rescanning", 10))
        CT_SetMessage(s_contentStoreMessage[0] ? s_contentStoreMessage : "Installed track files checked.");
    if (s_ctLegacyDownload)
    {
        int state = AP_CustomContentDownloadStatus(detail, sizeof detail);
        CT_SetMessage(detail);
        if (state == AP_CT_DOWNLOAD_SUCCEEDED)
        {
            s_ctLegacyDownload = 0;
            AP_CustomContentVerify();
            CT_SetMessage(AP_CustomContentStatus()->detail);
        }
        else if (state == AP_CT_DOWNLOAD_FAILED) s_ctLegacyDownload = 0;
    }
}

static int CT_JobsBusy(void)
{
    return MM_CustomLibrary_Busy() || AP_SeedContentInstallBusy() || s_ctLegacyDownload;
}

/* Back belongs to the screen until the tab strip has focus. */
static int MM_CustomTracks_ConsumesBack(int section)
{
    return section == s_customContentSection &&
           (s_ctFocus != CT_FOCUS_TABS || MM_CustomImport_Active() || s_contentSearchEditing);
}

static void CT_GoTab(int tab)
{
    if (tab == s_ctTab) return;
    DecalFont_ResetLineBox(TEXT_SLOT_LIBRARY);
    s_ctTab = tab;
    s_ctSub = CT_SUB_NONE;
    s_ctMessage[0] = 0;
    OtherFX_Play(0, 1);
}

static void CT_OpenImport(void)
{
    if (s_ctLegacyDownload || AP_SeedContentInstallBusy())
    {
        CT_SetMessage("Wait for the install to finish, then import.");
        s_ctTab = CT_TAB_IMPORT;
        s_ctFocus = CT_FOCUS_TABS;
        return;
    }
    s_ctTab = CT_TAB_IMPORT;
    s_ctSub = CT_SUB_NONE;
    s_ctFocus = CT_FOCUS_LIST;
    MM_CustomImport_Open();
}

/* ---- drawing ------------------------------------------------------------ */

static void CT_DrawTabs(uint32_t *ot)
{
    static const int centre[CT_TAB_COUNT] = {140, 256, 372};
    int t;
    MM_CustomText_Draw("L1", 42, CT_TAB_Y, 28, 1, WHITE, ot);
    for (t = 0; t < CT_TAB_COUNT; t++)
    {
        DecalFont_DrawLineOT((char *)s_ctTabNames[t], centre[t], CT_TAB_Y, FONT_SMALL,
                             JUSTIFY_CENTER | (t == s_ctTab ? ORANGE : WHITE), ot);
        if (t == s_ctTab && s_ctFocus == CT_FOCUS_TABS)
        {
            int w = DecalFont_GetLineWidth((char *)s_ctTabNames[t], FONT_SMALL);
            RECT box = {centre[t] - w / 2 - 6, CT_TAB_Y - 2, w + 12, 12};
            CTR_Box_DrawClearBox(&box, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
        }
    }
    MM_CustomText_Draw("R1", 456, CT_TAB_Y, 28, 1, WHITE, ot);
}

static void CT_DrawPanels(uint32_t *ot)
{
    RECT listBox = {20, 64, 268, 108}, detailBox = {297, 64, 190, 108};
    RECTMENU_DrawInnerRect(&listBox, 0, ot);
    RECTMENU_DrawInnerRect(&detailBox, 0, ot);
}

static void CT_DrawActions(const struct CT_Action *a, int n, uint32_t *ot)
{
    int i, first = CT_First(s_ctActionSel);
    for (i = first; i < first + CT_ROWS && i < n; i++)
        CT_Row(i - first, a[i].label, i == s_ctActionSel, a[i].reason[0] ? GRAY : WHITE, ot);
    CT_ScrollBar(n, first, ot);
}

/* The detail panel under an action list: the title, then what the selected
   action does or why it cannot, then the last result. */
static void CT_DrawActionDetail(const char *title, const struct CT_Action *a, uint32_t *ot)
{
    CT_DrawPaged(title, CT_DETAIL_X, 70, CT_DETAIL_W, 2, ORANGE, ot);
    if (s_ctMessage[0]) CT_DrawPaged(s_ctMessage, CT_DETAIL_X, 98, CT_DETAIL_W, 6, WHITE, ot);
    else if (a && a->reason[0]) CT_DrawPaged(a->reason, CT_DETAIL_X, 98, CT_DETAIL_W, 6, GRAY, ot);
    else if (a) CT_DrawPaged(CT_ActionHelp(a), CT_DETAIL_X, 98, CT_DETAIL_W, 6, WHITE, ot);
}

/* ---- Library tab -------------------------------------------------------- */

static void CT_LibraryVersions(int trackID, size_t *rows, int *count)
{
    size_t i;
    *count = 0;
    for (i = 0; i < CustomSaphi_Count(s_saphiCatalogue) && *count < CTR_LIBRARY_MAX; i++)
        if (CustomSaphi_Row(s_saphiCatalogue, i)->trackID == trackID) rows[(*count)++] = i;
}

static void CT_LibraryFrame(int tapped, uint32_t *ot)
{
    static size_t versions[CTR_LIBRARY_MAX];
    struct CT_Action actions[CT_ACTIONS_MAX];
    struct CT_LibView v;
    const struct CT_LibItem *item = NULL;
    int nActions = 0, *sel = &s_ctListSel[CT_TAB_LIBRARY], i, first;
    char line[256], badges[64];

    CT_BuildLibrary();
    if (*sel >= s_ctItemCount) *sel = s_ctItemCount ? s_ctItemCount - 1 : 0;
    if (s_ctFocus == CT_FOCUS_LIST)
    {
        *sel = CT_Step(*sel, s_ctItemCount, tapped);
        if ((tapped & (BTN_CROSS | BTN_CIRCLE)) && s_ctItemCount)
        {
            s_ctFocus = CT_FOCUS_ACTIONS; s_ctActionSel = 0; s_ctMessage[0] = 0;
            OtherFX_Play(1, 1); tapped = 0;
        }
        else if (tapped & (BTN_TRIANGLE | BTN_START)) { s_ctFocus = CT_FOCUS_CONTROLS; OtherFX_Play(2, 1); tapped = 0; }
        else if (tapped & BTN_SQUARE) { s_ctFocus = CT_FOCUS_CONTROLS; tapped = 0; }
    }
    if (s_ctItemCount) { item = &s_ctItems[*sel]; CT_View(item, &v); nActions = CT_LibraryActions(item, &v, actions); }
    else if (s_ctFocus == CT_FOCUS_ACTIONS || s_ctFocus == CT_FOCUS_SUB) { s_ctFocus = CT_FOCUS_LIST; s_ctSub = CT_SUB_NONE; }
    CT_LibraryMarqueeOwner(item, item ? &v : NULL);

    /* Page controls: Filter, Search, Refresh catalogue, Rescan local files. */
    if (s_ctFocus == CT_FOCUS_CONTROLS)
    {
        int *c = &s_ctControlSel[CT_TAB_LIBRARY];
        *c = CT_Step(*c, 4, tapped);
        if (*c == 0 && (tapped & (BTN_LEFT | BTN_RIGHT)))
        {
            s_ctFilter = (s_ctFilter + ((tapped & BTN_LEFT) ? CT_FILTER_COUNT - 1 : 1)) % CT_FILTER_COUNT;
            *sel = 0; OtherFX_Play(0, 1);
        }
        if (tapped & (BTN_CROSS | BTN_CIRCLE))
        {
            OtherFX_Play(1, 1);
            if (*c == 0) { s_ctFilter = (s_ctFilter + 1) % CT_FILTER_COUNT; *sel = 0; }
            else if (*c == 1)
            {
                strcpy(s_contentSearchBackup, s_saphiQuery);
                NativeText_Begin(s_saphiQuery, sizeof s_saphiQuery, 28, CT_STATUS_Y, 456, 12, 0);
                s_contentSearchEditing = 1;
            }
            else if (*c == 2)
            {
                if (s_saphiRefreshBusy) CT_SetMessage("The catalogue is already loading.");
                else
                {
                    s_saphiRefreshBusy = CustomSaphi_StartRefresh();
                    s_saphiRefreshFailed = !s_saphiRefreshBusy;
                    ap_track_consent_refresh_assets(NativeAssets_GetAssetDir()); /* A4-74 */
                    CT_SetMessage(s_saphiRefreshBusy ? "Refreshing the Project Saphi catalogue..." : "Could not refresh the catalogue.");
                }
            }
            else
            {
                s_contentStoreWanted = 1;
                CT_SetMessage("Rescanning the installed track files...");
            }
        }
        if (tapped & (BTN_TRIANGLE | BTN_START)) { s_ctFocus = CT_FOCUS_TABS; OtherFX_Play(2, 1); }
        else if (tapped & BTN_SQUARE) { s_ctFocus = CT_FOCUS_LIST; OtherFX_Play(0, 1); }
    }
    else if (s_ctFocus == CT_FOCUS_ACTIONS && item)
    {
        s_ctActionSel = CT_Step(s_ctActionSel, nActions, tapped);
        if (tapped & (BTN_TRIANGLE | BTN_START)) { s_ctFocus = CT_FOCUS_LIST; s_ctMessage[0] = 0; OtherFX_Play(2, 1); }
        else if (tapped & (BTN_CROSS | BTN_CIRCLE))
        {
            const struct CT_Action *a = &actions[s_ctActionSel];
            OtherFX_Play(1, 1);
            if (a->reason[0]) CT_SetMessage(a->reason);
            else switch (a->id)
            {
            case CT_ACT_INSTALL:
                if (!v.allowed && item->kind == CT_ITEM_PACKAGE)
                {
                    AP_CustomContentVerify();
                    CT_SetMessage(AP_CustomContentStatus()->detail);
                }
                else if (!v.allowed) { s_contentStoreWanted = 1; CT_SetMessage("Checking the installed files again."); }
                else if (item->kind == CT_ITEM_PACKAGE) CT_LegacyDownload();
                else if (v.source) CT_StartSaphiInstall(v.source, v.exact);
                else { s_contentStoreWanted = 1; CT_SetMessage("Checking the installed files again."); }
                break;
            case CT_ACT_VERSIONS:
                s_ctSub = CT_SUB_VERSIONS; s_ctFocus = CT_FOCUS_SUB; s_ctSubSel = 0; s_ctMessage[0] = 0;
                break;
            case CT_ACT_SOURCE:
                if (item->kind == CT_ITEM_PACKAGE) CT_OpenURL(AP_CustomContentPackage()->sourceUrl);
                else CT_OpenTrackSource(CT_SourceTrackID(&v));
                break;
            case CT_ACT_VERIFY:
                AP_CustomContentVerify();
                CT_SetMessage(AP_CustomContentStatus()->detail);
                break;
            case CT_ACT_EXPORT:
                s_ctSub = CT_SUB_EXPORT; s_ctFocus = CT_FOCUS_SUB; s_ctSubSel = 0;
                CT_SetMessage("Copy puts the verified custom_tracks YAML on the clipboard. Save writes it to a file.");
                break;
            case CT_ACT_RELIC:
                s_ctSub = CT_SUB_RELIC; s_ctFocus = CT_FOCUS_SUB; CT_RelicOpen();
                break;
            }
        }
    }
    else if (s_ctFocus == CT_FOCUS_SUB && item)
    {
        if (s_ctSub == CT_SUB_VERSIONS)
        {
            int count;
            CT_LibraryVersions(v.trackID, versions, &count);
            s_ctSubSel = CT_Step(s_ctSubSel, count, tapped);
            if (tapped & (BTN_TRIANGLE | BTN_START)) { s_ctFocus = CT_FOCUS_ACTIONS; s_ctSub = CT_SUB_NONE; s_ctMessage[0] = 0; OtherFX_Play(2, 1); }
            else if ((tapped & (BTN_CROSS | BTN_CIRCLE)) && count)
            {
                const struct CustomSaphiRevision *r = CustomSaphi_Row(s_saphiCatalogue, versions[s_ctSubSel]);
                int older;
                OtherFX_Play(1, 1);
                if (s_saphiDownloading) CT_SetMessage("A download is running. Wait for it to finish.");
                else CT_StartSaphiInstall(r, MM_CustomSourceInstalled(r, &older));
            }
        }
        else if (s_ctSub == CT_SUB_EXPORT)
        {
            s_ctSubSel = CT_Step(s_ctSubSel, 2, tapped);
            if (tapped & (BTN_TRIANGLE | BTN_START)) { s_ctFocus = CT_FOCUS_ACTIONS; s_ctSub = CT_SUB_NONE; OtherFX_Play(2, 1); }
            else if (tapped & (BTN_CROSS | BTN_CIRCLE)) { OtherFX_Play(1, 1); CT_Export(s_ctSubSel); }
        }
        else if (s_ctSub == CT_SUB_RELIC)
        {
            if (CT_RelicInput(tapped)) { s_ctFocus = CT_FOCUS_ACTIONS; s_ctSub = CT_SUB_NONE; }
        }
    }

    /* Draw. */
    if (s_ctFocus == CT_FOCUS_CONTROLS)
    {
        static const char *const help[4] = {
            "Left/Right or Confirm changes the filter. All lists installed tracks and the catalogue; "
            "Installed only the track store; Browse the Project Saphi catalogue.",
            "Search titles, authors and installed versions. Back on the keyboard keeps the old search.",
            "Load the Project Saphi catalogue again. Installed tracks stay listed if it fails.",
            "Check the installed track files again. Rooms, Play Solo and Arcade use the same copies."};
        const int c = s_ctControlSel[CT_TAB_LIBRARY];
        snprintf(line, sizeof line, "Filter: %s", s_ctFilterNames[s_ctFilter]);
        CT_Row(0, line, c == 0, WHITE, ot);
        snprintf(line, sizeof line, "Search: %s", s_saphiQuery[0] ? s_saphiQuery : "none");
        CT_Row(1, line, c == 1, WHITE, ot);
        CT_Row(2, "Refresh catalogue", c == 2, WHITE, ot);
        CT_Row(3, "Rescan local files", c == 3, WHITE, ot);
        CT_DrawPaged(help[c], CT_DETAIL_X, 70, CT_DETAIL_W, 4, WHITE, ot);
        {
            const char *state = s_contentStoreMessage[0] ? s_contentStoreMessage :
                s_ctMessage[0] ? s_ctMessage : s_saphiRefreshBusy ? "Catalogue: loading..." :
                !s_saphiCatalogue ? "Catalogue: unavailable. Installed tracks still listed." :
                s_saphiRefreshFailed ? "Catalogue: offline, showing the last copy loaded." : "Catalogue: loaded.";
            /* A4-74: the permission list's state after the catalogue's. */
            char permission[200], text[CT_TEXT_MAX];
            CT_PermissionLine(permission, sizeof permission);
            snprintf(text, sizeof text, "%s%s%s", state, permission[0] ? " " : "", permission);
            CT_DrawPaged(text, CT_DETAIL_X, 122, CT_DETAIL_W, 4, ORANGE, ot);
        }
    }
    else if (s_ctFocus == CT_FOCUS_ACTIONS && item)
    {
        CT_DrawActions(actions, nActions, ot);
        CT_DrawActionDetail(v.title, &actions[s_ctActionSel], ot);
    }
    else if (s_ctFocus == CT_FOCUS_SUB && item && s_ctSub == CT_SUB_VERSIONS)
    {
        int count;
        CT_LibraryVersions(v.trackID, versions, &count);
        first = CT_First(s_ctSubSel);
        for (i = first; i < first + CT_ROWS && i < count; i++)
        {
            const struct CustomSaphiRevision *r = CustomSaphi_Row(s_saphiCatalogue, versions[i]);
            int older;
            snprintf(line, sizeof line, "v%.32s %s", r->version, (r->modeTags & 1) ? "Arcade" : (r->modeTags & 2) ? "TT" : "Other");
            CT_RowState(i - first, line, MM_CustomSourceInstalled(r, &older) ? "Installed" : r->disabledReason[0] ? "Off" : "",
                        i == s_ctSubSel, WHITE, ot);
        }
        CT_ScrollBar(count, first, ot);
        if (count)
        {
            const struct CustomSaphiRevision *r = CustomSaphi_Row(s_saphiCatalogue, versions[s_ctSubSel]);
            int older;
            CT_DrawPaged(r->title, CT_DETAIL_X, 70, CT_DETAIL_W, 2, ORANGE, ot);
            snprintf(line, sizeof line, "v%s%s", r->version, r->current ? ", current" : "");
            MM_CustomText_Draw(line, CT_DETAIL_X, 98, CT_DETAIL_W, 1, WHITE, ot);
            CT_DrawPaged(s_ctMessage[0] ? s_ctMessage : r->disabledReason[0] ? r->disabledReason :
                         MM_CustomSourceInstalled(r, &older) ? "Installed. Confirm checks the files again." :
                         "Confirm downloads this exact version. A room keeps the version it pins.",
                         CT_DETAIL_X, 112, CT_DETAIL_W, 5, WHITE, ot);
        }
    }
    else if (s_ctFocus == CT_FOCUS_SUB && item && s_ctSub == CT_SUB_EXPORT)
    {
        CT_Row(0, "Copy YAML", s_ctSubSel == 0, WHITE, ot);
        CT_Row(1, "Save YAML", s_ctSubSel == 1, WHITE, ot);
        CT_DrawActionDetail(v.title, NULL, ot);
    }
    else if (s_ctFocus == CT_FOCUS_SUB && item && s_ctSub == CT_SUB_RELIC)
    {
        CT_RelicDraw(ot);
        CT_DrawActionDetail("Relic times", NULL, ot);
    }
    else
    {
        first = CT_First(*sel);
        for (i = first; i < first + CT_ROWS && i < s_ctItemCount; i++)
        {
            struct CT_LibView row;
            CT_View(&s_ctItems[i], &row);
            CT_Row(i - first, row.title, i == *sel && s_ctFocus == CT_FOCUS_LIST, WHITE, ot);
        }
        CT_ScrollBar(s_ctItemCount, first, ot);
        if (!s_ctItemCount)
            MM_CustomText_Draw(s_saphiQuery[0] ? "No search matches" :
                               s_ctFilter == CT_FILTER_INSTALLED ? "No installed tracks. Import or Browse to add some." :
                               s_saphiRefreshBusy ? "Loading Project Saphi..." :
                               s_ctNotAllowed ? CT_PermissionEmpty() :
                               s_ctFilter == CT_FILTER_BROWSE ? "Catalogue unavailable. Refresh it from the page controls." :
                               "No tracks yet. Refresh the catalogue or import a patch.",
                               CT_LIST_X, 84, CT_LIST_W, 3, WHITE, ot);
        if (s_ctFocus == CT_FOCUS_TABS && !s_saphiDownloading && !s_ctLegacyDownload &&
            !strstr(s_saphiMessage, "publication failed"))
        {
            CT_DrawPaged("Local tracks and Saphi catalogue.", CT_DETAIL_X, 70, CT_DETAIL_W, 4, WHITE, ot);
            CT_DrawPaged(s_saphiRefreshBusy ? "Loading Project Saphi..." :
                         s_saphiRefreshFailed ? "Catalogue failed. Use controls to refresh." :
                         "Open for tracks. Controls to browse.",
                         CT_DETAIL_X, 122, CT_DETAIL_W, 4, ORANGE, ot);
        }
        else if (item)
        {
            CT_DrawPaged(v.title, CT_DETAIL_X, 70, CT_DETAIL_W, 2, ORANGE, ot);
            snprintf(line, sizeof line, "v%.32s", v.version);
            MM_CustomText_Draw(line, CT_DETAIL_X, 96, CT_DETAIL_W, 1, WHITE, ot);
            MM_CustomText_Draw(v.author && v.author[0] ? v.author : "Unknown author", CT_DETAIL_X, 108, CT_DETAIL_W, 1, WHITE, ot);
            CT_Badges(&v, item->kind, badges, sizeof badges);
            MM_CustomText_Draw(badges, CT_DETAIL_X, 120, CT_DETAIL_W, 1, ORANGE, ot);
            {
                const struct CustomTrackLibraryEntry *e = v.installed;
                const char *why = e && !e->installed ? e->disabledReason :
                    e && !e->local.arcade ? (e->local.timeTrial ? e->local.arcadeReason : e->local.timeTrialReason) : "";
                const char *status = s_saphiDownloading ? s_saphiMessage :
                    strstr(s_saphiMessage, "publication failed") ? s_saphiMessage :
                    !v.allowed ? CT_PermissionShort() :
                    item->kind == CT_ITEM_PACKAGE ? AP_CustomContentStatus()->detail :
                    v.source && v.source->disabledReason[0] && !e ? v.source->disabledReason :
                    v.older && !v.exact && v.source ? "Update available" : "";
                snprintf(line, sizeof line, "%s%s%s", CT_ModesText(&v), why && why[0] ? ". " : "", why ? why : "");
                const int bad = (v.source && v.source->disabledReason[0] && !e) || strstr(s_saphiMessage, "failed") ||
                                (item->kind == CT_ITEM_PACKAGE && AP_CustomContentStatus()->state != CTR_CT_MANAGER_READY);
                CT_DrawPaged(status[0] ? status : line, CT_DETAIL_X, 132, CT_DETAIL_W, 2, status[0] && bad ? RED : WHITE, ot);
            }
            MM_CustomText_Draw(NativeMenu_Labels("*: Actions"), CT_DETAIL_X, 159 + 2, CT_DETAIL_W, 1, ORANGE, ot);
        }
    }

    if (!s_contentSearchEditing)
    {
        snprintf(line, sizeof line, "Filter: %s   Search: %.24s   %d/%d", s_ctFilterNames[s_ctFilter],
                 s_saphiQuery[0] ? s_saphiQuery : "none", s_ctItemCount ? *sel + 1 : 0, s_ctItemCount);
        if (s_saphiRefreshBusy) snprintf(line, sizeof line, "Loading Saphi catalogue...");
        else if (s_saphiRefreshFailed) snprintf(line, sizeof line, "Saphi refresh failed. Retry Refresh.");
        CT_Footer(line, NULL, ot);
    }
    CT_Footer(NULL, s_ctFocus == CT_FOCUS_TABS ? NULL : s_ctFocus == CT_FOCUS_LIST ? NativeMenu_Labels("*: Actions  [: Controls  ^: Back") :
                    s_ctFocus == CT_FOCUS_CONTROLS ? NativeMenu_Labels("*: Select   [: Tracks   ^: Tabs") :
                    s_ctFocus == CT_FOCUS_ACTIONS ? NativeMenu_Labels("*: Select   ^: Tracks") :
                    s_ctSub == CT_SUB_RELIC ? NativeMenu_Labels("Left/Right: Move   ^: Cancel") : NativeMenu_Labels("*: Select   ^: Back"), ot);
}

/* ---- This Seed tab ------------------------------------------------------ */

static void CT_SeedFrame(int tapped, uint32_t *ot)
{
    struct CT_Action actions[CT_ACTIONS_MAX];
    const struct AP_SeedContentRow *r = NULL;
    int nActions = 0, *sel = &s_ctListSel[CT_TAB_SEED], i, first;
    char line[256], size[24], seed[96] = "";

    CT_BuildSeed();
    if (*sel >= s_ctSeedCount) *sel = s_ctSeedCount ? s_ctSeedCount - 1 : 0;
    if (s_ctFocus == CT_FOCUS_LIST)
    {
        *sel = CT_Step(*sel, s_ctSeedCount, tapped);
        if ((tapped & (BTN_CROSS | BTN_CIRCLE)) && s_ctSeedCount)
        {
            s_ctFocus = CT_FOCUS_ACTIONS; s_ctActionSel = 0; s_ctMessage[0] = 0;
            OtherFX_Play(1, 1); tapped = 0;
        }
        else if (tapped & (BTN_TRIANGLE | BTN_START)) { s_ctFocus = CT_FOCUS_CONTROLS; OtherFX_Play(2, 1); tapped = 0; }
    }
    if (s_ctSeedCount) { r = &s_ctSeedRows[*sel]; nActions = CT_SeedActions(r, actions); }
    else if (s_ctFocus == CT_FOCUS_ACTIONS || (s_ctFocus == CT_FOCUS_SUB && s_ctSub != CT_SUB_INFO))
    {
        s_ctFocus = CT_FOCUS_CONTROLS; s_ctSub = CT_SUB_NONE;
    }

    if (s_ctFocus == CT_FOCUS_CONTROLS)
    {
        int *c = &s_ctControlSel[CT_TAB_SEED];
        *c = CT_Step(*c, 3, tapped);
        if (tapped & (BTN_CROSS | BTN_CIRCLE))
        {
            OtherFX_Play(1, 1);
            if (*c == 0) CT_InstallMissing();
            else if (*c == 1) CT_SeedRescan();
            else CT_SeedIdentity();
        }
        else if (tapped & (BTN_TRIANGLE | BTN_START)) { s_ctFocus = CT_FOCUS_TABS; OtherFX_Play(2, 1); }
        else if ((tapped & BTN_SQUARE) && s_ctSeedCount) { s_ctFocus = CT_FOCUS_LIST; OtherFX_Play(0, 1); }
    }
    else if (s_ctFocus == CT_FOCUS_ACTIONS && r)
    {
        s_ctActionSel = CT_Step(s_ctActionSel, nActions, tapped);
        if (tapped & (BTN_TRIANGLE | BTN_START)) { s_ctFocus = CT_FOCUS_LIST; s_ctMessage[0] = 0; OtherFX_Play(2, 1); }
        else if (tapped & (BTN_CROSS | BTN_CIRCLE))
        {
            const struct CT_Action *a = &actions[s_ctActionSel];
            OtherFX_Play(1, 1);
            if (a->reason[0]) CT_SetMessage(a->reason);
            else switch (a->id)
            {
            case CT_ACT_RESOLVE: CT_Resolve(r); break;
            case CT_ACT_SEED_VERIFY:
                AP_CustomContentVerify();
                CT_BuildSeed();
                if (r->index == -2) CT_SetMessage(AP_CustomContentStatus()->detail);
                else snprintf(s_ctMessage, sizeof s_ctMessage, "Checked. %s: %s.", r->title, CT_SeedStateText(r));
                break;
            case CT_ACT_SOURCE:
                if (r->index == -2) CT_OpenURL(AP_CustomContentPackage()->sourceUrl);
                else
                {
                    int trackID = r->saphi_track_id;
                    size_t k;
                    for (k = 0; k < s_contentLibrary.count && !trackID; k++)
                        if (!strcmp(s_contentLibrary.entries[k].title, r->title)) trackID = s_contentLibrary.entries[k].local.trackID;
                    for (k = 0; k < CustomSaphi_Count(s_saphiCatalogue) && !trackID; k++)
                        if (!strcmp(CustomSaphi_Row(s_saphiCatalogue, k)->title, r->title)) trackID = CustomSaphi_Row(s_saphiCatalogue, k)->trackID;
                    CT_OpenTrackSource(trackID);
                }
                break;
            case CT_ACT_FILES: CT_RequiredFiles(r); break;
            case CT_ACT_COPIES:
            {
                char copies[CT_TEXT_MAX];
                CT_CopiesText(r, copies, sizeof copies);
                CT_Info(r->title, copies);
                break;
            }
            }
        }
    }
    else if (s_ctFocus == CT_FOCUS_SUB && s_ctSub == CT_SUB_INSTALL)
    {
        if (tapped & (BTN_TRIANGLE | BTN_START)) { s_ctSub = CT_SUB_NONE; s_ctFocus = CT_FOCUS_CONTROLS; CT_SetMessage("Nothing was installed."); OtherFX_Play(2, 1); }
        else if (tapped & (BTN_CROSS | BTN_CIRCLE))
        {
            s_ctSub = CT_SUB_NONE; s_ctFocus = CT_FOCUS_CONTROLS;
            OtherFX_Play(1, 1);
            AP_SeedContentInstallStart(s_ctMessage, sizeof s_ctMessage);
        }
    }

    /* Draw. */
    if (s_ctFocus == CT_FOCUS_SUB && s_ctSub == CT_SUB_INSTALL)
    {
        int plan[AP_SEED_ROWS_MAX], queued = 0, manual = 0, cached = 0;
        uint64_t bytes = 0;
        char reason[200];
        if (AP_SeedContentInstallPreview(plan, &queued, &bytes, reason, sizeof reason) != AP_SEED_PLAN_OK)
        {
            /* Something changed while the overview was open: show why and close. */
            CT_SetMessage(reason);
            s_ctSub = CT_SUB_NONE; s_ctFocus = CT_FOCUS_CONTROLS;
        }
        else
        {
            for (i = 0; i < queued && i < CT_ROWS; i++)
            {
                const struct AP_SeedContentRow *q = &s_ctSeedRows[plan[i]];
                ap_seed_format_bytes((uint64_t)q->lev_bytes + q->vrm_bytes, size, sizeof size);
                snprintf(line, sizeof line, "%.48s v%.24s", q->title, q->revision);
                CT_RowState(i, i == CT_ROWS - 1 && queued > CT_ROWS ? "and more" : line, size, 0, WHITE, ot);
            }
            for (i = 0; i < s_ctSeedCount; i++)
                if (ap_seed_row_state(&s_ctSeedRows[i]) != AP_SEED_STATE_READY && !ap_seed_row_downloadable(&s_ctSeedRows[i])) manual++;
            for (i = 0; i < queued; i++)
                if (s_ctSeedRows[plan[i]].registry == AP_SEED_REGISTRY_CACHED) cached++;
            ap_seed_format_bytes(bytes, size, sizeof size);
            snprintf(line, sizeof line, "Install missing: %d track%s, %s from Project Saphi.%s%s", queued, queued == 1 ? "" : "s", size,
                     manual ? " Tracks without a download need their files added by hand." : "",
                     cached ? " Locations from the cached registry." : "");
            CT_DrawPaged(line, CT_DETAIL_X, 70, CT_DETAIL_W, 7, WHITE, ot);
        }
    }
    else if (s_ctFocus == CT_FOCUS_CONTROLS)
    {
        static const char *const help[3] = {
            "Download every missing track this seed can get from Project Saphi. You see the list first.",
            "Check the track files and the track registry again.",
            "Which room or Play Solo seed this list is for."};
        const int c = s_ctControlSel[CT_TAB_SEED];
        CT_Row(0, "Install missing", c == 0, WHITE, ot);
        CT_Row(1, "Rescan", c == 1, WHITE, ot);
        CT_Row(2, "Seed identity", c == 2, WHITE, ot);
        CT_DrawPaged(help[c], CT_DETAIL_X, 70, CT_DETAIL_W, 3, WHITE, ot);
        if (s_ctMessage[0]) CT_DrawPaged(s_ctMessage, CT_DETAIL_X, 110, CT_DETAIL_W, 5, ORANGE, ot);
    }
    else if (s_ctFocus == CT_FOCUS_ACTIONS && r)
    {
        CT_DrawActions(actions, nActions, ot);
        CT_DrawActionDetail(r->title, &actions[s_ctActionSel], ot);
    }
    else
    {
        first = CT_First(*sel);
        for (i = first; i < first + CT_ROWS && i < s_ctSeedCount; i++)
        {
            const struct AP_SeedContentRow *q = &s_ctSeedRows[i];
            CT_RowState(i - first, q->title, CT_SeedStateText(q), i == *sel && s_ctFocus == CT_FOCUS_LIST,
                        ap_seed_row_state(q) == AP_SEED_STATE_READY ? WHITE : RED, ot);
        }
        CT_ScrollBar(s_ctSeedCount, first, ot);
        if (!s_ctSeedCount)
        {
            ap_net_seed_name(seed, sizeof seed);
            MM_CustomText_Draw(ap_net_is_connected() ?
                               "This seed needs no custom tracks. Retail tracks are ready to play." :
                               seed[0] || AP_Solo_SessionActive() ?
                               "Seed data is not loaded yet. Wait for the room connection." :
                               "No seed loaded. Connect to a room or start a Play Solo seed to see its tracks.",
                               CT_LIST_X, 72, CT_LIST_W, 7, WHITE, ot);
            CT_DrawPaged("Only required custom tracks. Library and Import need no seed.", CT_DETAIL_X, 70, CT_DETAIL_W, 7, WHITE, ot);
        }
        if (r)
        {
            char instruction[256], about[400];
            const int ready = ap_seed_row_state(r) == AP_SEED_STATE_READY;
            CT_DrawPaged(r->title, CT_DETAIL_X, 70, CT_DETAIL_W, 2, ORANGE, ot);
            snprintf(line, sizeof line, "Needs v%.40s", r->revision);
            MM_CustomText_Draw(r->source == AP_SEED_SOURCE_BUNDLED ? "Bundled" : line, CT_DETAIL_X, 96, CT_DETAIL_W, 1, WHITE, ot);
            MM_CustomText_Draw(r->source == AP_SEED_SOURCE_BUNDLED ? "With the client" : r->author[0] ? r->author : "Unknown author",
                               CT_DETAIL_X, 108, CT_DETAIL_W, 1, WHITE, ot);
            MM_CustomText_Draw(CT_SeedStateLong(r), CT_DETAIL_X, 120, CT_DETAIL_W, 1, ready ? ORANGE : RED, ot);
            ap_seed_instruction(r, instruction, sizeof instruction);
            if (r->index == -2 && s_ctLegacyDownload) snprintf(instruction, sizeof instruction, "%s", s_ctMessage);
            ap_seed_format_bytes((uint64_t)r->lev_bytes + r->vrm_bytes, size, sizeof size);
            if (r->source == AP_SEED_SOURCE_BUNDLED || ready) snprintf(about, sizeof about, "%s", instruction);
            else snprintf(about, sizeof about, "%s%s%s. %s", r->lev_bytes ? size : "", r->lev_bytes ? ", " : "",
                          ap_seed_source_text(r), instruction);
            CT_DrawPaged(about, CT_DETAIL_X, 132, CT_DETAIL_W, 2, WHITE, ot);
            MM_CustomText_Draw(NativeMenu_Labels("*: Actions"), CT_DETAIL_X, 161, CT_DETAIL_W, 1, ORANGE, ot);
        }
    }

    ap_net_seed_name(seed, sizeof seed);
    if (!s_ctSeedCount && !ap_net_is_connected() && (seed[0] || AP_Solo_SessionActive()))
        snprintf(line, sizeof line, "Waiting for seed data");
    else if (s_ctSeedCount || ap_net_is_connected())
    {
        char full[256];
        snprintf(full, sizeof full, "Ready %d/%d   %s %s", CT_SeedReady(), s_ctSeedCount,
                 AP_Solo_SessionActive() ? "Play Solo" : "Room", seed[0] ? seed : "connected");
        CT_Fit(full, line, sizeof line, 456);
    }
    else snprintf(line, sizeof line, "No seed loaded");
    if (AP_SeedContentInstallBusy() || s_ctLegacyDownload) snprintf(line, sizeof line, "Installing...");
    CT_Footer(line, s_ctFocus == CT_FOCUS_TABS ? NULL : s_ctFocus == CT_FOCUS_LIST ? NativeMenu_Labels("*: Actions   ^: Page controls") :
                    s_ctFocus == CT_FOCUS_CONTROLS ? (s_ctSeedCount ? NativeMenu_Labels("*: Select   [: Tracks   ^: Tabs") : NativeMenu_Labels("*: Select   ^: Tabs")) :
                    s_ctFocus == CT_FOCUS_ACTIONS ? NativeMenu_Labels("*: Select   ^: Tracks") :
                    s_ctSub == CT_SUB_INSTALL ? NativeMenu_Labels("*: Install   ^: Cancel") : NativeMenu_Labels("^: Back"), ot);
}

/* ---- Import tab --------------------------------------------------------- */

static void CT_ImportFrame(int tapped, uint32_t *ot, struct GamepadBuffer *pad)
{
    if (MM_CustomImport_Active())
    {
        /* The importer owns the pad and both panels until it closes; its
           result screen closing leads to the next steps below. */
        const int fromResult = s_importStage == IMPORT_RESULT;
        if (s_importStage == IMPORT_CHOOSE)
            MM_CustomText_Draw(NativeMenu_Labels("^: Tabs"), 256, CT_STATUS_Y, 456, 1, JUSTIFY_CENTER | WHITE, ot);
        MM_CustomImport_Proc(ot, pad);
        if (!MM_CustomImport_Active())
        {
            if (fromResult)
            {
                s_ctSub = CT_SUB_IMPORT_DONE; s_ctFocus = CT_FOCUS_SUB; s_ctSubSel = 0;
                s_contentStoreWanted = 1; /* the Library shows the new tracks */
            }
            else s_ctFocus = CT_FOCUS_TABS;
        }
        return;
    }
    if (s_ctFocus == CT_FOCUS_SUB && s_ctSub == CT_SUB_IMPORT_DONE)
    {
        static const char *const rows[3] = {"Show in Library", "Go to This Seed", "Import another"};
        s_ctSubSel = CT_Step(s_ctSubSel, 3, tapped);
        if (tapped & (BTN_TRIANGLE | BTN_START)) { s_ctSub = CT_SUB_NONE; s_ctFocus = CT_FOCUS_TABS; OtherFX_Play(2, 1); return; }
        if (tapped & (BTN_CROSS | BTN_CIRCLE))
        {
            OtherFX_Play(1, 1);
            s_ctSub = CT_SUB_NONE;
            if (s_ctSubSel == 0)
            {
                s_ctTab = CT_TAB_LIBRARY; s_ctFilter = CT_FILTER_INSTALLED; s_ctFocus = CT_FOCUS_LIST;
                s_contentStoreWanted = 1;
            }
            else if (s_ctSubSel == 1)
            {
                s_ctTab = CT_TAB_SEED; s_ctFocus = CT_FOCUS_LIST;
                CT_SeedRescan();
            }
            else CT_OpenImport();
            return;
        }
        for (int i = 0; i < 3; i++) CT_Row(i, rows[i], i == s_ctSubSel, WHITE, ot);
        CT_DrawPaged("Import done", CT_DETAIL_X, 70, CT_DETAIL_W, 1, ORANGE, ot);
        CT_DrawPaged(s_importMessage, CT_DETAIL_X, 86, CT_DETAIL_W, 7, WHITE, ot);
        CT_Footer(NULL, NativeMenu_Labels("*: Select   ^: Tabs"), ot);
        return;
    }
    if (s_ctFocus == CT_FOCUS_LIST || s_ctFocus == CT_FOCUS_CONTROLS) CT_OpenImport();
    /* The tab strip shows what the tab does. */
    MM_CustomText_Draw("Import an xdelta patch using your own game disc. Open to choose a patch file.", CT_LIST_X, 72, CT_LIST_W, 7, WHITE, ot);
    CT_DrawPaged(s_ctMessage[0] ? s_ctMessage :
                 "Choose a file, scan assets/tracks/import, or drop a patch. Confirm after review.",
                 CT_DETAIL_X, 70, CT_DETAIL_W, 8, WHITE, ot);
}

/* ---- the page ----------------------------------------------------------- */

static void MM_ConfigProc_CustomTracks(struct RectMenu *menu, uint32_t *ot, struct GamepadBuffer *pad)
{
    const unsigned int now = (unsigned int)sdata->gGT->timer;
    int tapped = pad->buttonsTapped;
    int rawPad = Platform_InputRawGamepadButtons();
    int rawTapped = rawPad & ~s_contentSearchPadPrev;
    (void)menu;
    s_contentSearchPadPrev = rawPad;
    s_ctPageClock = tapped ? 0 : s_ctPageClock + 1;

    CT_Poll();
    CT_BuildSeed();
    /* A fresh visit: This Seed when the loaded seed misses tracks, otherwise
       Library (ruled 2026-11-04 19:38 CET). */
    if (now - s_ctLastFrame > 2 && !MM_CustomImport_Active() && !s_contentSearchEditing)
    {
        s_ctTab = s_ctSeedCount && CT_SeedReady() < s_ctSeedCount ? CT_TAB_SEED : CT_TAB_LIBRARY;
        s_ctFocus = CT_FOCUS_TABS;
        s_ctSub = CT_SUB_NONE;
        if (!AP_SeedContentInstallBusy() && !s_ctLegacyDownload) s_ctMessage[0] = 0;
    }
    s_ctLastFrame = now;
    if (s_ctSaphiOutcome) { CT_SetMessage(s_saphiMessage); s_ctSaphiOutcome = 0; }

    /* Search text entry (Library page controls): physical-pad commit/cancel,
       the old query back on cancel. */
    if (s_contentSearchEditing)
    {
        if (!NativeText_Result())
        {
            if (rawTapped & CONN_PAD_CANCEL) NativeText_Resolve(2);
            else if (rawTapped & CONN_PAD_COMMIT) NativeText_Resolve(1);
        }
        if (NativeText_Result())
        {
            if (NativeText_Result() == 2) strcpy(s_saphiQuery, s_contentSearchBackup);
            NativeText_End();
            s_contentSearchEditing = 0;
            s_ctListSel[CT_TAB_LIBRARY] = 0;
        }
        tapped = 0;
    }

    /* A patch dropped on the window opens Import with it, unless a job runs. */
    if (!s_contentSearchEditing && !CT_JobsBusy() && s_ctSub != CT_SUB_RELIC && MM_CustomImport_TakeDrop())
    {
        s_ctTab = CT_TAB_IMPORT; s_ctFocus = CT_FOCUS_LIST; s_ctSub = CT_SUB_NONE;
        tapped = 0;
    }
    /* Select stays a shortcut to Import (seed instructions name it). */
    if ((tapped & BTN_SELECT) && !MM_CustomImport_Active() && s_ctSub != CT_SUB_RELIC && !s_contentSearchEditing)
    {
        CT_OpenImport();
        OtherFX_Play(1, 1);
        tapped = 0;
    }

    if (s_ctFocus == CT_FOCUS_TABS && !MM_CustomImport_Active())
    {
        if (tapped & (BTN_LEFT | BTN_L1)) CT_GoTab((s_ctTab + CT_TAB_COUNT - 1) % CT_TAB_COUNT);
        else if (tapped & (BTN_RIGHT | BTN_R1)) CT_GoTab((s_ctTab + 1) % CT_TAB_COUNT);
        else if (tapped & (BTN_DOWN | BTN_CROSS | BTN_CIRCLE))
        {
            s_ctFocus = s_ctTab == CT_TAB_SEED && !s_ctSeedCount ? CT_FOCUS_CONTROLS : CT_FOCUS_LIST;
            OtherFX_Play(1, 1);
        }
        tapped = 0; /* Back on the tab strip is the Options menu's */
    }
    else if ((tapped & (BTN_L1 | BTN_R1)) && !MM_CustomImport_Active() &&
             (s_ctFocus == CT_FOCUS_LIST || s_ctFocus == CT_FOCUS_CONTROLS))
    {
        /* Shoulders change tabs from a list too; never needed. */
        CT_GoTab((s_ctTab + ((tapped & BTN_L1) ? CT_TAB_COUNT - 1 : 1)) % CT_TAB_COUNT);
        s_ctFocus = CT_FOCUS_TABS;
        tapped = 0;
    }

    /* Info views (required files, installed copies, seed identity) scroll
       with Up / Down and close with Back. */
    if (s_ctFocus == CT_FOCUS_SUB && s_ctSub == CT_SUB_INFO)
    {
        static char wrapped[80][128];
        const int lines = CT_Wrap(s_ctInfo, 452, wrapped, 80);
        int i;
        if ((tapped & BTN_DOWN) && s_ctInfoTop + 7 < lines) s_ctInfoTop++;
        if ((tapped & BTN_UP) && s_ctInfoTop > 0) s_ctInfoTop--;
        if (tapped & (BTN_TRIANGLE | BTN_START | BTN_CROSS | BTN_CIRCLE))
        {
            s_ctSub = CT_SUB_NONE;
            s_ctFocus = s_ctInfoReturn == CT_FOCUS_ACTIONS && s_ctSeedCount ? CT_FOCUS_ACTIONS : CT_FOCUS_CONTROLS;
            OtherFX_Play(2, 1);
        }
        else
        {
            DecalFont_DrawLineOT("CUSTOM TRACKS", 256, 24, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
            CT_DrawTabs(ot);
            MM_CustomText_Draw(s_ctInfoTitle, 32, 70, 452, 1, ORANGE, ot);
            for (i = 0; i < 7 && s_ctInfoTop + i < lines; i++)
                DecalFont_DrawLineOT(wrapped[s_ctInfoTop + i], 32, 84 + i * CT_LINE_H, FONT_SMALL, WHITE, ot);
            CT_Footer(lines > 7 ? "Up/Down: Scroll" : NULL, NativeMenu_Labels("^: Back"), ot);
            {
                RECT box = {20, 64, 467, 108};
                RECTMENU_DrawInnerRect(&box, 0, ot);
            }
            return;
        }
        tapped = 0;
    }

    if (!(s_ctTab == CT_TAB_IMPORT && MM_CustomImport_Active()))
        DecalFont_DrawLineOT("CUSTOM TRACKS", 256, 24, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
    CT_DrawTabs(ot);
    if (s_ctTab == CT_TAB_LIBRARY) CT_LibraryFrame(tapped, ot);
    else if (s_ctTab == CT_TAB_SEED) CT_SeedFrame(tapped, ot);
    else
    {
        const int importing = MM_CustomImport_Active();
        CT_ImportFrame(tapped, ot, pad);
        if (importing) return; /* the importer draws its own panels */
        if (s_ctFocus == CT_FOCUS_TABS) CT_Footer(NULL, NativeMenu_Labels("*: Open   ^: Options"), ot);
        else if (MM_CustomImport_Active()) CT_Footer(NULL, NativeMenu_Labels("^: Tabs"), ot);
    }
    if (s_ctFocus == CT_FOCUS_TABS && s_ctTab != CT_TAB_IMPORT)
        CT_Footer(NULL, CT_JobsBusy() ? NativeMenu_Labels("Working...   *: Open") : NativeMenu_Labels("*: Open   ^: Options"), ot);
    CT_DrawPanels(ot);
}
