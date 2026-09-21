// Behavioral harness for the disc copy state machine (issue #334, slice 2).
// It includes the production copy unit and its real filesystem operations
// directly, so these assertions run against the code main.c compiles:
//
//   * platform/native_disc_copy.c    the state machine and the real ops
//   * platform/native_fs_utf8.c      the UTF-8 filesystem layer (POSIX branch)
//   * platform/native_disc_image.c   the real single-candidate validator
//
// It drives the real state machine against temporary directories and wraps the
// real ops to inject write, flush, validation and replace failures. Cases:
// exclusive temp creation, a pre-existing <destination>.tmp as a normal file,
// hard link and symlink, partial write, flush failure, invalid completed copy,
// replace failure, cleanup of only the owned temp, and a valid destination that
// is a hard link never opened for writing.
//
//   cc -Wall -Wextra -D_FILE_OFFSET_BITS=64 -I . -I include -o /tmp/test-disc-copy tools/test-disc-copy.c
//
// Exit 0 = every assertion held; failures are printed otherwise.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "platform/native_fs_utf8.c"
#include "platform/native_disc_image.c"
#include "platform/native_disc_copy.c"

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

static int fileEqualsString(const char *path, const char *expected)
{
	size_t len;
	char *data = readTextFile(path, &len);
	int ok;

	if (data == NULL)
		return 0;

	ok = (strcmp(data, expected) == 0);
	free(data);
	return ok;
}

// Count sibling files whose name starts with "<basename>.tmp". Used to prove a
// run cleaned up only the temporary file it owned.
static int countTempSiblings(const char *dir, const char *basename)
{
	char prefix[512];
	NativeFsDir *iter = NativeFs_OpenDir(dir);
	char name[512];
	int count = 0;

	snprintf(prefix, sizeof(prefix), "%s.tmp", basename);

	if (iter == NULL)
		return 0;

	while (NativeFs_ReadDir(iter, name, sizeof(name)))
	{
		if (strncmp(name, prefix, strlen(prefix)) == 0)
			count++;
	}

	NativeFs_CloseDir(iter);
	return count;
}

// ── failure-injecting wrappers over the real ops ────────────────────────────

struct FailCtx
{
	int failWriteAfter; // fail once writeCount reaches this; -1 = never
	int writeCount;
	int failFlush;
	int failValidate;
	int failReplace;
};

static void *wrapOpenRead(void *ctx, const char *path)
{
	return NativeDiscCopyReal_OpenRead(ctx, path);
}

static void *wrapCreateTemp(void *ctx, const char *destination, char *tempPathOut, size_t tempPathOutSize)
{
	return NativeDiscCopyReal_CreateTempExclusive(ctx, destination, tempPathOut, tempPathOutSize);
}

static long wrapRead(void *ctx, void *handle, void *buffer, size_t size)
{
	return NativeDiscCopyReal_Read(ctx, handle, buffer, size);
}

static int wrapWrite(void *ctx, void *handle, const void *buffer, size_t size)
{
	struct FailCtx *c = (struct FailCtx *)ctx;

	if ((c->failWriteAfter >= 0) && (c->writeCount >= c->failWriteAfter))
		return 0;

	c->writeCount++;
	return NativeDiscCopyReal_Write(NULL, handle, buffer, size);
}

static int wrapFlush(void *ctx, void *handle)
{
	struct FailCtx *c = (struct FailCtx *)ctx;

	if (c->failFlush)
		return 0;

	return NativeDiscCopyReal_FlushToDisk(NULL, handle);
}

static int wrapClose(void *ctx, void *handle)
{
	(void)ctx;
	return NativeDiscCopyReal_Close(NULL, handle);
}

static void wrapRemove(void *ctx, const char *tempPath)
{
	(void)ctx;
	NativeDiscCopyReal_RemoveOwned(NULL, tempPath);
}

