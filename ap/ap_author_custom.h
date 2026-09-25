#ifndef AP_AUTHOR_CUSTOM_H
#define AP_AUTHOR_CUSTOM_H

// Box placements on custom tracks, box authoring build only (CTR_CUSTOM_PACKAGES).
//
// A custom track races on a borrowed host slot (Roo's Tubes), so gGT->levelID
// says nothing about which track is on screen. Placements authored on one are
// therefore keyed by the PACKAGE IDENTITY the offline loader served: the
// package UUID plus the SHA-256 of the LEV and of the VRM. A new revision with
// different geometry is a different key, so a spot can never silently move
// onto changed geometry. The host levelID is never part of the key.
//
// They go to their own file, AP_CUSTOM_BOX_FILE, never into the retail table
// (ap-box-placements-authoring.json) and never into a file any player client
// reads. The retail author mode and its file are untouched by this header.
//
// FILE FORMAT. The same one-placement-per-line JSON shape as the retail file,
// so it diffs well in a pull request and a helper can read it:
//
//   {
//     "format": "ctr-ap-custom-box-placements",
//     "version": 1,
//     "client": "<CTR_AP_VERSION>",
//     "units": "...",
//     "placements": [
//       {"package_uuid": "...", "lev_sha256": "...", "vrm_sha256": "...", "pos": [x, y, z], "rot_y": r, "package_version": "...", "track": "..."},
//       ...
//     ]
//   }
//
// package_uuid, lev_sha256 and vrm_sha256 are the key. pos and rot_y are the
// same LEV InstDef units as the retail file. package_version and track are for
// people reading the file and are never matched on. Strings are JSON-escaped.
//
// Freestanding: C99 and stdio only, so tools/test-author-custom.c exercises the
// exact key, row and file code the authoring client runs.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define AP_CUSTOM_BOX_FILE "ap-box-placements-custom-authoring.json"
#define AP_CUSTOM_BOX_FORMAT "ctr-ap-custom-box-placements"
#define AP_CUSTOM_BOX_UUID_CHARS 36
#define AP_CUSTOM_BOX_SHA_CHARS 64
#define AP_CUSTOM_BOX_TEXT_MAX 128 // stored copy of the track name / version
#define AP_CUSTOM_BOX_LINE_MAX 2048 // one formatted placement line, escaped

typedef struct
{
	char uuid[AP_CUSTOM_BOX_UUID_CHARS + 1];     // lowercase 8-4-4-4-12
	char levSha256[AP_CUSTOM_BOX_SHA_CHARS + 1]; // lowercase hex
	char vrmSha256[AP_CUSTOM_BOX_SHA_CHARS + 1]; // lowercase hex
} AP_CustomBoxKey;

typedef struct
{
	AP_CustomBoxKey key;
	char title[AP_CUSTOM_BOX_TEXT_MAX];   // informational
	char version[AP_CUSTOM_BOX_TEXT_MAX]; // informational
	short x, y, z, rotY;
} AP_CustomBoxRow;

// ── the key ─────────────────────────────────────────────────────────────────

static inline int AP_CustomBox_Hex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static inline char AP_CustomBox_Lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Validate and normalise a key. Returns 1 and fills *out only when the UUID is
// canonical 8-4-4-4-12 hex and both digests are exactly 64 hex digits; on 0,
// *out is left untouched.
static inline int AP_CustomBoxKey_Make(AP_CustomBoxKey *out, const char *uuid, const char *lev, const char *vrm)
{
	AP_CustomBoxKey key;
	int i;

	if (out == NULL || uuid == NULL || lev == NULL || vrm == NULL)
		return 0;
	if (strlen(uuid) != AP_CUSTOM_BOX_UUID_CHARS || strlen(lev) != AP_CUSTOM_BOX_SHA_CHARS ||
	    strlen(vrm) != AP_CUSTOM_BOX_SHA_CHARS)
		return 0;
	for (i = 0; i < AP_CUSTOM_BOX_UUID_CHARS; i++)
	{
		int dash = (i == 8 || i == 13 || i == 18 || i == 23);
		if (dash ? uuid[i] != '-' : !AP_CustomBox_Hex(uuid[i]))
			return 0;
		key.uuid[i] = AP_CustomBox_Lower(uuid[i]);
	}
	key.uuid[AP_CUSTOM_BOX_UUID_CHARS] = '\0';
	for (i = 0; i < AP_CUSTOM_BOX_SHA_CHARS; i++)
	{
		if (!AP_CustomBox_Hex(lev[i]) || !AP_CustomBox_Hex(vrm[i]))
			return 0;
		key.levSha256[i] = AP_CustomBox_Lower(lev[i]);
		key.vrmSha256[i] = AP_CustomBox_Lower(vrm[i]);
	}
	key.levSha256[AP_CUSTOM_BOX_SHA_CHARS] = '\0';
	key.vrmSha256[AP_CUSTOM_BOX_SHA_CHARS] = '\0';
	*out = key;
	return 1;
}

