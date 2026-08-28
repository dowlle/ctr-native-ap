// Integration assertions for the custom-track loader against REAL files on disk
// (Baby T Park event spike, rungs 1/2a/2c). Where
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

void LOAD_AppendQueue(struct BigHeader *bigfile, int type, int fileIndex, void *destinationPtr, void (*callback)(struct LoadQueueSlot *))
{
	(void)bigfile; (void)type; (void)fileIndex; (void)destinationPtr; (void)callback;
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

static void expect_log_silent(const char *what)
{
	if (g_log[0] == '\0')
		return;
	printf("FAIL %s: loader logged when it should have said nothing:\n%s\n", what, g_log);
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

	// The event race: served, and expanded.
	expect_int(CustomTrack_ServingLoad(eventLoad.levelID, eventLoad.adventureCupActive, eventLoad.cupID), 1,
	           "the event race is a served load");
	expect_int(CustomTrackPolicy_PrimArenaBytes(1, retailArena) == CTR_CT_PRIM_ARENA_BYTES, 1,
	           "so it gets the measured arena");

	// A retail pad to the very same host slot in the same armed session: not
	// served, so not expanded. Same slot, same session, different answer.
	expect_int(CustomTrack_ServingLoad(retailPad.levelID, retailPad.adventureCupActive, retailPad.cupID), 0,
	           "a retail pad to the host slot is not a served load");
	expect_int(CustomTrackPolicy_PrimArenaBytes(0, retailArena) == retailArena, 1,
	           "so it keeps the retail arena byte for byte");

	// The two questions agree: the load that gets custom bytes is the load that
	// gets the expanded arena.
	expect_int(CustomTrack_GetOverride(TEST_HOST * CTR_CT_GROUP_SIZE + 1, &eventLoad, &path, &size),
	           CustomTrack_ServingLoad(eventLoad.levelID, eventLoad.adventureCupActive, eventLoad.cupID),
	           "serving the bytes and sizing the arena are the same decision");
	expect_int(CustomTrack_GetOverride(TEST_HOST * CTR_CT_GROUP_SIZE + 1, &retailPad, &path, &size),
	           CustomTrack_ServingLoad(retailPad.levelID, retailPad.adventureCupActive, retailPad.cupID),
	           "and they agree on the retail pad too");

	// With the feature disarmed nothing is expanded, whatever the load says.
	CustomTrack_ClearSeedDescriptor();
	expect_int(CustomTrack_ServingLoad(eventLoad.levelID, eventLoad.adventureCupActive, eventLoad.cupID), 0,
	           "a withdrawn descriptor serves no load");
	expect_int(CustomTrackPolicy_PrimArenaBytes(0, retailArena) == retailArena, 1,
	           "and expands no arena");
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

	if (g_failures != 0)
	{
		printf("FAILED: %d assertion(s) (scratch dir kept at %s)\n", g_failures, dir);
		return 1;
	}

	printf("test-custom-track-load: all assertions held\n");
	return 0;
}
