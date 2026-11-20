#ifndef NATIVE_TRACK_CREDIT_H
#define NATIVE_TRACK_CREDIT_H

#include <stddef.h>

#define CTR_TRACK_CREDIT_MAX 1408
#define CTR_CORTEX_CREDIT "Track and music by Lockheart"

#ifdef __cplusplus
extern "C" {
#endif

/* A geometry pair takes precedence over a catalogue ID. The optional ID is
   for catalogue rows before the registry has a matching revision. */
void CustomTrackCredit_Format(char *out, size_t size, const char *lev, const char *vrm,
                              int saphiID, const char *author, const char *source);
void CustomTrackCredit_EnsureRegistry(const char *assets);

#ifdef __cplusplus
}
#endif
#endif