static inline int AP_CustomBoxKey_Equal(const AP_CustomBoxKey *a, const AP_CustomBoxKey *b)
{
	return a != NULL && b != NULL && strcmp(a->uuid, b->uuid) == 0 &&
	       strcmp(a->levSha256, b->levSha256) == 0 && strcmp(a->vrmSha256, b->vrmSha256) == 0;
}

// Copy informational text, cut on a UTF-8 boundary so a stored name is never a
// broken sequence.
static inline void AP_CustomBox_CopyText(char *dst, size_t cap, const char *src)
{
	size_t n;

	if (dst == NULL || cap == 0)
		return;
	if (src == NULL)
		src = "";
	n = strlen(src);
	if (n >= cap)
	{
		n = cap - 1;
		while (n > 0 && (((unsigned char)src[n]) & 0xC0) == 0x80)
			n--;
	}
	memcpy(dst, src, n);
	dst[n] = '\0';
}

// The HUD font draws ASCII only: keep printable ASCII, turn anything else into
// '?', collapse each multi-byte UTF-8 sequence into a single '?'.
static inline void AP_CustomBox_HudName(char *dst, size_t cap, const char *title)
{
	size_t n = 0;

	if (dst == NULL || cap == 0)
		return;
	for (; title != NULL && *title != '\0' && n + 1 < cap; title++)
	{
		unsigned char c = (unsigned char)*title;
		if ((c & 0xC0) == 0x80)
			continue; // continuation byte: already counted by its lead byte
		dst[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
	}
	dst[n] = '\0';
	if (n == 0)
		AP_CustomBox_CopyText(dst, cap, "CUSTOM TRACK");
}

// ── rows ────────────────────────────────────────────────────────────────────

// JSON string body for src into dst (no surrounding quotes). Returns 0 when it
// does not fit.
static inline int AP_CustomBox_Escape(char *dst, size_t cap, const char *src)
{
	size_t n = 0;

	for (; *src != '\0'; src++)
	{
		unsigned char c = (unsigned char)*src;
		char tmp[8];
		const char *put = tmp;
		size_t len;

		if (c == '"' || c == '\\')
		{
			tmp[0] = '\\';
			tmp[1] = (char)c;
			tmp[2] = '\0';
		}
		else if (c < 0x20)
			snprintf(tmp, sizeof tmp, "\\u%04x", (unsigned)c);
		else
		{
			tmp[0] = (char)c;
			tmp[1] = '\0';
		}
		len = strlen(put);
		if (n + len + 1 > cap)
			return 0;
		memcpy(dst + n, put, len);
		n += len;
	}
	if (n + 1 > cap)
		return 0;
	dst[n] = '\0';
	return 1;
}

// One placement line, without the trailing comma/newline. Returns 0 when the
// row's key is invalid or the line does not fit.
static inline int AP_CustomBoxRow_Format(char *dst, size_t cap, const AP_CustomBoxRow *row)
{
	char title[AP_CUSTOM_BOX_TEXT_MAX * 6];
	char version[AP_CUSTOM_BOX_TEXT_MAX * 6];
	AP_CustomBoxKey check;
	int wrote;

	if (dst == NULL || row == NULL ||
	    !AP_CustomBoxKey_Make(&check, row->key.uuid, row->key.levSha256, row->key.vrmSha256) ||
	    !AP_CustomBox_Escape(title, sizeof title, row->title) ||
	    !AP_CustomBox_Escape(version, sizeof version, row->version))
		return 0;
	wrote = snprintf(dst, cap,
	                 "{\"package_uuid\": \"%s\", \"lev_sha256\": \"%s\", \"vrm_sha256\": \"%s\", "
	                 "\"pos\": [%d, %d, %d], \"rot_y\": %d, \"package_version\": \"%s\", \"track\": \"%s\"}",
	                 check.uuid, check.levSha256, check.vrmSha256, (int)row->x, (int)row->y, (int)row->z,
	                 (int)row->rotY, version, title);
	return wrote > 0 && (size_t)wrote < cap;
}

// A tiny reader for exactly one flat JSON object per line: string keys, values
// that are strings, integers or an array of integers. Anything else, a
// duplicate key, or trailing garbage refuses the line. Hand edits that reorder
// fields or reflow spaces are fine; restructuring a line is not.
static inline const char *AP_CustomBox_Ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	return p;
}

