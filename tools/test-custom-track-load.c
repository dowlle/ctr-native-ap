// Integration assertions for the custom-track loader against REAL files on disk,
// for the Baby T Park event spike. Each decision's own heading in
// native_custom_tracks_policy.h carries its rung, so this line cannot go stale.
// Where
// tools/test-custom-track-policy.c pins the decisions and
// tools/test-custom-tracks-seedcfg.cpp pins the wire parse, this pins the half
// that needs a filesystem: config parsing, seed-descriptor application, content
// verification, and the bytes GetOverride/ReadFile actually serve.
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
// track's bytes only for the race a SEED told it about, only after both files
// hash to exactly what that seed promised, and every other outcome refuses
// loudly and falls back to the retail BIGFILE.
//
// WHAT RUNG 2C CHANGED. config.ini now carries only the two file paths. The
// digests, lap count, host slot, replaced cup and box policy all arrive in a
// seed descriptor, and no descriptor means the feature is fully off however
// config.ini is written. That is why almost every scenario below is "apply a
// descriptor and see what happens" rather than "write a config and reload".
//
// What this pins:
//   1. the off states that must behave exactly like retail: no config.ini, no
//      track files, and -- new in 2c -- files configured but no seed,
//   2. the happy path end to end: parse, apply, hash, arm, then serve the right
//      source file for each of the eight subfile slots,
//   3. every refusal, each as its own scenario, and each asserting the same two
//      things: nothing is served AND the cup is left vanilla. Those must move
//      together, because serving without the redirect races retail bytes for the
//      seed's Gem, and redirecting without serving races the host slot's retail
//      track for it,
//   4. that a refusal is LOUD -- scenarios assert on the loader's own log,
//      because a silent fallback is the failure mode this feature exists to
//      prevent,
//   5. the two measured flags the loader actually acts on (ai_nav, spawns),
//   6. descriptor lifecycle: idempotence by content (this is called every frame,
//      so a repeat must not re-hash a multi-MiB file), replacement by a
//      different seed, and withdrawal,
//   7. the serve-time size re-check, and ReadFile's zero-padded sector tail.
//
// NOTE ON CIRCULARITY: the expected digests here are produced by the same
// SHA-256 this file links. That is deliberate -- what is under test is the
// loader's accept/reject wiring, not the digest. The primitive itself is pinned
// independently against the published NIST vectors in
// tools/test-custom-track-policy.c.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define CTR_CUSTOM_TRACKS 1

#include "platform/native_custom_tracks.c"

// The engine's own pointer-map fixup, compiled here for the same reason the
// loader is: the ST1 scenario below turns on exactly how a LEV expresses an
// absent table entry, and a transcription of LOAD_RunPtrMap could drift from
// the one the game runs. Pulling in the translation unit costs five stubs (see
// below) and nothing else -- none of its other functions are called.
#include "game/LOAD/LOAD_Assets.c"

// The engine's own RNG, compiled in for the same reason as the fixup above. The
// event race's field is a permutation driven by these exact draws, and the
// roster scenario below runs the real LOAD_Robots1P through the real generator
// rather than a stand-in. It is self-contained -- two words of state, no engine
// globals -- so it costs no stubs, and LOAD_Assets.c now needs it to link.
#include "game/MixRNG/RngDeadCoed.c"

// regionsEXE.h defines the global `sdata` pointer as &sdata_static, so pulling
// in common.h drags in that one symbol. The loader never dereferences it -- it
// touches no engine state at all -- so an empty definition is all the link
// needs, and it stays empty precisely BECAUSE the loader is engine-independent.
struct sData sdata_static;

// The rest of LOAD_Assets.c's link footprint. LOAD_RunPtrMap itself references
// none of these; they are here so the translation unit links, and are wired to
// abort rather than to no-op so a test that started calling one says so.
struct Data data;
NativeConfig g_config;

// LOAD_AppendQueue aborts by default, like the other stubs, so a test that
// started calling one says so. The roster scenario is the one exception: it
// drives the real LOAD_DriverMPK to find out WHICH PACK the event race queues,
// which is the fork the shuffle depends on, so for the length of that scenario
// the stub records the queued subfile indices instead of aborting.
static int g_queueRecording = 0;
static int g_queued[8];
static int g_queuedCount = 0;

void LOAD_AppendQueue(struct BigHeader *bigfile, int type, int fileIndex, void *destinationPtr, void (*callback)(struct LoadQueueSlot *))
{
	(void)bigfile; (void)type; (void)destinationPtr; (void)callback;

	if (g_queueRecording)
	{
		if (g_queuedCount < (int)(sizeof g_queued / sizeof g_queued[0]))
			g_queued[g_queuedCount++] = fileIndex;
		return;
	}

	printf("FAIL: LOAD_AppendQueue is not available in this harness\n");
	abort();
}

void *LOAD_ReadFile_ex(struct BigHeader *bigfile, u32 loadType, int subfileIndex, void *ptrDst, u32 *sizePtr, void (*callback)(struct LoadQueueSlot *))
{
	(void)bigfile; (void)loadType; (void)subfileIndex; (void)ptrDst; (void)sizePtr; (void)callback;
	printf("FAIL: LOAD_ReadFile_ex is not available in this harness\n");
	abort();
}

void *MEMPACK_AllocMem(int size)
{
	(void)size;
	printf("FAIL: MEMPACK_AllocMem is not available in this harness\n");
	abort();
}

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

static void expect_str(const char *got, const char *want, const char *what)
{
	if (got != NULL && strcmp(got, want) == 0)
		return;
	printf("FAIL %s:\n  got  %s\n  want %s\n", what, got == NULL ? "(null)" : got, want);
	g_failures++;
}

static void expect_log_lacks(const char *needle, const char *what)
{
	if (strstr(g_log, needle) == NULL)
		return;
	printf("FAIL %s: loader log mentioned \"%s\" when it should not have\n", what, needle);
	printf("  log was:\n%s\n", g_log);
	g_failures++;
}

static void expect_log_silent(const char *what)
{
	if (g_log[0] == '\0')
		return;
	printf("FAIL %s: loader logged when it should have said nothing:\n%s\n", what, g_log);
	g_failures++;
}

static int log_line_count(void)
{
	int lines = 0;
	const char *p;

	for (p = g_log; *p != '\0'; p++)
	{
		if (*p == '\n')
			lines++;
	}

	return lines;
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
	memset(&s_descriptor, 0, sizeof(s_descriptor));
	s_haveDescriptor = 0;
}

// Run loader calls with stdout captured into g_log, so a scenario can assert
// what was (or was not) announced.
#define CAPTURING(stmt)                                                                            \
	do                                                                                             \
	{                                                                                              \
		int savedFd = dup(fileno(stdout));                                                         \
		FILE *readback;                                                                            \
		size_t got;                                                                                \
		fflush(stdout);                                                                            \
		g_log[0] = '\0';                                                                           \
		if (freopen("loader.log", "w", stdout) == NULL)                                            \
		{                                                                                          \
			close(savedFd);                                                                        \
			break;                                                                                 \
		}                                                                                          \
		stmt;                                                                                      \
		fflush(stdout);                                                                            \
		dup2(savedFd, fileno(stdout));                                                             \
		close(savedFd);                                                                            \
		clearerr(stdout);                                                                          \
		readback = fopen("loader.log", "rb");                                                      \
		if (readback != NULL)                                                                      \
		{                                                                                          \
			got = fread(g_log, 1, sizeof(g_log) - 1, readback);                                    \
			g_log[got] = '\0';                                                                     \
			fclose(readback);                                                                      \
		}                                                                                          \
	} while (0)

// ---------------------------------------------------------------------------
// Fixtures.
// ---------------------------------------------------------------------------

static char g_vrmHash[NATIVE_SHA256_HEX_BYTES];
static char g_levHash[NATIVE_SHA256_HEX_BYTES];

#define TEST_VRM_BYTES 4096
#define TEST_LEV_BYTES 9000 // deliberately not a multiple of 2048
#define TEST_HOST      6    // the arcade slot the bytes borrow
#define TEST_CUP_LEVEL 104  // Purple Gem Cup
#define TEST_CUP       4
#define TEST_BASE      (TEST_HOST * CTR_CT_GROUP_SIZE)

static void make_track_files(void)
{
	mkdir("tracks", 0755);
	write_blob("tracks/track.vrm", 11, TEST_VRM_BYTES);
	write_blob("tracks/track.lev", 22, TEST_LEV_BYTES);
	hash_file_hex("tracks/track.vrm", g_vrmHash);
	hash_file_hex("tracks/track.lev", g_levHash);
}

// config.ini as rung 2c leaves it: two paths and nothing else.
static void write_paths_config(void)
{
	write_config("[Video]\n"
	             "fullscreen = 1\n"
	             "\n"
	             "; the loader's own section -- paths only; the seed says the rest\n"
	             "[CustomTracks]\n"
	             "custom_track_vrm = tracks/track.vrm\n"
	             "custom_track_lev = tracks/track.lev\n");
}

// The event seed's descriptor, matching the fixture files.
static struct CustomTrackSeedDescriptor good_descriptor(void)
{
	struct CustomTrackSeedDescriptor d;

	memset(&d, 0, sizeof(d));
	d.laps = 7;
	d.hostLevelID = TEST_HOST;
	d.replacesCupLevelID = TEST_CUP_LEVEL;
	d.boxes = 1;
	snprintf(d.levSha256, sizeof(d.levSha256), "%s", g_levHash);
	snprintf(d.vrmSha256, sizeof(d.vrmSha256), "%s", g_vrmHash);
	d.flagCrates = 1;
	d.flagCtrLetters = 1;
	d.flagRelicCrates = 1;
	d.flagAiNav = 1;
	d.flagMinimap = 0;
	d.flagGhosts = 0;
	d.flagSpawns = 8;
	d.flagCheckpoints = 35;
	return d;
}

// The load context of the event race: a gem cup is in progress, it is the
// replaced cup, and the level being loaded is the host slot.
static struct CustomTrackLoadContext event_ctx(void)
{
	struct CustomTrackLoadContext ctx;

	ctx.levelID = TEST_HOST;
	ctx.adventureCupActive = 1;
	ctx.cupID = TEST_CUP;
	return ctx;
}

