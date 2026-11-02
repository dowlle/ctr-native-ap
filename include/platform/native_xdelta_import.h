#ifndef NATIVE_XDELTA_IMPORT_H
#define NATIVE_XDELTA_IMPORT_H
/* xdelta track importer (0.3.0, race tracks only). Reads a .xdelta patch the
   player supplies, applies it window by window against the player's own
   NTSC-U disc, and turns every changed race track into a normal custom-track
   package. Nothing from the patch is ever kept except the cut LEV and VRM of
   the tracks the player installs. Track names, laps and relic times come only
   from an optional reviewed pack descriptor (<patch>.ctrpack.json); the
   importer never reads pack code. Design: xdelta track importer design,
   2026-10-15. */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CTR_XDELTA_CUTTER_VERSION 1
#define CTR_XDELTA_ITEM_MAX 96
#define CTR_XDELTA_TITLE_MAX 641
#define CTR_XDELTA_REASON_MAX 96

struct XdeltaImportItem
{
    int bigfileIndex;  /* -1 for a grouped line (code, other content). */
    int offered;       /* 1: an installable race track. */
    char title[CTR_XDELTA_TITLE_MAX];
    char tags[48];     /* "Race TT Arcade Relic CTR", offered items only. */
    char reason[CTR_XDELTA_REASON_MAX]; /* Skip reason; "" when offered. */
    char levSha256[65], vrmSha256[65];
    char pin[65];      /* Manifest pin of the built package, offered items only. */
    char trackKey[65]; /* Room folder: assets/tracks/<trackKey>/original, offered items only. */
};

struct XdeltaImportSummary
{
    char patchSha256[65];
    unsigned long long patchBytes;
    char discSha256[65];
    char descriptorSha256[65]; /* "" when no descriptor was used. */
    char descriptorNote[CTR_XDELTA_REASON_MAX]; /* Why a descriptor was not used, or "". */
    unsigned int passes;
    unsigned int count;
    struct XdeltaImportItem items[CTR_XDELTA_ITEM_MAX];
};

struct XdeltaImportScan;

struct XdeltaImportRequest
{
    const char *patchPath;
    const char *discPath;       /* Raw MODE2/2352 retail NTSC-U image. */
    const char *descriptorPath; /* Optional; NULL looks for <patch stem>.ctrpack.json beside the patch. */
    const char *scratchDir;     /* Existing directory; a .import-<random> folder is made inside and removed. */
    int skipRetailCheck;        /* Harnesses only: synthetic discs carry no retail executable. */
};

/* Synchronous scan. progress (0..100) may be NULL; cancel is polled. On
   success *out owns every built package of the review list and the summary.
   Returns 1, or 0 with a one-line player-facing error. */
int XdeltaImport_Scan(const struct XdeltaImportRequest *request, struct XdeltaImportScan **out,
                      volatile int *progress, const volatile int *cancel, char *error, size_t errorSize);
const struct XdeltaImportSummary *XdeltaImport_Summary(const struct XdeltaImportScan *scan);
/* Owned manifest bytes of an offered item, for harnesses and the corpus tool. */
int XdeltaImport_CopyManifest(const struct XdeltaImportScan *scan, unsigned int item, void *destination,
                              size_t capacity, size_t *written);
/* Where Install writes (decision 35, 2026-10-28). PACKAGES: the package
   store ASSETS/tracks/packages the Track Manager reads (authoring builds).
   ROOM: the LEV and VRM in ASSETS/tracks/<track key>/original, where rooms
   and solo seeds find a custom track by its hashes (every build). */
#define CTR_XDELTA_INSTALL_PACKAGES 1
#define CTR_XDELTA_INSTALL_ROOM 2
/* Install the selected offered items. selected has summary.count entries.
   Returns the number installed or reverified, -1 on a failure (error names
   the first failing track). */
int XdeltaImport_Install(const struct XdeltaImportScan *scan, const unsigned char *selected,
                         const char *assets, int where, char *error, size_t errorSize);
/* The room folder of a track: the descriptor's track_key, otherwise the
   registry's key rule over the title (NFKD, ASCII only, lowercase, runs of
   anything else become one '-', at most 48 characters, "track" when empty).
   Returns 1, or 0 when no key fits. */
int XdeltaImport_TrackKey(const char *title, char out[65]);
void XdeltaImport_Free(struct XdeltaImportScan **scan);

/* UUIDv5 over "xdelta:<patch_sha256>:<bigfile_index>" (the saphi_import_uuid recipe shape). */
void XdeltaImport_PackageUuid(const char *patchSha256, int bigfileIndex, char out[37]);
/* Cutter version 1: cut a LEV at the end of its pointer map and a VRM at its
   terminator. Return the cut size, or 0 when the bytes are not that format. */
size_t XdeltaImport_CutLev(const unsigned char *bytes, size_t size);
size_t XdeltaImport_CutVrm(const unsigned char *bytes, size_t size);

/* The fallback folder ASSETS/tracks/import (Steam Deck Game Mode, where a
   desktop file dialog may not show): file names ending .xdelta or .vcdiff,
   sorted, at most max. Returns the count; the folder is created if missing. */
#define CTR_XDELTA_FOLDER_MAX 24
#define CTR_XDELTA_NAME_MAX 128
int XdeltaImport_ListFolder(const char *assets, char names[][CTR_XDELTA_NAME_MAX], int max);

/* Main-thread job API for the Track Manager. One job at a time; no game or UI
   pointer enters the worker. */
int XdeltaImport_StartScan(const char *patchPath, const char *discPath, const char *assets);
/* -1 idle, 0 running (progress set), 1 scan ready (summary copied), -2 failed (error set). */
int XdeltaImport_PollScan(int *progress, struct XdeltaImportSummary *summary, char *error, size_t errorSize);
void XdeltaImport_CancelScan(void);
int XdeltaImport_StartInstall(const unsigned char *selected, unsigned int count, const char *assets, int where);
/* -1 idle, 0 running, 1 done (installed set), -2 failed (error set). Drops the scan. */
int XdeltaImport_PollInstall(int *installed, char *error, size_t errorSize);
void XdeltaImport_DiscardScan(void);

#ifdef __cplusplus
}
#endif
#endif
