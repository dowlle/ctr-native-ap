// Reusable custom-content verification core harness.
// Build with cc -Wall -Wextra -Werror -DCTR_CUSTOM_TRACKS -I include -I .
// Add tools/test-custom-content-verify.c and choose an output path.

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_faultStage;
static int ccv_fault(int stage) { return stage == g_faultStage; }
#define CTR_CCV_FAULT(stage) ccv_fault(stage)
#define CTR_CUSTOM_TRACKS 1
#include "platform/native_custom_content_verify.c"

#define BODY_INST 0x220
#define BODY_CHECK 0x500
#define BODY_NAV 0x700
#define BODY_MODELS 0xa80
#define MODEL_STRIDE 0x20
#define BODY_PTRMAP 0x1200
#define FIXTURE_INSTANCES 9
#define IMAGE_BYTES (4 + BODY_PTRMAP + 4 + (6 + FIXTURE_INSTANCES) * 4)

static int checks;
static int failures;

static void put32(unsigned char *b, size_t o, unsigned long v)
{
	b[o] = (unsigned char)v; b[o + 1] = (unsigned char)(v >> 8);
	b[o + 2] = (unsigned char)(v >> 16); b[o + 3] = (unsigned char)(v >> 24);
}

static void put16(unsigned char *b, size_t o, unsigned long v)
{
	b[o] = (unsigned char)v; b[o + 1] = (unsigned char)(v >> 8);
}

static void expect(int condition, const char *name)
{
	checks++;
	if (!condition) { failures++; printf("FAIL %s\n", name); }
}

static void write_bytes(const char *path, const void *data, size_t bytes)
{
	FILE *f = fopen(path, "wb");
	if (f == NULL || fwrite(data, 1, bytes, f) != bytes || fclose(f) != 0) exit(2);
}

static size_t build_lev(unsigned char *b, int full)
{
	static const unsigned long models[] = {
		START, FINISH, PU_RANDOM, STATIC_TIME1, STATIC_TIME2,
		LETTER_C, LETTER_T, LETTER_R, CRYSTAL};
	static const unsigned long navHeads[] = {0x740, 0x880, 0x9c0};
	static const unsigned long slots[] = {
		INST_P, CHECK_P, NAV_P, BODY_NAV, BODY_NAV+4, BODY_NAV+8,
		BODY_INST + 0 * INST_SIZE + INST_MODEL_P,
		BODY_INST + 1 * INST_SIZE + INST_MODEL_P,
		BODY_INST + 2 * INST_SIZE + INST_MODEL_P,
		BODY_INST + 3 * INST_SIZE + INST_MODEL_P,
		BODY_INST + 4 * INST_SIZE + INST_MODEL_P,
		BODY_INST + 5 * INST_SIZE + INST_MODEL_P,
		BODY_INST + 6 * INST_SIZE + INST_MODEL_P,
		BODY_INST + 7 * INST_SIZE + INST_MODEL_P,
		BODY_INST + 8 * INST_SIZE + INST_MODEL_P};
	size_t i;
	memset(b, 0, IMAGE_BYTES);
	put32(b, 0, BODY_PTRMAP);
	put32(b, 4 + INST_N, full ? FIXTURE_INSTANCES : 0);
	put32(b, 4 + INST_P, full ? BODY_INST : 0);
	put32(b, 4 + CHECK_N, full ? 35 : 0);
	if (full)
	{
		for (i = 0; i < sizeof models / sizeof models[0]; i++)
		{
			const size_t model = BODY_MODELS + i * MODEL_STRIDE;
			put32(b, 4 + BODY_INST + i * INST_SIZE + INST_MODEL_P, model);
			put16(b, 4 + model + MODEL_ID, models[i]);
			put32(b, 4 + BODY_INST + i * INST_SIZE + 0x3c,
			      (models[i] == LETTER_T || models[i] == LETTER_R) ? LETTER_C : models[i]);
		}
		for (i = 0; i < 8; i++) b[4 + SPAWN_P + i * 12] = (unsigned char)(i + 1);
		put32(b, 4 + CHECK_P, BODY_CHECK);
		for (i = 0; i < 35; i++) { memset(b + 4 + BODY_CHECK + i * 12 + 8, 0xff, 4); b[4 + BODY_CHECK + i * 12 + 8] = (unsigned char)((i + 1) % 35); }
		put32(b, 4 + NAV_P, BODY_NAV);
		for (i = 0; i < 3; i++) { put32(b, 4 + BODY_NAV + i * 4, navHeads[i]); b[4 + navHeads[i]] = 0xfd; b[4 + navHeads[i] + 1] = 0xec; b[4 + navHeads[i] + 2] = 4; }
		put32(b, 4 + BODY_PTRMAP, (sizeof slots / sizeof slots[0]) * 4);
		for (i = 0; i < sizeof slots / sizeof slots[0]; i++) put32(b, 4 + BODY_PTRMAP + 4 + i * 4, slots[i]);
	}
	return full ? IMAGE_BYTES : (4 + BODY_PTRMAP + 4);
}

