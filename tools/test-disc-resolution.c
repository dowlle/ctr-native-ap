// Behavioral harness for startup disc resolution (issue #334, implementation
// slice 2). It includes the production units directly, so these assertions run
// against the code main.c compiles:
//
//   * platform/native_disc_image.c   the real single-candidate validator
//   * platform/native_config.c       the bounded config line reader and the
//                                    CFG_DISC_PATH rejection rules
//   * include/platform/native_disc_resolution.h   the precedence, fallthrough
//                                    and copy-decision state machine, driven
//                                    here with stubbed filesystem and dialog
//                                    operations exactly as production drives it
//                                    with the real ones.
//
//   cc -Wall -Wextra -DCTR_AP -I . -I include -o /tmp/test-disc-resolution tools/test-disc-resolution.c
//
// Exit 0 = every assertion held; failures are printed otherwise.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "platform/native_config.c"
#include "platform/native_disc_image.c"
#include "platform/native_disc_resolution.h"

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

// ── synthetic disc fixtures ─────────────────────────────────────────────────
//
// A minimal but structurally real raw MODE2/2352 disc: valid sync headers, an
// ISO9660 PVD at LBA 16, a one-sector root directory at LBA 18 and a
// SYSTEM.CNF at LBA 19. Only the boot serial changes between region fixtures.

#define DISC_SECTOR_SIZE   2352u
#define DISC_USER_OFFSET   24u
#define DISC_USER_SIZE     2048u
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

static int buildRawDisc(const char *path, const char *bootSerial)
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

	snprintf(cnf, sizeof(cnf), "BOOT = cdrom:\\%s;1\r\n", bootSerial);
	setDirRecord(&root[68], DISC_CNF_LBA, (u32)strlen(cnf), 0x00, (const u8 *)"SYSTEM.CNF;1", 12);
	memcpy(&image[DISC_CNF_LBA * DISC_SECTOR_SIZE + DISC_USER_OFFSET], cnf, strlen(cnf));

	ok = writeFile(path, image, DISC_TOTAL_SECTORS * DISC_SECTOR_SIZE);
	free(image);
	return ok;
}

// A cooked 2048-byte ISO has no MODE2 sync headers, so the validator must
// reject it as not a raw image even though the ISO9660 payload is fine.
static int buildCookedIso(const char *path)
{
	u8 *image = (u8 *)calloc(DISC_TOTAL_SECTORS, DISC_USER_SIZE);
	u8 *pvd;
	u8 *root;
	int ok;

	if (image == NULL)
		return 0;

	pvd = &image[DISC_PVD_LBA * DISC_USER_SIZE];
	pvd[0] = 1;
	memcpy(&pvd[1], "CD001", 5);
	pvd[6] = 1;
	setDirRecord(&pvd[156], DISC_ROOT_LBA, DISC_ROOT_SIZE, 0x02, (const u8 *)"\0", 1);

	root = &image[DISC_ROOT_LBA * DISC_USER_SIZE];
	setDirRecord(&root[0], DISC_ROOT_LBA, DISC_ROOT_SIZE, 0x02, (const u8 *)"\0", 1);

	ok = writeFile(path, image, DISC_TOTAL_SECTORS * DISC_USER_SIZE);
	free(image);
	return ok;
}

