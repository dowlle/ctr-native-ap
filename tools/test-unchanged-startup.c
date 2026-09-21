// Behavioral harness proving the disc-resolution slice leaves today's startup
// untouched (issue #334, slice 2, corrected for slice 6). It calls the same
// production startup function main.c calls, NativeStartup_ResolveDisc, with the
// arguments parsed by the production NativeStartup_ParseArgs, against a
// real-layout fixture (assets/ctr-u.bin and no new config keys), and asserts:
//
//   * no picker and no confirmation are shown (the decision never prompts);
//   * config.ini is not written (bytes identical before and after);
//   * the mounted file is the one main would pick, with the case-insensitive
//     ctr-u.bin-first then other .bin precedence.
//
// The assets scan, config loader, candidate validator and the whole startup
// sequence are the real production code. Only the final full asset validation
// (BIGFILE/XA, which this host fixture does not carry) is stubbed through the
// NativeStartupOps, exactly as the work order allows.
//
//   cc -Wall -Wextra -DCTR_AP -I . -I include -o /tmp/test-unchanged-startup tools/test-unchanged-startup.c
//
// Exit 0 = every assertion held; failures are printed otherwise.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "platform/native_config.c"
#include "platform/native_fs_utf8.c"
#include "platform/native_disc_image.c"
#include "platform/native_assets.c"
#include "platform/native_disc_copy.c"
#include "platform/native_startup.c"

static int g_checks;
static int g_failures;

static void expect(int condition, const char *name)
{
	g_checks++;
	if (!condition)
	{
		g_failures++;
		printf("FAIL: %s\n", name);
	}
}

// ── synthetic disc fixture ──────────────────────────────────────────────────

#define DISC_SECTOR_SIZE   2352u
#define DISC_USER_OFFSET   24u
#define DISC_TOTAL_SECTORS 20u
#define DISC_PVD_LBA       16u
#define DISC_ROOT_LBA      18u
#define DISC_CNF_LBA       19u
#define DISC_ROOT_SIZE     2048u

static void putLE32(u8 *dst, u32 value)
{
	dst[0] = (u8)value;
	dst[1] = (u8)(value >> 8);
	dst[2] = (u8)(value >> 16);
	dst[3] = (u8)(value >> 24);
}

static void initRawSector(u8 *sector)
{
	memset(sector, 0, DISC_SECTOR_SIZE);
	sector[0] = 0x00;
	memset(&sector[1], 0xFF, 10);
	sector[11] = 0x00;
	sector[15] = 0x02;
	sector[16] = 0x00;
	sector[17] = 0x00;
	sector[18] = 0x08;
	sector[19] = 0x00;
	sector[20] = 0x00;
	sector[21] = 0x00;
	sector[22] = 0x08;
	sector[23] = 0x00;
}

static void setDirRecord(u8 *record, u32 lba, u32 size, u8 flags, const u8 *name, u8 nameLen)
{
	u32 length = 33u + nameLen;

	if ((length & 1u) != 0)
		length++;

	memset(record, 0, length);
	record[0] = (u8)length;
	putLE32(&record[2], lba);
	putLE32(&record[10], size);
	record[25] = flags;
	record[32] = nameLen;
	memcpy(&record[33], name, nameLen);
}

static int writeFile(const char *path, const void *data, size_t size)
{
	FILE *file = fopen(path, "wb");
	int ok;

	if (file == NULL)
		return 0;

	ok = (fwrite(data, 1, size, file) == size);
	fclose(file);
	return ok;
}

static int buildRawDisc(const char *path)
{
	u8 *image = (u8 *)calloc(DISC_TOTAL_SECTORS, DISC_SECTOR_SIZE);
	u8 *pvd;
	u8 *root;
	char cnf[64];
	u32 sector;
	int ok;

	if (image == NULL)
		return 0;

	for (sector = 0; sector < DISC_TOTAL_SECTORS; sector++)
		initRawSector(&image[sector * DISC_SECTOR_SIZE]);

	pvd = &image[DISC_PVD_LBA * DISC_SECTOR_SIZE + DISC_USER_OFFSET];
	pvd[0] = 1;
	memcpy(&pvd[1], "CD001", 5);
	pvd[6] = 1;
	setDirRecord(&pvd[156], DISC_ROOT_LBA, DISC_ROOT_SIZE, 0x02, (const u8 *)"\0", 1);

	root = &image[DISC_ROOT_LBA * DISC_SECTOR_SIZE + DISC_USER_OFFSET];
	setDirRecord(&root[0], DISC_ROOT_LBA, DISC_ROOT_SIZE, 0x02, (const u8 *)"\0", 1);
	setDirRecord(&root[34], DISC_ROOT_LBA, DISC_ROOT_SIZE, 0x02, (const u8 *)"\x01", 1);

	snprintf(cnf, sizeof(cnf), "BOOT = cdrom:\\SCUS_944.26;1\r\n");
	setDirRecord(&root[68], DISC_CNF_LBA, (u32)strlen(cnf), 0x00, (const u8 *)"SYSTEM.CNF;1", 12);
	memcpy(&image[DISC_CNF_LBA * DISC_SECTOR_SIZE + DISC_USER_OFFSET], cnf, strlen(cnf));

	ok = writeFile(path, image, DISC_TOTAL_SECTORS * DISC_SECTOR_SIZE);
	free(image);
	return ok;
}

