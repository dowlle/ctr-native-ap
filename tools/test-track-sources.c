// Track source lookup (A4-2): the table in include/platform/native_track_sources.inc
// and the sentences the Seed tab and the preflight show for a missing track.
// cc -Wall -Wextra -Werror -I include -o /tmp/test-track-sources tools/test-track-sources.c
//
// Cases: a Project Saphi hit, an xdelta hit, an unknown hash, two packs that
// share a LEV (the pack players most likely have wins and the other is named),
// the Coco Team Racing normalized LEV, the Seed tab instruction, table
// integrity, and a text-fit check against the 176 px detail panel.

#include <stdio.h>
#include <string.h>
#include "../ap/ap_seed_content_logic.h"
#include <platform/native_track_sources.h>

static int failures, checks;

static void expect_(int ok, const char *what)
{
	checks++;
	if (!ok)
	{
		failures++;
		printf("FAIL: %s\n", what);
	}
}

#define expect(cond, what) expect_((cond) != 0, what)

#define SUNKEN_LEV "286d6574d87e158d78cc89986f16b8b51e5b94f1b153574ea9358582e32c6ba1"
#define SUNKEN_VRM "bc19e0e64c89c4c6b7afbf3e33225bc640aeb10cf64f58a0cde7089620b016c4"
#define LOCKBOX7_LEV "f59b5f953babac7b781916ef8aa0d5e9c1553d6056a8d760d46434c3c3c3ac86"
#define TOMB_LEV "7dd312574cda7a61b0dc216b9667a29d9a65d09bde64a18b8830d8b895b8b103"
#define SHARED2_LEV "87f416ec29a93f16013f7497de35c0832c13712aaf94695b6597d309e37edf80"
#define COCO_LEV "3e80b47339d243db00acc33d6b7eb9efb15eaf946aa830b1766d56f5b1c15e42"
#define UNKNOWN_LEV "0000000000000000000000000000000000000000000000000000000000000001"

/* Lines a text needs in a panel: greedy word wrap at about 6 px a character. */
static int wrap_lines(const char *text, int width_px)
{
	const int cols = width_px / 6;
	int lines = 1, col = 0;
	const char *p = text;
	while (*p)
	{
		int n = (int)strcspn(p, " ");
		if (col && col + 1 + n > cols) { lines++; col = 0; }
		if (col) col++;
		while (n > cols - col) { n -= cols - col; lines++; col = 0; } /* a long word breaks at any character */
		col += n;
		p += strcspn(p, " ");
		while (*p == ' ') p++;
	}
	return lines;
}