// Assert the loader is fully off: no bytes for any subfile in the host group,
// and the event destination left vanilla.
static void expect_fully_off(const char *what)
{
	struct CustomTrackLoadContext ctx = event_ctx();
	char label[160];
	int i;

	snprintf(label, sizeof(label), "%s: contentVerified", what);
	expect_int(CustomTrack_Config()->contentVerified, 0, label);

	snprintf(label, sizeof(label), "%s: redirect off", what);
	expect_int(CustomTrack_CupRaceRedirectActive(TEST_CUP, 1), 0, label);

	snprintf(label, sizeof(label), "%s: feature reports disabled", what);
	expect_int(CustomTrack_RaceFeatureEnabled(), 0, label);

	snprintf(label, sizeof(label), "%s: the cup keeps four legs", what);
	expect_int(CustomTrack_CupIsComplete(TEST_CUP, 1, 1), 0, label);

	snprintf(label, sizeof(label), "%s: HUD reads TRACK n/4", what);
	expect_int(CustomTrack_CupLegCount(TEST_CUP, 1), 4, label);

	snprintf(label, sizeof(label), "%s: box policy untouched", what);
	expect_int(CustomTrack_BoxVerdict(TEST_HOST, 1, TEST_CUP), CTR_CT_BOX_UNCHANGED, label);

	for (i = 0; i < CTR_CT_GROUP_SIZE; i++)
	{
		const char *path = NULL;
		u32 size = 0;

		snprintf(label, sizeof(label), "%s: subfile %d falls back to BIGFILE", what, TEST_BASE + i);
		expect_int(CustomTrack_GetOverride(TEST_BASE + i, &ctx, &path, &size), 0, label);
	}
}

// Bring the loader up with paths configured and a descriptor applied.
static void arm_with(struct CustomTrackSeedDescriptor d)
{
	reset_loader();
	write_paths_config();
	CAPTURING(CustomTrack_Load());
	CAPTURING(CustomTrack_ApplySeedDescriptor(&d));
}

// ---------------------------------------------------------------------------
// Off states.
// ---------------------------------------------------------------------------

static void test_no_config_file(void)
{
	reset_loader();
	unlink("config.ini");
	CAPTURING(CustomTrack_Load());

	expect_log_contains("config.ini not found", "no config.ini announces itself");
	expect_fully_off("no config.ini");
}

static void test_config_without_paths(void)
{
	reset_loader();
	write_config("[Video]\nfullscreen = 1\n[Audio]\nvolume = 80\n");
	CAPTURING(CustomTrack_Load());

	expect_log_contains("no track files configured", "a config with no paths announces itself");
	expect_fully_off("config without paths");
}

// THE RUNG-2C PRECEDENCE RULE. Files configured, loader loaded, but no seed has
// spoken: the feature is off. There is no longer any config-only path to a
// working custom track.
static void test_paths_but_no_seed(void)
{
	reset_loader();
	write_paths_config();
	CAPTURING(CustomTrack_Load());

	expect_log_contains("awaiting a seed descriptor", "the loader says it is waiting on a seed");
	expect_fully_off("paths configured but no seed");
}

// ---------------------------------------------------------------------------
// The happy path.
// ---------------------------------------------------------------------------

static void test_happy_path(void)
{
	struct CustomTrackLoadContext ctx = event_ctx();
	const char *path;
	u32 size;
	int i;

	arm_with(good_descriptor());

	expect_log_contains("armed", "arming is announced");
	expect_log_contains("still loads retail bytes", "arming says the retail pad is unaffected");
	expect_int(CustomTrack_Config()->contentVerified, 1, "happy path: contentVerified");
	expect_int(CustomTrack_Config()->mappedLevelID, TEST_HOST, "happy path: host slot from the seed");
	expect_int(CustomTrack_Config()->raceLaps, 7, "happy path: laps from the seed");
	expect_int(CustomTrack_Config()->raceCupID, TEST_CUP, "happy path: cup from the seed");

	expect_int(CustomTrack_CupRaceRedirectActive(TEST_CUP, 1), 1, "happy path: the cup redirects");
	expect_int(CustomTrack_CupRaceLevelID(TEST_CUP, 1), TEST_HOST, "happy path: races the host slot");
	expect_int(CustomTrack_CupRaceLaps(TEST_CUP, 1), 7, "happy path: races 7 laps");
	expect_int(CustomTrack_CupRaceRedirectActive(0, 1), 0, "happy path: Red does not redirect");
	expect_int(CustomTrack_CupIsComplete(TEST_CUP, 1, 1), 1, "happy path: completes after one race");
	expect_int(CustomTrack_CupLegCount(TEST_CUP, 1), 1, "happy path: HUD reads TRACK n/1");

	// Pair auto-expand through the real serving path.
	for (i = 0; i < CTR_CT_GROUP_SIZE; i++)
	{
		char label[128];
		int wantLev = (i & 1);

		path = NULL;
		size = 0;
		snprintf(label, sizeof(label), "happy path: subfile %d is served", TEST_BASE + i);
		expect_int(CustomTrack_GetOverride(TEST_BASE + i, &ctx, &path, &size), 1, label);

		snprintf(label, sizeof(label), "happy path: subfile %d serves the %s", TEST_BASE + i,
		         wantLev ? "lev" : "vrm");
		expect_int((int)size, wantLev ? TEST_LEV_BYTES : TEST_VRM_BYTES, label);
	}

	expect_int(CustomTrack_GetOverride(TEST_BASE - 1, &ctx, &path, &size), 0,
	           "happy path: the subfile below the group is not served");
	expect_int(CustomTrack_GetOverride(TEST_BASE + CTR_CT_GROUP_SIZE, &ctx, &path, &size), 0,
	           "happy path: the subfile above the group is not served");

	// ReadFile's contract: fill the payload, zero-pad out to the sector-rounded
	// buffer, matching what LOAD_ReadFile_ex's CD path allocates.
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
		CustomTrack_GetOverride(TEST_BASE + 1, &ctx, &path, &size);

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
		expect_int(memcmp(buf, expected, TEST_LEV_BYTES), 0, "happy path: served bytes are the file's");

		for (j = TEST_LEV_BYTES; j < sectorBytes; j++)
			if (buf[j] != 0)
			{
				tailClean = 0;
				break;
			}
		expect_int(tailClean, 1, "happy path: sector tail is zero-padded");

		free(buf);
		free(expected);
	}
}

// The rung-2a deliverable, still true under a seed descriptor: in one armed
// session the event race is served custom bytes and the host slot's own retail
// race pad is not.
static void test_retail_pad_stays_retail(void)
{
	struct CustomTrackLoadContext eventLoad = event_ctx();
	struct CustomTrackLoadContext retailPad;
	struct CustomTrackLoadContext otherCupLeg;
	const char *path = NULL;
	u32 size = 0;
	int i;

	arm_with(good_descriptor());

	retailPad = event_ctx();
	retailPad.adventureCupActive = 0; // cupID deliberately left stale at 4
	otherCupLeg = event_ctx();
	otherCupLeg.cupID = 1;

	for (i = 0; i < CTR_CT_GROUP_SIZE; i++)
	{
		char label[128];

		snprintf(label, sizeof(label), "event race: subfile %d serves custom bytes", TEST_BASE + i);
		expect_int(CustomTrack_GetOverride(TEST_BASE + i, &eventLoad, &path, &size), 1, label);

		snprintf(label, sizeof(label), "retail pad: subfile %d falls back to BIGFILE", TEST_BASE + i);
		expect_int(CustomTrack_GetOverride(TEST_BASE + i, &retailPad, &path, &size), 0, label);

		snprintf(label, sizeof(label), "another cup's leg: subfile %d falls back", TEST_BASE + i);
		expect_int(CustomTrack_GetOverride(TEST_BASE + i, &otherCupLeg, &path, &size), 0, label);
	}

	expect_int(CustomTrack_BoxVerdict(TEST_HOST, 1, TEST_CUP), CTR_CT_BOX_ALLOW,
	           "event race: boxes allowed when the seed says so");
	expect_int(CustomTrack_BoxVerdict(TEST_HOST, 0, TEST_CUP), CTR_CT_BOX_UNCHANGED,
	           "retail pad: box policy unchanged");
}

// ---------------------------------------------------------------------------
// Refusals: content.
// ---------------------------------------------------------------------------

static void test_wrong_hash(void)
{
	struct CustomTrackSeedDescriptor d = good_descriptor();

	snprintf(d.levSha256, sizeof(d.levSha256),
	         "0000000000000000000000000000000000000000000000000000000000000000");
	arm_with(d);

	expect_log_contains("sha256 mismatch", "wrong digest names the mismatch");
	expect_log_contains("DISARMED", "wrong digest disarms loudly");
	expect_log_contains("the cup stays vanilla", "wrong digest says the cup is untouched");
	expect_fully_off("seed digest does not match the file");
}

static void test_truncated_file(void)
{
	write_blob("tracks/track.lev", 22, TEST_LEV_BYTES - 512);
	arm_with(good_descriptor());

	expect_log_contains("sha256 mismatch", "a truncated file is a mismatch");
	expect_fully_off("truncated file");

	write_blob("tracks/track.lev", 22, TEST_LEV_BYTES); // restore
}

static void test_missing_file(void)
{
	struct CustomTrackSeedDescriptor d = good_descriptor();

	reset_loader();
	write_config("[CustomTracks]\n"
	             "custom_track_vrm = tracks/track.vrm\n"
	             "custom_track_lev = tracks/does-not-exist.lev\n");
	CAPTURING(CustomTrack_Load());
	CAPTURING(CustomTrack_ApplySeedDescriptor(&d));

	expect_log_contains("file missing or empty", "a missing file is named");
	expect_fully_off("missing file");
}

static void test_missing_folder(void)
{
	struct CustomTrackSeedDescriptor d = good_descriptor();

	reset_loader();
	write_config("[CustomTracks]\n"
	             "custom_track_vrm = no-such-folder/track.vrm\n"
	             "custom_track_lev = no-such-folder/track.lev\n");
	CAPTURING(CustomTrack_Load());
	CAPTURING(CustomTrack_ApplySeedDescriptor(&d));

	expect_log_contains("file missing or empty", "a missing folder is refused like a missing file");
	expect_fully_off("missing folder");
}