static void writeTextFile(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");

	if (file != NULL)
	{
		fwrite(text, 1, strlen(text), file);
		fclose(file);
	}
}

static char *readTextFile(const char *path, size_t *sizeOut)
{
	FILE *file = fopen(path, "rb");
	long len;
	char *data;

	*sizeOut = 0;
	if (file == NULL)
		return NULL;

	fseek(file, 0, SEEK_END);
	len = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (len < 0)
	{
		fclose(file);
		return NULL;
	}

	data = (char *)malloc((size_t)len + 1u);
	if (data == NULL)
	{
		fclose(file);
		return NULL;
	}

	*sizeOut = fread(data, 1, (size_t)len, file);
	data[*sizeOut] = '\0';
	fclose(file);
	return data;
}

static int endsWith(const char *text, const char *suffix)
{
	size_t textLen = strlen(text);
	size_t suffixLen = strlen(suffix);

	return (textLen >= suffixLen) && (strcmp(text + textLen - suffixLen, suffix) == 0);
}

// ── prompt-counting stubs and the asset-validation stub ─────────────────────

struct StartupCtx
{
	int pickCalls;
	int confirmCalls;
	int validateCalls;
};

static int ctxPick(void *ctx, char *outPath, size_t outPathSize)
{
	struct StartupCtx *c = (struct StartupCtx *)ctx;

	(void)outPath;
	(void)outPathSize;
	c->pickCalls++;
	return 0;
}

static int ctxConfirm(void *ctx, const char *question)
{
	struct StartupCtx *c = (struct StartupCtx *)ctx;

	(void)question;
	c->confirmCalls++;
	return 0;
}

// The ONLY stubbed step in the whole sequence: the final full asset validation
// needs the retail BIGFILE/XA layout, absent from this host fixture. Everything
// else, including the argument parsing, config load, assets scan, candidate
// validation and the disc-resolution decision, is real production code.
static int ctxValidateAssets(void *ctx)
{
	struct StartupCtx *c = (struct StartupCtx *)ctx;

	c->validateCalls++;
	return 1;
}

// Run the production startup sequence for one fixture and assert the
// unchanged-startup contract.
static void runStartupCase(const char *base, const char *expectSuffix, const char *label)
{
	char cwd[1024];
	char configPath[600];
	size_t beforeLen;
	size_t afterLen;
	char *before;
	char *after;
	struct StartupCtx ctx;
	NativeStartupArgs startupArgs;
	NativeStartupOps startupOps;
	NativeDiscResolutionResult result;
	char *argv[1];

	snprintf(configPath, sizeof(configPath), "%s/config.ini", base);
	writeTextFile(configPath, "[State]\nwindow_x = 10\n[Video & QoL]\nskip_intro = true\n");
	before = readTextFile(configPath, &beforeLen);
	expect(before != NULL, label);
	expect(getcwd(cwd, sizeof(cwd)) != NULL, label);

	// main.c order: discover base/assets without mounting, then chdir. The
	// production startup function does the config load and everything after.
	expect(NativeAssets_Init(base) == 1, label);
	expect(chdir(base) == 0, label);

	memset(&ctx, 0, sizeof(ctx));

	// Parse through the production argument parser (no --disc, no validation).
	expect(NativeStartup_ParseArgs(1, argv, &startupArgs) == NATIVE_STARTUP_ARG_OK, label);
	expect(startupArgs.explicitDiscPath == NULL, label);
	expect(startupArgs.validateDiscOnly == 0, label);

	startupOps.pickDisc = ctxPick;
	startupOps.confirm = ctxConfirm;
	startupOps.validateAssets = ctxValidateAssets;

	expect(NativeStartup_ResolveDisc(&startupArgs, &startupOps, &ctx, &result) == NATIVE_STARTUP_OK, label);
	expect(result.source == NATIVE_DISC_SOURCE_ASSETS, label);
	expect(result.wizardRan == 0, label);
	expect(result.persistExternal == 0, label);
	expect(endsWith(result.chosenPath, expectSuffix), label);
	expect(ctx.pickCalls == 0, label);
	expect(ctx.confirmCalls == 0, label);
	expect(ctx.validateCalls == 1, label);

	// The assets source must never reach NativeConfig_Save, so config.ini is
	// byte-for-byte identical.
	after = readTextFile(configPath, &afterLen);
	expect(after != NULL, label);
	expect((before != NULL) && (after != NULL) && (beforeLen == afterLen) && (memcmp(before, after, beforeLen) == 0), label);

	free(before);
	free(after);
	chdir(cwd);
}

