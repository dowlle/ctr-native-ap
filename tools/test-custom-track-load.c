// Integration assertions for the custom-track loader against REAL files on
// disk (Baby T Park event spike, rung 1). Where tools/test-custom-track-policy.c
// pins the decisions, this pins the half that needs a filesystem: config
// parsing, content verification, the accept/reject verdicts, and the bytes
// GetOverride/ReadFile actually serve.
//
// It compiles the REAL loader -- platform/native_custom_tracks.c is included
// directly, so there is no reimplementation to drift. That also puts the file's
// statics in scope, which is what lets one process run every scenario from a
// clean slate (see reset_loader below).
//
//   cc -Wall -Wextra -m32 -DCTR_CUSTOM_TRACKS -DCTR_NATIVE -DBUILD=926
//      -I . -I include -o /tmp/test-custom-track-load tools/test-custom-track-load.c
//   /tmp/test-custom-track-load
//
// -m32 is required: the engine headers static-assert retail structure sizes,
// which only hold with 32-bit pointers. Exit 0 = every assertion held.
//
// The binding behaviour under test, in one sentence: the loader serves a custom
// track's bytes only when both source files hash to exactly what the feature
// config promised, and every other outcome refuses loudly and falls back to the
// retail BIGFILE rather than serving something the config did not name.
//
// What this pins:
//   1. the disarmed states that must behave exactly like retail: no config.ini,
//      a config.ini with no [CustomTracks] section, and a section naming no
//      files,
//   2. the happy path end to end: parse, hash, arm, and then serve the right
//      source file for each of the eight subfile slots,
//   3. every refusal, each as its own scenario: wrong hash, a truncated file, a
//      missing file, a missing folder, an absent expected digest, a malformed
//      expected digest, and an out-of-range levelID,
//   4. that a refusal is LOUD -- each scenario asserts on the loader's own log
//      output, because a silent fallback is the failure mode this feature
//      exists to prevent,
//   5. that a refusal is TOTAL: contentVerified staying 0 also switches the
//      Purple-destination redirect off, so unverified content cannot produce a
//      7-lap race on the retail track sitting in the mapped slot,
//   6. the serve-time size re-check, which catches a file swapped or truncated
//      after startup verification,
//   7. that ReadFile fills the payload and zero-pads the sector-rounded tail,
//      which is the contract LOAD_ReadFile_ex's CD path is written against.
//
// NOTE ON CIRCULARITY: the expected digests here are produced by the same
// SHA-256 this file links. That is deliberate -- what is under test here is the
// loader's accept/reject wiring, not the digest. The primitive itself is pinned
// independently against the published NIST vectors in
// tools/test-custom-track-policy.c, so a broken SHA-256 fails there.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define CTR_CUSTOM_TRACKS 1

#include "platform/native_custom_tracks.c"

// regionsEXE.h defines the global `sdata` pointer as &sdata_static, so pulling
// in common.h drags in that one symbol. The loader never dereferences it -- it
// touches no engine state at all -- so an empty definition is all the link
// needs, and it stays empty precisely BECAUSE the loader is engine-independent.
struct sData sdata_static;

static int g_failures = 0;
static char g_log[65536];

static void expect_int(int got, int want, const char *what)
{
	if (got == want)
		return;
	printf("FAIL %s: got %d, want %d\n", what, got, want);
	g_failures++;
}

static void expect_log_contains(const char *needle, const char *what)
{
	if (strstr(g_log, needle) != NULL)
		return;
	printf("FAIL %s: loader log did not mention \"%s\"\n", what, needle);
	printf("  log was:\n%s\n", g_log);
	g_failures++;
}

// ---------------------------------------------------------------------------
// Scenario plumbing.
// ---------------------------------------------------------------------------