static int wrapValidate(void *ctx, const char *path)
{
	struct FailCtx *c = (struct FailCtx *)ctx;

	if (c->failValidate)
		return 0;

	return NativeDiscCopyReal_Validate(NULL, path);
}

static int wrapReplace(void *ctx, const char *tempPath, const char *destination)
{
	struct FailCtx *c = (struct FailCtx *)ctx;

	if (c->failReplace)
		return 0;

	return NativeDiscCopyReal_Replace(NULL, tempPath, destination);
}

static NativeDiscCopyResult runInjected(struct FailCtx *ctx, const char *source, const char *destination, char *tempPath, size_t tempPathSize)
{
	NativeDiscCopyOps ops = {
	    wrapOpenRead,
	    wrapCreateTemp,
	    wrapRead,
	    wrapWrite,
	    wrapFlush,
	    wrapClose,
	    wrapRemove,
	    wrapValidate,
	    wrapReplace,
	};

	return NativeDiscCopy_Run(&ops, ctx, source, destination, tempPath, tempPathSize);
}

static NativeDiscCopyResult runReal(const char *source, const char *destination, char *tempPath, size_t tempPathSize)
{
	return NativeDiscCopy_Run(&g_nativeDiscCopyRealOps, NULL, source, destination, tempPath, tempPathSize);
}

// ── tests ───────────────────────────────────────────────────────────────────

static char g_root[256];
static char g_source[512];
static char g_badSource[512];

static void joinPath(char *dst, size_t dstSize, const char *name)
{
	snprintf(dst, dstSize, "%s/%s", g_root, name);
}

static void TestExclusiveTempCreation(void)
{
	char dir[512];
	char dest[512];
	char first[600];
	char second[600];
	char tempPath[600];
	FILE *file;
	void *handle;

	joinPath(dir, sizeof(dir), "exclusive");
	mkdir(dir, 0700);
	snprintf(dest, sizeof(dest), "%s/ctr-u.bin", dir);
	snprintf(first, sizeof(first), "%s.tmp.%ld", dest, (long)getpid());
	snprintf(second, sizeof(second), "%s.tmp.%ld.1", dest, (long)getpid());

	// A pre-existing first candidate is never reused: the exclusive create
	// fails and the unit picks another name.
	file = fopen(first, "wb");
	expect(file != NULL, "exclusive: pre-create first candidate");
	if (file != NULL)
	{
		fwrite("SENTINEL", 1, 8, file);
		fclose(file);
	}

	handle = NativeDiscCopyReal_CreateTempExclusive(NULL, dest, tempPath, sizeof(tempPath));
	expect(handle != NULL, "exclusive: create succeeds beside a pre-existing candidate");
	if (handle != NULL)
	{
		expect(strcmp(tempPath, first) != 0, "exclusive: owned temp is not the pre-existing candidate");
		fclose((FILE *)handle);
	}
	expect(fileEqualsString(first, "SENTINEL"), "exclusive: pre-existing candidate untouched");

	// The second candidate is also refused when it already exists.
	file = fopen(second, "wb");
	expect(file != NULL, "exclusive: pre-create second candidate");
	if (file != NULL)
	{
		fwrite("SECOND", 1, 6, file);
		fclose(file);
	}
	handle = NativeDiscCopyReal_CreateTempExclusive(NULL, dest, tempPath, sizeof(tempPath));
	expect(handle != NULL, "exclusive: create succeeds past both existing names");
	if (handle != NULL)
	{
		expect(strcmp(tempPath, first) != 0, "exclusive: owned temp is not the first candidate");
		expect(strcmp(tempPath, second) != 0, "exclusive: owned temp is not the second candidate");
		fclose((FILE *)handle);
	}
	expect(fileEqualsString(second, "SECOND"), "exclusive: second pre-existing candidate untouched");
}

