#ifndef NATIVE_CUSTOM_TRACKS_H
#define NATIVE_CUSTOM_TRACKS_H

#ifdef CTR_CUSTOM_TRACKS

#include <common.h>

// Custom-track loader (Phase-0 spike). With a config entry mapping an arcade
// track's levelID to a folder of dumped subfiles, the game serves that folder's
// bytes for the track's 8-subfile BIGFILE group instead of the BIGFILE's own
// bytes. The override is byte-for-byte group serving: the dumped bytes come from
// a patched ROM whose track already runs on this engine, so the generic pointer
// map fixup and VRAM upload downstream of the read need no changes.
//
// Arcade tracks occupy BIGFILE indices [levelID*8, levelID*8 + 8) for
// levelID < NITRO_COURT (18); BI_ARCADETRACKS is 0. Subfile index i in that
// range is served from "<dir>/<i - levelID*8>.bin".

// Parse the [CustomTracks] section of config.ini once, at startup. Safe to call
// when config.ini is absent (leaves the override table empty). Mirrors the parse
// style of platform/native_config.c but reads its own dynamic keys.
void CustomTrack_Load(void);

// If subfileIndex belongs to a track that has an override folder configured and
// the mapped file exists, return 1 and set *outPath to the file path (points at
// an internal static buffer, valid until the next CustomTrack_GetOverride call)
// and *outSize to its byte size. Returns 0 for any non-overridden index. If the
// track is mapped but the specific file is missing, logs a loud warning and
// returns 0 so the caller falls back to the BIGFILE path.
int CustomTrack_GetOverride(int subfileIndex, const char **outPath, u32 *outSize);

// Fill dst with the override file's bytes, zero-padding the tail out to bufBytes
// (the sector-rounded buffer size). fileBytes is the file's real size as
// reported by CustomTrack_GetOverride. Returns 1 on success, 0 on any I/O error.
int CustomTrack_ReadFile(const char *path, void *dst, u32 bufBytes, u32 fileBytes);

#endif // CTR_CUSTOM_TRACKS

#endif // NATIVE_CUSTOM_TRACKS_H