// Deterministic pseudo-random bytes, so a "track file" is real content rather
// than a run of zeroes that any bug could accidentally reproduce.
static void write_blob(const char *path, unsigned seed, size_t bytes)
{
	FILE *f = fopen(path, "wb");
	size_t i;
	unsigned state = seed * 2654435761u + 1u;

	if (f == NULL)
	{
		printf("FAIL: could not create %s\n", path);
		g_failures++;
		return;
	}

	for (i = 0; i < bytes; i++)
	{
		state = state * 1103515245u + 12345u;
		fputc((int)((state >> 16) & 0xff), f);
	}
	fclose(f);
}

static void hash_file_hex(const char *path, char out[NATIVE_SHA256_HEX_BYTES])
{
	struct NativeSha256Ctx ctx;
	unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
	unsigned char buf[4096];
	size_t got;
	FILE *f = fopen(path, "rb");

	out[0] = '\0';
	if (f == NULL)
		return;

	NativeSha256_Init(&ctx);
	while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
		NativeSha256_Update(&ctx, buf, got);
	fclose(f);

	NativeSha256_Final(&ctx, digest);
	NativeSha256_ToHex(digest, out);
}

static void write_config(const char *text)
{
	FILE *f = fopen("config.ini", "w");

	if (f == NULL)
	{
		printf("FAIL: could not write config.ini\n");
		g_failures++;
		return;
	}
	fputs(text, f);
	fclose(f);
}

// Put the loader back to its pre-startup state. Legal here because this file
// includes the loader's translation unit, so its statics are in scope; it is
// what lets one process exercise every scenario without re-exec.
static void reset_loader(void)
{
	s_customTracksLoaded = 0;
	memset(&s_customTrackConfig, 0, sizeof(s_customTrackConfig));
	memset(&s_customTrackVrm, 0, sizeof(s_customTrackVrm));
	memset(&s_customTrackLev, 0, sizeof(s_customTrackLev));
}

// Run CustomTrack_Load with its stdout captured into g_log, so a scenario can
// assert that a refusal was actually announced.
static void load_capturing_log(void)
{
	FILE *redirected;
	FILE *readback;
	size_t got;
	int savedFd = dup(fileno(stdout));

	fflush(stdout);
	g_log[0] = '\0';

	redirected = freopen("loader.log", "w", stdout);
	if (redirected == NULL)
	{
		close(savedFd);
		printf("FAIL: could not capture loader stdout\n");
		g_failures++;
		return;
	}

	CustomTrack_Load();

	fflush(stdout);
	dup2(savedFd, fileno(stdout));
	close(savedFd);
	clearerr(stdout);

	readback = fopen("loader.log", "rb");
	if (readback != NULL)
	{
		got = fread(g_log, 1, sizeof(g_log) - 1, readback);
		g_log[got] = '\0';
		fclose(readback);
	}
}

// ---------------------------------------------------------------------------
// Scenarios.
// ---------------------------------------------------------------------------

static char g_vrmHash[NATIVE_SHA256_HEX_BYTES];
static char g_levHash[NATIVE_SHA256_HEX_BYTES];

#define TEST_VRM_BYTES 4096
#define TEST_LEV_BYTES 9000 // deliberately not a multiple of 2048

static void make_track_files(void)
{
	mkdir("tracks", 0755);
	write_blob("tracks/track.vrm", 11, TEST_VRM_BYTES);
	write_blob("tracks/track.lev", 22, TEST_LEV_BYTES);
	hash_file_hex("tracks/track.vrm", g_vrmHash);
	hash_file_hex("tracks/track.lev", g_levHash);
}

static void write_good_config(void)
{
	char text[2048];

	snprintf(text, sizeof(text),
	         "[Video]\n"
	         "fullscreen = 1\n"
	         "\n"
	         "; the loader's own section\n"
	         "[CustomTracks]\n"
	         "custom_track_level = 6\n"
	         "custom_track_vrm = tracks/track.vrm\n"
	         "custom_track_lev = tracks/track.lev\n"
	         "custom_track_vrm_sha256 = %s\n"
	         "custom_track_lev_sha256 = %s\n"
	         "custom_track_race = 1\n"
	         "custom_track_race_cup = 4\n"
	         "custom_track_race_laps = 7\n",
	         g_vrmHash, g_levHash);
	write_config(text);
}