// A seed binds a custom track this client has no files for. Loud, because the
// player CAN fix it, and total, because the alternative is racing the host
// slot's retail track for a Gem the seed thinks is on the custom track.
static void test_seed_without_local_files(void)
{
	struct CustomTrackSeedDescriptor d = good_descriptor();

	reset_loader();
	write_config("[Video]\nfullscreen = 1\n");
	CAPTURING(CustomTrack_Load());
	CAPTURING(CustomTrack_ApplySeedDescriptor(&d));

	expect_log_contains("names no custom_track_lev/custom_track_vrm files",
	                    "a seed with no local files says exactly that");
	expect_log_contains("add the track files", "and tells the player how to fix it");
	expect_fully_off("seed binds a track this client has no files for");
}

// ---------------------------------------------------------------------------
// Refusals: descriptor values the engine cannot serve.
// ---------------------------------------------------------------------------

static void test_bad_host_slot(void)
{
	struct CustomTrackSeedDescriptor d = good_descriptor();

	d.hostLevelID = 18; // NITRO_COURT: past data.ArcadeDifficulty[18]
	arm_with(d);
	expect_log_contains("not an arcade slot", "an out-of-range host slot is refused");
	expect_fully_off("host slot 18");

	d = good_descriptor();
	d.hostLevelID = -1;
	arm_with(d);
	expect_fully_off("host slot -1");
}

static void test_bad_cup(void)
{
	struct CustomTrackSeedDescriptor d = good_descriptor();

	d.replacesCupLevelID = 105;
	arm_with(d);
	expect_log_contains("is not a Gem Cup", "an out-of-range cup LevelID is refused");
	expect_fully_off("cup LevelID 105");

	// A cup INDEX where a LevelID belongs is the mistake most likely to be made.
	d = good_descriptor();
	d.replacesCupLevelID = 4;
	arm_with(d);
	expect_fully_off("cup index 4 sent where a LevelID belongs");
}

static void test_bad_laps(void)
{
	struct CustomTrackSeedDescriptor d = good_descriptor();

	d.laps = 8;
	arm_with(d);
	expect_log_contains("outside 1..7", "an out-of-range lap count is refused");
	expect_fully_off("8 laps");

	d = good_descriptor();
	d.laps = 0;
	arm_with(d);
	expect_fully_off("0 laps");
}

// The two measured flags the loader actually acts on. The other six are carried
// and logged but inert, so a track that measured false for them still arms.
static void test_measured_flags(void)
{
	struct CustomTrackSeedDescriptor d = good_descriptor();

	d.flagAiNav = 0;
	arm_with(d);
	expect_log_contains("no AI nav paths", "a track with no AI nav is refused");
	expect_fully_off("ai_nav false");

	// The Purple cup grids five karts (one player + four bosses), so four spawns
	// is one short and five is exactly enough.
	d = good_descriptor();
	d.flagSpawns = 4;
	arm_with(d);
	expect_log_contains("fewer driver spawns", "too few spawns is refused");
	expect_fully_off("4 spawns for a 5-kart grid");

	d = good_descriptor();
	d.flagSpawns = 5;
	arm_with(d);
	expect_int(CustomTrack_Config()->contentVerified, 1, "5 spawns is exactly enough for cup 4");

	// A non-Purple cup grids eight, so the same track would need more.
	d = good_descriptor();
	d.replacesCupLevelID = 101;
	d.flagSpawns = 5;
	arm_with(d);
	expect_fully_off("5 spawns for a non-Purple cup's 8-kart grid");

	// The inert six do not gate the race.
	d = good_descriptor();
	d.flagCrates = 0;
	d.flagCtrLetters = 0;
	d.flagRelicCrates = 0;
	d.flagMinimap = 0;
	d.flagGhosts = 0;
	arm_with(d);
	expect_int(CustomTrack_Config()->contentVerified, 1, "the inert flags do not gate the race");
}

// ---------------------------------------------------------------------------
// Box policy and descriptor lifecycle.
// ---------------------------------------------------------------------------

static void test_boxes_denied(void)
{
	struct CustomTrackSeedDescriptor d = good_descriptor();

	// The event YAML emits boxes:false, because placement still resolves through
	// the host slot's retail identity: allowing them would light the HOST track's
	// box locations at the host track's coordinates.
	d.boxes = 0;
	arm_with(d);

	expect_int(CustomTrack_Config()->contentVerified, 1, "boxes denied: still armed");
	expect_int(CustomTrack_BoxVerdict(TEST_HOST, 1, TEST_CUP), CTR_CT_BOX_DENY,
	           "boxes:false denies boxes on the event race");
	expect_int(CustomTrack_BoxVerdict(TEST_HOST, 0, TEST_CUP), CTR_CT_BOX_UNCHANGED,
	           "denial never leaks onto the retail pad");
	expect_int(CustomTrack_BoxVerdict(TEST_HOST, 1, 1), CTR_CT_BOX_UNCHANGED,
	           "denial never leaks onto another cup's leg");

	// Denying boxes changes nothing else about the race.
	expect_int(CustomTrack_CupRaceRedirectActive(TEST_CUP, 1), 1, "boxes denied: still redirects");
	expect_int(CustomTrack_CupRaceLaps(TEST_CUP, 1), 7, "boxes denied: still 7 laps");
}

// ApplySeedDescriptor runs once a frame from AP_OnFrame, so a repeat with the
// same descriptor must cost a memcmp and nothing else. An empty log is the
// observable: re-hashing would announce itself.
static void test_apply_is_idempotent(void)
{
	struct CustomTrackSeedDescriptor d = good_descriptor();

	arm_with(d);
	expect_int(CustomTrack_Config()->contentVerified, 1, "armed once");

	CAPTURING(expect_int(CustomTrack_ApplySeedDescriptor(&d), 1, "a repeat apply stays armed"));
	expect_log_silent("an unchanged descriptor re-hashes nothing");

	CAPTURING(CustomTrack_ApplySeedDescriptor(&d));
	expect_log_silent("and still says nothing on a third frame");
	expect_int(CustomTrack_Config()->contentVerified, 1, "still armed after repeats");
}

// A different seed replaces the descriptor outright rather than merging into it.
static void test_descriptor_replacement(void)
{
	struct CustomTrackSeedDescriptor d = good_descriptor();

	arm_with(d);
	expect_int(CustomTrack_Config()->raceLaps, 7, "first seed: 7 laps");
	expect_int(CustomTrack_Config()->raceCupID, TEST_CUP, "first seed: cup 4");

	d.laps = 3;
	d.replacesCupLevelID = 101; // Green
	d.hostLevelID = 9;
	CAPTURING(CustomTrack_ApplySeedDescriptor(&d));

	expect_int(CustomTrack_Config()->contentVerified, 1, "second seed arms");
	expect_int(CustomTrack_Config()->raceLaps, 3, "second seed: 3 laps");
	expect_int(CustomTrack_Config()->raceCupID, 1, "second seed: cup 1");
	expect_int(CustomTrack_Config()->mappedLevelID, 9, "second seed: host slot 9");
	expect_int(CustomTrack_CupRaceRedirectActive(TEST_CUP, 1), 0, "the old cup no longer redirects");
	expect_int(CustomTrack_CupRaceRedirectActive(1, 1), 1, "the new cup does");

	// A refused replacement must not leave the old seed's track armed.
	d.laps = 99;
	CAPTURING(CustomTrack_ApplySeedDescriptor(&d));
	expect_fully_off("a refused replacement does not leave the old seed armed");
}

// Disconnecting, or connecting to a seed with no block, takes the whole feature
// with it.
static void test_withdrawal(void)
{
	arm_with(good_descriptor());
	expect_int(CustomTrack_Config()->contentVerified, 1, "armed before withdrawal");

	CAPTURING(CustomTrack_ClearSeedDescriptor());
	expect_log_contains("withdrawn", "withdrawal announces itself");
	expect_fully_off("descriptor withdrawn");

	// NULL is the same thing, since the AP bridge may pass one.
	arm_with(good_descriptor());
	CAPTURING(CustomTrack_ApplySeedDescriptor(NULL));
	expect_fully_off("a NULL descriptor withdraws");
}

static void test_serve_time_size_recheck(void)
{
	struct CustomTrackLoadContext ctx = event_ctx();
	const char *path = NULL;
	u32 size = 0;

	arm_with(good_descriptor());
	expect_int(CustomTrack_GetOverride(TEST_BASE + 1, &ctx, &path, &size), 1,
	           "size recheck: serves first");

	// Swap the file under the running game. Startup verification already passed,
	// so only the serve-time check can catch this.
	write_blob("tracks/track.lev", 99, TEST_LEV_BYTES - 2048);

	expect_int(CustomTrack_GetOverride(TEST_BASE + 1, &ctx, &path, &size), 0,
	           "size recheck: refuses the swapped file");
	expect_int(CustomTrack_GetOverride(TEST_BASE, &ctx, &path, &size), 1,
	           "size recheck: the untouched VRM still serves");

	write_blob("tracks/track.lev", 22, TEST_LEV_BYTES); // restore
}

// ---------------------------------------------------------------------------
// An absent camera in the level's ST1 table.
// ---------------------------------------------------------------------------
//
// The loader serves a custom track's LEV byte for byte, so whatever the
// packager put in that file's SpawnType1 table is what the engine consumes. The
// scenario this pins is the one that crashed the event candidate.
//
// HOW A LEV SAYS "THIS ENTRY IS NOT HERE". A DRAM file's first word is the
// offset of its pointer map; the body follows at byte 4; LOAD_RunPtrMap walks
// the map and adds the body's load address to every slot the map lists. A slot
// the map does NOT list is never touched, so a stored 0 stays 0 and arrives as
// a NULL pointer. That is the entire mechanism, and it is why an absent entry
// is invisible to a count check: the table is full width, the hole is inside it.
//
// Retail never does this. Relocated through this same function, all 18 arcade
// LEVs in the NTSC-U BIGFILE come back with count == 4 and a non-NULL entry at
// both camera indices, and all 7 battle arenas come back with count == 0. Baby
// T Park comes back with count == 7 and NULLs at ST1_MAP, ST1_CAMERA_EOR,
// ST1_CAMERA_PATH and ST1_CREDITS. So the two encodings of "no intro camera"
// are a short table (retail) and a full table with a hole (this track), and
// only the first is what the engine's count thresholds were written against.
//
// The images below are built rather than shipped: the real .lev is a 2.4 MB
// third-party asset that is not in this repository, and the table shape is the
// only part of it this test is about.

#define ST1_TEST_ENTRIES 7