int main(void)
{
	struct TrackSource s;
	char hint[256], text[256];
	struct AP_SeedContentRow r;
	int i, j, dup = 0, bad = 0, longest = 0, maxlines = 0;

	/* 1. Saphi hit. */
	expect(TrackSources_Find(SUNKEN_LEV, SUNKEN_VRM, &s) && s.kind == TRACK_SOURCE_SAPHI, "Sunken Sprint is a Saphi hit");
	expect(s.saphiTrack == 123 && s.levDownload > 0 && s.vrmDownload > 0, "the Saphi row carries the track and download ids");
	TrackSources_Hint(SUNKEN_LEV, SUNKEN_VRM, hint, sizeof hint);
	expect(!strcmp(hint, "Download from Project Saphi."), "Saphi sentence");
	expect(TrackSources_Find("286D6574D87E158D78CC89986F16B8B51E5B94F1B153574EA9358582E32C6BA1", NULL, &s), "hash case does not matter");

	/* 2. xdelta hit. */
	expect(TrackSources_Find(LOCKBOX7_LEV, NULL, &s) && s.kind == TRACK_SOURCE_PATCH && s.slot == 7 &&
	       !strcmp(s.patchFile, "Lockbox_Adventure.xdelta"), "Lockbox Adventure slot 7");
	TrackSources_Hint(LOCKBOX7_LEV, NULL, hint, sizeof hint);
	expect(!strcmp(hint, "Import Lockbox_Adventure.xdelta (slot 7) from ctrcustomtracks.com/lockbox-adventure/."), "patch sentence with its page");
	expect(s.alsoPatch == NULL, "a LEV in one patch names no other");

	/* 3. Unknown hash and bad input keep the old text. */
	expect(!TrackSources_Find(UNKNOWN_LEV, NULL, &s), "unknown LEV");
	TrackSources_Hint(UNKNOWN_LEV, NULL, hint, sizeof hint);
	expect(hint[0] == '\0', "unknown hash gives no sentence");
	TrackSources_Hint(NULL, NULL, hint, sizeof hint);
	expect(hint[0] == '\0', "null hash gives no sentence");
	expect(!TrackSources_Find("abc", NULL, &s), "a short hash never matches");

	/* 4. Two packs share a LEV: Lockbox Adventure over the 30th Anniversary
	 * Adventure (both tester-known, Lockbox was reported first), and nightmarefueled
	 * over LRG2Pancho (tester-known). The other pack is named so the player can use either. */
	expect(TrackSources_Find(SHARED2_LEV, NULL, &s) && !strcmp(s.patchFile, "Lockbox_Adventure.xdelta") && s.slot == 145 &&
	       s.alsoPatch && !strcmp(s.alsoPatch, "Crash-30Anniversary_Adventure.xdelta"), "shared LEV: Lockbox first, 30th named");
	TrackSources_Hint(SHARED2_LEV, NULL, hint, sizeof hint);
	expect(strstr(hint, "Also in Crash-30Anniversary_Adventure.xdelta."), "the sentence names the other pack");
	expect(TrackSources_Find(TOMB_LEV, NULL, &s) && !strcmp(s.patchFile, "nightmarefueled.xdelta") && s.slot == 47 &&
	       s.alsoPatch && !strcmp(s.alsoPatch, "LRG2Pancho.xdelta"), "shared LEV: nightmarefueled first, LRG2Pancho named");
	TrackSources_Hint(TOMB_LEV, NULL, hint, sizeof hint);
	expect(strstr(hint, "(slot 47).") && !strstr(hint, "from "), "a pack without a known page says only file and slot");

	/* Coco Team Racing track 3 has a normalized LEV (A3-36). */
	expect(TrackSources_Find(COCO_LEV, NULL, &s) && s.slot == 47 && !strcmp(s.patchFile, "CocoTeamRacing.xdelta") &&
	       strstr(s.page, "coco-team-racing"), "the normalized Coco Team Racing LEV");

	/* 5. The Seed tab instruction. */
	memset(&r, 0, sizeof r);
	snprintf(r.title, sizeof r.title, "Lockbox track");
	snprintf(r.revision, sizeof r.revision, "1.0.0");
	snprintf(r.folder, sizeof r.folder, "lockbox-track");
	r.scan = AP_SEED_SCAN_NOT_INSTALLED;
	r.locator.bigfile_index = -1;
	ap_seed_instruction(&r, text, sizeof text);
	expect(strstr(text, "tracks/lockbox-track/original"), "no hint: today's text");
	TrackSources_Hint(LOCKBOX7_LEV, NULL, r.source_hint, sizeof r.source_hint);
	ap_seed_instruction(&r, text, sizeof text);
	expect(!strcmp(text, "No download. Import Lockbox_Adventure.xdelta (slot 7) from ctrcustomtracks.com/lockbox-adventure/. Then Rescan."),
	       "hint instruction");
	snprintf(r.locator.patch_sha256, sizeof r.locator.patch_sha256, "abcdef");
	ap_seed_instruction(&r, text, sizeof text);
	expect(strstr(text, "import the patch below"), "a room's own locator wins over the table");
	r.locator.patch_sha256[0] = '\0';
	r.source = AP_SEED_SOURCE_SAPHI;
	ap_seed_instruction(&r, text, sizeof text);
	expect(strstr(text, "Downloads from Project Saphi"), "a Saphi row keeps the download text");
	r.scan = AP_SEED_SCAN_READY;
	r.source = AP_SEED_SOURCE_MANUAL;
	ap_seed_instruction(&r, text, sizeof text);
	expect(!strcmp(text, "Files verified."), "a ready row ignores the hint");
	r.registry = AP_SEED_REGISTRY_TABLE;
	r.source = AP_SEED_SOURCE_SAPHI;
	expect(!strcmp(ap_seed_source_text(&r), "Project Saphi (built-in list)"), "source text for a table match");

	/* 6. Table integrity and text fit. */
	for (i = 0; i < TRACK_SOURCE_COUNT; i++)
	{
		const struct TrackSource *a = &g_trackSources[i];
		const char *h;
		if (strlen(a->lev) != 64 || strlen(a->vrm) != 64) bad++;
		for (h = a->lev; *h; h++) if (!((*h >= '0' && *h <= '9') || (*h >= 'a' && *h <= 'f'))) bad++;
		if (a->kind == TRACK_SOURCE_PATCH && (strlen(a->patchSha256) != 64 || a->slot < 0 || !a->patchFile[0] ||
		    (a->page[0] && strncmp(a->page, "https://", 8)))) bad++;
		if (a->kind == TRACK_SOURCE_SAPHI && (a->saphiTrack <= 0 || a->levDownload <= 0 || a->vrmDownload <= 0)) bad++;
		for (j = i + 1; j < TRACK_SOURCE_COUNT; j++)
			if (!strcmp(a->lev, g_trackSources[j].lev) && g_trackSources[j].kind == a->kind &&
			    !strcmp(a->patchSha256, g_trackSources[j].patchSha256) && a->slot == g_trackSources[j].slot) dup++;
		TrackSources_Hint(a->lev, a->vrm, r.source_hint, sizeof r.source_hint);
		if ((int)strlen(r.source_hint) > longest) longest = (int)strlen(r.source_hint);
		r.scan = AP_SEED_SCAN_NOT_INSTALLED;
		r.source = AP_SEED_SOURCE_MANUAL;
		r.registry = AP_SEED_REGISTRY_NONE;
		ap_seed_instruction(&r, text, sizeof text);
		/* The About line is "<size>, <source>. <instruction>" in the 176 px panel, two lines a page. */
		{ const int n = wrap_lines(text, 176); if (n > maxlines) maxlines = n; if (a->kind == TRACK_SOURCE_PATCH && n > 8) bad++; }
		if (TrackSources_Find(a->lev, a->vrm, &s) && s.kind == TRACK_SOURCE_PATCH && !strstr(r.source_hint, s.patchFile)) bad++;
	}
	expect(TRACK_SOURCE_COUNT > 250, "the table is loaded");
	expect(bad == 0, "every row has 64 lower-case hex hashes, a slot, an https page or none, and Saphi ids; every instruction fits 4 pages");
	expect(dup == 0, "no repeated (LEV, patch, slot) row");
	expect(longest < AP_SEED_HINT_MAX - 40, "the longest sentence leaves room for the instruction around it");
	printf("%d rows, longest sentence %d chars, longest instruction %d lines in the 176 px panel\n", TRACK_SOURCE_COUNT, longest, maxlines);

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