// Assert the loader is fully disarmed: no bytes served for any subfile in the
// mapped group, and the event destination left vanilla.
static void expect_fully_disarmed(const char *what)
{
	char label[128];
	int i;

	snprintf(label, sizeof(label), "%s: contentVerified", what);
	expect_int(CustomTrack_Config()->contentVerified, 0, label);

	snprintf(label, sizeof(label), "%s: redirect off", what);
	expect_int(CustomTrack_CupRaceRedirectActive(4, 1), 0, label);

	snprintf(label, sizeof(label), "%s: feature reports disabled", what);
	expect_int(CustomTrack_RaceFeatureEnabled(), 0, label);

	snprintf(label, sizeof(label), "%s: Purple keeps four legs", what);
	expect_int(CustomTrack_CupIsComplete(4, 1, 1), 0, label);

	for (i = 0; i < CTR_CT_GROUP_SIZE; i++)
	{
		const char *path = NULL;
		u32 size = 0;

		snprintf(label, sizeof(label), "%s: subfile %d falls back to BIGFILE", what, 48 + i);
		expect_int(CustomTrack_GetOverride(48 + i, &path, &size), 0, label);
	}
}

static void test_no_config_file(void)
{
	reset_loader();
	unlink("config.ini");
	load_capturing_log();

	expect_log_contains("config.ini not found", "no config.ini announces itself");
	expect_fully_disarmed("no config.ini");
}

static void test_config_without_section(void)
{
	reset_loader();
	write_config("[Video]\nfullscreen = 1\n[Audio]\nvolume = 80\n");
	load_capturing_log();

	expect_log_contains("no track configured", "config without [CustomTracks] announces itself");
	expect_fully_disarmed("config without [CustomTracks]");
}

static void test_happy_path(void)
{
	const char *path;
	u32 size;
	int i;

	reset_loader();
	write_good_config();
	load_capturing_log();

	expect_log_contains("armed", "happy path announces arming");
	expect_int(CustomTrack_Config()->contentVerified, 1, "happy path: contentVerified");
	expect_int(CustomTrack_Config()->mappedLevelID, 6, "happy path: mapped levelID parsed");
	expect_int(CustomTrack_Config()->raceLaps, 7, "happy path: laps parsed");
	expect_int(CustomTrack_Config()->raceCupID, 4, "happy path: cup parsed");

	// The redirect is on, and only for the configured cup.
	expect_int(CustomTrack_CupRaceRedirectActive(4, 1), 1, "happy path: Purple redirects");
	expect_int(CustomTrack_CupRaceLevelID(4, 1), 6, "happy path: races levelID 6");
	expect_int(CustomTrack_CupRaceLaps(4, 1), 7, "happy path: races 7 laps");
	expect_int(CustomTrack_CupRaceRedirectActive(0, 1), 0, "happy path: Red does not redirect");
	expect_int(CustomTrack_CupIsComplete(4, 1, 1), 1, "happy path: Purple completes after one race");
	expect_int(CustomTrack_CupIsComplete(0, 1, 1), 0, "happy path: Red still needs four legs");

	// Pair auto-expand, through the real serving path: all four mode-pairs are
	// answered by the single .vrm/.lev pair, with the right parity and size.
	for (i = 0; i < CTR_CT_GROUP_SIZE; i++)
	{
		char label[128];
		int wantLev = (i & 1);

		path = NULL;
		size = 0;
		snprintf(label, sizeof(label), "happy path: subfile %d is served", 48 + i);
		expect_int(CustomTrack_GetOverride(48 + i, &path, &size), 1, label);

		snprintf(label, sizeof(label), "happy path: subfile %d serves the %s", 48 + i, wantLev ? "lev" : "vrm");
		expect_int((int)size, wantLev ? TEST_LEV_BYTES : TEST_VRM_BYTES, label);
	}

	// The neighbouring tracks' subfiles are untouched.
	expect_int(CustomTrack_GetOverride(47, &path, &size), 0, "happy path: subfile 47 is not served");
	expect_int(CustomTrack_GetOverride(56, &path, &size), 0, "happy path: subfile 56 is not served");
	expect_int(CustomTrack_GetOverride(0, &path, &size), 0, "happy path: subfile 0 is not served");

	// ReadFile's contract: fill the payload, zero-pad out to the sector-rounded
	// buffer. LOAD_ReadFile_ex allocates ceil(size/2048)*2048 and the CD path it
	// mirrors leaves no stale bytes in the tail.
	{
		u32 sectorBytes = ((TEST_LEV_BYTES + 2047u) / 2048u) * 2048u;
		unsigned char *buf = (unsigned char *)malloc(sectorBytes);
		unsigned char *expected = (unsigned char *)malloc(TEST_LEV_BYTES);
		FILE *f;
		u32 j;
		int tailClean = 1;

		memset(buf, 0xCD, sectorBytes); // poison, so padding must be written
		path = NULL;
		size = 0;
		CustomTrack_GetOverride(49, &path, &size); // odd slot: the LEV

		expect_int(CustomTrack_ReadFile(path, buf, sectorBytes, size), 1, "happy path: ReadFile succeeds");

		f = fopen("tracks/track.lev", "rb");
		if (f != NULL)
		{
			if (fread(expected, 1, TEST_LEV_BYTES, f) != TEST_LEV_BYTES)
			{
				printf("FAIL: could not read back the test LEV\n");
				g_failures++;
			}
			fclose(f);
		}
		expect_int(memcmp(buf, expected, TEST_LEV_BYTES), 0, "happy path: served bytes are the file's bytes");

		for (j = TEST_LEV_BYTES; j < sectorBytes; j++)
		{
			if (buf[j] != 0)
			{
				tailClean = 0;
				break;
			}
		}
		expect_int(tailClean, 1, "happy path: sector tail is zero-padded");

		free(buf);
		free(expected);
	}
}