// Lay out a DRAM-file image whose Level::ptrSpawnType1 points at a table of
// `count` entries, where `present` says which of them the pointer map lists.
// Returns the malloc'd image; *outBody is the relocated body.
static char *build_lev_image(int count, const int *present, char **outBody)
{
	const int levelSize = ((int)sizeof(struct Level) + 3) & ~3;
	const int st1Off = levelSize;
	const int entriesOff = st1Off + (int)sizeof(struct SpawnType1);
	const int targetsOff = entriesOff + ST1_TEST_ENTRIES * 4;
	const int targetSize = 16;
	const int ptrMapOff = targetsOff + ST1_TEST_ENTRIES * targetSize;

	// The map always lists Level::ptrSpawnType1 itself, plus one slot per
	// present entry. An absent entry is simply left out -- that omission IS the
	// thing under test, so it must not be written as a special case elsewhere.
	int offsets[1 + ST1_TEST_ENTRIES];
	int numOffsets = 0;
	int i;

	int imageSize = 4 + ptrMapOff + (int)sizeof(struct DramPointerMap) + (int)sizeof(offsets);
	char *image = calloc(1, (size_t)imageSize);
	char *body;

	if (image == NULL)
	{
		printf("FAIL: out of memory building a LEV image\n");
		g_failures++;
		return NULL;
	}

	*(int *)&image[0] = ptrMapOff;
	body = &image[4];

	// Pre-relocation, every listed slot holds a body-relative offset.
	*(int *)&body[offsetof(struct Level, ptrSpawnType1)] = st1Off;
	offsets[numOffsets++] = (int)offsetof(struct Level, ptrSpawnType1);

	*(int *)&body[st1Off] = count;

	for (i = 0; i < ST1_TEST_ENTRIES; i++)
	{
		if (!present[i])
			continue; // left at 0 and out of the map: this is an absent entry

		*(int *)&body[entriesOff + i * 4] = targetsOff + i * targetSize;
		offsets[numOffsets++] = entriesOff + i * 4;
	}

	((struct DramPointerMap *)&body[ptrMapOff])->numBytes = numOffsets * 4;
	memcpy(DRAM_GETOFFSETS((struct DramPointerMap *)&body[ptrMapOff]), offsets, (size_t)numOffsets * 4);

	// The engine's own fixup, exactly as LOAD_DramFileCallback invokes it.
	LOAD_RunPtrMap(body, DRAM_GETOFFSETS((struct DramPointerMap *)&body[ptrMapOff]), numOffsets);

	*outBody = body;
	return image;
}

static void test_absent_camera_path(void)
{
	// Baby T Park's measured shape: a full-width table with four holes.
	const int babyTPark[ST1_TEST_ENTRIES] = {
		0, // ST1_MAP          -- no minimap
		1, // ST1_SPAWN
		0, // ST1_CAMERA_EOR   -- no end-of-race cameras
		0, // ST1_CAMERA_PATH  -- no start-line fly-in path
		1, // ST1_NTROPY
		1, // ST1_NOXIDE
		0  // ST1_CREDITS
	};
	// A retail arcade track: short table, every entry present.
	const int retailArcade[ST1_TEST_ENTRIES] = { 1, 1, 1, 1, 0, 0, 0 };

	char *body;
	char *image = build_lev_image(7, babyTPark, &body);
	struct Level *lev;
	struct SpawnType1 *st1;
	void **entries;

	if (image == NULL)
		return;

	lev = (struct Level *)body;
	st1 = lev->ptrSpawnType1;

	// The relocation did happen -- otherwise "everything is NULL" would pass
	// the assertions below for the wrong reason.
	expect_int(st1 == (struct SpawnType1 *)(body + (((int)sizeof(struct Level) + 3) & ~3)), 1,
	           "the table pointer was relocated to the body");
	expect_int(st1->count, 7, "the table is full width");

	entries = ST1_GETPOINTERS(st1);
	expect_int(entries[ST1_SPAWN] != NULL, 1, "a listed entry relocated to a real address");
	expect_int(entries[ST1_NTROPY] != NULL, 1, "and so did the N. Tropy ghost");
	expect_int(entries[ST1_NOXIDE] != NULL, 1, "and the Oxide ghost");

	// The omission is what produces the NULL. No zeroing, no sentinel.
	expect_int(entries[ST1_CAMERA_PATH] == NULL, 1, "an unlisted entry arrives NULL");
	expect_int(entries[ST1_CAMERA_EOR] == NULL, 1, "and so does the end-of-race one");
	expect_int(entries[ST1_MAP] == NULL, 1, "and the minimap");
	expect_int(entries[ST1_CREDITS] == NULL, 1, "and the credits camera");

	// THE REGRESSION. Both count thresholds CAM.c used to guard its two cameras
	// with pass on this table, which is why the fly-in dereferenced NULL + 0x354
	// at CAM.c's fly-in interpolation and the end-of-race block would have
	// dereferenced NULL the frame the first driver finished.
	expect_int(st1->count < 4, 0, "the old fly-in threshold does NOT catch this table");
	expect_int(st1->count < 3, 0, "nor does the old end-of-race threshold");

	// What the guards ask now. Both refuse, so the fly-in reports itself done
	// and the end-of-race camera is never armed.
	expect_int(CustomTrackPolicy_St1EntryPresent(st1->count, ST1_CAMERA_PATH, (const void *const *)entries), 0,
	           "the fly-in guard refuses a track with no camera path");
	expect_int(CustomTrackPolicy_St1EntryPresent(st1->count, ST1_CAMERA_EOR, (const void *const *)entries), 0,
	           "the end-of-race guard refuses a track with no EOR table");

	free(image);

	// The same walk on a retail-shaped table, where the guards must be inert.
	image = build_lev_image(4, retailArcade, &body);
	if (image == NULL)
		return;

	lev = (struct Level *)body;
	st1 = lev->ptrSpawnType1;
	entries = ST1_GETPOINTERS(st1);

	expect_int(st1->count, 4, "a retail arcade table is four entries");
	expect_int(entries[ST1_CAMERA_PATH] != NULL, 1, "with an intro camera path");
	expect_int(entries[ST1_CAMERA_EOR] != NULL, 1, "and end-of-race cameras");

	// Old and new agree on retail content, in both directions. This is the
	// safety argument for the swap, asserted rather than asserted-to.
	expect_int(CustomTrackPolicy_St1EntryPresent(st1->count, ST1_CAMERA_PATH, (const void *const *)entries),
	           st1->count >= 4, "the fly-in guard matches the old threshold on retail");
	expect_int(CustomTrackPolicy_St1EntryPresent(st1->count, ST1_CAMERA_EOR, (const void *const *)entries),
	           st1->count >= 3, "and the end-of-race guard matches its own");

	free(image);
}

// ---------------------------------------------------------------------------
// A skybox that does not fit the level slot's primitive budget.
// ---------------------------------------------------------------------------
//
// The engine sizes the frame's primitive arena BY LEVEL ID
// (data.primMem_SizePerLEV_1P[levelID] << 10, game/MAIN/MainInit.c). A custom
// track borrows an arcade slot, so it inherits a budget retail chose for that
// slot's content while bringing its own. DrawSky trusted the LEV's face counts
// against that budget, and past the end of the arena the native GPU-link bridge
// has no token for the primitive's address and aborts.
//
// Measured by relocating the real files through LOAD_RunPtrMap: the event
// track's skybox has 2,772 faces in EVERY one of its eight segments, and
// DrawSky_Full draws four segments per frame. The 18 retail arcade tracks have
// 69 to 385 faces across their worst four, and four of them have no skybox at
// all. This scenario builds both shapes and drives the clamp over them.

#define SKY_TEST_SEGMENTS NUM_SKYBOX_SEGMENTS

// Lay out a DRAM-file image whose Level::ptr_skybox points at a Skybox with the
// given per-segment face counts. The faces themselves are never read -- what is
// under test is the DEMAND those counts create -- so all eight segments point at
// one small shared array rather than megabytes of unread face records.
static char *build_lev_image_with_sky(const int *facesPerSegment, char **outBody)
{
	const int levelSize = ((int)sizeof(struct Level) + 3) & ~3;
	const int skyOff = levelSize;
	const int vertsOff = skyOff + (int)sizeof(struct Skybox);
	const int facesOff = vertsOff + (int)sizeof(struct ShortVertex);
	const int ptrMapOff = facesOff + (int)sizeof(struct SkyboxFace);

	int offsets[2 + SKY_TEST_SEGMENTS];
	int numOffsets = 0;
	int i;

	int imageSize = 4 + ptrMapOff + (int)sizeof(struct DramPointerMap) + (int)sizeof(offsets);
	char *image = calloc(1, (size_t)imageSize);
	char *body;
	struct Skybox *sky;

	if (image == NULL)
	{
		printf("FAIL: out of memory building a LEV image\n");
		g_failures++;
		return NULL;
	}

	*(int *)&image[0] = ptrMapOff;
	body = &image[4];

	*(int *)&body[offsetof(struct Level, ptr_skybox)] = skyOff;
	offsets[numOffsets++] = (int)offsetof(struct Level, ptr_skybox);

	sky = (struct Skybox *)(void *)&body[skyOff];
	sky->numVertex = 1;
	*(int *)&body[skyOff + (int)offsetof(struct Skybox, ptrVertex)] = vertsOff;
	offsets[numOffsets++] = skyOff + (int)offsetof(struct Skybox, ptrVertex);

	for (i = 0; i < SKY_TEST_SEGMENTS; i++)
	{
		sky->numFaces[i] = (s16)facesPerSegment[i];
		*(int *)&body[skyOff + (int)offsetof(struct Skybox, ptrFaces) + i * 4] = facesOff;
		offsets[numOffsets++] = skyOff + (int)offsetof(struct Skybox, ptrFaces) + i * 4;
	}

	((struct DramPointerMap *)&body[ptrMapOff])->numBytes = numOffsets * 4;
	memcpy(DRAM_GETOFFSETS((struct DramPointerMap *)&body[ptrMapOff]), offsets, (size_t)numOffsets * 4);
	LOAD_RunPtrMap(body, DRAM_GETOFFSETS((struct DramPointerMap *)&body[ptrMapOff]), numOffsets);

	*outBody = body;
	return image;
}