static void TestRealValidator(void)
{
	char dir[] = "/tmp/ctr-disc-fixtures";
	char valid[256];
	char pal[256];
	char jpn[256];
	char cooked[256];
	char malformed[256];
	char missing[256];
	u8 zeros[4096];

	mkdir(dir, 0700);

	snprintf(valid, sizeof(valid), "%s/valid.bin", dir);
	snprintf(pal, sizeof(pal), "%s/pal.bin", dir);
	snprintf(jpn, sizeof(jpn), "%s/jpn.bin", dir);
	snprintf(cooked, sizeof(cooked), "%s/cooked.iso", dir);
	snprintf(malformed, sizeof(malformed), "%s/malformed.bin", dir);
	snprintf(missing, sizeof(missing), "%s/does-not-exist.bin", dir);

	expect(buildRawDisc(valid, "SCUS_944.26"), "fixture: build raw NTSC-U disc");
	expect(buildRawDisc(pal, "SCES_123.45"), "fixture: build raw PAL disc");
	expect(buildRawDisc(jpn, "SCPS_100.01"), "fixture: build raw NTSC-J disc");
	expect(buildCookedIso(cooked), "fixture: build cooked ISO");
	memset(zeros, 0, sizeof(zeros));
	expect(writeFile(malformed, zeros, sizeof(zeros)), "fixture: build malformed image");

	expect(NativeDiscImage_ValidateCandidate(missing, 0) == NATIVE_DISC_IMAGE_OPEN_FAILED, "missing file reports open failure");
	expect(NativeDiscImage_ValidateCandidate(valid, 0) == NATIVE_DISC_IMAGE_VALID, "raw NTSC-U disc validates");
	expect(NativeDiscImage_ValidateCandidate(pal, 0) == NATIVE_DISC_IMAGE_WRONG_REGION, "PAL disc reports wrong region");
	expect(strstr(NativeDiscImage_LastDetail(), "PAL") != NULL, "wrong-region detail names the detected release");
	expect(NativeDiscImage_ValidateCandidate(jpn, 0) == NATIVE_DISC_IMAGE_WRONG_REGION, "NTSC-J disc reports wrong region");
	expect(NativeDiscImage_ValidateCandidate(cooked, 0) == NATIVE_DISC_IMAGE_NOT_MODE2, "cooked ISO reports not MODE2");
	expect(NativeDiscImage_ValidateCandidate(malformed, 0) == NATIVE_DISC_IMAGE_NOT_MODE2, "malformed image reports not MODE2");

	// keepMounted semantics: a valid mount survives later failed candidates and
	// read-only checks.
	expect(NativeDiscImage_ValidateCandidate(valid, 1) == NATIVE_DISC_IMAGE_VALID, "valid disc mounts");
	expect(strcmp(NativeDiscImage_GetPath(), valid) == 0, "mounted path is the candidate");
	expect(NativeDiscImage_ValidateCandidate(pal, 1) == NATIVE_DISC_IMAGE_WRONG_REGION, "failed candidate rejected while mounted");
	expect(strcmp(NativeDiscImage_GetPath(), valid) == 0, "failed candidate leaves the good mount active");
	expect(NativeDiscImage_ValidateCandidate(pal, 0) == NATIVE_DISC_IMAGE_WRONG_REGION, "read-only candidate rejected");
	expect(strcmp(NativeDiscImage_GetPath(), valid) == 0, "read-only check leaves the good mount active");
}

// ── stubbed decision operations ─────────────────────────────────────────────

#define STUB_MAX_FILES 8
#define STUB_MAX_PICKS 8
#define STUB_MAX_CONFIRMS 4

struct StubFile
{
	const char *path;
	enum NativeDiscImageValidation result;
};

struct StubCtx
{
	struct StubFile files[STUB_MAX_FILES];
	int fileCount;
	const char *existing[STUB_MAX_FILES];
	int existingCount;
	int scanResult;
	const char *scanPath;
	const char *picks[STUB_MAX_PICKS];
	int pickCount;
	int pickIndex;
	int confirmAnswers[STUB_MAX_CONFIRMS];
	int confirmCount;
	int confirmIndex;
	int copyResult;
	int copyCalls;
	char copiedSource[NATIVE_DISC_PATH_MAX];
	char copiedDestination[NATIVE_DISC_PATH_MAX];
	int validateCalls;
	int scanCalls;
	int confirmCalls;
	int pickCalls;
	char status[2048];
};