static void test_wrong_hash(void)
{
	char text[2048];

	reset_loader();
	// A well-formed digest that is simply not this file's.
	snprintf(text, sizeof(text),
	         "[CustomTracks]\n"
	         "custom_track_level = 6\n"
	         "custom_track_vrm = tracks/track.vrm\n"
	         "custom_track_lev = tracks/track.lev\n"
	         "custom_track_vrm_sha256 = %s\n"
	         "custom_track_lev_sha256 = 0000000000000000000000000000000000000000000000000000000000000000\n"
	         "custom_track_race = 1\n",
	         g_vrmHash);
	write_config(text);
	load_capturing_log();

	expect_log_contains("sha256 mismatch", "wrong hash names the mismatch");
	expect_log_contains("DISARMED", "wrong hash disarms loudly");
	expect_log_contains("expected 0000", "wrong hash prints the expected digest");
	expect_fully_disarmed("wrong hash");
}

static void test_truncated_file(void)
{
	reset_loader();
	write_good_config();
	// Same path, same expected digest, fewer bytes: the packaging accident the
	// hash check exists to catch.
	write_blob("tracks/track.lev", 22, TEST_LEV_BYTES - 512);
	load_capturing_log();

	expect_log_contains("sha256 mismatch", "truncated file is a mismatch");
	expect_fully_disarmed("truncated file");

	write_blob("tracks/track.lev", 22, TEST_LEV_BYTES); // restore
}

static void test_missing_file(void)
{
	char text[2048];

	reset_loader();
	snprintf(text, sizeof(text),
	         "[CustomTracks]\n"
	         "custom_track_level = 6\n"
	         "custom_track_vrm = tracks/track.vrm\n"
	         "custom_track_lev = tracks/does-not-exist.lev\n"
	         "custom_track_vrm_sha256 = %s\n"
	         "custom_track_lev_sha256 = %s\n"
	         "custom_track_race = 1\n",
	         g_vrmHash, g_levHash);
	write_config(text);
	load_capturing_log();

	expect_log_contains("file missing or empty", "missing file is named");
	expect_fully_disarmed("missing file");
}

