#ifndef NATIVE_TRACK_CREDIT_H
#define NATIVE_TRACK_CREDIT_H

#include <stddef.h>

#define CTR_TRACK_CREDIT_MAX 1408
#define CTR_CORTEX_TRACK_CREDIT "Track by Lockheart"
#define CTR_CORTEX_MUSIC_CREDIT "Music by arbee"
#define CTR_CORTEX_CREDIT CTR_CORTEX_TRACK_CREDIT " / " CTR_CORTEX_MUSIC_CREDIT

#ifdef __cplusplus
extern "C" {
#endif

/* A geometry pair takes precedence over a catalogue ID. The optional ID is
   for catalogue rows before the registry has a matching revision. */
void CustomTrackCredit_Format(char *out, size_t size, const char *lev, const char *vrm,
                              int saphiID, const char *author, const char *source);
/* Use the recorded import locator before inferring provenance from geometry. */
const char *CustomTrackCredit_PatchSource(const char *name, const char *sha256);
void CustomTrackCredit_EnsureRegistry(const char *assets);

#ifdef __cplusplus
}
#endif
#endif
