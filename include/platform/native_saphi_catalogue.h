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
    unsigned int modeTags;
    int current;
    int sourceLaps; /* Track-level source metadata, zero if unspecified. */
    char disabledReason[129]; /* Ambiguous/missing pair remains visible. */
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