static void test_missing_folder(void)
{
	char text[2048];

	reset_loader();
	snprintf(text, sizeof(text),
	         "[CustomTracks]\n"
	         "custom_track_level = 6\n"
	         "custom_track_vrm = no-such-folder/track.vrm\n"
	         "custom_track_lev = no-such-folder/track.lev\n"
	         "custom_track_vrm_sha256 = %s\n"
	         "custom_track_lev_sha256 = %s\n"
	         "custom_track_race = 1\n",
	         g_vrmHash, g_levHash);
	write_config(text);
	load_capturing_log();

	expect_log_contains("file missing or empty", "missing folder is refused like a missing file");
	expect_fully_disarmed("missing folder");
}

static void test_no_expected_hash(void)
{
	reset_loader();
	write_config("[CustomTracks]\n"
	             "custom_track_level = 6\n"
	             "custom_track_vrm = tracks/track.vrm\n"
	             "custom_track_lev = tracks/track.lev\n"
	             "custom_track_race = 1\n");
	load_capturing_log();

	// Naming files with no digests must NOT be treated as "nothing to check".
	expect_log_contains("no expected sha256", "absent digest is refused, not skipped");
	expect_fully_disarmed("no expected hash");
}

static void test_malformed_expected_hash(void)
{
	char text[2048];

	reset_loader();
	snprintf(text, sizeof(text),
	         "[CustomTracks]\n"
	         "custom_track_level = 6\n"
	         "custom_track_vrm = tracks/track.vrm\n"
	         "custom_track_lev = tracks/track.lev\n"
	         "custom_track_vrm_sha256 = %s\n"
	         "custom_track_lev_sha256 = deadbeef\n"
	         "custom_track_race = 1\n",
	         g_vrmHash);
	write_config(text);
	load_capturing_log();

	expect_log_contains("not 64 hex digits", "short digest is refused as malformed");
	expect_fully_disarmed("malformed expected hash");
}

static void test_out_of_range_level(void)
{
	char text[2048];

	reset_loader();
	// 18 is NITRO_COURT, the first battle arena: past the end of
	// data.ArcadeDifficulty[18], which is indexed by levelID unchecked.
	snprintf(text, sizeof(text),
	         "[CustomTracks]\n"
	         "custom_track_level = 18\n"
	         "custom_track_vrm = tracks/track.vrm\n"
	         "custom_track_lev = tracks/track.lev\n"
	         "custom_track_vrm_sha256 = %s\n"
	         "custom_track_lev_sha256 = %s\n"
	         "custom_track_race = 1\n",
	         g_vrmHash, g_levHash);
	write_config(text);
	load_capturing_log();

	expect_log_contains("must be an arcade slot", "out-of-range levelID is refused");
	expect_fully_disarmed("out-of-range levelID");
}

static void test_missing_level_key(void)
{
	char text[2048];

	reset_loader();
	snprintf(text, sizeof(text),
	         "[CustomTracks]\n"
	         "custom_track_vrm = tracks/track.vrm\n"
	         "custom_track_lev = tracks/track.lev\n"
	         "custom_track_vrm_sha256 = %s\n"
	         "custom_track_lev_sha256 = %s\n"
	         "custom_track_race = 1\n",
	         g_vrmHash, g_levHash);
	write_config(text);
	load_capturing_log();

	expect_log_contains("must be an arcade slot", "files with no slot are refused");
	expect_fully_disarmed("no custom_track_level");
}

