#ifndef AP_AUTHOR_EXPORT_H
#define AP_AUTHOR_EXPORT_H

// Box submissions: one file per track, for a helper to share by hand.
//
// Box Author Mode's Export action (hold Select + Triangle, ap_author_pad.h)
// writes one file per track into AP_BOX_EXPORT_DIR next to the executable:
// every custom track that holds a box, and every retail track whose boxes
// differ from the built-in set. A file is self-contained, so one track can be
// attached to a submission without the whole table. Nothing is uploaded and
// nothing is read back: the placement files stay the store.
//
// FORMAT, one box per line so a pull request diff stays readable:
//
//   {
//     "format": "ctr-ap-box-submission",
//     "version": 1,
//     "client": "<CTR_AP_VERSION>",
//     "track": "<display name>",
//     "kind": "retail" | "custom",
//     retail: "level_id": <LevelID>, "level": "<enum name>",
//     custom: "package_uuid", "package_version", "lev_sha256", "vrm_sha256",
//     "units": "...",
//     "boxes": [
//       {"pos": [x, y, z], "rot_y": r},
//       ...
//     ]
//   }
//
// A custom track is named by its package identity (UUID plus the LEV and VRM
// digests, ap_author_custom.h), never by the host slot it loads into.
// package_version is the revision people read; the digests are the one the
// geometry is bound to.
//
// Freestanding (C99 and stdio), so tools/test-author-export.c checks exactly
// the files the client writes.

#include <stdio.h>
#include <string.h>

#include "ap_author_custom.h"
#include "ap_placement_table.h"

#define AP_BOX_EXPORT_DIR "box-submissions"
#define AP_BOX_EXPORT_FORMAT "ctr-ap-box-submission"
#define AP_BOX_EXPORT_UNITS "pos is LEV InstDef world units (signed 16-bit); rot_y is an engine angle, 0x1000 = one full turn"

// File name inside AP_BOX_EXPORT_DIR. Retail: "retail-<LEVEL>.json". Custom:
// "custom-<uuid>-<first 12 of the LEV digest>.json", so two revisions of one
// package never overwrite each other. Returns 0 when it does not fit.
static inline int AP_BoxExport_RetailName(char *dst, size_t cap, const char *levelName)
{
	int n = snprintf(dst, cap, AP_BOX_EXPORT_DIR "/retail-%s.json", levelName != NULL ? levelName : "?");
	return n > 0 && (size_t)n < cap;
}

static inline int AP_BoxExport_CustomName(char *dst, size_t cap, const AP_CustomBoxKey *key)
{
	int n = snprintf(dst, cap, AP_BOX_EXPORT_DIR "/custom-%s-%.12s.json", key->uuid, key->levSha256);
	return n > 0 && (size_t)n < cap;
}

static inline void AP_BoxExport_Head(FILE *f, const char *client, const char *track, const char *kind)
{
	char text[AP_CUSTOM_BOX_TEXT_MAX * 6];

	fputs("{\n", f);
	fputs("  \"format\": \"" AP_BOX_EXPORT_FORMAT "\",\n", f);
	fputs("  \"version\": 1,\n", f);
	if (!AP_CustomBox_Escape(text, sizeof text, client != NULL ? client : ""))
		text[0] = '\0';
	fprintf(f, "  \"client\": \"%s\",\n", text);
	if (!AP_CustomBox_Escape(text, sizeof text, track != NULL ? track : ""))
		text[0] = '\0';
	fprintf(f, "  \"track\": \"%s\",\n", text);
	fprintf(f, "  \"kind\": \"%s\",\n", kind);
}

static inline void AP_BoxExport_Box(FILE *f, int x, int y, int z, int rotY, int last)
{
	fprintf(f, "    {\"pos\": [%d, %d, %d], \"rot_y\": %d}%s\n", x, y, z, rotY, last ? "" : ",");
}