static void stubAddFile(struct StubCtx *ctx, const char *path, enum NativeDiscImageValidation result)
{
	if (ctx->fileCount < STUB_MAX_FILES)
	{
		ctx->files[ctx->fileCount].path = path;
		ctx->files[ctx->fileCount].result = result;
		ctx->fileCount++;
	}
}

static void stubAddExisting(struct StubCtx *ctx, const char *path)
{
	if (ctx->existingCount < STUB_MAX_FILES)
		ctx->existing[ctx->existingCount++] = path;
}

static int stubFileExists(void *ctx, const char *path)
{
	struct StubCtx *c = (struct StubCtx *)ctx;
	int i;

	for (i = 0; i < c->existingCount; i++)
		if (strcmp(c->existing[i], path) == 0)
			return 1;

	return 0;
}

static enum NativeDiscImageValidation stubValidate(void *ctx, const char *path, char *chosenPath, size_t chosenPathSize)
{
	struct StubCtx *c = (struct StubCtx *)ctx;
	int i;

	c->validateCalls++;

	for (i = 0; i < c->fileCount; i++)
	{
		if (strcmp(c->files[i].path, path) == 0)
		{
			if (c->files[i].result == NATIVE_DISC_IMAGE_VALID)
				NativeDiscResolution_CopyString(chosenPath, chosenPathSize, path);
			return c->files[i].result;
		}
	}

	return NATIVE_DISC_IMAGE_OPEN_FAILED;
}

static int stubScan(void *ctx, char *chosenPath, size_t chosenPathSize)
{
	struct StubCtx *c = (struct StubCtx *)ctx;

	c->scanCalls++;
	if (!c->scanResult)
		return 0;

	NativeDiscResolution_CopyString(chosenPath, chosenPathSize, c->scanPath);
	return 1;
}

static int stubPick(void *ctx, char *outPath, size_t outPathSize)
{
	struct StubCtx *c = (struct StubCtx *)ctx;
	const char *path;

	c->pickCalls++;
	if (c->pickIndex >= c->pickCount)
		return 0;

	path = c->picks[c->pickIndex++];
	if (path == NULL)
		return 0;

	NativeDiscResolution_CopyString(outPath, outPathSize, path);
	return 1;
}

static int stubConfirm(void *ctx, const char *question)
{
	struct StubCtx *c = (struct StubCtx *)ctx;
	int answer;

	(void)question;
	c->confirmCalls++;
	if (c->confirmIndex >= c->confirmCount)
		return 0;

	answer = c->confirmAnswers[c->confirmIndex++];
	return answer;
}

static int stubCopy(void *ctx, const char *source, const char *destination)
{
	struct StubCtx *c = (struct StubCtx *)ctx;

	c->copyCalls++;
	NativeDiscResolution_CopyString(c->copiedSource, sizeof(c->copiedSource), source);
	NativeDiscResolution_CopyString(c->copiedDestination, sizeof(c->copiedDestination), destination);
	return c->copyResult;
}

static void stubReport(void *ctx, const char *line)
{
	struct StubCtx *c = (struct StubCtx *)ctx;
	size_t used = strlen(c->status);

	snprintf(c->status + used, sizeof(c->status) - used, "%s\n", line);
}

static int runStub(struct StubCtx *ctx, const char *explicitPath, const char *savedPath, const char *destinationPath, int allowWizard,
                   NativeDiscResolutionResult *out)
{
	NativeDiscResolutionOps ops;
	NativeDiscResolutionRequest request;

	memset(&ops, 0, sizeof(ops));
	ops.fileExists = stubFileExists;
	ops.validateCandidate = stubValidate;
	ops.scanAssets = stubScan;
	ops.pickDisc = stubPick;
	ops.confirm = stubConfirm;
	ops.copyAndMount = stubCopy;
	ops.reportStatus = stubReport;

	request.explicitPath = explicitPath;
	request.savedPath = savedPath;
	request.destinationPath = destinationPath;
	request.allowWizard = allowWizard;

	return NativeDiscResolution_Run(&ops, ctx, &request, out);
}

