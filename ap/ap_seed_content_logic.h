#ifndef AP_SEED_CONTENT_LOGIC_H
#define AP_SEED_CONTENT_LOGIC_H

// Current Seed (#133 PR 7): the freestanding decisions behind Options >
// Custom Content > Current Seed. Which required tracks are Ready, Missing or
// need attention, what "Install missing content" would download, the checks
// that refuse an install before anything on disk changes, and the text each
// row shows.
//
// Nothing here reads the plan, the file system or engine globals: the caller
// (ap/ap_hooks_seed_content.c) gathers every fact, and the downloader
// (ap/ap_custom_track_download.cpp) reuses the size rule. Room text is
// untrusted: it is copied bounded and drawn as plain printable ASCII.
// tools/test-seed-content.c pins all of it.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AP_SEED_ROWS_MAX 33 /* AP_CONTENT_MAX_PACKAGES plus Cortex Vortex */
#define AP_SEED_TEXT_MAX 129
#define AP_SEED_ID_MAX 65
/* The largest track file the Saphi downloader accepts (kTrackFileMax). A room
 * may pin a larger LEV; such a track is not downloadable. */
#define AP_SEED_DOWNLOAD_FILE_MAX (8u * 1024u * 1024u)

/* What a scan of one required track found, mapped from the package manager's
 * state by the caller. */
enum
{
	AP_SEED_SCAN_UNKNOWN = 0,   /* not scanned yet */
	AP_SEED_SCAN_READY,         /* exact bytes found and measured */
	AP_SEED_SCAN_NOT_INSTALLED, /* no candidate files at all */
	AP_SEED_SCAN_MISSING_FILE,  /* one of LEV / VRM is absent */
	AP_SEED_SCAN_MISMATCH,      /* files present, none is the pinned revision */
	AP_SEED_SCAN_SETUP,         /* verified, local setup (manifest) unfinished */
	AP_SEED_SCAN_REFUSED,       /* bytes lack what the room claims */
	AP_SEED_SCAN_IO_ERROR       /* could not read the folder or files */
};

/* Where a missing track can come from. */
enum
{
	AP_SEED_SOURCE_MANUAL = 0, /* no trusted download: the player adds the files */
	AP_SEED_SOURCE_SAPHI,      /* a compiled release profile with a Project Saphi download */
	AP_SEED_SOURCE_BUNDLED     /* Cortex Vortex, shipped with the client */
};

/* Row states the page shows. */
enum
{
	AP_SEED_STATE_READY = 0,
	AP_SEED_STATE_MISSING,
	AP_SEED_STATE_ATTENTION
};

/* An xdelta patch locator ([[xdelta track importer design]]): which patch and
 * BIGFILE slot a track comes from. The room's package record has no such
 * field yet, so it is always empty in this build; the page still shows the
 * field so the importer can fill it without a layout change. */
struct AP_SeedContentLocator
{
	char patch_name[AP_SEED_ID_MAX];
	char patch_sha256[AP_SEED_ID_MAX];
	int bigfile_index; /* -1 when not named */
};

struct AP_SeedContentRow
{
	char title[AP_SEED_TEXT_MAX];
	char author[AP_SEED_TEXT_MAX];
	char revision[AP_SEED_ID_MAX];
	char folder[AP_SEED_ID_MAX]; /* the track key: tracks/<folder>/original */
	uint32_t lev_bytes, vrm_bytes;
	int scan;   /* AP_SEED_SCAN_* */
	int source; /* AP_SEED_SOURCE_* */
	int index;  /* package index, -1 for Cortex Vortex */
	struct AP_SeedContentLocator locator;
};

/* Copy room text for drawing: printable ASCII only (a UTF-8 sequence becomes
 * one '?'), bounded, never longer than size - 1, always terminated. */
static inline void ap_seed_plain_text(const char *src, char *out, size_t size)
{
	size_t n = 0;
	const unsigned char *p = (const unsigned char *)src;
	if (!out || size == 0) return;
	for (; p && *p && n + 1 < size; p++)
	{
		if ((*p & 0xC0) == 0x80) continue; /* continuation byte of the '?' already written */
		out[n++] = (*p >= 0x20 && *p < 0x7F) ? (char)*p : '?';
	}
	out[n] = '\0';
}

/* One folder name under assets/tracks: 1..64 of [a-z0-9.-], starting with a
 * letter or digit, never "..". Rejects separators, drive colons, spaces,
 * upper case and anything a file system could read as another place. */