// The retail rows of `level`, in table order. Returns 1 on success.
static inline int AP_BoxExport_WriteRetail(FILE *f, const AP_PlacementRow *rows, int count, int level,
                                           const char *levelName, const char *client)
{
	int i, n = 0, written = 0;

	if (f == NULL || (rows == NULL && count > 0))
		return 0;
	for (i = 0; i < count; i++)
		if (rows[i].level == level)
			n++;
	AP_BoxExport_Head(f, client, levelName, "retail");
	fprintf(f, "  \"level_id\": %d,\n", level);
	fprintf(f, "  \"level\": \"%s\",\n", levelName != NULL ? levelName : "?");
	fputs("  \"units\": \"" AP_BOX_EXPORT_UNITS "\",\n", f);
	fputs("  \"boxes\": [\n", f);
	for (i = 0; i < count; i++)
	{
		if (rows[i].level != level)
			continue;
		written++;
		AP_BoxExport_Box(f, rows[i].x, rows[i].y, rows[i].z, rows[i].rotY, written == n);
	}
	fputs("  ]\n}\n", f);
	return ferror(f) == 0;
}

// The custom rows of `key`, in table order. The track name and version come
// from the newest row of that key. Returns 1 on success.
static inline int AP_BoxExport_WriteCustom(FILE *f, const AP_CustomBoxRow *rows, int count,
                                           const AP_CustomBoxKey *key, const char *client)
{
	char text[AP_CUSTOM_BOX_TEXT_MAX * 6];
	int i, n = 0, written = 0, last = -1;

	if (f == NULL || key == NULL || (rows == NULL && count > 0))
		return 0;
	for (i = 0; i < count; i++)
	{
		if (AP_CustomBoxKey_Equal(&rows[i].key, key))
		{
			n++;
			last = i;
		}
	}
	if (last < 0)
		return 0;
	AP_BoxExport_Head(f, client, rows[last].title, "custom");
	fprintf(f, "  \"package_uuid\": \"%s\",\n", key->uuid);
	if (!AP_CustomBox_Escape(text, sizeof text, rows[last].version))
		text[0] = '\0';
	fprintf(f, "  \"package_version\": \"%s\",\n", text);
	fprintf(f, "  \"lev_sha256\": \"%s\",\n", key->levSha256);
	fprintf(f, "  \"vrm_sha256\": \"%s\",\n", key->vrmSha256);
	fputs("  \"units\": \"" AP_BOX_EXPORT_UNITS "\",\n", f);
	fputs("  \"boxes\": [\n", f);
	for (i = 0; i < count; i++)
	{
		if (!AP_CustomBoxKey_Equal(&rows[i].key, key))
			continue;
		written++;
		AP_BoxExport_Box(f, rows[i].x, rows[i].y, rows[i].z, rows[i].rotY, written == n);
	}
	fputs("  ]\n}\n", f);
	return ferror(f) == 0;
}

// 1 when `level`'s rows in the edited table differ from the built-in set:
// another count, or any box moved, turned or reordered. Order matters because
// slot N of a track is its Nth box (ap_box_map.h).
static inline int AP_BoxExport_RetailChanged(const AP_PlacementRow *rows, int count, int level)
{
	int i = 0, j = 0;

	for (;;)
	{
		while (i < count && rows[i].level != level)
			i++;
		while (j < AP_EMBEDDED_PLACEMENT_COUNT && AP_EMBEDDED_PLACEMENTS[j].level != level)
			j++;
		if (i >= count || j >= AP_EMBEDDED_PLACEMENT_COUNT)
			return (i >= count) != (j >= AP_EMBEDDED_PLACEMENT_COUNT);
		if (rows[i].x != AP_EMBEDDED_PLACEMENTS[j].x || rows[i].y != AP_EMBEDDED_PLACEMENTS[j].y ||
		    rows[i].z != AP_EMBEDDED_PLACEMENTS[j].z || rows[i].rotY != AP_EMBEDDED_PLACEMENTS[j].rotY)
			return 1;
		i++;
		j++;
	}
}

// 1 when rows[index] is the first row of its key, so each custom track is
// exported once.
static inline int AP_BoxExport_FirstOfKey(const AP_CustomBoxRow *rows, int index)
{
	int i;

	for (i = 0; i < index; i++)
		if (AP_CustomBoxKey_Equal(&rows[i].key, &rows[index].key))
			return 0;
	return 1;
}

#endif