static void test_bad_lap_count(void)
{
	char text[2048];

	reset_loader();
	snprintf(text, sizeof(text),
	         "[CustomTracks]\n"
	         "custom_track_level = 6\n"
	         "custom_track_vrm = tracks/track.vrm\n"
	         "custom_track_lev = tracks/track.lev\n"
	         "custom_track_vrm_sha256 = %s\n"
	         "custom_track_lev_sha256 = %s\n"
	         "custom_track_race = 1\n"
	         "custom_track_race_laps = 99\n",
	         g_vrmHash, g_levHash);
	write_config(text);
	load_capturing_log();

	// The content is fine, so the loader still ARMS and serves the track; only
	// the event destination is refused. These are separable failures.
	expect_log_contains("custom_track_race_laps must be 1..7", "illegal lap count is refused");
	expect_int(CustomTrack_Config()->contentVerified, 1, "bad laps: content still verified");
	expect_int(CustomTrack_CupRaceRedirectActive(4, 1), 0, "bad laps: redirect is off");
	expect_int(CustomTrack_CupIsComplete(4, 1, 1), 0, "bad laps: Purple keeps four legs");
}

static void test_race_flag_off_still_serves(void)
{
	char text[2048];
	const char *path = NULL;
	u32 size = 0;

	reset_loader();
	snprintf(text, sizeof(text),
	         "[CustomTracks]\n"
	         "custom_track_level = 6\n"
	         "custom_track_vrm = tracks/track.vrm\n"
	         "custom_track_lev = tracks/track.lev\n"
	         "custom_track_vrm_sha256 = %s\n"
	         "custom_track_lev_sha256 = %s\n"
	         "custom_track_race = 0\n",
	         g_vrmHash, g_levHash);
	write_config(text);
	load_capturing_log();

	// The loader and the event destination are independent seams: a verified
	// track can be mapped onto a slot without the Gem Cup being touched.
	expect_int(CustomTrack_Config()->contentVerified, 1, "race off: content verified");
	expect_int(CustomTrack_GetOverride(48, &path, &size), 1, "race off: track is still served");
	expect_int(CustomTrack_CupRaceRedirectActive(4, 1), 0, "race off: redirect is off");
	expect_int(CustomTrack_CupIsComplete(4, 1, 1), 0, "race off: Purple keeps four legs");
	expect_int(CustomTrack_CupIsComplete(4, 1, 4), 1, "race off: Purple completes at leg 4");
}

static void test_serve_time_size_recheck(void)
{
	const char *path = NULL;
	u32 size = 0;

	reset_loader();
	write_good_config();
	load_capturing_log();
	expect_int(CustomTrack_Config()->contentVerified, 1, "size recheck: armed to begin with");
	expect_int(CustomTrack_GetOverride(49, &path, &size), 1, "size recheck: serves before the swap");

	// Swap the file under the running game. Startup verification already passed,
	// so only the serve-time check can catch this.
	write_blob("tracks/track.lev", 99, TEST_LEV_BYTES - 2048);

	expect_int(CustomTrack_GetOverride(49, &path, &size), 0, "size recheck: refuses the swapped file");
	expect_int(CustomTrack_GetOverride(48, &path, &size), 1, "size recheck: the untouched VRM still serves");

	write_blob("tracks/track.lev", 22, TEST_LEV_BYTES); // restore
}

int main(void)
{
	char tmpl[] = "/tmp/ctr-custom-track-test-XXXXXX";
	char *dir = mkdtemp(tmpl);

	if (dir == NULL || chdir(dir) != 0)
	{
		printf("FAIL: could not create a scratch directory\n");
		return 1;
	}

	make_track_files();

	test_no_config_file();
	test_config_without_section();
	test_happy_path();
	test_wrong_hash();
	test_truncated_file();
	test_missing_file();
	test_missing_folder();
	test_no_expected_hash();
	test_malformed_expected_hash();
	test_out_of_range_level();
	test_missing_level_key();
	test_bad_lap_count();
	test_race_flag_off_still_serves();
	test_serve_time_size_recheck();

	if (g_failures != 0)
	{
		printf("FAILED: %d assertion(s) (scratch dir kept at %s)\n", g_failures, dir);
		return 1;
	}

	printf("test-custom-track-load: all assertions held\n");
	return 0;
}