static inline int ap_seed_folder_safe(const char *name)
{
	size_t n;
	if (!name || !name[0]) return 0;
	if (!((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9'))) return 0;
	for (n = 0; name[n]; n++)
	{
		const char c = name[n];
		if (n >= 64) return 0;
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) return 0;
		if (c == '.' && name[n + 1] == '.') return 0;
	}
	return 1;
}

/* A package root the installer may create or write: exactly
 * <tracks>/<one safe folder>. '\\' is a separator only on Windows; on POSIX
 * it is an ordinary file-name character, so "tracks\\x" is a sibling. */
static inline int ap_seed_root_safe(const char *tracks, const char *root)
{
	size_t n;
	if (!tracks || !root || !tracks[0]) return 0;
	n = strlen(tracks);
	if (strncmp(root, tracks, n) != 0) return 0;
#ifdef _WIN32
	if (root[n] != '/' && root[n] != '\\') return 0;
#else
	if (root[n] != '/') return 0;
#endif
	return ap_seed_folder_safe(root + n + 1);
}

static inline int ap_seed_row_state(const struct AP_SeedContentRow *row)
{
	if (row->scan == AP_SEED_SCAN_READY) return AP_SEED_STATE_READY;
	if (row->scan == AP_SEED_SCAN_NOT_INSTALLED || row->scan == AP_SEED_SCAN_MISSING_FILE)
		return AP_SEED_STATE_MISSING;
	return AP_SEED_STATE_ATTENTION;
}

static inline const char *ap_seed_state_text(int state)
{
	return state == AP_SEED_STATE_READY ? "Ready" : state == AP_SEED_STATE_MISSING ? "Missing" : "Check files";
}

static inline int ap_seed_ready_count(const struct AP_SeedContentRow *rows, int count)
{
	int i, ready = 0;
	for (i = 0; i < count; i++)
		if (ap_seed_row_state(&rows[i]) == AP_SEED_STATE_READY) ready++;
	return ready;
}

/* A row "Install missing content" downloads: missing, from a trusted source. */
static inline int ap_seed_row_downloadable(const struct AP_SeedContentRow *row)
{
	return ap_seed_row_state(row) == AP_SEED_STATE_MISSING && row->source == AP_SEED_SOURCE_SAPHI;
}

/* Whether a listed or downloaded size agrees with the room's pin. A pin of 0
 * means no pin (the legacy single-package download), which never refuses. */
static inline int ap_seed_size_agrees(uint32_t pinned, uint64_t actual)
{
	return pinned == 0 || (uint64_t)pinned == actual;
}

enum
{
	AP_SEED_PLAN_OK = 0,       /* plan_out holds the rows to download */
	AP_SEED_PLAN_ALL_READY,    /* nothing is missing */
	AP_SEED_PLAN_MANUAL_ONLY,  /* missing tracks, none with a trusted download */
	AP_SEED_PLAN_BUSY,         /* a download is already running */
	AP_SEED_PLAN_BAD_FOLDER,   /* a queued row's folder is unsafe */
	AP_SEED_PLAN_BAD_SIZE      /* a queued row pins a size the downloader cannot take */
};

/* Decide what "Install missing content" does, before anything changes. Every
 * downloadable row is checked first; one bad row refuses the whole action, so
 * a refusal never follows a partial install. */
static inline int ap_seed_install_plan(const struct AP_SeedContentRow *rows, int count, int busy,
                                       int *plan_out, int *plan_count, uint64_t *plan_bytes,
                                       char *reason, size_t reason_size)
{
	int i, missing = 0, queued = 0;
	uint64_t bytes = 0;
	char name[48];
	if (plan_count) *plan_count = 0;
	if (plan_bytes) *plan_bytes = 0;
	if (reason && reason_size) reason[0] = '\0';
	for (i = 0; i < count; i++)
		if (ap_seed_row_state(&rows[i]) != AP_SEED_STATE_READY) missing++;
	if (!missing)
	{
		if (reason) snprintf(reason, reason_size, "Every track this seed needs is ready.");
		return AP_SEED_PLAN_ALL_READY;
	}
	if (busy)
	{
		if (reason) snprintf(reason, reason_size, "A download is already running. Wait for it to finish.");
		return AP_SEED_PLAN_BUSY;
	}
	for (i = 0; i < count; i++)
	{
		const struct AP_SeedContentRow *row = &rows[i];
		if (!ap_seed_row_downloadable(row)) continue;
		ap_seed_plain_text(row->title, name, sizeof name);
		if (!ap_seed_folder_safe(row->folder))
		{
			if (reason) snprintf(reason, reason_size, "%s has an unsafe folder name. Nothing was installed.", name);
			return AP_SEED_PLAN_BAD_FOLDER;
		}
		if (row->lev_bytes == 0 || row->vrm_bytes == 0 ||
		    row->lev_bytes > AP_SEED_DOWNLOAD_FILE_MAX || row->vrm_bytes > AP_SEED_DOWNLOAD_FILE_MAX)
		{
			if (reason) snprintf(reason, reason_size, "%s is larger than the download allows. Nothing was installed.", name);
			return AP_SEED_PLAN_BAD_SIZE;
		}
		if (plan_out) plan_out[queued] = i;
		queued++;
		bytes += (uint64_t)row->lev_bytes + row->vrm_bytes;
	}
	if (!queued)
	{
		if (reason) snprintf(reason, reason_size, "No missing track has a download. Follow each track's instructions.");
		return AP_SEED_PLAN_MANUAL_ONLY;
	}
	if (plan_count) *plan_count = queued;
	if (plan_bytes) *plan_bytes = bytes;
	return AP_SEED_PLAN_OK;
}

/* "512 KB", "2.4 MB" (binary units, rounded up so a size is never understated). */
static inline void ap_seed_format_bytes(uint64_t bytes, char *out, size_t size)
{
	if (!out || size == 0) return;
	if (bytes < 1024u * 1024u)
	{
		unsigned kb = (unsigned)((bytes + 1023u) / 1024u);
		snprintf(out, size, "%u KB", kb ? kb : 1u);
	}
	else
	{
		uint64_t tenths = (bytes * 10u + 1024u * 1024u - 1u) / (1024u * 1024u);
		snprintf(out, size, "%u.%u MB", (unsigned)(tenths / 10u), (unsigned)(tenths % 10u));
	}
}

/* The per-track instruction under the list. */
static inline void ap_seed_instruction(const struct AP_SeedContentRow *row, char *out, size_t size)
{
	char folder[AP_SEED_ID_MAX], rev[AP_SEED_ID_MAX];
	if (!out || size == 0) return;
	ap_seed_plain_text(row->folder, folder, sizeof folder);
	ap_seed_plain_text(row->revision, rev, sizeof rev);
	if (row->source == AP_SEED_SOURCE_BUNDLED)
	{
		snprintf(out, size, "%s", row->scan == AP_SEED_SCAN_READY ? "Comes with the client. Files verified." :
		         "Comes with the client. Reinstall the client to restore tracks/cortex-vortex.");
		return;
	}
	switch (row->scan)
	{
	case AP_SEED_SCAN_READY:
		snprintf(out, size, "Files verified.");
		return;
	case AP_SEED_SCAN_NOT_INSTALLED:
	case AP_SEED_SCAN_MISSING_FILE:
		if (row->source == AP_SEED_SOURCE_SAPHI)
			snprintf(out, size, "Downloads from Project Saphi with Install missing content.");
		else
			snprintf(out, size, "No download. Put the v%s LEV and VRM in tracks/%s/original, then Rescan.", rev, folder);
		return;
	case AP_SEED_SCAN_MISMATCH:
		if (row->source == AP_SEED_SOURCE_SAPHI)
			snprintf(out, size, "The files in its folder are not v%s. Move them out, then Install missing content.", rev);
		else
			snprintf(out, size, "tracks/%s/original has other files. Add the v%s LEV and VRM, then Rescan.", folder, rev);
		return;
	case AP_SEED_SCAN_SETUP:
		snprintf(out, size, "Files verified. Press Rescan to finish setup.");
		return;
	case AP_SEED_SCAN_REFUSED:
		snprintf(out, size, "The files lack what this room says they have. Ask the room host.");
		return;
	case AP_SEED_SCAN_IO_ERROR:
		snprintf(out, size, "Could not read tracks/%s/original. Check the folder, then Rescan.", folder);
		return;
	default:
		snprintf(out, size, "Not checked yet. Press Rescan.");
		return;
	}
}

/* The patch locator field: which xdelta patch carries this track. */
static inline void ap_seed_locator_line(const struct AP_SeedContentRow *row, char *out, size_t size)
{
	char name[AP_SEED_ID_MAX], hash[9];
	if (!out || size == 0) return;
	if (!row->locator.patch_sha256[0])
	{
		snprintf(out, size, "Patch: none named by this room");
		return;
	}
	ap_seed_plain_text(row->locator.patch_name[0] ? row->locator.patch_name : "patch", name, sizeof name);
	ap_seed_plain_text(row->locator.patch_sha256, hash, sizeof hash);
	if (row->locator.bigfile_index >= 0)
		snprintf(out, size, "Patch: %s (%s...) slot %d", name, hash, row->locator.bigfile_index);
	else
		snprintf(out, size, "Patch: %s (%s...)", name, hash);
}

#ifdef __cplusplus
}
#endif

#endif