/* The Arcade and Time Trial verdicts and their menu reasons. The measured
   counts are the five Saphi tracks of 2026-09-25 (Arabian Heights Night
   1.1.0, Choco Island 2 1.0.0, Bowser Castle 1 1.0.0, Country Speedway 1.2.1,
   Coco Park 2025 1.0.0) as the verifier reads their LEVs. */
static struct CustomContentVerification measured_report(unsigned long checkpoints, unsigned long spawns,
                                                        unsigned long navPaths)
{
	struct CustomContentVerification r;
	memset(&r, 0, sizeof r);
	r.loadable = 1;
	r.measured.checkpoints = checkpoints;
	r.measured.spawns = spawns;
	r.measured.navPaths = navPaths;
	derive(&r);
	return r;
}

static void arcade_reason_checks(void)
{
	struct CustomContentVerification r;
	char why[CTR_CCV_ARCADE_REASON_MAX];
	unsigned long cp, sp, nav;

	r = measured_report(90, 8, 3); /* Arabian Heights Night */
	expect(CustomContentVerify_ArcadeRaceReason(&r, why, sizeof why) && !why[0], "Arabian Heights Night races Arcade");
	r = measured_report(119, 8, 0); /* Choco Island 2, Bowser Castle 1: grid, no AI paths */
	expect(!CustomContentVerify_ArcadeRaceReason(&r, why, sizeof why) && !strcmp(why, "No AI paths"),
	       "eight starts without AI paths: no Arcade, says no AI paths");
	expect(CustomContentVerify_TimeTrialReason(&r, why, sizeof why) && !why[0], "and runs Time Trial");
	r = measured_report(130, 0, 0); /* Country Speedway, Coco Park 2025: start at the origin */
	expect(!CustomContentVerify_ArcadeRaceReason(&r, why, sizeof why) && !strcmp(why, "No AI paths"),
	       "origin start without AI paths: no Arcade, says no AI paths");
	expect(CustomContentVerify_TimeTrialReason(&r, why, sizeof why) && !why[0],
	       "an all-zero start grid (the origin) still runs Time Trial");
	r = measured_report(40, 3, 2);
	expect(!CustomContentVerify_ArcadeRaceReason(&r, why, sizeof why) && !strcmp(why, "Grid: 3 of 8"),
	       "AI paths but three starts: the grid count");
	r = measured_report(0, 8, 3);
	expect(!CustomContentVerify_ArcadeRaceReason(&r, why, sizeof why) && !strcmp(why, "No lap checkpoints"),
	       "no checkpoints: no Arcade");
	expect(!CustomContentVerify_TimeTrialReason(&r, why, sizeof why) && !strcmp(why, "No lap checkpoints"),
	       "no checkpoints: no Time Trial");
	r.loadable = 0;
	expect(!CustomContentVerify_ArcadeRaceReason(&r, why, sizeof why) && !strcmp(why, "Files failed checks"),
	       "unloadable pair: files failed");
	expect(!CustomContentVerify_TimeTrialReason(&r, why, sizeof why) && !strcmp(why, "Files failed checks"),
	       "unloadable pair: files failed (Time Trial)");
	expect(!CustomContentVerify_ArcadeRaceReason(NULL, why, sizeof why), "no report refuses");

	/* The verdict is exactly the old offline rule: Arcade detected and eight
	   starts; Time Trial detected. Every text fits two 13-character lines. */
	for (cp = 0; cp <= 2; cp++)
		for (sp = 0; sp <= 8; sp++)
			for (nav = 0; nav <= 3; nav++)
			{
				int arcade, trial;
				r = measured_report(cp, sp, nav);
				arcade = CustomContentVerify_ArcadeRaceReason(&r, why, sizeof why);
				expect(arcade == (r.fileAnalysis[CTR_CCV_ARCADE].result == CTR_CCV_DETECTED && sp >= 8),
				       "Arcade verdict equals the eight-start rule");
				expect(arcade == !why[0] && strlen(why) <= 26, "Arcade reason present iff refused, short");
				trial = CustomContentVerify_TimeTrialReason(&r, why, sizeof why);
				expect(trial == (r.fileAnalysis[CTR_CCV_TIME_TRIAL].result == CTR_CCV_DETECTED),
				       "Time Trial verdict equals the detected lap graph");
				expect(trial == !why[0], "Time Trial reason present iff refused");
			}
}