// The production argument parser is itself under test here: the unchanged
// startup contract depends on it accepting exactly the documented flags.
static void TestArgumentParsing(void)
{
	NativeStartupArgs args;
	char *ok[] = {"ctr_native_ap", "--disc", "/discs/ctr-u.bin", NULL};
	char *dup[] = {"ctr_native_ap", "--disc", "/a.bin", "--disc", "/b.bin", NULL};
	char *missing[] = {"ctr_native_ap", "--disc", NULL};
	char *validate[] = {"ctr_native_ap", "--validate-disc", NULL};
	char *none[] = {"ctr_native_ap", NULL};

	expect(NativeStartup_ParseArgs(3, ok, &args) == NATIVE_STARTUP_ARG_OK, "args: --disc value accepted");
	expect((args.explicitDiscPath != NULL) && (strcmp(args.explicitDiscPath, "/discs/ctr-u.bin") == 0), "args: explicit path stored");
	expect(args.validateDiscOnly == 0, "args: --disc alone is not validation-only");

	expect(NativeStartup_ParseArgs(5, dup, &args) == NATIVE_STARTUP_ARG_DUPLICATE_DISC, "args: duplicate --disc rejected");
	expect(NativeStartup_ParseArgs(2, missing, &args) == NATIVE_STARTUP_ARG_MISSING_DISC_VALUE, "args: missing --disc value rejected");

	expect(NativeStartup_ParseArgs(2, validate, &args) == NATIVE_STARTUP_ARG_OK, "args: --validate-disc accepted");
	expect(args.validateDiscOnly == 1, "args: validation-only flag set");

	expect(NativeStartup_ParseArgs(1, none, &args) == NATIVE_STARTUP_ARG_OK, "args: no disc flags accepted");
	expect((args.explicitDiscPath == NULL) && (args.validateDiscOnly == 0), "args: no disc flags leave defaults");
}


int main(void)
{
	char root[256];
	char baseA[400];
	char baseB[400];
	char baseC[400];
	char assetsA[400];
	char assetsB[400];
	char assetsC[400];
	char discA[500];
	char discA2[500];
	char discB[500];
	char discC[500];
	char discC2[500];

	snprintf(root, sizeof(root), "/tmp/ctr-startup-%ld", (long)getpid());
	mkdir(root, 0700);

	snprintf(baseA, sizeof(baseA), "%s/case-a", root);
	snprintf(baseB, sizeof(baseB), "%s/case-b", root);
	snprintf(baseC, sizeof(baseC), "%s/case-c", root);
	snprintf(assetsA, sizeof(assetsA), "%s/assets", baseA);
	snprintf(assetsB, sizeof(assetsB), "%s/assets", baseB);
	snprintf(assetsC, sizeof(assetsC), "%s/assets", baseC);
	mkdir(baseA, 0700);
	mkdir(baseB, 0700);
	mkdir(baseC, 0700);
	mkdir(assetsA, 0700);
	mkdir(assetsB, 0700);
	mkdir(assetsC, 0700);

	// Case A: a canonical name in non-lowercase form plus another .bin. The
	// canonical name must win even though "aaa.bin" sorts first.
	snprintf(discA, sizeof(discA), "%s/CTR-U.BIN", assetsA);
	snprintf(discA2, sizeof(discA2), "%s/aaa.bin", assetsA);
	expect(buildRawDisc(discA), "fixture A: build canonical disc");
	expect(buildRawDisc(discA2), "fixture A: build other disc");

	// Case B: no canonical name, only another .bin.
	snprintf(discB, sizeof(discB), "%s/another.bin", assetsB);
	expect(buildRawDisc(discB), "fixture B: build other disc");

	// Case C: lowercase canonical and another .bin.
	snprintf(discC, sizeof(discC), "%s/ctr-u.bin", assetsC);
	snprintf(discC2, sizeof(discC2), "%s/aaa.bin", assetsC);
	expect(buildRawDisc(discC), "fixture C: build canonical disc");
	expect(buildRawDisc(discC2), "fixture C: build other disc");

	TestArgumentParsing();
	runStartupCase(baseA, "CTR-U.BIN", "case A: canonical (any case) wins over another .bin");
	runStartupCase(baseB, "another.bin", "case B: other .bin used when no canonical");
	runStartupCase(baseC, "ctr-u.bin", "case C: lowercase canonical wins");

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
