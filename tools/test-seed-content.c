// Current Seed (#133 PR 7): the freestanding decisions in
// ap/ap_seed_content_logic.h.
// cc -Wall -Wextra -Werror -o /tmp/test-seed-content tools/test-seed-content.c
//
// What it pins:
//   1. Ready / Missing / Check files from every scan result, and the ready count,
//   2. which rows "Install missing content" downloads: missing and from Saphi,
//      never a manual or bundled row, never one that only needs attention,
//   3. the install refusals, decided before anything changes: nothing missing,
//      a download already running, only manual rows, an unsafe folder, a size
//      the downloader cannot take; one bad row refuses the whole action,
//   4. folder and package-root safety: traversal, separators, drive colons,
//      upper case, empty, overlong,
//   5. the room-size rule the downloader uses (a zero pin is the legacy path),
//   6. room text is drawn as bounded printable ASCII,
//   7. sizes, per-track instructions and the patch locator line.

#include <stdio.h>
#include <string.h>
#include "../ap/ap_seed_content_logic.h"

static int failures, checks;

static void expect(int ok, const char *what)
{
	checks++;
	if (!ok)
	{
		failures++;
		printf("FAIL: %s\n", what);
	}
}

static struct AP_SeedContentRow row(const char *title, int scan, int source, uint32_t lev, uint32_t vrm)
{
	struct AP_SeedContentRow r;
	memset(&r, 0, sizeof r);
	snprintf(r.title, sizeof r.title, "%s", title);
	snprintf(r.author, sizeof r.author, "Author");
	snprintf(r.revision, sizeof r.revision, "1.0.2");
	snprintf(r.folder, sizeof r.folder, "baby-t-park");
	r.lev_bytes = lev;
	r.vrm_bytes = vrm;
	r.scan = scan;
	r.source = source;
	r.locator.bigfile_index = -1;
	return r;
}