// ── precedence and fallthrough ──────────────────────────────────────────────

static void TestPrecedenceAndFallthrough(void)
{
	NativeDiscResolutionResult result;

	// Explicit wins over everything.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		stubAddFile(&ctx, "/discs/explicit.bin", NATIVE_DISC_IMAGE_VALID);
		stubAddFile(&ctx, "/discs/saved.bin", NATIVE_DISC_IMAGE_VALID);
		ctx.scanResult = 1;
		ctx.scanPath = "/game/assets/ctr-u.bin";

		expect(runStub(&ctx, "/discs/explicit.bin", "/discs/saved.bin", "/game/assets/ctr-u.bin", 1, &result) == 1, "explicit: found");
		expect(result.source == NATIVE_DISC_SOURCE_EXPLICIT, "explicit: source");
		expect(strcmp(result.chosenPath, "/discs/explicit.bin") == 0, "explicit: chosen path");
		expect(ctx.scanCalls == 0, "explicit: assets never scanned");
		expect(ctx.pickCalls == 0, "explicit: no wizard");
		expect(result.persistExternal == 0, "explicit: nothing persisted");
	}

	// Saved wins over assets when no explicit argument.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		stubAddFile(&ctx, "/discs/saved.bin", NATIVE_DISC_IMAGE_VALID);
		ctx.scanResult = 1;
		ctx.scanPath = "/game/assets/ctr-u.bin";

		expect(runStub(&ctx, NULL, "/discs/saved.bin", "/game/assets/ctr-u.bin", 1, &result) == 1, "saved: found");
		expect(result.source == NATIVE_DISC_SOURCE_SAVED, "saved: source");
		expect(strcmp(result.chosenPath, "/discs/saved.bin") == 0, "saved: chosen path");
		expect(ctx.scanCalls == 0, "saved: assets never scanned");
		expect(result.staleSavedPath == 0, "saved: not stale");
	}

	// Assets: the existing layout, unchanged, no prompt and no config write.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		ctx.scanResult = 1;
		ctx.scanPath = "/game/assets/ctr-u.bin";

		expect(runStub(&ctx, NULL, "", "/game/assets/ctr-u.bin", 1, &result) == 1, "assets: found");
		expect(result.source == NATIVE_DISC_SOURCE_ASSETS, "assets: source");
		expect(result.wizardRan == 0, "assets: no wizard");
		expect(result.cancelled == 0, "assets: no cancel");
		expect(result.persistExternal == 0, "assets: no config rewrite");
		expect(ctx.validateCalls == 0, "assets: no candidate validation before scan");
	}

	// Missing saved path falls through to assets with a stale status.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		ctx.scanResult = 1;
		ctx.scanPath = "/game/assets/ctr-u.bin";

		expect(runStub(&ctx, NULL, "/discs/gone.bin", "/game/assets/ctr-u.bin", 1, &result) == 1, "missing saved: found");
		expect(result.staleSavedPath == 1, "missing saved: stale status");
		expect(result.source == NATIVE_DISC_SOURCE_ASSETS, "missing saved: falls to assets");
		expect(strstr(ctx.status, NATIVE_DISC_STATUS_SAVED_STALE) != NULL, "missing saved: status line emitted");
	}

	// Invalid saved path (wrong region) falls through too.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		stubAddFile(&ctx, "/discs/pal.bin", NATIVE_DISC_IMAGE_WRONG_REGION);
		ctx.scanResult = 1;
		ctx.scanPath = "/game/assets/ctr-u.bin";

		expect(runStub(&ctx, NULL, "/discs/pal.bin", "/game/assets/ctr-u.bin", 1, &result) == 1, "invalid saved: found");
		expect(result.staleSavedPath == 1, "invalid saved: stale status");
		expect(result.source == NATIVE_DISC_SOURCE_ASSETS, "invalid saved: falls to assets");
	}

	// Invalid explicit path is terminal: no scan, no wizard.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		stubAddFile(&ctx, "/discs/bad.bin", NATIVE_DISC_IMAGE_NOT_MODE2);
		ctx.scanResult = 1;
		ctx.scanPath = "/game/assets/ctr-u.bin";

		expect(runStub(&ctx, "/discs/bad.bin", NULL, "/game/assets/ctr-u.bin", 1, &result) == 0, "invalid explicit: not found");
		expect(result.explicitInvalid == 1, "invalid explicit: terminal flag");
		expect(ctx.scanCalls == 0, "invalid explicit: assets not scanned");
		expect(ctx.pickCalls == 0, "invalid explicit: no wizard");
		expect(strstr(ctx.status, NATIVE_DISC_STATUS_EXPLICIT_INVALID) != NULL, "invalid explicit: status line emitted");
	}

	// Validation-only mode never runs the wizard.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));

		expect(runStub(&ctx, NULL, "", "/game/assets/ctr-u.bin", 0, &result) == 0, "validation-only: no disc");
		expect(result.wizardRan == 0, "validation-only: no wizard");
		expect(result.persistExternal == 0, "validation-only: writes nothing");
		expect(ctx.pickCalls == 0, "validation-only: picker not called");
	}
}