static inline const char *AP_CustomBox_String(const char *p, char *out, size_t cap)
{
	size_t n = 0;

	if (*p != '"')
		return NULL;
	p++;
	while (*p != '"')
	{
		char c = *p;
		if (c == '\0' || (unsigned char)c < 0x20)
			return NULL;
		if (c == '\\')
		{
			p++;
			switch (*p)
			{
			case '"': c = '"'; break;
			case '\\': c = '\\'; break;
			case '/': c = '/'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case 'u':
			{
				char hex[5];
				long v;
				int i;
				for (i = 0; i < 4; i++)
				{
					if (!AP_CustomBox_Hex(p[1 + i]))
						return NULL;
					hex[i] = p[1 + i];
				}
				hex[4] = '\0';
				v = strtol(hex, NULL, 16);
				c = (v > 0 && v < 0x80) ? (char)v : '?'; // informational text only
				p += 4;
				break;
			}
			default:
				return NULL;
			}
		}
		if (n + 1 < cap)
			out[n++] = c;
		p++;
	}
	out[n] = '\0';
	return p + 1;
}

static inline const char *AP_CustomBox_Int(const char *p, long *out)
{
	char *end;

	if (*p != '-' && (*p < '0' || *p > '9'))
		return NULL;
	*out = strtol(p, &end, 10);
	return end == p ? NULL : end;
}

static inline short AP_CustomBox_S16(long v, int *ok)
{
	if (v < -32768 || v > 32767)
		*ok = 0;
	return (short)v;
}

// Parse one placement line (surrounding whitespace and a trailing comma are
// allowed). Returns 1 with *out filled only for a complete, valid row.
static inline int AP_CustomBoxRow_Parse(const char *line, AP_CustomBoxRow *out)
{
	char uuid[64] = "", lev[80] = "", vrm[80] = "";
	AP_CustomBoxRow row;
	unsigned seen = 0;
	int ok = 1;
	const char *p;

	if (line == NULL || out == NULL)
		return 0;
	memset(&row, 0, sizeof row);
	p = AP_CustomBox_Ws(line);
	if (*p != '{')
		return 0;
	p = AP_CustomBox_Ws(p + 1);
	for (;;)
	{
		char name[32];
		unsigned bit;

		p = AP_CustomBox_String(p, name, sizeof name);
		if (p == NULL)
			return 0;
		p = AP_CustomBox_Ws(p);
		if (*p != ':')
			return 0;
		p = AP_CustomBox_Ws(p + 1);

		if (strcmp(name, "package_uuid") == 0) { bit = 1u; p = AP_CustomBox_String(p, uuid, sizeof uuid); }
		else if (strcmp(name, "lev_sha256") == 0) { bit = 2u; p = AP_CustomBox_String(p, lev, sizeof lev); }
		else if (strcmp(name, "vrm_sha256") == 0) { bit = 4u; p = AP_CustomBox_String(p, vrm, sizeof vrm); }
		else if (strcmp(name, "package_version") == 0) { bit = 8u; p = AP_CustomBox_String(p, row.version, sizeof row.version); }
		else if (strcmp(name, "track") == 0) { bit = 16u; p = AP_CustomBox_String(p, row.title, sizeof row.title); }
		else if (strcmp(name, "rot_y") == 0)
		{
			long v;
			bit = 32u;
			p = AP_CustomBox_Int(p, &v);
			if (p != NULL)
				row.rotY = AP_CustomBox_S16(v, &ok);
		}
		else if (strcmp(name, "pos") == 0)
		{
			long v[3];
			int i;
			bit = 64u;
			if (*p != '[')
				return 0;
			p = AP_CustomBox_Ws(p + 1);
			for (i = 0; i < 3 && p != NULL; i++)
			{
				p = AP_CustomBox_Int(p, &v[i]);
				if (p == NULL)
					return 0;
				p = AP_CustomBox_Ws(p);
				if (i < 2)
				{
					if (*p != ',')
						return 0;
					p = AP_CustomBox_Ws(p + 1);
				}
			}
			if (p == NULL || *p != ']')
				return 0;
			p++;
			row.x = AP_CustomBox_S16(v[0], &ok);
			row.y = AP_CustomBox_S16(v[1], &ok);
			row.z = AP_CustomBox_S16(v[2], &ok);
		}
		else
			return 0; // unknown field: refuse rather than guess
		if (p == NULL || (seen & bit) != 0)
			return 0;
		seen |= bit;
		p = AP_CustomBox_Ws(p);
		if (*p == ',')
		{
			p = AP_CustomBox_Ws(p + 1);
			continue;
		}
		if (*p != '}')
			return 0;
		p = AP_CustomBox_Ws(p + 1);
		if (*p == ',')
			p = AP_CustomBox_Ws(p + 1);
		if (*p != '\0')
			return 0;
		break;
	}
	// Key and position are required; rot_y, version and name are optional.
	if ((seen & (1u | 2u | 4u | 64u)) != (1u | 2u | 4u | 64u) || !ok ||
	    !AP_CustomBoxKey_Make(&row.key, uuid, lev, vrm))
		return 0;
	*out = row;
	return 1;
}