int main(void)
{
	struct AP_SeedContentRow rows[6];
	int plan[AP_SEED_ROWS_MAX], count, i;
	uint64_t bytes;
	char text[256];

	/* 1. States. */
	{
		static const int scans[] = {AP_SEED_SCAN_UNKNOWN, AP_SEED_SCAN_READY, AP_SEED_SCAN_NOT_INSTALLED,
		                            AP_SEED_SCAN_MISSING_FILE, AP_SEED_SCAN_MISMATCH, AP_SEED_SCAN_SETUP,
		                            AP_SEED_SCAN_REFUSED, AP_SEED_SCAN_IO_ERROR};
		static const int want[] = {AP_SEED_STATE_ATTENTION, AP_SEED_STATE_READY, AP_SEED_STATE_MISSING,
		                           AP_SEED_STATE_MISSING, AP_SEED_STATE_ATTENTION, AP_SEED_STATE_ATTENTION,
		                           AP_SEED_STATE_ATTENTION, AP_SEED_STATE_ATTENTION};
		for (i = 0; i < (int)(sizeof scans / sizeof scans[0]); i++)
		{
			struct AP_SeedContentRow r = row("T", scans[i], AP_SEED_SOURCE_MANUAL, 1, 1);
			snprintf(text, sizeof text, "scan %d maps to state %d", scans[i], want[i]);
			expect(ap_seed_row_state(&r) == want[i], text);
		}
		expect(!strcmp(ap_seed_state_text(AP_SEED_STATE_READY), "Ready") &&
		       !strcmp(ap_seed_state_text(AP_SEED_STATE_MISSING), "Missing") &&
		       !strcmp(ap_seed_state_text(AP_SEED_STATE_ATTENTION), "Check files"), "state labels");
	}

	rows[0] = row("Ready one", AP_SEED_SCAN_READY, AP_SEED_SOURCE_SAPHI, 100, 200);
	rows[1] = row("Saphi missing", AP_SEED_SCAN_NOT_INSTALLED, AP_SEED_SOURCE_SAPHI, 2470836, 115732);
	rows[2] = row("Manual missing", AP_SEED_SCAN_NOT_INSTALLED, AP_SEED_SOURCE_MANUAL, 5, 5);
	rows[3] = row("Saphi mismatch", AP_SEED_SCAN_MISMATCH, AP_SEED_SOURCE_SAPHI, 5, 5);
	rows[4] = row("Cortex Vortex", AP_SEED_SCAN_NOT_INSTALLED, AP_SEED_SOURCE_BUNDLED, 0, 0);
	rows[5] = row("Saphi one file", AP_SEED_SCAN_MISSING_FILE, AP_SEED_SOURCE_SAPHI, 10, 20);
	expect(ap_seed_ready_count(rows, 6) == 1, "one of six ready");

	/* 2. Downloadable rows. */
	expect(!ap_seed_row_downloadable(&rows[0]) && ap_seed_row_downloadable(&rows[1]) && !ap_seed_row_downloadable(&rows[2]) &&
	       !ap_seed_row_downloadable(&rows[3]) && !ap_seed_row_downloadable(&rows[4]) && ap_seed_row_downloadable(&rows[5]),
	       "only missing Saphi rows download; manual, bundled, mismatched and ready rows do not");

	/* 3. The plan. */
	count = -1; bytes = 1;
	expect(ap_seed_install_plan(rows, 6, 0, plan, &count, &bytes, text, sizeof text) == AP_SEED_PLAN_OK &&
	       count == 2 && plan[0] == 1 && plan[1] == 5 && bytes == 2470836u + 115732u + 30u, "plan queues rows 1 and 5 with their pinned sizes");
	expect(ap_seed_install_plan(rows, 6, 1, plan, &count, &bytes, text, sizeof text) == AP_SEED_PLAN_BUSY &&
	       count == 0 && bytes == 0 && strstr(text, "already running"), "a running download refuses");
	expect(ap_seed_install_plan(rows, 1, 0, plan, &count, &bytes, text, sizeof text) == AP_SEED_PLAN_ALL_READY &&
	       count == 0, "nothing missing: nothing to do");
	expect(ap_seed_install_plan(rows, 1, 1, plan, &count, &bytes, text, sizeof text) == AP_SEED_PLAN_ALL_READY,
	       "nothing missing wins over busy");
	expect(ap_seed_install_plan(&rows[2], 3, 0, plan, &count, &bytes, text, sizeof text) == AP_SEED_PLAN_MANUAL_ONLY &&
	       count == 0 && strstr(text, "instructions"), "only manual, mismatched or bundled rows: refuse with instructions");
	{
		struct AP_SeedContentRow bad[3];
		bad[0] = rows[1];
		bad[1] = rows[5];
		snprintf(bad[1].folder, sizeof bad[1].folder, "../escape");
		expect(ap_seed_install_plan(bad, 2, 0, plan, &count, &bytes, text, sizeof text) == AP_SEED_PLAN_BAD_FOLDER &&
		       count == 0 && bytes == 0 && strstr(text, "Nothing was installed"), "one unsafe folder refuses the whole plan, the good row too");
		bad[1] = rows[5];
		bad[1].lev_bytes = AP_SEED_DOWNLOAD_FILE_MAX + 1;
		expect(ap_seed_install_plan(bad, 2, 0, plan, &count, &bytes, text, sizeof text) == AP_SEED_PLAN_BAD_SIZE && count == 0,
		       "a LEV past the download limit refuses the whole plan");
		bad[1].lev_bytes = AP_SEED_DOWNLOAD_FILE_MAX;
		expect(ap_seed_install_plan(bad, 2, 0, plan, &count, &bytes, text, sizeof text) == AP_SEED_PLAN_OK && count == 2,
		       "exactly the download limit is allowed");
		bad[1].vrm_bytes = 0;
		expect(ap_seed_install_plan(bad, 2, 0, plan, &count, &bytes, text, sizeof text) == AP_SEED_PLAN_BAD_SIZE,
		       "a zero size pin refuses");
		bad[1] = rows[5];
		snprintf(bad[1].title, sizeof bad[1].title, "%%s%%n\x1b[2J\xc3\xa9");
		snprintf(bad[1].folder, sizeof bad[1].folder, "A");
		expect(ap_seed_install_plan(bad, 2, 0, plan, &count, &bytes, text, sizeof text) == AP_SEED_PLAN_BAD_FOLDER &&
		       !strchr(text, '\x1b') && strstr(text, "%s%n?[2J?") == text, "a hostile title is quoted as plain text in the refusal");
		expect(ap_seed_install_plan(bad, 2, 0, NULL, NULL, NULL, NULL, 0) == AP_SEED_PLAN_BAD_FOLDER, "null outputs are allowed");
	}

	/* 4. Folders and roots. */
	{
		static const char *good[] = {"baby-t-park", "baby-t-park-1.0.2", "a", "0x", "snowy-valley-lrg2"};
		static const char *badf[] = {"", "..", ".", "../x", "a/b", "a\\b", "c:", "Baby", "-lead", ".hidden", "a..b",
		                             "a b", "a\nb", "\xc3\xa9", "a%", NULL};
		char longName[80];
		for (i = 0; i < (int)(sizeof good / sizeof good[0]); i++)
		{
			snprintf(text, sizeof text, "folder '%s' is safe", good[i]);
			expect(ap_seed_folder_safe(good[i]), text);
		}
		for (i = 0; i < (int)(sizeof badf / sizeof badf[0]); i++)
		{
			snprintf(text, sizeof text, "folder '%s' is refused", badf[i] ? badf[i] : "(null)");
			expect(!ap_seed_folder_safe(badf[i]), text);
		}
		memset(longName, 'a', 64); longName[64] = '\0';
		expect(ap_seed_folder_safe(longName), "64 characters is the limit");
		longName[64] = 'a'; longName[65] = '\0';
		expect(!ap_seed_folder_safe(longName), "65 characters is refused");
		expect(ap_seed_root_safe("/a/assets/tracks", "/a/assets/tracks/baby-t-park"), "root under tracks");
#ifdef _WIN32
		expect(ap_seed_root_safe("C:\\g\\assets/tracks", "C:\\g\\assets/tracks\\baby-t-park-1.0.2"), "Windows separator");
#else
		expect(!ap_seed_root_safe("/a/assets/tracks", "/a/assets/tracks\\sibling"), "a POSIX backslash is a sibling name, refused");
#endif
		expect(!ap_seed_root_safe("/a/assets/tracks", "/a/assets/tracks"), "tracks itself is not a package root");
		expect(!ap_seed_root_safe("/a/assets/tracks", "/a/assets/tracks/"), "empty folder");
		expect(!ap_seed_root_safe("/a/assets/tracks", "/a/assets/tracks/../x"), "traversal out of tracks");
		expect(!ap_seed_root_safe("/a/assets/tracks", "/a/assets/tracks/a/b"), "nested folder");
		expect(!ap_seed_root_safe("/a/assets/tracks", "/a/assets/tracksx/a"), "a sibling with the same prefix");
		expect(!ap_seed_root_safe("/a/assets/tracks", "/b/assets/tracks/a"), "another tree");
		expect(!ap_seed_root_safe("", "/a"), "empty tracks folder");
		expect(!ap_seed_root_safe(NULL, "/a") && !ap_seed_root_safe("/a", NULL), "null paths");
	}

	/* 5. Size pins. */
	expect(ap_seed_size_agrees(0, 12345), "a zero pin is the legacy download and never refuses");
	expect(ap_seed_size_agrees(10, 10) && !ap_seed_size_agrees(10, 11) && !ap_seed_size_agrees(10, 9) &&
	       !ap_seed_size_agrees(10, 10ull + 0x100000000ull), "a pin must match exactly, without 32-bit wrap");

	/* 6. Plain text. */
	{
		char small[6];
		ap_seed_plain_text("Snowy\tValley \xe2\x98\x83 X", text, sizeof text);
		expect(!strcmp(text, "Snowy?Valley ? X"), "tabs and a UTF-8 snowman become one '?' each");
		ap_seed_plain_text("abcdefghij", small, sizeof small);
		expect(!strcmp(small, "abcde"), "bounded copy");
		ap_seed_plain_text(NULL, small, sizeof small);
		expect(small[0] == '\0', "null text is empty");
		ap_seed_plain_text("\x80\x80z", small, sizeof small);
		expect(!strcmp(small, "z"), "stray continuation bytes are dropped");
	}

	/* 7. Sizes, instructions, locator. */
	ap_seed_format_bytes(1, text, sizeof text);
	expect(!strcmp(text, "1 KB"), "one byte rounds up to 1 KB");
	ap_seed_format_bytes(0, text, sizeof text);
	expect(!strcmp(text, "1 KB"), "zero shows as 1 KB, never 0");
	ap_seed_format_bytes(1024 * 1024 - 1, text, sizeof text);
	expect(!strcmp(text, "1024 KB"), "just under a MB");
	ap_seed_format_bytes(2470836u + 115732u, text, sizeof text);
	expect(!strcmp(text, "2.5 MB"), "Baby T Park's two files round up to 2.5 MB");
	ap_seed_format_bytes(1024 * 1024, text, sizeof text);
	expect(!strcmp(text, "1.0 MB"), "exactly one MB");

	ap_seed_instruction(&rows[0], text, sizeof text);
	expect(!strcmp(text, "Files verified."), "ready instruction");
	ap_seed_instruction(&rows[1], text, sizeof text);
	expect(strstr(text, "Project Saphi") && strstr(text, "Install missing content"), "Saphi row instruction");
	ap_seed_instruction(&rows[2], text, sizeof text);
	expect(strstr(text, "No download") && strstr(text, "tracks/baby-t-park/original") && strstr(text, "v1.0.2"),
	       "manual row names its folder and revision");
	ap_seed_instruction(&rows[3], text, sizeof text);
	expect(strstr(text, "Move them out") != NULL, "a mismatched Saphi folder is never overwritten: the player moves the files");
	{
		struct AP_SeedContentRow r = rows[2];
		r.scan = AP_SEED_SCAN_MISMATCH;
		ap_seed_instruction(&r, text, sizeof text);
		expect(strstr(text, "has other files") && strstr(text, "Add the v1.0.2"), "a manual folder keeps its other revisions");
		r.scan = AP_SEED_SCAN_REFUSED;
		ap_seed_instruction(&r, text, sizeof text);
		expect(strstr(text, "room host") != NULL, "capability refusal");
		r.scan = AP_SEED_SCAN_SETUP;
		ap_seed_instruction(&r, text, sizeof text);
		expect(strstr(text, "Rescan") != NULL, "unfinished setup");
		r.scan = AP_SEED_SCAN_IO_ERROR;
		ap_seed_instruction(&r, text, sizeof text);
		expect(strstr(text, "Could not read") != NULL, "read error");
		r.scan = AP_SEED_SCAN_UNKNOWN;
		ap_seed_instruction(&r, text, sizeof text);
		expect(strstr(text, "Not checked") != NULL, "not scanned");
		snprintf(r.folder, sizeof r.folder, "a\x1b%%n");
		r.scan = AP_SEED_SCAN_NOT_INSTALLED;
		ap_seed_instruction(&r, text, sizeof text);
		expect(!strchr(text, '\x1b') && strstr(text, "tracks/a?%n/original"), "room text in an instruction is plain");
	}
	ap_seed_instruction(&rows[4], text, sizeof text);
	expect(strstr(text, "Reinstall the client") && strstr(text, "cortex-vortex"), "bundled Cortex Vortex missing");
	rows[4].scan = AP_SEED_SCAN_READY;
	ap_seed_instruction(&rows[4], text, sizeof text);
	expect(strstr(text, "Files verified") != NULL, "bundled Cortex Vortex ready");

	ap_seed_locator_line(&rows[2], text, sizeof text);
	expect(!strcmp(text, "Patch: none named by this room"), "no locator on the wire yet");
	snprintf(rows[2].locator.patch_name, sizeof rows[2].locator.patch_name, "LRG2Pancho.xdelta");
	snprintf(rows[2].locator.patch_sha256, sizeof rows[2].locator.patch_sha256, "52a420e5ffffffff");
	rows[2].locator.bigfile_index = 216;
	ap_seed_locator_line(&rows[2], text, sizeof text);
	expect(!strcmp(text, "Patch: LRG2Pancho.xdelta (52a420e5...) slot 216"), "locator with slot");
	rows[2].locator.bigfile_index = -1;
	rows[2].locator.patch_name[0] = '\0';
	ap_seed_locator_line(&rows[2], text, sizeof text);
	expect(!strcmp(text, "Patch: patch (52a420e5...)"), "locator without name or slot");
	{
		char tiny[8];
		ap_seed_instruction(&rows[2], tiny, sizeof tiny);
		expect(strlen(tiny) == 7, "instructions truncate safely");
		ap_seed_locator_line(&rows[2], tiny, sizeof tiny);
		expect(strlen(tiny) == 7, "locator truncates safely");
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