// ── wizard and copy decision ────────────────────────────────────────────────

static void TestWizardAndCopy(void)
{
	NativeDiscResolutionResult result;

	// Cancel leaves everything untouched.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		ctx.picks[0] = NULL;
		ctx.pickCount = 1;

		expect(runStub(&ctx, NULL, "", "/game/assets/ctr-u.bin", 1, &result) == 0, "cancel: no disc");
		expect(result.cancelled == 1, "cancel: cancelled flag");
		expect(result.wizardRan == 1, "cancel: wizard ran");
		expect(result.persistExternal == 0, "cancel: nothing persisted");
		expect(ctx.copyCalls == 0, "cancel: no copy");
	}

	// An invalid pick is rejected and the picker is offered again.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		stubAddFile(&ctx, "/discs/bad.bin", NATIVE_DISC_IMAGE_WRONG_REGION);
		stubAddFile(&ctx, "/discs/good.bin", NATIVE_DISC_IMAGE_VALID);
		ctx.picks[0] = "/discs/bad.bin";
		ctx.picks[1] = "/discs/good.bin";
		ctx.pickCount = 2;
		ctx.confirmAnswers[0] = 0; // decline the copy
		ctx.confirmCount = 1;

		expect(runStub(&ctx, NULL, "", "/game/assets/ctr-u.bin", 1, &result) == 1, "invalid pick: eventually found");
		expect(result.source == NATIVE_DISC_SOURCE_PICKED, "invalid pick: picked source");
		expect(strstr(ctx.status, NATIVE_DISC_TEXT_INVALID) != NULL, "invalid pick: invalid message emitted");
		expect(ctx.pickCalls == 2, "invalid pick: picker re-offered");
		expect(result.persistExternal == 1, "copy declined: external path remembered");
		expect(strcmp(result.chosenPath, "/discs/good.bin") == 0, "copy declined: chosen external path");
	}

	// No destination: ask the copy question. Accept copies into assets.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		stubAddFile(&ctx, "/discs/good.bin", NATIVE_DISC_IMAGE_VALID);
		ctx.picks[0] = "/discs/good.bin";
		ctx.pickCount = 1;
		ctx.confirmAnswers[0] = 1;
		ctx.confirmCount = 1;
		ctx.copyResult = 1;

		expect(runStub(&ctx, NULL, "", "/game/assets/ctr-u.bin", 1, &result) == 1, "copy accept: found");
		expect(result.copyOffered == 1, "copy accept: offered");
		expect(result.copyAccepted == 1, "copy accept: accepted");
		expect(ctx.confirmCalls == 1, "copy accept: one question");
		expect(ctx.copyCalls == 1, "copy accept: copied");
		expect(strcmp(ctx.copiedSource, "/discs/good.bin") == 0, "copy accept: source");
		expect(strcmp(ctx.copiedDestination, "/game/assets/ctr-u.bin") == 0, "copy accept: destination");
		expect(result.source == NATIVE_DISC_SOURCE_ASSETS, "copy accept: chosen from assets");
		expect(result.persistExternal == 0, "copy accept: nothing persisted");
	}

	// Interrupted copy: destination untouched, external path remembered.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		stubAddFile(&ctx, "/discs/good.bin", NATIVE_DISC_IMAGE_VALID);
		ctx.picks[0] = "/discs/good.bin";
		ctx.pickCount = 1;
		ctx.confirmAnswers[0] = 1;
		ctx.confirmCount = 1;
		ctx.copyResult = 0;

		expect(runStub(&ctx, NULL, "", "/game/assets/ctr-u.bin", 1, &result) == 1, "interrupted copy: found");
		expect(result.copyAccepted == 1, "interrupted copy: accepted");
		expect(result.source == NATIVE_DISC_SOURCE_PICKED, "interrupted copy: external disc used");
		expect(result.persistExternal == 1, "interrupted copy: external path remembered");
		expect(strstr(ctx.status, NATIVE_DISC_STATUS_COPY_FAILED) != NULL, "interrupted copy: failure reported");
	}

	// Invalid completed copy: same safe outcome as an interrupted copy.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		stubAddFile(&ctx, "/discs/good.bin", NATIVE_DISC_IMAGE_VALID);
		ctx.picks[0] = "/discs/good.bin";
		ctx.pickCount = 1;
		ctx.confirmAnswers[0] = 1;
		ctx.confirmCount = 1;
		ctx.copyResult = 0; // the copy validated as not a disc

		expect(runStub(&ctx, NULL, "", "/game/assets/ctr-u.bin", 1, &result) == 1, "invalid copy: found");
		expect(result.persistExternal == 1, "invalid copy: external path remembered");
		expect(result.source == NATIVE_DISC_SOURCE_PICKED, "invalid copy: external disc used");
	}

	// Existing invalid destination needs a separate replacement confirmation.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		stubAddFile(&ctx, "/discs/good.bin", NATIVE_DISC_IMAGE_VALID);
		stubAddFile(&ctx, "/game/assets/ctr-u.bin", NATIVE_DISC_IMAGE_NOT_MODE2);
		stubAddExisting(&ctx, "/game/assets/ctr-u.bin");
		ctx.picks[0] = "/discs/good.bin";
		ctx.pickCount = 1;
		ctx.confirmAnswers[0] = 1; // confirm replacement
		ctx.confirmCount = 1;
		ctx.copyResult = 1;

		expect(runStub(&ctx, NULL, "", "/game/assets/ctr-u.bin", 1, &result) == 1, "replace: found");
		expect(result.replacementConfirmed == 1, "replace: confirmation recorded");
		expect(ctx.copyCalls == 1, "replace: copied");
		expect(result.source == NATIVE_DISC_SOURCE_ASSETS, "replace: chosen from assets");
		expect(result.persistExternal == 0, "replace: nothing persisted");
	}

	// Replacement declined keeps the destination untouched.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		stubAddFile(&ctx, "/discs/good.bin", NATIVE_DISC_IMAGE_VALID);
		stubAddFile(&ctx, "/game/assets/ctr-u.bin", NATIVE_DISC_IMAGE_NOT_MODE2);
		stubAddExisting(&ctx, "/game/assets/ctr-u.bin");
		ctx.picks[0] = "/discs/good.bin";
		ctx.pickCount = 1;
		ctx.confirmAnswers[0] = 0; // decline replacement
		ctx.confirmCount = 1;

		expect(runStub(&ctx, NULL, "", "/game/assets/ctr-u.bin", 1, &result) == 1, "replace declined: found");
		expect(result.replacementConfirmed == 0, "replace declined: not confirmed");
		expect(ctx.copyCalls == 0, "replace declined: no copy");
		expect(result.persistExternal == 1, "replace declined: external path remembered");
	}

	// Existing valid destination is reused and never overwritten.
	{
		struct StubCtx ctx;
		memset(&ctx, 0, sizeof(ctx));
		stubAddFile(&ctx, "/discs/good.bin", NATIVE_DISC_IMAGE_VALID);
		stubAddFile(&ctx, "/game/assets/ctr-u.bin", NATIVE_DISC_IMAGE_VALID);
		stubAddExisting(&ctx, "/game/assets/ctr-u.bin");
		ctx.picks[0] = "/discs/good.bin";
		ctx.pickCount = 1;

		expect(runStub(&ctx, NULL, "", "/game/assets/ctr-u.bin", 1, &result) == 1, "reuse: found");
		expect(result.destinationReused == 1, "reuse: destination reused");
		expect(result.source == NATIVE_DISC_SOURCE_ASSETS, "reuse: chosen from assets");
		expect(strcmp(result.chosenPath, "/game/assets/ctr-u.bin") == 0, "reuse: chosen destination");
		expect(ctx.confirmCalls == 0, "reuse: no copy question");
		expect(ctx.copyCalls == 0, "reuse: never overwritten");
		expect(result.persistExternal == 0, "reuse: nothing persisted");
	}
}