// The worst four-segment frame, over all eight camera rotations. DrawSky_Full
// draws base, base+1, base-1 and base-2 modulo 8 (game/DrawSky.c).
static long sky_worst_four(const struct Skybox *sky)
{
	long worst = 0;
	int b;

	for (b = 0; b < SKY_TEST_SEGMENTS; b++)
	{
		const int idx[4] = { b, (b + 1) & 7, (b - 1) & 7, (b - 2) & 7 };
		long sum = 0;
		int k;

		for (k = 0; k < 4; k++)
			sum += (u16)sky->numFaces[idx[k]];

		if (sum > worst)
			worst = sum;
	}

	return worst;
}

// Walk `faces` faces through the clamp the way DrawSky_Piece does, and report
// how many were emitted and where the cursor ended up.
static long sky_emit_clamped(char *arenaStart, const char *guard, long faces, char **cursorOut)
{
	char *prim = arenaStart;
	long emitted = 0;
	long i;

	for (i = 0; i < faces; i++)
	{
		if (!CustomTrackPolicy_PrimFits(prim, sizeof(POLY_G3), guard))
			break;

		prim += sizeof(POLY_G3);
		emitted++;
	}

	*cursorOut = prim;
	return emitted;
}

static void test_sky_primitive_budget(void)
{
	// data.primMem_SizePerLEV_1P[6] == 0x67 (game/zGlobal_DATA.c), shifted
	// left by 10 in MainInit.c for a 1P race.
	const long budget = 0x67L << 10;
	const long usable = budget - 0x100; // MainDB_PrimMem: guardEnd = end - 0x100

	// Measured shapes.
	const int eventTrack[SKY_TEST_SEGMENTS] = { 2772, 2772, 2772, 2772, 2772, 2772, 2772, 2772 };
	const int retailSlot6[SKY_TEST_SEGMENTS] = { 48, 48, 48, 48, 48, 48, 48, 48 }; // 192 across four

	char *arena = malloc((size_t)budget);
	char *body;
	char *image;
	char *cursor;
	const char *guard;
	struct Level *lev;
	struct Skybox *sky;
	long worst;
	long emitted;

	if (arena == NULL)
	{
		printf("FAIL: out of memory allocating a stand-in primitive arena\n");
		g_failures++;
		return;
	}
	guard = arena + usable;

	// --- the event track's shape -------------------------------------------
	image = build_lev_image_with_sky(eventTrack, &body);
	if (image == NULL)
	{
		free(arena);
		return;
	}

	lev = (struct Level *)body;
	sky = lev->ptr_skybox;
	expect_int(sky != NULL, 1, "the skybox pointer was relocated");
	expect_int(sky->numVertex, 1, "and points at the skybox we built");
	expect_int((u16)sky->numFaces[0], 2772, "the event track's first segment is 2,772 faces");

	worst = sky_worst_four(sky);
	expect_int(worst == 11088, 1, "four segments of it are 11,088 faces");
	expect_int(worst * (long)sizeof(POLY_G3) > usable, 1, "which is more than the whole arena holds");

	// Unclamped, this is where the abort came from: the cursor would run
	// 205,248 bytes past the end of a 105,472-byte arena.
	expect_int(worst * (long)sizeof(POLY_G3) - usable == 205248L, 1,
	           "and overruns it by 205,248 bytes");

	emitted = sky_emit_clamped(arena, guard, worst, &cursor);

	// The clamp stops inside the arena, every time, and draws as much sky as
	// the budget allows rather than refusing the frame outright.
	expect_int(cursor <= guard, 1, "the clamped cursor never passes the guard");
	expect_int(emitted < worst, 1, "the clamp did stop short of the demand");
	expect_int(emitted > 0, 1, "but still drew as much sky as fits");
	expect_int(emitted == usable / (long)sizeof(POLY_G3), 1, "filling the arena exactly");

	free(image);

	// --- a retail-shaped sky must be untouched ------------------------------
	image = build_lev_image_with_sky(retailSlot6, &body);
	if (image == NULL)
	{
		free(arena);
		return;
	}

	lev = (struct Level *)body;
	sky = lev->ptr_skybox;
	worst = sky_worst_four(sky);
	expect_int(worst == 192, 1, "retail slot 6's sky is 192 faces across four segments");

	emitted = sky_emit_clamped(arena, guard, worst, &cursor);
	expect_int(emitted == worst, 1, "every retail sky face is drawn, so the clamp is inert on retail");
	expect_int(cursor <= guard, 1, "and the cursor stays well inside the arena");

	free(image);

	// --- a level with no skybox at all --------------------------------------
	// Four of the 18 retail arcade tracks have a NULL ptr_skybox, and
	// DrawSky_Full returns before touching the context. Pinned so the added
	// guardEnd plumbing cannot introduce a dereference on that path.
	{
		const int noFaces[SKY_TEST_SEGMENTS] = { 0, 0, 0, 0, 0, 0, 0, 0 };

		image = build_lev_image_with_sky(noFaces, &body);
		if (image != NULL)
		{
			lev = (struct Level *)body;
			expect_int(sky_worst_four(lev->ptr_skybox), 0, "an empty skybox demands nothing");
			emitted = sky_emit_clamped(arena, guard, 0, &cursor);
			expect_int(emitted == 0, 1, "and emits nothing");
			expect_int(cursor == arena, 1, "leaving the cursor where it started");
			free(image);
		}
	}

	free(arena);
}

// ---------------------------------------------------------------------------
// The primitive arena a served load is given.
// ---------------------------------------------------------------------------
//
// The arena is sized per LEVEL ID from a retail table, so a custom track
// borrowing a slot inherits a budget chosen for someone else's geometry. The
// expansion is keyed on the SAME decision as serving the bytes, which is what
// keeps the two from disagreeing: a load that races retail content out of the
// BIGFILE must also get retail's arena, and a load that races custom bytes must
// get the arena those bytes were measured against.
//
// This is the seam that pins the pair together, because CustomTrack_ServingLoad
// is what MainInit_PrimMem asks and CustomTrack_GetOverride is what the reader
// asks, and they must answer identically for the same load.
static void test_prim_arena_for_served_load(void)
{
	const unsigned long retailArena = 0x67uL << 10; // the borrowed slot's own budget
	struct CustomTrackLoadContext eventLoad;
	struct CustomTrackLoadContext retailPad;
	const char *path = NULL;
	u32 size = 0;

	arm_with(good_descriptor());

	eventLoad = event_ctx();
	retailPad = event_ctx();
	retailPad.adventureCupActive = 0; // cupID deliberately left stale

	// The event race: served, and expanded on the SERVING term alone. Asked at a
	// player count the 1P widening does not cover, so the two reasons for the
	// floor stay separable and this assertion still pins the loader seam rather
	// than the widening.
	expect_int(CustomTrack_ServingLoad(eventLoad.levelID, eventLoad.adventureCupActive, eventLoad.cupID), 1,
	           "the event race is a served load");
	expect_int(CustomTrackPolicy_PrimArenaBytes(1, 2, retailArena) == CTR_CT_PRIM_ARENA_BYTES, 1,
	           "so it gets the measured arena on the serving term alone");

	// A retail pad to the very same host slot in the same armed session: not
	// served, so the serving term expands nothing. Same slot, same session,
	// different answer.
	expect_int(CustomTrack_ServingLoad(retailPad.levelID, retailPad.adventureCupActive, retailPad.cupID), 0,
	           "a retail pad to the host slot is not a served load");
	expect_int(CustomTrackPolicy_PrimArenaBytes(0, 2, retailArena) == retailArena, 1,
	           "so at a player count the widening skips it keeps the retail arena byte for byte");

	// The two questions agree: the load that gets custom bytes is the load that
	// gets the expanded arena.
	expect_int(CustomTrack_GetOverride(TEST_HOST * CTR_CT_GROUP_SIZE + 1, &eventLoad, &path, &size),
	           CustomTrack_ServingLoad(eventLoad.levelID, eventLoad.adventureCupActive, eventLoad.cupID),
	           "serving the bytes and sizing the arena are the same decision");
	expect_int(CustomTrack_GetOverride(TEST_HOST * CTR_CT_GROUP_SIZE + 1, &retailPad, &path, &size),
	           CustomTrack_ServingLoad(retailPad.levelID, retailPad.adventureCupActive, retailPad.cupID),
	           "and they agree on the retail pad too");

	// With the feature disarmed the serving term expands nothing, whatever the
	// load says.
	CustomTrack_ClearSeedDescriptor();
	expect_int(CustomTrack_ServingLoad(eventLoad.levelID, eventLoad.adventureCupActive, eventLoad.cupID), 0,
	           "a withdrawn descriptor serves no load");
	expect_int(CustomTrackPolicy_PrimArenaBytes(0, 2, retailArena) == retailArena, 1,
	           "and expands no arena");
}

// ---------------------------------------------------------------------------
// The floor on a RETAIL level load, and retail sizing left alone.
// ---------------------------------------------------------------------------
//
// The widening's whole point is that it applies to loads the loader knows
// nothing about, so it has to be shown working in a session where the loader is
// armed for a completely different track AND in a session where it is disarmed
// entirely. It runs the retail sizing rule itself -- the same branch structure
// MainInit_GetPrimMemSize uses -- rather than a hand-copied number, so a change
// to that function's constants shows up here as a changed input, not a silently
// stale expectation.
static unsigned long retail_prim_arena_bytes(int numPlyr, int levelID, int adventureArena)
{
	// The retail 1P table, game/zGlobal_DATA.c. Only the entries this test
	// asks about are transcribed; the rest are zero, which the bound below
	// never reaches.
	static const unsigned char sizePerLev1P[] = {0x5f, 0x67, 0x67, 0x5f, 0x6e, 0x5f, 0x67, 0x67, 0x67, 0x5f, 0x67, 0x5f, 0x5f, 0x5f,
	                                             0x5f, 0x67, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x00, 0x00, 0x00};

	if (numPlyr != 1)
		return 0; // this helper speaks only for the 1P branch

	if (adventureArena)
		return 0x1c000uL;

	if ((unsigned)(levelID - 30) < 9u) // INTRO_RACE_TODAY .. INTRO_OXIDE
		return 0x1e000uL;

	if (levelID < 25) // GEM_STONE_VALLEY
		return (unsigned long)sizePerLev1P[levelID] << 10;

	return 0x17c00uL;
}

