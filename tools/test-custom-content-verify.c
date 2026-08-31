// Reusable custom-content verification core harness.
// Build with cc -Wall -Wextra -Werror -DCTR_CUSTOM_TRACKS -I include -I .
// Add tools/test-custom-content-verify.c and choose an output path.

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CTR_CUSTOM_TRACKS 1
#include "platform/native_custom_content_verify.c"

#define BODY_INST 0x220
#define BODY_NAV 0x620
#define BODY_PTRMAP 0x700
#define IMAGE_BYTES (4 + BODY_PTRMAP + 4)

static int checks;
static int failures;

static void put32(unsigned char *b, size_t o, unsigned long v)
{
	b[o] = (unsigned char)v; b[o + 1] = (unsigned char)(v >> 8);
	b[o + 2] = (unsigned char)(v >> 16); b[o + 3] = (unsigned char)(v >> 24);
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
		CCV_START_LINE, CCV_FINISH_LINE, CCV_PU_RANDOM_CRATE,
		CCV_STATIC_TIME_CRATE_1, CCV_STATIC_TIME_CRATE_2,
		CCV_STATIC_C, CCV_STATIC_T, CCV_STATIC_R, CCV_STATIC_CRYSTAL};
	size_t i;
	memset(b, 0, IMAGE_BYTES);
	put32(b, 0, BODY_PTRMAP);
	put32(b, 4 + CCV_LEVEL_NUM_INSTANCES, full ? 9 : 0);
	put32(b, 4 + CCV_LEVEL_PTR_INSTDEFS, BODY_INST);
	put32(b, 4 + CCV_LEVEL_CHECKPOINT_COUNT, full ? 35 : 0);
	if (full)
	{
		for (i = 0; i < sizeof models / sizeof models[0]; i++)
			put32(b, 4 + BODY_INST + i * CCV_INSTDEF_STRIDE + CCV_INSTDEF_MODEL_ID, models[i]);
		for (i = 0; i < 8; i++) b[4 + CCV_LEVEL_DRIVER_SPAWN + i * CCV_SPAWN_BYTES] = (unsigned char)(i + 1);
		put32(b, 4 + CCV_LEVEL_NAV_TABLE, BODY_NAV);
		put32(b, 4 + BODY_NAV, 0x680);
		put32(b, 4 + BODY_NAV + 4, 0x690);
		put32(b, 4 + BODY_NAV + 8, 0x6a0);
	}
	return IMAGE_BYTES;
}

int main(void)
{
	char root[] = "/tmp/ctr-ccv-XXXXXX";
	char lev[256], vrm[256], error[256];
	unsigned char image[IMAGE_BYTES];
	struct CustomContentVerification v;
	FILE *f;

	if (mkdtemp(root) == NULL) return 2;
	snprintf(lev, sizeof lev, "%s/track.lev", root);
	snprintf(vrm, sizeof vrm, "%s/track.vrm", root);
	write_bytes(lev, image, build_lev(image, 1));
	write_bytes(vrm, "vrm", 3);
	expect(CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "full fixture verifies");
	expect(v.measured.instances == 9, "instance count");
	expect(v.measured.checkpoints == 35, "checkpoint count");
	expect(v.measured.spawns == 8, "spawn count");
	expect(v.measured.navPaths == 3, "nav path count");
	expect(v.measured.ordinaryCrates == 1, "ordinary crate count");
	expect(v.measured.relicCrates == 2, "relic crate count");
	expect(v.measured.letterC == 1 && v.measured.letterT == 1 && v.measured.letterR == 1, "letter counts");
	expect(v.measured.crystals == 1, "crystal count");
	for (int i = 0; i < CTR_CCV_MODE_COUNT; i++)
		expect(v.fileAnalysis[i].result == CTR_CCV_YES, "full fixture mode yes");

	write_bytes(lev, image, build_lev(image, 0));
	expect(CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "empty valid fixture verifies");
	for (int i = 0; i < CTR_CCV_MODE_COUNT; i++)
		expect(v.fileAnalysis[i].result == CTR_CCV_NO, "empty fixture mode no");

	f = fopen(lev, "r+b"); put32(image, 0, 0xfffffffful); fwrite(image, 1, IMAGE_BYTES, f); fclose(f);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "bad pointer map refused");
	expect(strstr(error, "pointer-map") != NULL, "bad pointer map diagnostic");

	remove(vrm);
	expect(!CustomContentVerify_Files(lev, vrm, &v, error, sizeof error), "missing VRM refused");

	printf("custom-content verifier: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