// ── path validation and config loading ──────────────────────────────────────

static void TestPathValidation(void)
{
	char longPath[NATIVE_DISC_PATH_MAX + 16];
	char multibyte[] = "caf\xc3\xa9/ctr-u.bin";
	char overlong[] = "\xc0\x80";
	char badByte[] = "\xff\xfe";
	char control[] = "bad\npath";

	memset(longPath, 'a', sizeof(longPath));
	longPath[NATIVE_DISC_PATH_CONTENT_MAX] = '\0';

	expect(NativeDiscPath_Validate("", 0) == NATIVE_DISC_PATH_OK, "path: empty is absent");
	expect(NativeDiscPath_Validate(longPath, strlen(longPath)) == NATIVE_DISC_PATH_OK, "path: at the content limit");
	longPath[NATIVE_DISC_PATH_CONTENT_MAX] = 'a';
	longPath[NATIVE_DISC_PATH_CONTENT_MAX + 1] = '\0';
	expect(NativeDiscPath_Validate(longPath, strlen(longPath)) == NATIVE_DISC_PATH_TOO_LONG, "path: over the content limit");
	expect(NativeDiscPath_Validate(control, strlen(control)) == NATIVE_DISC_PATH_CONTROL, "path: control character rejected");
	expect(NativeDiscPath_Validate(overlong, strlen(overlong)) == NATIVE_DISC_PATH_INVALID_UTF8, "path: overlong UTF-8 rejected");
	expect(NativeDiscPath_Validate(badByte, strlen(badByte)) == NATIVE_DISC_PATH_INVALID_UTF8, "path: invalid UTF-8 rejected");
	expect(NativeDiscPath_Validate(multibyte, strlen(multibyte)) == NATIVE_DISC_PATH_OK, "path: multibyte UTF-8 accepted");
	expect(NativeDiscPath_Validate(" lead", 5) == NATIVE_DISC_PATH_NOT_LOSSLESS, "path: leading whitespace rejected");
	expect(NativeDiscPath_Validate("trail ", 6) == NATIVE_DISC_PATH_NOT_LOSSLESS, "path: trailing whitespace rejected");
}