static void test_prim_arena_for_retail_load(void)
{
	const unsigned long hubRetail = retail_prim_arena_bytes(1, 25, 1);
	const unsigned long arcadeRetail = retail_prim_arena_bytes(1, 6, 0);
	struct CustomTrackLoadContext hubLoad;

	// The figures the retail rule produces for the two loads under test, pinned
	// so a change to MainInit_GetPrimMemSize's constants is visible here.
	expect_int(hubRetail == 114688uL, 1, "the hub's retail arena is 114,688 bytes");
	expect_int(arcadeRetail == 105472uL, 1, "and a levelID-6 1P race's is 105,472");

	// A session armed for the event track. The hub is a completely different
	// level and the loader serves it nothing, yet it still gets the floor --
	// which is exactly what the widening is.
	arm_with(good_descriptor());

	hubLoad = event_ctx();
	hubLoad.levelID = 25;

	expect_int(CustomTrack_ServingLoad(hubLoad.levelID, hubLoad.adventureCupActive, hubLoad.cupID), 0,
	           "the hub is not a served load, even in an armed session");
	expect_int(CustomTrackPolicy_PrimArenaBytes(0, 1, hubRetail) == CTR_CT_PRIM_ARENA_BYTES, 1,
	           "and it gets the floor anyway, on the 1P term");
	expect_int(CustomTrackPolicy_PrimArenaBytes(0, 1, arcadeRetail) == CTR_CT_PRIM_ARENA_BYTES, 1,
	           "so does a retail 1P arcade race in the same session");

	// Disarmed: the loader is doing nothing at all and the floor still applies,
	// because it does not depend on the loader.
	CustomTrack_ClearSeedDescriptor();
	expect_int(CustomTrack_ServingLoad(hubLoad.levelID, hubLoad.adventureCupActive, hubLoad.cupID), 0,
	           "a withdrawn descriptor serves the hub nothing");
	expect_int(CustomTrackPolicy_PrimArenaBytes(0, 1, hubRetail) == CTR_CT_PRIM_ARENA_BYTES, 1,
	           "and the hub still gets the floor");

	// RETAIL SIZING IS UNTOUCHED. Every load the widening does not cover keeps
	// the exact bytes the retail rule computed, in the same armed session.
	{
		int levelID;

		for (levelID = 0; levelID < 25; levelID++)
		{
			unsigned long retail = retail_prim_arena_bytes(1, levelID, 0);

			if (CustomTrackPolicy_PrimArenaBytes(0, 2, retail) != retail)
			{
				expect_int(0, 1, "a 2P load kept its retail arena");
				break;
			}
			if (CustomTrackPolicy_PrimArenaBytes(0, 4, retail) != retail)
			{
				expect_int(0, 1, "a 4P load kept its retail arena");
				break;
			}
		}
		expect_int(levelID, 25, "all 25 arcade slots keep retail sizing outside 1P");
	}
}

// ---------------------------------------------------------------------------
// The rendered-quadblock list stops at the end of its array.
// ---------------------------------------------------------------------------
//
// The engine's own array length is tied to CTR_CT_RENDERED_QUADBLOCK_SLOTS by a
// CTR_STATIC_ASSERT next to the bound in game/226/226_00_DrawLevelOvr1P.c, so
// drift is a build failure rather than something this harness has to catch.
// What it pins here is the BEHAVIOUR of the two call sites: that the pair of
// predicate calls the engine makes stops writing at the array's end, and that
// the same loop without them writes into what follows.
//
// The layout mirrors sdata_static: the array, and immediately after it the
// GamepadSystem, which is what a 257th rendered quadblock actually overwrites.
struct RenderedListImage
{
	void *slots[CTR_CT_RENDERED_QUADBLOCK_SLOTS];
	unsigned char neighbour[512];
};

// The bounded call site, in the order the engine runs it: append until the
// bucket is done, then terminate.
static unsigned long rendered_list_append_bounded(struct RenderedListImage *image, unsigned long appends)
{
	void **cursor = &image->slots[0];
	void **end = &image->slots[CTR_CT_RENDERED_QUADBLOCK_SLOTS];
	unsigned long written = 0;
	unsigned long i;

	for (i = 0; i < appends; i++)
	{
		if (!CustomTrackPolicy_RenderedSlotsFit(cursor, end, sizeof *cursor, CTR_CT_RENDERED_APPEND_SLOTS))
			continue;

		*cursor = (void *)(uintptr_t)(i + 1);
		cursor++;
		written++;
	}

	if (CustomTrackPolicy_RenderedSlotsFit(cursor, end, sizeof *cursor, 1uL))
		*cursor = NULL;

	return written;
}

// The same loop with the bound removed: what this code did before, and what
// retail's arena arithmetic used to make unreachable.
static unsigned long rendered_list_append_unbounded(struct RenderedListImage *image, unsigned long appends)
{
	void **cursor = &image->slots[0];
	unsigned long i;

	for (i = 0; i < appends; i++)
	{
		*cursor = (void *)(uintptr_t)(i + 1);
		cursor++;
	}

	*cursor = NULL;
	return appends;
}

static int neighbour_is_clean(const struct RenderedListImage *image)
{
	size_t i;

	for (i = 0; i < sizeof image->neighbour; i++)
	{
		if (image->neighbour[i] != 0xAA)
			return 0;
	}

	return 1;
}

static void test_rendered_quadblock_bound(void)
{
	struct RenderedListImage image;
	unsigned long written;

	// A bucket that renders fewer than the array holds is untouched by the
	// bound: every append lands, and the terminator lands after it. This is the
	// retail-behaviour half, and it covers every frame of every retail level.
	memset(&image, 0, sizeof image);
	memset(image.neighbour, 0xAA, sizeof image.neighbour);
	written = rendered_list_append_bounded(&image, 200uL);
	expect_int(written == 200uL, 1, "200 rendered quadblocks all land");
	expect_int(image.slots[199] == (void *)(uintptr_t)200uL, 1, "the last one is where it should be");
	expect_int(image.slots[200] == NULL, 1, "and the terminator follows it");
	expect_int(neighbour_is_clean(&image), 1, "nothing was written past the array");

	// Exactly full: 255 entries plus the terminator fills all 256 slots. One
	// less and the bound would be refusing work retail does.
	memset(&image, 0, sizeof image);
	memset(image.neighbour, 0xAA, sizeof image.neighbour);
	written = rendered_list_append_bounded(&image, 255uL);
	expect_int(written == 255uL, 1, "255 rendered quadblocks all land");
	expect_int(image.slots[254] == (void *)(uintptr_t)255uL, 1, "the 255th is the last entry");
	expect_int(image.slots[255] == NULL, 1, "and the terminator takes the final slot exactly");
	expect_int(neighbour_is_clean(&image), 1, "still nothing past the array");

	// Past the array: the appends are refused and the neighbour survives.
	memset(&image, 0, sizeof image);
	memset(image.neighbour, 0xAA, sizeof image.neighbour);
	written = rendered_list_append_bounded(&image, 400uL);
	expect_int(written == 255uL, 1, "400 rendered quadblocks stop at 255");
	expect_int(image.slots[255] == NULL, 1, "the terminator still lands in the last slot");
	expect_int(neighbour_is_clean(&image), 1, "and the array's neighbour is untouched");

	// The same loop without the bound writes past the array and into the
	// neighbour. That is the corruption the bound exists for, shown rather than
	// asserted about. 300 rather than 400 only because the stand-in neighbour
	// here is 512 bytes and the real one is a whole GamepadSystem.
	memset(&image, 0, sizeof image);
	memset(image.neighbour, 0xAA, sizeof image.neighbour);
	rendered_list_append_unbounded(&image, 300uL);
	expect_int(neighbour_is_clean(&image), 0, "unbounded, the neighbour IS overwritten");
	expect_int(image.neighbour[0] != 0xAA, 1, "starting at its very first byte");
}

// ---------------------------------------------------------------------------
// The per-load render report says one thing per load.
// ---------------------------------------------------------------------------
//
// Rung 1 printed on every new per-load maximum -- 101 lines in one session --
// and keyed the load on the arena size, so two levels sharing a budget merged
// into one report. This drives the real accumulator (it is compiled into this
// harness with the rest of platform/native_custom_tracks.c) and pins the two
// things that fixed: the line count, and that the counters a completed frame
// cannot show reach the report.
static void feed_frames(int levelID, unsigned long capacity, int frames, unsigned long spendPerFrame)
{
	int i;

	for (i = 0; i < frames; i++)
	{
		CustomTrackDiag_BeginFrame(levelID, capacity);
		CustomTrackDiag_NoteFrameSpend(spendPerFrame / 2uL, (spendPerFrame * 3uL) / 4uL, spendPerFrame + (unsigned long)i, 100 + i, 40);
	}
}