static void TestHappyPath(void)
{
	char dir[512];
	char dest[512];
	char tempPath[600];
	NativeDiscCopyResult result;

	joinPath(dir, sizeof(dir), "happy");
	mkdir(dir, 0700);
	snprintf(dest, sizeof(dest), "%s/ctr-u.bin", dir);

	result = runReal(g_source, dest, tempPath, sizeof(tempPath));
	expect(result == NATIVE_DISC_COPY_OK, "happy: copy succeeds");
	expect(NativeFs_FileExists(dest), "happy: destination exists");
	expect(NativeDiscImage_ValidateCandidate(dest, 0) == NATIVE_DISC_IMAGE_VALID, "happy: destination is a valid disc");
	expect(countTempSiblings(dir, "ctr-u.bin") == 0, "happy: no temporary sibling left");
}

static void TestPreexistingTmpFile(void)
{
	char dir[512];
	char dest[512];
	char tmp[512];
	char tempPath[600];
	NativeDiscCopyResult result;

	joinPath(dir, sizeof(dir), "tmpfile");
	mkdir(dir, 0700);
	snprintf(dest, sizeof(dest), "%s/ctr-u.bin", dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", dest);

	expect(writeFile(tmp, "SENTINEL", 8), "tmpfile: pre-create .tmp");

	result = runReal(g_source, dest, tempPath, sizeof(tempPath));
	expect(result == NATIVE_DISC_COPY_OK, "tmpfile: copy succeeds");
	expect(NativeDiscImage_ValidateCandidate(dest, 0) == NATIVE_DISC_IMAGE_VALID, "tmpfile: destination is a valid disc");
	expect(fileEqualsString(tmp, "SENTINEL"), "tmpfile: pre-existing .tmp untouched");
}

static void TestPreexistingTmpHardlink(void)
{
	char dir[512];
	char dest[512];
	char victim[512];
	char tmp[512];
	char tempPath[600];
	NativeDiscCopyResult result;

	joinPath(dir, sizeof(dir), "tmphard");
	mkdir(dir, 0700);
	snprintf(dest, sizeof(dest), "%s/ctr-u.bin", dir);
	snprintf(victim, sizeof(victim), "%s/victim.txt", dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", dest);

	expect(writeFile(victim, "VICTIM", 6), "tmphard: create victim");
	expect(link(victim, tmp) == 0, "tmphard: hard link .tmp to victim");

	result = runReal(g_source, dest, tempPath, sizeof(tempPath));
	expect(result == NATIVE_DISC_COPY_OK, "tmphard: copy succeeds");
	expect(fileEqualsString(victim, "VICTIM"), "tmphard: hard-linked victim untouched");
}

static void TestPreexistingTmpSymlink(void)
{
	char dir[512];
	char dest[512];
	char victim[512];
	char tmp[512];
	char tempPath[600];
	struct stat info;
	NativeDiscCopyResult result;

	joinPath(dir, sizeof(dir), "tmpsym");
	mkdir(dir, 0700);
	snprintf(dest, sizeof(dest), "%s/ctr-u.bin", dir);
	snprintf(victim, sizeof(victim), "%s/victim.txt", dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", dest);

	expect(writeFile(victim, "VICTIM", 6), "tmpsym: create victim");
	expect(symlink(victim, tmp) == 0, "tmpsym: symlink .tmp to victim");

	result = runReal(g_source, dest, tempPath, sizeof(tempPath));
	expect(result == NATIVE_DISC_COPY_OK, "tmpsym: copy succeeds");
	expect(fileEqualsString(victim, "VICTIM"), "tmpsym: symlink target untouched");
	expect(lstat(tmp, &info) == 0 && S_ISLNK(info.st_mode), "tmpsym: symlink itself untouched");
}

static void TestWriteFailure(void)
{
	char dir[512];
	char dest[512];
	char tempPath[600];
	struct FailCtx ctx;
	NativeDiscCopyResult result;

	joinPath(dir, sizeof(dir), "wfail");
	mkdir(dir, 0700);
	snprintf(dest, sizeof(dest), "%s/ctr-u.bin", dir);

	memset(&ctx, 0, sizeof(ctx));
	ctx.failWriteAfter = 0;

	result = runInjected(&ctx, g_source, dest, tempPath, sizeof(tempPath));
	expect(result == NATIVE_DISC_COPY_WRITE_FAILED, "wfail: partial write fails the copy");
	expect(!NativeFs_FileExists(dest), "wfail: destination untouched");
	expect(countTempSiblings(dir, "ctr-u.bin") == 0, "wfail: owned temp cleaned up");
}

static void TestFlushFailure(void)
{
	char dir[512];
	char dest[512];
	char tempPath[600];
	struct FailCtx ctx;
	NativeDiscCopyResult result;

	joinPath(dir, sizeof(dir), "ffail");
	mkdir(dir, 0700);
	snprintf(dest, sizeof(dest), "%s/ctr-u.bin", dir);

	memset(&ctx, 0, sizeof(ctx));
	ctx.failWriteAfter = -1;
	ctx.failFlush = 1;

	result = runInjected(&ctx, g_source, dest, tempPath, sizeof(tempPath));
	expect(result == NATIVE_DISC_COPY_FLUSH_FAILED, "ffail: flush failure fails the copy");
	expect(!NativeFs_FileExists(dest), "ffail: destination untouched");
	expect(countTempSiblings(dir, "ctr-u.bin") == 0, "ffail: owned temp cleaned up");
}

static void TestInvalidCompletedCopy(void)
{
	char dir[512];
	char dest[512];
	char tempPath[600];
	NativeDiscCopyResult result;

	joinPath(dir, sizeof(dir), "invalid");
	mkdir(dir, 0700);
	snprintf(dest, sizeof(dest), "%s/ctr-u.bin", dir);

	result = runReal(g_badSource, dest, tempPath, sizeof(tempPath));
	expect(result == NATIVE_DISC_COPY_VALIDATE_FAILED, "invalid: a non-disc source fails validation");
	expect(!NativeFs_FileExists(dest), "invalid: destination untouched");
	expect(countTempSiblings(dir, "ctr-u.bin") == 0, "invalid: owned temp cleaned up");
}

static void TestReplaceFailure(void)
{
	char dir[512];
	char dest[512];
	char tempPath[600];
	struct FailCtx ctx;
	NativeDiscCopyResult result;

	joinPath(dir, sizeof(dir), "rfail");
	mkdir(dir, 0700);
	snprintf(dest, sizeof(dest), "%s/ctr-u.bin", dir);

	memset(&ctx, 0, sizeof(ctx));
	ctx.failWriteAfter = -1;
	ctx.failReplace = 1;

	result = runInjected(&ctx, g_source, dest, tempPath, sizeof(tempPath));
	expect(result == NATIVE_DISC_COPY_REPLACE_FAILED, "rfail: replace failure fails the copy");
	expect(!NativeFs_FileExists(dest), "rfail: destination untouched");
	expect(countTempSiblings(dir, "ctr-u.bin") == 0, "rfail: owned temp cleaned up");
}

static void TestDestinationHardlinkNeverWritten(void)
{
	char dir[512];
	char dest[512];
	char victim[512];
	char tempPath[600];
	size_t sourceLen;
	size_t victimLen;
	char *sourceBytes;
	char *victimBytes;
	struct FailCtx ctx;
	NativeDiscCopyResult result;

	joinPath(dir, sizeof(dir), "harddest");
	mkdir(dir, 0700);
	snprintf(dest, sizeof(dest), "%s/ctr-u.bin", dir);
	snprintf(victim, sizeof(victim), "%s/victim.bin", dir);

	// A valid destination that is a hard link: a successful copy replaces the
	// directory entry, it must never write through to the linked file.
	expect(buildRawDisc(victim), "harddest: create hard-linked valid victim");
	expect(link(victim, dest) == 0, "harddest: hard link destination to victim");

	result = runReal(g_source, dest, tempPath, sizeof(tempPath));
	expect(result == NATIVE_DISC_COPY_OK, "harddest: copy succeeds");
	sourceBytes = readTextFile(g_source, &sourceLen);
	victimBytes = readTextFile(victim, &victimLen);
	expect((sourceBytes != NULL) && (victimBytes != NULL) && (sourceLen == victimLen) && (memcmp(sourceBytes, victimBytes, sourceLen) == 0),
	       "harddest: hard-linked victim bytes unchanged");
	free(sourceBytes);
	free(victimBytes);

	// The same destination with an injected replace failure must still leave the
	// linked victim bytes alone: the destination is never opened for writing.
	unlink(dest);
	expect(link(victim, dest) == 0, "harddest: restore hard link");
	memset(&ctx, 0, sizeof(ctx));
	ctx.failWriteAfter = -1;
	ctx.failReplace = 1;
	result = runInjected(&ctx, g_source, dest, tempPath, sizeof(tempPath));
	expect(result == NATIVE_DISC_COPY_REPLACE_FAILED, "harddest: injected replace failure reported");
	victimBytes = readTextFile(victim, &victimLen);
	expect((victimBytes != NULL) && (victimLen == sourceLen) && (memcmp(victimBytes, sourceBytes, sourceLen) == 0),
	       "harddest: victim bytes unchanged after a failed replace");
	free(victimBytes);
}

static void TestOnlyOwnedTempRemoved(void)
{
	char dir[512];
	char dest[512];
	char first[512];
	char second[512];
	char tempPath[600];
	NativeDiscCopyResult result;

	joinPath(dir, sizeof(dir), "owned");
	mkdir(dir, 0700);
	snprintf(dest, sizeof(dest), "%s/ctr-u.bin", dir);
	snprintf(first, sizeof(first), "%s.tmp.%ld", dest, (long)getpid());
	snprintf(second, sizeof(second), "%s.tmp.%ld.1", dest, (long)getpid());

	// Both names the generator will try first already exist. The copy must use
	// the next candidate and remove only that one.
	expect(writeFile(first, "FIRST", 5), "owned: pre-create first candidate");
	expect(writeFile(second, "SECOND", 6), "owned: pre-create second candidate");

	result = runReal(g_source, dest, tempPath, sizeof(tempPath));
	expect(result == NATIVE_DISC_COPY_OK, "owned: copy succeeds");
	expect(NativeDiscImage_ValidateCandidate(dest, 0) == NATIVE_DISC_IMAGE_VALID, "owned: destination is a valid disc");
	expect(fileEqualsString(first, "FIRST"), "owned: first pre-existing temp untouched");
	expect(fileEqualsString(second, "SECOND"), "owned: second pre-existing temp untouched");
	expect(countTempSiblings(dir, "ctr-u.bin") == 2, "owned: only the owned temp was removed");
}

int main(void)
{
	// A per-run root keeps the hard-link and pre-existing-temp fixtures from
	// colliding with a previous run's leftovers.
	snprintf(g_root, sizeof(g_root), "/tmp/ctr-disc-copy-%ld", (long)getpid());
	mkdir(g_root, 0700);
	joinPath(g_source, sizeof(g_source), "source.bin");
	joinPath(g_badSource, sizeof(g_badSource), "not-a-disc.bin");

	expect(buildRawDisc(g_source), "fixture: build raw NTSC-U source disc");
	{
		u8 zeros[4096];
		memset(zeros, 0, sizeof(zeros));
		expect(writeFile(g_badSource, zeros, sizeof(zeros)), "fixture: build non-disc source");
	}

	TestExclusiveTempCreation();
	TestHappyPath();
	TestPreexistingTmpFile();
	TestPreexistingTmpHardlink();
	TestPreexistingTmpSymlink();
	TestWriteFailure();
	TestFlushFailure();
	TestInvalidCompletedCopy();
	TestReplaceFailure();
	TestDestinationHardlinkNeverWritten();
	TestOnlyOwnedTempRemoved();

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
