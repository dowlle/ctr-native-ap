#ifndef NATIVE_SAPHI_CATALOGUE_H
#define NATIVE_SAPHI_CATALOGUE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Source identity before acquisition. A Saphi media pair is not a package
   pin or a Ready verdict; the downloaded, verified snapshot produces those. */
struct CustomSaphiMedia
{
    int id;
    unsigned int bytes;
    uint32_t crc32;
    char path[241];
};
struct CustomSaphiRevision
{
    int trackID;
    char title[641], author[81], version[65];
    struct CustomSaphiMedia lev, vrm;
    /* The track's audio (.sca), versioned apart from LEV and VRM. Set only on
       current revisions, and only when Saphi lists exactly one current .sca
       with a download; id zero otherwise. Optional: never disables a row. */
    struct CustomSaphiMedia sca;
    unsigned int modeTags;
    int current;
    int sourceLaps; /* Track-level source metadata, zero if unspecified. */
    char disabledReason[129]; /* Ambiguous/missing pair remains visible. */
    /* Track author permission check (A4-74). authorID is Saphi's author_id,
       zero when absent or unreadable. saphiPermission is Saphi's own
       permission value for this revision as CustomSaphi_Permission reads it:
       CUSTOM_SAPHI_PERMISSION_*, from the track's `permissions` object
       (public_non_profit; A4-79). creditRequired is the author's credit
       setting, CUSTOM_SAPHI_CREDIT_*, unknown until the author specified. */
    int authorID;
    int saphiPermission;
    int creditRequired;
};
/* Same values as AP_TRACK_PERMISSION_* (ap/ap_track_consent.h). */
enum
{
    CUSTOM_SAPHI_PERMISSION_UNKNOWN = 0,
    CUSTOM_SAPHI_PERMISSION_ALLOW = 1,
    CUSTOM_SAPHI_PERMISSION_DENY = 2
};
/* The author's credit setting (A4-79); unknown until the author specified. */
enum
{
    CUSTOM_SAPHI_CREDIT_UNKNOWN = 0,
    CUSTOM_SAPHI_CREDIT_REQUIRED = 1,
    CUSTOM_SAPHI_CREDIT_NOT_REQUIRED = 2
};
struct CustomSaphiCatalogue;
/* Parse a complete, bounded response transactionally. No downloads/writes.
   Exactly one LEV and VRM must match both version and mode signature. */
int CustomSaphi_ParseCatalogue(const char *json, size_t size,
    struct CustomSaphiCatalogue **out, char *error, size_t errorSize);
size_t CustomSaphi_Count(const struct CustomSaphiCatalogue *catalogue);
const struct CustomSaphiRevision *CustomSaphi_Row(const struct CustomSaphiCatalogue *catalogue, size_t index);
void CustomSaphi_Free(struct CustomSaphiCatalogue **catalogue);
/* Main-thread network job. Poll: 0 running, 1 transfers completed catalogue,
   -1 idle, -2 failed. On failure the caller's prior catalogue is untouched. */
int CustomSaphi_StartRefresh(void);
int CustomSaphi_PollRefresh(struct CustomSaphiCatalogue **out, char *error, size_t errorSize);
/* Explicit download of the copied source revision. Installed result identifies
   the complete immutable package; it does not certify any gameplay mode. */
int CustomSaphi_StartInstall(const struct CustomSaphiRevision *revision, const char *assets);
int CustomSaphi_PollInstall(char pin[65], char *error, size_t errorSize);
#ifdef __cplusplus
}
#endif
#endif