static void test_render_report_is_one_line_per_load(void)
{
	const unsigned long hubCapacity = 0x1c000uL;

	// A quiet load of 600 frames, then a level change. The change is what emits,
	// so exactly one line appears and it names the level that just ended.
	CAPTURING({
		feed_frames(25, hubCapacity, 600, 90000uL);
		CustomTrackDiag_BeginFrame(6, CTR_CT_PRIM_ARENA_BYTES);
		CustomTrackDiag_NoteFrameSpend(0uL, 0uL, 1uL, 0, 0);
	});
	expect_int(log_line_count(), 1, "600 quiet frames produce exactly one line");
	expect_log_contains("level 25 over 600 frames", "and it names the load that ended and its frame count");
	expect_log_contains("reserve refused 0, rendered list full 0, bsp records dropped 0",
	                    "with all three drop counters at zero");

	// Two DIFFERENT levels that share an arena size are two loads, not one. Rung
	// 1 keyed the report on the arena size alone, so a hub-to-hub move -- and
	// under the floor every 1P load has the same capacity -- merged into a
	// single report.
	CustomTrackDiag_FlushLevelLoad(); // close the level-6 load opened above
	CAPTURING({
		feed_frames(25, CTR_CT_PRIM_ARENA_BYTES, 5, 90000uL);
		feed_frames(26, CTR_CT_PRIM_ARENA_BYTES, 5, 90000uL);
		CustomTrackDiag_FlushLevelLoad();
	});
	expect_int(log_line_count(), 2, "two levels sharing an arena size report separately");
	expect_log_contains("level 25 over 5 frames", "the first load is named");
	expect_log_contains("level 26 over 5 frames", "and so is the second");

	// Set the accumulator back up for the assertions below.
	CAPTURING({
		CustomTrackDiag_BeginFrame(6, CTR_CT_PRIM_ARENA_BYTES);
		CustomTrackDiag_NoteFrameSpend(0uL, 0uL, 1uL, 0, 0);
	});

	// Flushing the load that is still open emits its own single line, and
	// flushing again says nothing: a load is reported once.
	CAPTURING(CustomTrackDiag_FlushLevelLoad());
	expect_int(log_line_count(), 1, "the open load flushes as one line");
	expect_log_contains("level 6 over 1 frames", "naming the load that was in flight");

	CAPTURING(CustomTrackDiag_FlushLevelLoad());
	expect_log_silent("a second flush of the same load says nothing");

	// A load that refused reserves reports the count AND a loud second line
	// carrying the closest refusal, because that is the prefix cut itself.
	CAPTURING({
		CustomTrackDiag_BeginFrame(25, hubCapacity);
		CustomTrackDiag_NoteReserveRefused(9984uL, 8000uL); // 1,984 short
		CustomTrackDiag_NoteReserveRefused(6656uL, 6000uL); // 656 short: the closest
		CustomTrackDiag_NoteRenderedListFull();
		CustomTrackDiag_NoteBspRecordDropped();
		CustomTrackDiag_NoteBspRecordDropped();
		CustomTrackDiag_NoteFrameSpend(1000uL, 2000uL, 3000uL, 7, 3);
		CustomTrackDiag_FlushLevelLoad();
	});
	expect_int(log_line_count(), 3, "a load with drops reports the summary plus two loud lines");
	expect_log_contains("reserve refused 2, rendered list full 1, bsp records dropped 2",
	                    "the summary carries all three counters");
	expect_log_contains("LOST LEVEL GEOMETRY: a bucket reserve was refused 2 times across 1 frames",
	                    "the loud line names the prefix cut");
	expect_log_contains("closest 6656 bytes wanted with 6000 left", "and reports the CLOSEST refusal, not the first");
	expect_log_contains("rendered-quadblock list hit the end", "the bound reports itself too");

	// Counters raised BETWEEN this load's frames belong to no frame of it and
	// are discarded. That is what keeps a split-screen race, which never opens a
	// frame here, out of the report of the 1P load it interleaves with: the load
	// stays open the whole time, so nothing resets these counters on its own.
	CAPTURING({
		CustomTrackDiag_BeginFrame(25, hubCapacity);
		CustomTrackDiag_NoteFrameSpend(1uL, 2uL, 3uL, 1, 1);

		CustomTrackDiag_NoteReserveRefused(9984uL, 0uL); // outside any open frame
		CustomTrackDiag_NoteBspRecordDropped();
		CustomTrackDiag_NoteRenderedListFull();

		CustomTrackDiag_BeginFrame(25, hubCapacity);
		CustomTrackDiag_NoteFrameSpend(1uL, 2uL, 4uL, 1, 1);
		CustomTrackDiag_FlushLevelLoad();
	});
	expect_int(log_line_count(), 1, "and the load still reports as one line");
	expect_log_contains("level 25 over 2 frames", "with both of its own frames counted");
	expect_log_contains("reserve refused 0, rendered list full 0, bsp records dropped 0",
	                    "drops outside an open frame reach no report");
}

// ---------------------------------------------------------------------------
// The displaced cup's display name, through the REAL config parse.
//
// The policy harness pins the width rule and the gate as arithmetic. This pins
// the half that only exists in engine: that the key is read out of config.ini
// at all, that an unusable value is dropped once with a reason instead of being
// re-judged at every draw, and -- the part that would be easy to get wrong --
// that the name is NOT armed state. It comes from config.ini like the two
// paths, so it must survive a descriptor being withdrawn and replaced, while
// still never reaching a cup that is no longer displaced.
// ---------------------------------------------------------------------------

static void write_named_config(const char *nameLine)
{
	char text[512];

	snprintf(text, sizeof text,
	         "[CustomTracks]\n"
	         "custom_track_vrm = tracks/track.vrm\n"
	         "custom_track_lev = tracks/track.lev\n"
	         "%s",
	         nameLine);
	write_config(text);
}

static void test_display_name_from_config(void)
{
	struct CustomTrackSeedDescriptor d = good_descriptor();

	// The ordinary case: the key is read, accepted, and announced with the width
	// it measured, so a log from a real session says why a name was taken.
	reset_loader();
	write_named_config("custom_track_name = BABY T PARK\n");
	CAPTURING(CustomTrack_Load());
	expect_log_contains("will be called \"BABY T PARK\"", "the configured name is announced");
	expect_log_contains("187 of 238 pixels", "and the width it measured is in the log");

	CAPTURING(CustomTrack_ApplySeedDescriptor(&d));
	expect_str(CustomTrack_CupDisplayName(TEST_CUP, 1), "BABY T PARK", "the displaced cup takes the configured name");
	expect_int(CustomTrack_CupDisplayName(TEST_CUP, 0) == NULL, 1, "an arcade cup of the same id does not");
	expect_int(CustomTrack_CupDisplayName(0, 1) == NULL, 1, "and neither does another gem cup");

	// The name outlives the descriptor, exactly as the two paths do -- but it
	// cannot be SHOWN while nothing is displaced, because the accessor asks the
	// redirect predicate first. Both halves in one scenario, because getting one
	// right and the other wrong is the plausible failure.
	CAPTURING(CustomTrack_ClearSeedDescriptor());
	expect_int(CustomTrack_CupDisplayName(TEST_CUP, 1) == NULL, 1, "a withdrawn seed shows the retail name");
	expect_str(CustomTrack_Config()->raceName, "BABY T PARK", "but the configured name is still held");

	CAPTURING(CustomTrack_ApplySeedDescriptor(&d));
	expect_str(CustomTrack_CupDisplayName(TEST_CUP, 1), "BABY T PARK", "and comes back with the next seed");

	// A refused seed leaves nothing armed, so it also leaves nothing renamed.
	d = good_descriptor();
	d.hostLevelID = 18;
	reset_loader();
	write_named_config("custom_track_name = BABY T PARK\n");
	CAPTURING(CustomTrack_Load());
	CAPTURING(CustomTrack_ApplySeedDescriptor(&d));
	expect_int(CustomTrack_CupDisplayName(TEST_CUP, 1) == NULL, 1, "a refused seed renames nothing");

	// An unusable name is dropped at parse time with a reason, and the loader
	// still arms -- the name is presentation and must never be able to refuse a
	// track the seed bound.
	d = good_descriptor();
	reset_loader();
	write_named_config("custom_track_name = A VERY LONG CUSTOM TRACK NAME INDEED\n");
	CAPTURING(CustomTrack_Load());
	expect_log_contains("custom_track_name ignored", "an over-wide name is refused loudly");
	expect_log_contains("keeps its retail name", "and says what happens instead");
	CAPTURING(CustomTrack_ApplySeedDescriptor(&d));
	expect_int(CustomTrack_Config()->contentVerified, 1, "an unusable name does not stop the track arming");
	expect_int(CustomTrack_CupDisplayName(TEST_CUP, 1) == NULL, 1, "and the cup keeps its retail name");

	// No key at all is the same outcome, with nothing said about it: a client
	// that configured no name is not doing anything wrong.
	d = good_descriptor();
	reset_loader();
	write_paths_config();
	CAPTURING(CustomTrack_Load());
	expect_log_lacks("custom_track_name", "no key configured says nothing about names");
	CAPTURING(CustomTrack_ApplySeedDescriptor(&d));
	expect_int(CustomTrack_CupDisplayName(TEST_CUP, 1) == NULL, 1, "no name configured shows the retail name");
}

// ---------------------------------------------------------------------------
// The event race's field, through the REAL LOAD_Robots1P and the REAL RNG.
//
// This is the assertion the whole of decision 11 rests on. An AI's model is
// resolved by NAME against the single MPK pack the load queued, so the field
// may only ever hold characters that pack carries -- and the pack for player P
// is built around exactly what LOAD_Robots1P(P) writes. Permuting that output
// keeps the property; selecting from the roster would not.
//
// So the scenario drives the engine's own composer for every one of the sixteen
// characters a player can be, permutes with real draws from the real generator,
// and asserts on the result rather than on the reasoning.
// ---------------------------------------------------------------------------

static void test_event_roster(void)
{
	int player;
	int roll;
	int distinctFields = 0;
	int seen[8][8][8][8];

	memset(seen, 0, sizeof seen);

	for (player = 0; player < 16; player++)
	{
		int baseline[CTR_CT_ROBOT_SLOTS];
		int i;

		// The engine's own composer, not a transcription of it.
		LOAD_Robots1P((short)player);

		for (i = 0; i < CTR_CT_ROBOT_SLOTS; i++)
			baseline[i] = data.characterIDs[CTR_CT_ROBOT_FIRST_SLOT + i];

		// What the pack guarantee actually says: every id LOAD_Robots1P produces
		// is one of the eight base characters, and none of them is the player.
		// The permutation below cannot break either, but if a future change to
		// LOAD_Robots1P broke them the shuffle would inherit the break, so they
		// are asserted against the engine here rather than assumed.
		for (i = 0; i < CTR_CT_ROBOT_SLOTS; i++)
		{
			expect_int(baseline[i] >= 0 && baseline[i] < 8, 1, "LOAD_Robots1P only names characters the pack carries");
			expect_int(baseline[i] != player, 1, "and never the player's own character");
		}

		sdata->advRng.state0 = 0x30215400;
		sdata->advRng.state1 = 0x493583fe;

		for (roll = 0; roll < 64; roll++)
		{
			int draws[CTR_CT_ROBOT_SLOTS - 1];
			int ids[CTR_CT_ROBOT_SLOTS];
			int j;

			for (i = 0; i < CTR_CT_ROBOT_SLOTS; i++)
				ids[i] = baseline[i];

			for (i = 0; i < CTR_CT_ROBOT_SLOTS - 1; i++)
				draws[i] = RngDeadCoed(&sdata->advRng);

			CustomTrackPolicy_PermuteRoster(ids, CTR_CT_ROBOT_SLOTS, draws);

			// The three rules, checked on EVERY slot rather than on the first
			// four. At CTR_CT_FIELD_MAX all seven reach the grid, so all seven
			// have to hold them; at a smaller field the extra rows are simply
			// checking slots that will not be seated, which costs nothing and
			// keeps the assertion independent of the field size.
			for (i = 0; i < CTR_CT_ROBOT_SLOTS; i++)
			{
				expect_int(ids[i] >= 0 && ids[i] < 8, 1, "every racer is in the pack the load queued");
				expect_int(ids[i] != player, 1, "the player is never on track twice");

				for (j = 0; j < i; j++)
					expect_int(ids[i] != ids[j], 1, "no character races itself");
			}

			if (!seen[ids[0]][ids[1]][ids[2]][ids[3]])
			{
				seen[ids[0]][ids[1]][ids[2]][ids[3]] = 1;
				distinctFields++;
			}
		}
	}

	// The rows above all pass on a shuffle that does nothing, so this is the one
	// that fails if the swap is dropped: sixteen players times sixty-four rolls
	// must produce far more than the single field the four-boss branch gave.
	// C(7,4) * 4! = 840 ordered fields exist; the count only has to show that
	// the roll is live and reaches many of them.
	expect_int(distinctFields > 100, 1, "the event race draws a fresh field, not one fixed lineup");

	// And the field is composed at all only for the event race. This is the
	// same predicate as the byte serving and the arena sizing, so a retail pad
	// to the very same host slot in the very same armed session keeps the
	// engine's own roster -- the seam a wrong predicate here would break.
	{
		struct CustomTrackSeedDescriptor d = good_descriptor();
		struct CustomTrackLoadContext retail = event_ctx();

		arm_with(d);

		expect_int(CustomTrack_ServingLoad(TEST_HOST, 1, TEST_CUP), 1, "the event race composes a field");

		retail.adventureCupActive = 0;
		expect_int(CustomTrack_ServingLoad(retail.levelID, retail.adventureCupActive, retail.cupID), 0,
		           "a retail pad to the same host slot does not");
		expect_int(CustomTrack_ServingLoad(TEST_HOST, 1, 0), 0, "and neither does another cup's leg");
	}
}

