#ifndef NATIVE_TRACK_SOURCES_H
#define NATIVE_TRACK_SOURCES_H
/* Where a missing custom track comes from (A4-2). A fixed table, keyed by the
   LEV SHA-256 every room pins, of the Project Saphi download or the xdelta
   patch the track was first published in. The rows are in
   native_track_sources.inc. The table only decides what to tell the player and
   which Saphi files to try: the room's hashes still decide what installs, and
   nothing here fetches or stores a patch. A LEV that is not listed gets the
   old text. Header-only, freestanding C, so the harnesses can include it. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define TRACK_SOURCE_SAPHI 1
#define TRACK_SOURCE_PATCH 2

struct TrackSource
{
	int kind;               /* TRACK_SOURCE_* */
	const char *lev, *vrm;  /* lower-case SHA-256 hex */
	const char *title;
	const char *patchFile;  /* patches: the file name players know */
	const char *patchSha256;
	int slot;               /* patches: BIGFILE index */
	const char *page;       /* patches: public download page, "" when none is known */
	int saphiTrack, levDownload, vrmDownload; /* Saphi rows */
	const char *alsoPatch;  /* patches: another patch with the same LEV, or NULL */
};

#define TS_SAPHI(lev, vrm, title, track, levDl, vrmDl) {TRACK_SOURCE_SAPHI, lev, vrm, title, "", "", -1, "", track, levDl, vrmDl, NULL},
#define TS_PATCH(lev, vrm, title, file, sha, slot, page) {TRACK_SOURCE_PATCH, lev, vrm, title, file, sha, slot, page, 0, 0, 0, NULL},
static const struct TrackSource g_trackSources[] = {
#include "native_track_sources.inc"
};
#undef TS_SAPHI
#undef TS_PATCH
#define TRACK_SOURCE_COUNT ((int)(sizeof g_trackSources / sizeof g_trackSources[0]))

static inline int TrackSources_HexEqual(const char *a, const char *b)
{
	size_t i;
	if (!a || !b) return 0;
	for (i = 0; i < 64; i++)
	{
		char x = a[i], y = b[i];
		if (!x || !y) return 0;
		if (x >= 'A' && x <= 'F') x = (char)(x + 32);
		if (y >= 'A' && y <= 'F') y = (char)(y + 32);
		if (x != y) return 0;
	}
	return a[64] == '\0' && b[64] == '\0';
}

/* The first row for this LEV, preferring a row whose VRM also matches when a
   VRM is given. Rows are ordered so Saphi wins, then the likeliest patch; out
   gets a copy with alsoPatch set when a later row names another patch. NULL
   out only tests for a hit. Returns 0 for an unknown LEV. */
static inline int TrackSources_Find(const char *levSha256, const char *vrmSha256, struct TrackSource *out)
{
	int i, first = -1, exact = -1, also = -1;
	for (i = 0; i < TRACK_SOURCE_COUNT; i++)
	{
		if (!TrackSources_HexEqual(g_trackSources[i].lev, levSha256)) continue;
		if (first < 0) first = i;
		else if (also < 0 && g_trackSources[i].kind == TRACK_SOURCE_PATCH &&
		         strcmp(g_trackSources[i].patchSha256, g_trackSources[first].patchSha256) != 0) also = i;
		if (exact < 0 && vrmSha256 && TrackSources_HexEqual(g_trackSources[i].vrm, vrmSha256)) exact = i;
	}
	if (first < 0) return 0;
	if (out)
	{
		*out = g_trackSources[exact >= 0 ? exact : first];
		out->alsoPatch = also >= 0 && g_trackSources[first].kind == TRACK_SOURCE_PATCH ? g_trackSources[also].patchFile : NULL;
	}
	return 1;
}

/* The player-facing sentence, plain printable ASCII, "" for an unknown LEV.
   saphi: "Download from Project Saphi." Patch: "Import <file> (slot N) from
   <page>." The page is shown without its scheme; a patch with no known page
   says only the file and slot. */
static inline void TrackSources_Hint(const char *levSha256, const char *vrmSha256, char *out, size_t size)
{
	struct TrackSource s;
	const char *page;
	if (!out || size == 0) return;
	out[0] = '\0';
	if (!TrackSources_Find(levSha256, vrmSha256, &s)) return;
	if (s.kind == TRACK_SOURCE_SAPHI)
	{
		snprintf(out, size, "Download from Project Saphi.");
		return;
	}
	page = s.page;
	if (strncmp(page, "https://", 8) == 0) page += 8;
	if (page[0])
		snprintf(out, size, "Import %s (slot %d) from %s.", s.patchFile, s.slot, page);
	else
		snprintf(out, size, "Import %s (slot %d).", s.patchFile, s.slot);
	if (s.alsoPatch && strlen(out) + 8 < size)
		snprintf(out + strlen(out), size - strlen(out), " Also in %s.", s.alsoPatch);
}
#endif
