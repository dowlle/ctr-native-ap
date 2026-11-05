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

/* Where an item's title came from (XdeltaImportItem.nameSource). */
#define XDELTA_NAME_LABEL 0      /* A plain label, e.g. "Lego Adventure track 3"; the player edits it. */
#define XDELTA_NAME_DESCRIPTOR 1 /* The pack descriptor (<patch>.ctrpack.json). */
#define XDELTA_NAME_KNOWN 2      /* The table of tracks whose names were read from pack code. */
#define XDELTA_NAME_EDITED 3     /* The player typed it at import. */

struct XdeltaImportItem
{
    int bigfileIndex;  /* -1 for a grouped line (code, other content). */
    int offered;       /* 1: an installable race track. */
    int nameSource;    /* XDELTA_NAME_*; offered items only. */
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
    char history[CTR_XDELTA_REASON_MAX]; /* "Imported before: 3 of 5 tracks", or ""; set by StartScan's job. */
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
/* PACKAGES is the shared installed store for rooms, Solo and Arcade. ROOM
   remains for compatibility with older installs and harness fixtures. */
#define CTR_XDELTA_INSTALL_PACKAGES 1
#define CTR_XDELTA_INSTALL_ROOM 2
/* Install the selected offered items. selected has summary.count entries.
   Returns the number installed or reverified, -1 on a failure (error names
   the first failing track). */
int XdeltaImport_Install(const struct XdeltaImportScan *scan, const unsigned char *selected,
                         const char *assets, int where, char *error, size_t errorSize);
/* The Play Solo descriptor of an offered item (ruling of 2026-11-02): one
   complete custom_tracks package descriptor as JSON, the form solo/tracks
   keeps and worlds/ctr/custom_packages.py admits. Every capability, the
   optional super_turbo_pads, weapon_crates, checkpoints and ai_nav included,
   is measured on the item's verified files with the rules the client checks
   a descriptor against on load. Laps, author and relic times come from the
   pack descriptor and are never guessed; the pack descriptor may also take
   relic and ctr_tokens away. No author: "Unknown". Returns 1 with the JSON,
   its track key and revision; 2 when the pack descriptor gives no laps (or
   there is none): the JSON then has no "laps" and no relic, and the player
   picks the laps on Play Solo's Custom Tracks page; 0 with a short reason. */
int XdeltaImport_PlanDescriptor(const struct XdeltaImportScan *scan, unsigned int item, char *json, size_t capacity,
                                char contentId[65], char revision[65], char *why, size_t whySize);
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
/* Names. The review list's titles are prefilled from the name sources
   (see XDELTA_NAME_*); the player may change one before installing.
   RenameItem rebuilds an offered item's package under the new title (1, or 0
   with a short reason and nothing changed). SetItemTitle only remembers the
   name for a review row; StartInstall applies it on the worker, and an
   install whose name cannot be applied fails before it writes anything. */
int XdeltaImport_RenameItem(struct XdeltaImportScan *scan, unsigned int item, const char *title, char *why, size_t whySize);
int XdeltaImport_SetItemTitle(unsigned int item, const char *title);

/* The record of applied patches (assets/tracks/import-record.json): what each
   patch file in the folder has done before. A patch is found by its SHA-256, so
   a renamed copy counts; only files whose size matches a record are hashed. */
#define XDELTA_STATE_NEW 0      /* Not imported. */
#define XDELTA_STATE_IMPORTED 1 /* Every offered track installed. */
#define XDELTA_STATE_PARTLY 2   /* Some offered tracks installed. */
#define XDELTA_STATE_FAILED 3   /* Scan or install failed; reason says why. */
struct XdeltaPatchState
{
    int state, imported, offered;
    char reason[CTR_XDELTA_REASON_MAX];
};
/* Synchronous: states for the folder files `names`; returns 0 when cancelled. */
int XdeltaImport_PatchStates(const char *assets, char names[][CTR_XDELTA_NAME_MAX], int count,
                             struct XdeltaPatchState *out, const volatile int *cancel);
/* Worker version for the picker: Start, then Poll each frame (-1 idle, 0 running, 1 ready, out filled). */
int XdeltaImport_StartPatchStates(const char *assets, char names[][CTR_XDELTA_NAME_MAX], int count);
int XdeltaImport_PollPatchStates(struct XdeltaPatchState *out, int max);
/* Harnesses: note an outcome the way an install does. */
void XdeltaImport_RecordResult(const char *assets, const char *patchName, const struct XdeltaImportSummary *summary,
                               const int *installedIndexes, int installedCount, const char *failure);

/* After PollInstall reported done or failed: the Play Solo descriptors of the
   tracks that install wrote to the room folders (CTR_XDELTA_INSTALL_ROOM), in
   list order, kept until the next install starts. *needsLaps (may be NULL)
   is 1 for a descriptor without laps (XdeltaImport_PlanDescriptor's 2). The
   note names a track without a descriptor and why, otherwise a track whose
   laps the player picks ("" when every track is complete). */
int XdeltaImport_InstalledDescriptorCount(void);
int XdeltaImport_InstalledDescriptor(int index, char contentId[65], char revision[65], char *json, size_t capacity,
                                     int *needsLaps);
void XdeltaImport_InstalledDescriptorNote(char *note, size_t size);

#ifdef __cplusplus
}
#endif
#endif