// ---------------------------------------------------------------------------
// WHICH PACK the event race queues, through the real LOAD_DriverMPK.
//
// The scenario above pins the permutation. This pins the fork it depends on,
// and it is the fork that a shuffle would silently no-op without: an AI's model
// is resolved by name against the ONE pack the load queued, so shuffling while
// the four-boss pack is still queued would hand three of the four drivers a
// character whose model is not resident -- a NULL out of
// VehBirth_GetModelByName straight into a dereference.
//
// So the assertion is on the queued subfile index, not on the branch condition:
// dropping the serve term from that condition leaves the boss pack queued and
// turns these rows red.
// ---------------------------------------------------------------------------

static int queued_contains(int subfileIndex)
{
	int i;

	for (i = 0; i < g_queuedCount; i++)
		if (g_queued[i] == subfileIndex)
			return 1;

	return 0;
}

static void run_driver_mpk(int levelID, int adventureCup, int cupID, int player)
{
	struct GameTracker gt;

	memset(&gt, 0, sizeof gt);
	gt.levelID = (short)levelID;
	gt.gameMode1 = adventureCup ? ADVENTURE_CUP : 0;
	gt.cup.cupID = cupID;

	sdata->gGT = &gt;
	data.characterIDs[0] = (short)player;

	g_queuedCount = 0;
	g_queueRecording = 1;
	CAPTURING(LOAD_DriverMPK(NULL, 1, NULL));
	g_queueRecording = 0;

	sdata->gGT = NULL;
}

static void test_event_roster_uses_the_arcade_pack(void)
{
	const int player = 0;

	arm_with(good_descriptor());

	// The event race. It must queue the player's own 1P arcade pack -- the one
	// carrying all seven opponents -- and must NOT queue the four-boss pack.
	run_driver_mpk(TEST_HOST, 1, TEST_CUP, player);
	expect_int(queued_contains(BI_1PARCADEPACK + player), 1, "the event race queues the player's 1P arcade pack");
	expect_int(queued_contains(BI_2PARCADEPACK + 7), 0, "and not the four-boss pack");
	expect_int(data.characterIDs[1] == RIPPER_ROO && data.characterIDs[2] == PAPU_PAPU &&
	               data.characterIDs[3] == KOMODO_JOE && data.characterIDs[4] == PINSTRIPE,
	           0, "and does not race the fixed boss lineup");
	expect_log_contains("event race field:", "and says which karts it drew");

	// Every id it composed is still one the queued pack carries, checked across
	// ALL seven opponent slots now that a full field seats all of them. This is
	// the pack guarantee and the shuffle checked together, on the real fork.
	{
		int i;
		int j;

		for (i = CTR_CT_ROBOT_FIRST_SLOT; i < CTR_CT_ROBOT_FIRST_SLOT + CTR_CT_ROBOT_SLOTS; i++)
		{
			expect_int(data.characterIDs[i] >= 0 && data.characterIDs[i] < 8, 1,
			           "the event race's field is inside the pack it queued");
			expect_int(data.characterIDs[i] != player, 1, "and never the player");

			for (j = CTR_CT_ROBOT_FIRST_SLOT; j < i; j++)
				expect_int(data.characterIDs[i] != data.characterIDs[j], 1, "and never twice");
		}
	}

	// The field the descriptor's spawn count asks for, through the real
	// predicate. Baby T Park reports eight slots, so the event race seats a full
	// grid; the same load in a session armed for a five-slot track seats five.
	expect_int(CustomTrack_EventFieldSize(TEST_HOST, 1, TEST_CUP), CTR_CT_FIELD_MAX,
	           "an 8-spawn descriptor grids a full field");
	expect_int(CustomTrack_EventFieldSize(TEST_HOST, 0, TEST_CUP), 0, "a retail pad grids retail");

	{
		struct CustomTrackSeedDescriptor narrow = good_descriptor();

		narrow.flagSpawns = 5;
		arm_with(narrow);
		expect_log_contains("grids 5 karts", "a 5-spawn track says so when it arms");
		expect_int(CustomTrack_EventFieldSize(TEST_HOST, 1, TEST_CUP), CTR_CT_FIELD_MIN,
		           "and grids the floor rather than being refused");

		narrow.flagSpawns = 6;
		arm_with(narrow);
		expect_int(CustomTrack_EventFieldSize(TEST_HOST, 1, TEST_CUP), 6,
		           "a 6-spawn track grids six rather than being refused for not reporting eight");

		// One below the floor is still refused, exactly as before the field grew:
		// the point of following the spawn count is that this edge did not move.
		narrow.flagSpawns = 4;
		arm_with(narrow);
		expect_log_contains("REFUSED", "one short of the floor is still refused");
		expect_int(CustomTrack_EventFieldSize(TEST_HOST, 1, TEST_CUP), 0, "and grids nothing");

		arm_with(good_descriptor());
	}

	// End to end, from the armed descriptor to the two numbers the engine uses:
	// the driver count MainInit_Drivers seats and the standings layout that
	// count reaches. Driven through the real loader rather than through a
	// hand-built config, so a descriptor whose spawn count stopped reaching the
	// field size would show up here rather than only on screen.
	{
		int field = CustomTrack_EventFieldSize(TEST_HOST, 1, TEST_CUP);
		int seated = CustomTrackPolicy_DriverCount(5, 1, field);

		expect_int(seated, CTR_CT_FIELD_MAX, "the armed event race seats a full grid");
		expect_int(CustomTrackPolicy_StandingsUsesNarrowLayout(TEST_CUP, seated), 0,
		           "and its standings take the eight-icon layout, not the five-in-a-row one");

		// The same cup on a load that is not the event race keeps retail's five
		// and therefore keeps retail's layout, in the same armed session.
		expect_int(CustomTrackPolicy_DriverCount(5, 1, CustomTrack_EventFieldSize(3, 1, TEST_CUP)), 5,
		           "a non-event leg of the same cup still seats five");
		expect_int(CustomTrackPolicy_StandingsUsesNarrowLayout(TEST_CUP, 5), 1,
		           "and still lays them out five in a row");
	}

	// The SAME cup, in the SAME armed session, when this load is not the event
	// race: the Purple cup's own legs still get retail's four bosses out of the
	// boss pack. The displacement replaces one destination, not the cup.
	run_driver_mpk(3, 1, TEST_CUP, player);
	expect_int(queued_contains(BI_2PARCADEPACK + 7), 1, "a non-event leg of the same cup still queues the boss pack");
	expect_int(queued_contains(BI_1PARCADEPACK + player), 0, "and not the arcade pack");
	expect_int(data.characterIDs[1], RIPPER_ROO, "and races Ripper Roo");
	expect_int(data.characterIDs[4], PINSTRIPE, "through Pinstripe");

	// A retail race pad to the very host slot the custom track borrowed, in the
	// same session. Not a cup at all, so it takes the ordinary arcade branch and
	// the engine's own roster order, unshuffled.
	run_driver_mpk(TEST_HOST, 0, TEST_CUP, player);
	expect_int(queued_contains(BI_1PARCADEPACK + player), 1, "a retail pad queues the arcade pack");
	expect_log_lacks("event race field:", "and composes no event field");
	expect_int(data.characterIDs[1], 1, "leaving LOAD_Robots1P's own order in place");
	expect_int(data.characterIDs[7], 7, "all the way down");

	// With the loader disarmed the event race does not exist, so the cup is back
	// to the four bosses -- the guard-off shape, reached at runtime.
	CAPTURING(CustomTrack_ClearSeedDescriptor());
	run_driver_mpk(TEST_HOST, 1, TEST_CUP, player);
	expect_int(queued_contains(BI_2PARCADEPACK + 7), 1, "a withdrawn seed puts the boss pack back");
	expect_int(data.characterIDs[1], RIPPER_ROO, "and the boss lineup with it");
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
	test_config_without_paths();
	test_paths_but_no_seed();
	test_happy_path();
	test_retail_pad_stays_retail();
	test_wrong_hash();
	test_truncated_file();
	test_missing_file();
	test_missing_folder();
	test_seed_without_local_files();
	test_bad_host_slot();
	test_bad_cup();
	test_bad_laps();
	test_measured_flags();
	test_boxes_denied();
	test_apply_is_idempotent();
	test_descriptor_replacement();
	test_withdrawal();
	test_serve_time_size_recheck();
	test_absent_camera_path();
	test_sky_primitive_budget();
	test_prim_arena_for_served_load();
	test_prim_arena_for_retail_load();
	test_rendered_quadblock_bound();
	test_render_report_is_one_line_per_load();
	test_display_name_from_config();
	test_event_roster();
	test_event_roster_uses_the_arcade_pack();

	if (g_failures != 0)
	{
		printf("FAILED: %d assertion(s) (scratch dir kept at %s)\n", g_failures, dir);
		return 1;
	}

	printf("test-custom-track-load: all assertions held\n");
	return 0;
}