static void TestCopyPlan(void)
{
	expect(NativeDisc_PlanCopy(0, 0) == NATIVE_DISC_COPY_ASK, "plan: no destination asks");
	expect(NativeDisc_PlanCopy(1, 0) == NATIVE_DISC_COPY_ASK_REPLACE, "plan: invalid destination asks replacement");
	expect(NativeDisc_PlanCopy(1, 1) == NATIVE_DISC_COPY_REUSE_DESTINATION, "plan: valid destination reused");
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

static void TestConfigLoad(void)
{
	char dir[] = "/tmp/ctr-disc-config";
	char path[256];
	char text[8192];
	char longLine[6000];
	char longValue[NATIVE_DISC_PATH_MAX + 64];
	char cwd[1024];
	size_t offset;

	mkdir(dir, 0700);
	snprintf(path, sizeof(path), "%s/config.ini", dir);
	getcwd(cwd, sizeof(cwd));

	// A valid remembered path loads verbatim.
	writeTextFile(path, "[State]\ndisc_path = /discs/ctr-u.bin\n");
	expect(chdir(dir) == 0, "config: enter fixture dir");
	g_config.discPath[0] = '\0';
	NativeConfig_Load();
	expect(strcmp(g_config.discPath, "/discs/ctr-u.bin") == 0, "config: valid path loads verbatim");
	chdir(cwd);

	// An overlong path is treated as absent, never truncated.
	memset(longValue, 'b', sizeof(longValue));
	longValue[NATIVE_DISC_PATH_CONTENT_MAX + 1] = '\0';
	snprintf(text, sizeof(text), "[State]\ndisc_path = %s\n", longValue);
	writeTextFile(path, text);
	expect(chdir(dir) == 0, "config: re-enter fixture dir");
	g_config.discPath[0] = '\0';
	NativeConfig_Load();
	expect(g_config.discPath[0] == '\0', "config: overlong path treated as absent");
	chdir(cwd);

	// A path with a control character is treated as absent.
	writeTextFile(path, "[State]\ndisc_path = /discs/a\tb.bin\n");
	expect(chdir(dir) == 0, "config: re-enter fixture dir");
	g_config.discPath[0] = '\0';
	NativeConfig_Load();
	expect(g_config.discPath[0] == '\0', "config: control character path treated as absent");
	chdir(cwd);

	// A line longer than the reader bound is rejected whole, and the next line
	// still parses (the old fgets buffer silently split it).
	memset(longLine, 'c', sizeof(longLine) - 1);
	longLine[sizeof(longLine) - 1] = '\0';
	offset = 0;
	offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "[State]\nlong_key = %s\n", longLine);
	offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "disc_path = /discs/after-long-line.bin\n");
	writeTextFile(path, text);
	expect(chdir(dir) == 0, "config: re-enter fixture dir");
	g_config.discPath[0] = '\0';
	NativeConfig_Load();
	expect(strcmp(g_config.discPath, "/discs/after-long-line.bin") == 0, "config: line after an overlong line still parses");
	chdir(cwd);

	// An overlong CFG_STRING is rejected rather than truncated.
	memset(longValue, 'w', sizeof(longValue));
	longValue[200] = '\0';
	snprintf(text, sizeof(text), "[Connection]\nuri = %s\n", longValue);
	writeTextFile(path, text);
	expect(chdir(dir) == 0, "config: re-enter fixture dir");
	g_config.uri[0] = '\0';
	NativeConfig_Load();
	expect(g_config.uri[0] == '\0', "config: overlong string rejected, not truncated");
	chdir(cwd);

	// A saved disc path survives a save/load round trip verbatim.
	writeTextFile(path, "[State]\nwindow_x = 10\n");
	expect(chdir(dir) == 0, "config: re-enter fixture dir");
	NativeDiscResolution_CopyString(g_config.discPath, sizeof(g_config.discPath), "/discs/round trip/ctr-u.bin");
	NativeConfig_Save();
	g_config.discPath[0] = '\0';
	NativeConfig_Load();
	expect(strcmp(g_config.discPath, "/discs/round trip/ctr-u.bin") == 0, "config: saved disc path reloads verbatim");
	chdir(cwd);
}

int main(void)
{
	TestRealValidator();
	TestPrecedenceAndFallthrough();
	TestWizardAndCopy();
	TestPathValidation();
	TestCopyPlan();
	TestConfigLoad();

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
