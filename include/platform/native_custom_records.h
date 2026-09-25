#ifndef NATIVE_CUSTOM_RECORDS_H
#define NATIVE_CUSTOM_RECORDS_H
/* Time Trial records for custom tracks (box authoring build, CTR_CUSTOM_PACKAGES).

   A custom track's best times and its saved ghost never touch the memory card
   or the retail save layout. They live in their own files in custom-records/
   next to the authoring exe, one pair per track identity:

     custom-records/<uuid>-<lev 16 hex>-<vrm 16 hex>-<laps>l.times   text
     custom-records/<uuid>-<lev 16 hex>-<vrm 16 hex>-<laps>l.ghost   binary

   The identity is the package UUID, the SHA-256 of its LEV and VRM and its
   lap count, so a new version of a track's files, or a different lap count,
   starts a fresh table and never races an old ghost. Both files repeat the
   full identity and are refused if it differs from the one asked for.

   The .times file holds the Time Trial half of the retail table: the best lap
   and the five best race times, each a time in engine units (1/960 s), a
   character id and the name typed at the end of the race.

   The .ghost file holds one engine ghost (the 0x28-byte header plus its
   recorded moves, at most 0x3e00 bytes) and the SHA-256 of those bytes. The
   caller checks the engine header; this module checks identity and integrity.
   Freestanding apart from stdio, so the harness tests exactly this code. */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CTR_RECORDS_DIR "custom-records"
#define CTR_RECORDS_ENTRIES 6    /* [0] best lap, [1..5] best race times */
#define CTR_RECORDS_NAME 18      /* engine HighScoreEntry name field */
#define CTR_RECORDS_GHOST_MAX 0x3e00
#define CTR_RECORDS_STEM_MAX 96

struct CustomRecordKey
{
    char uuid[37];
    char lev[65];
    char vrm[65];
    unsigned int laps;
};

struct CustomRecordEntry
{
    unsigned int time;
    unsigned int characterID;
    char name[CTR_RECORDS_NAME];
};

struct CustomRecordTimes
{
    struct CustomRecordEntry entry[CTR_RECORDS_ENTRIES];
};

/* Canonical lowercase UUID and digests, laps 1..7. Returns 0 otherwise. */
int CustomRecords_MakeKey(struct CustomRecordKey *key, const char *uuid, const char *lev,
                          const char *vrm, unsigned int laps);
/* "<uuid>-<lev 16>-<vrm 16>-<laps>l". Returns 0 for an invalid key. */
int CustomRecords_Stem(const struct CustomRecordKey *key, char *out, size_t cap);
/* Text form of a table. Returns the length written, 0 if it does not fit. */
size_t CustomRecords_FormatTimes(const struct CustomRecordKey *key, const struct CustomRecordTimes *times,
                                 char *out, size_t cap);
/* Strict: header, identity equal to key, then exactly the six entries in order. */
int CustomRecords_ParseTimes(const struct CustomRecordKey *key, const char *text, size_t size,
                             struct CustomRecordTimes *out);
/* Binary ghost file. Returns the file size, 0 on refusal or if it does not fit. */
size_t CustomRecords_FormatGhost(const struct CustomRecordKey *key, const void *ghost, size_t ghostBytes,
                                 unsigned char *out, size_t cap);
/* Copies the ghost bytes out after the identity and SHA-256 checks. */
int CustomRecords_ParseGhost(const struct CustomRecordKey *key, const unsigned char *file, size_t size,
                             void *ghost, size_t cap, size_t *ghostBytes);
size_t CustomRecords_GhostFileMax(void);

/* File I/O under dir (normally CTR_RECORDS_DIR). Saves write a temporary file
   and then replace the old one, creating dir when needed. Loads return 0 when
   the file is missing or refused; the caller then uses its defaults. */
int CustomRecords_LoadTimes(const char *dir, const struct CustomRecordKey *key, struct CustomRecordTimes *out);
int CustomRecords_SaveTimes(const char *dir, const struct CustomRecordKey *key, const struct CustomRecordTimes *times);
int CustomRecords_LoadGhost(const char *dir, const struct CustomRecordKey *key, void *ghost, size_t cap,
                            size_t *ghostBytes);
int CustomRecords_SaveGhost(const char *dir, const struct CustomRecordKey *key, const void *ghost, size_t ghostBytes);

#ifdef __cplusplus
}
#endif
#endif