int main(void)
{
	char root[] = "/tmp/ctr-ccv-XXXXXX";
	char lev[256], vrm[256], error[256];
	unsigned char image[IMAGE_BYTES];
	struct CustomContentVerification v;
	struct CustomContentOwnedPair pair;
	FILE *f;
	unsigned char validVrm[22] = {0};
	unsigned char packedVrm[34] = {0};

	if (mkdtemp(root) == NULL) return 2;
	snprintf(lev, sizeof lev, "%s/track.lev", root);
	snprintf(vrm, sizeof vrm, "%s/track.vrm", root);
	write_bytes(lev, image, build_lev(image, 1));
	validVrm[16]=1; validVrm[18]=1; write_bytes(vrm, validVrm, sizeof validVrm);
	for (int stage = CTR_CCV_FAULT_AFTER_LEV_COPY; stage <= CTR_CCV_FAULT_AFTER_HASHING; stage++)
	{
		g_faultStage = stage;
		expect(!CustomContentVerify_AcquirePair(lev, vrm, &pair, error, sizeof error), "acquisition fault fails closed");
		expect(pair.lev == NULL && pair.vrm == NULL && pair.levBytes == 0 && pair.vrmBytes == 0,
		       "acquisition fault frees every partial buffer");
	}
	g_faultStage = 0;
	expect(CustomContentVerify_AcquirePair(lev, vrm, &pair, error, sizeof error), "pair acquisition owns exact bytes");
	expect(CustomContentVerify_AnalyzePair(&pair, error, sizeof error), "analysis succeeds independently");
	expect(pair.report.loadable == 0, "analysis alone never grants loader authority");
	expect(CustomContentVerify_ValidateLoadablePair(&pair, error, sizeof error), "loadability is a separate gate");
	expect(pair.report.loadable == 1, "only loadability validation grants authority");
	CustomContentVerify_FreePair(&pair);
	{ int ok = CustomContentVerify_Files(lev, vrm, &v, error, sizeof error); if (!ok) printf("fixture error: %s\n", error); expect(ok, "full fixture verifies"); }
	expect(v.measured.instances == 9, "instance count");
	expect(v.measured.checkpoints == 35, "checkpoint count");
	expect(v.measured.spawns == 8, "spawn count");
	expect(v.measured.navPaths == 3, "nav path count");
	expect(v.measured.ordinaryCrates == 1, "ordinary crate count");
	expect(v.measured.relicCrates == 2, "relic crate count");
	expect(v.measured.letterC == 1 && v.measured.letterT == 1 && v.measured.letterR == 1, "letter counts");
	expect(v.measured.letterC == 1 && v.measured.letterT == 1 && v.measured.letterR == 1,
	       "relocated model IDs override stale cached instance IDs");
	expect(v.measured.crystals == 1, "crystal count");
	for (int i = 0; i < CTR_CCV_MODE_COUNT; i++)
		expect(v.fileAnalysis[i].result == CTR_CCV_DETECTED, "full fixture mode detected");
	{
		char why[CTR_CCV_ARCADE_REASON_MAX];
		expect(CustomContentVerify_ArcadeRaceReason(&v, why, sizeof why) && !why[0], "full fixture races Arcade");
		expect(CustomContentVerify_TimeTrialReason(&v, why, sizeof why) && !why[0], "full fixture runs Time Trial");
	}
	arcade_reason_checks();

	build_lev(image, 1); put32(image, 4 + INST_N, 0); put32(image, 4 + INST_P, 0); put32(image, 4 + CHECK_N, 255);
	for (int i = 0; i < 255; i++)
	{
		memset(image + 4 + BODY_CHECK + i * CHECK_SIZE + 8, 0xff, 4);
		image[4 + BODY_CHECK + i * CHECK_SIZE + 8] = (unsigned char)((i + 1) % 255);
	}
	put32(image, 4 + NAV_P, 0);
	put32(image, 4 + BODY_PTRMAP, 3 * 4);
	write_bytes(lev, image, 4 + BODY_PTRMAP + 4 + 3 * 4);
	expect(CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "255-checkpoint table accepted");
	expect(v.measured.checkpoints == 255, "255-checkpoint count retained");
	put32(image, 4 + CHECK_N, 256);
	write_bytes(lev, image, 4 + BODY_PTRMAP + 4 + 3 * 4);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "256-checkpoint table refused");
	expect(strcmp(error, "invalid checkpoint table") == 0, "256-checkpoint diagnostic retained");

	build_lev(image, 1); put32(image, 4 + BODY_PTRMAP, 5);
	write_bytes(lev, image, IMAGE_BYTES);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "non-multiple pointer-map bytes refused");
	build_lev(image, 1); put32(image, 0, BODY_PTRMAP + 28);
	write_bytes(lev, image, IMAGE_BYTES);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "truncated pointer-map header refused");
	build_lev(image, 1); put32(image, 4 + BODY_PTRMAP, BODY_PTRMAP * 4);
	write_bytes(lev, image, IMAGE_BYTES);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "oversized pointer-map count refused");
	build_lev(image, 1); put32(image, 4 + BODY_PTRMAP + 4 + 4, INST_P);
	write_bytes(lev, image, IMAGE_BYTES);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "duplicate pointer slot refused");
	build_lev(image, 1); put32(image, 4 + BODY_PTRMAP + 4, INST_P + 1);
	write_bytes(lev, image, IMAGE_BYTES);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "unaligned pointer slot refused");
	build_lev(image, 1); put32(image, 4 + BODY_PTRMAP + 4, BODY_PTRMAP);
	write_bytes(lev, image, IMAGE_BYTES);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "pointer slot beyond retained payload refused");
	build_lev(image, 1); put32(image, 4 + BODY_PTRMAP + 4, 0x20);
	write_bytes(lev, image, IMAGE_BYTES);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "missing required instance patch slot refused");
	build_lev(image, 1); put32(image, 4 + BODY_PTRMAP + 4 + 6 * 4, 0x1f0);
	write_bytes(lev, image, IMAGE_BYTES);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "missing required model patch slot refused");
	build_lev(image, 1); put32(image, 4 + INST_P, BODY_PTRMAP + 4);
	write_bytes(lev, image, IMAGE_BYTES);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "pointer target beyond payload refused");
	build_lev(image, 1); put32(image, 4 + BODY_CHECK + 8, 250);
	write_bytes(lev, image, IMAGE_BYTES);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "checkpoint edge beyond count refused");
	build_lev(image, 1); write_bytes(lev, image, IMAGE_BYTES);
	write_bytes(vrm, validVrm, sizeof validVrm - 1);
	expect(CustomContentVerify_AcquirePair(lev, vrm, &pair, error, sizeof error), "malformed VRM pair remains safe to acquire");
	expect(CustomContentVerify_AnalyzePair(&pair, error, sizeof error), "LEV evidence can be analyzed without VRM authority");
	expect(pair.report.loadable == 0, "malformed VRM analysis remains non-loadable");
	expect(!CustomContentVerify_ValidateLoadablePair(&pair, error, sizeof error), "malformed VRM is refused by loader gate");
	CustomContentVerify_FreePair(&pair);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "truncated VRM pixels refused");
	validVrm[16] = 0; write_bytes(vrm, validVrm, sizeof validVrm);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "zero-width VRM refused");
	validVrm[16] = 1; validVrm[12] = 0; validVrm[13] = 4; write_bytes(vrm, validVrm, sizeof validVrm);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "out-of-bounds VRM rectangle refused");
	memset(packedVrm, 0, sizeof packedVrm); put32(packedVrm, 0, 0x20); put32(packedVrm, 4, 22);
	packedVrm[8 + 16] = 1; packedVrm[8 + 18] = 1; put32(packedVrm, 30, 0);
	write_bytes(vrm, packedVrm, sizeof packedVrm);
	expect(CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "valid packed VRM accepted");
	write_bytes(vrm, packedVrm, sizeof packedVrm - 4);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "packed VRM missing terminator refused");
	put32(packedVrm, 4, 23); write_bytes(vrm, packedVrm, sizeof packedVrm);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "malformed packed VRM size refused");
	memset(validVrm, 0, sizeof validVrm); validVrm[16] = 1; validVrm[18] = 1; write_bytes(vrm, validVrm, sizeof validVrm);

	write_bytes(lev, image, build_lev(image, 0));
	expect(CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "empty valid fixture verifies");
	for (int i = 0; i < CTR_CCV_MODE_COUNT; i++)
		expect(v.fileAnalysis[i].result == CTR_CCV_NOT_DETECTED, "empty fixture mode not detected");

	f = fopen(lev, "r+b"); put32(image, 0, 0xfffffffful); fwrite(image, 1, IMAGE_BYTES, f); fclose(f);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "bad pointer map refused");
	expect(strstr(error, "pointer-map") != NULL, "bad pointer map diagnostic");

	remove(vrm);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "missing VRM refused");
	write_bytes(vrm, validVrm, sizeof validVrm);
	{
		char linkPath[256];
		snprintf(linkPath, sizeof linkPath, "%s/link.lev", root);
		expect(symlink(lev, linkPath) == 0, "symlink fixture created");
		expect(!CustomContentVerify_AcquirePair(linkPath, vrm, &pair, error, sizeof error), "symlinked LEV refused before acquisition");
	}

	printf("custom-content verifier: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