// ── the file ────────────────────────────────────────────────────────────────

// Write every row (all packages). Returns 1 on success.
static inline int AP_CustomBoxFile_Write(FILE *f, const AP_CustomBoxRow *rows, int count, const char *client)
{
	char line[AP_CUSTOM_BOX_LINE_MAX];
	char clientText[128];
	int i;

	if (f == NULL || (rows == NULL && count > 0) || count < 0)
		return 0;
	if (!AP_CustomBox_Escape(clientText, sizeof clientText, client != NULL ? client : ""))
		clientText[0] = '\0';
	fputs("{\n", f);
	fputs("  \"format\": \"" AP_CUSTOM_BOX_FORMAT "\",\n", f);
	fputs("  \"version\": 1,\n", f);
	fprintf(f, "  \"client\": \"%s\",\n", clientText);
	fputs("  \"units\": \"pos is LEV InstDef world units (signed 16-bit); rot_y is an engine angle, "
	      "0x1000 = one full turn; package_uuid + lev_sha256 + vrm_sha256 name the custom track\",\n", f);
	fputs("  \"placements\": [\n", f);
	for (i = 0; i < count; i++)
	{
		if (!AP_CustomBoxRow_Format(line, sizeof line, &rows[i]))
			return 0;
		fprintf(f, "    %s%s\n", line, (i + 1 < count) ? "," : "");
	}
	fputs("  ]\n", f);
	fputs("}\n", f);
	return ferror(f) == 0;
}

// Read placement rows. Header lines and the array brackets are skipped; a line
// that looks like a placement ("package_uuid" in it) but does not parse counts
// in *rejected. Stops at maxRows and sets *overflow. Returns the row count.
static inline int AP_CustomBoxFile_Read(FILE *f, AP_CustomBoxRow *rows, int maxRows, int *rejected, int *overflow)
{
	char line[AP_CUSTOM_BOX_LINE_MAX * 2];
	int count = 0;

	if (rejected)
		*rejected = 0;
	if (overflow)
		*overflow = 0;
	if (f == NULL || rows == NULL || maxRows <= 0)
		return 0;
	while (fgets(line, sizeof line, f) != NULL)
	{
		size_t len = strlen(line);
		AP_CustomBoxRow row;

		if (len > 0 && line[len - 1] != '\n' && !feof(f))
		{
			// Longer than any line this writer produces: skip the rest of it.
			int c;
			while ((c = fgetc(f)) != EOF && c != '\n')
				;
			if (rejected)
				(*rejected)++;
			continue;
		}
		if (strstr(line, "\"package_uuid\"") == NULL)
			continue;
		if (!AP_CustomBoxRow_Parse(line, &row))
		{
			if (rejected)
				(*rejected)++;
			continue;
		}
		if (count >= maxRows)
		{
			if (overflow)
				*overflow = 1;
			break;
		}
		rows[count++] = row;
	}
	return count;
}

// ── table helpers ───────────────────────────────────────────────────────────

static inline int AP_CustomBox_CountFor(const AP_CustomBoxRow *rows, int count, const AP_CustomBoxKey *key)
{
	int i, n = 0;

	for (i = 0; i < count; i++)
		if (AP_CustomBoxKey_Equal(&rows[i].key, key))
			n++;
	return n;
}

static inline int AP_CustomBox_LastIndexFor(const AP_CustomBoxRow *rows, int count, const AP_CustomBoxKey *key)
{
	int i;

	for (i = count - 1; i >= 0; i--)
		if (AP_CustomBoxKey_Equal(&rows[i].key, key))
			return i;
	return -1;
}

// Remove rows[index], keeping the order of the rest. Returns the new count.
static inline int AP_CustomBox_Remove(AP_CustomBoxRow *rows, int count, int index)
{
	int i;

	if (index < 0 || index >= count)
		return count;
	for (i = index; i + 1 < count; i++)
		rows[i] = rows[i + 1];
	return count - 1;
}

#endif // AP_AUTHOR_CUSTOM_H
