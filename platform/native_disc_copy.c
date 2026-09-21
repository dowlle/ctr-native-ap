#include "platform/native_disc_copy.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "platform/native_disc_image.h"
#include "platform/native_fs_utf8.h"
#include "platform/native_disc_limits.h"

#define NATIVE_DISC_COPY_PATH_MAX NATIVE_DISC_PATH_MAX

// How many candidate temporary names to try before giving up. Every candidate
// carries the process id and an attempt counter, so no two runs share a name;
// the exclusive create then guarantees this run owns exactly the file it made
// and never reuses or truncates an existing one.
#define NATIVE_DISC_COPY_TEMP_ATTEMPTS 64

static long NativeDiscCopy_ProcessId(void)
{
#if defined(_WIN32)
	return (long)_getpid();
#else
	return (long)getpid();
#endif
}

// "<destination>.tmp.<pid>" for the first candidate, then
// "<destination>.tmp.<pid>.<attempt>". Every candidate is offered to an
// exclusive create, so a pre-existing path of any kind (including a plain
// "<destination>.tmp") is never opened, truncated, written through or deleted.
static int NativeDiscCopy_MakeTempName(const char *destination, int attempt, char *out, size_t outSize)
{
	int written;

	if (attempt <= 0)
		written = snprintf(out, outSize, "%s.tmp.%ld", destination, NativeDiscCopy_ProcessId());
	else
		written = snprintf(out, outSize, "%s.tmp.%ld.%d", destination, NativeDiscCopy_ProcessId(), attempt);

	return (written > 0) && ((size_t)written < outSize);
}

static void *NativeDiscCopyReal_OpenRead(void *ctx, const char *path)
{
	(void)ctx;
	return (void *)NativeFs_OpenRead(path);
}

static void *NativeDiscCopyReal_CreateTempExclusive(void *ctx, const char *destination, char *tempPathOut, size_t tempPathOutSize)
{
	int attempt;

	(void)ctx;

	for (attempt = 0; attempt < NATIVE_DISC_COPY_TEMP_ATTEMPTS; attempt++)
	{
		FILE *file;

		if (!NativeDiscCopy_MakeTempName(destination, attempt, tempPathOut, tempPathOutSize))
			return NULL;

		file = NativeFs_CreateExclusive(tempPathOut);
		if (file != NULL)
			return (void *)file;
	}

	tempPathOut[0] = '\0';
	return NULL;
}

static long NativeDiscCopyReal_Read(void *ctx, void *handle, void *buffer, size_t size)
{
	size_t got;

	(void)ctx;

	got = fread(buffer, 1, size, (FILE *)handle);
	if ((got == 0) && (ferror((FILE *)handle) != 0))
		return -1;

	return (long)got;
}

static int NativeDiscCopyReal_Write(void *ctx, void *handle, const void *buffer, size_t size)
{
	(void)ctx;
	return fwrite(buffer, 1, size, (FILE *)handle) == size;
}

static int NativeDiscCopyReal_FlushToDisk(void *ctx, void *handle)
{
	(void)ctx;
	return NativeFs_FlushToDisk((FILE *)handle);
}

static int NativeDiscCopyReal_Close(void *ctx, void *handle)
{
	(void)ctx;

	if (handle == NULL)
		return 1;

	return fclose((FILE *)handle) == 0;
}

static void NativeDiscCopyReal_RemoveOwned(void *ctx, const char *tempPath)
{
	(void)ctx;

	if (tempPath != NULL)
		NativeFs_Delete(tempPath);
}

static int NativeDiscCopyReal_Validate(void *ctx, const char *path)
{
	(void)ctx;
	return NativeDiscImage_ValidateCandidate(path, 0) == NATIVE_DISC_IMAGE_VALID;
}

static int NativeDiscCopyReal_Replace(void *ctx, const char *tempPath, const char *destination)
{
	(void)ctx;
	return NativeFs_Replace(tempPath, destination);
}

static int NativeDiscCopyReal_FlushDirectory(void *ctx, const char *destination)
{
	char directory[NATIVE_DISC_COPY_PATH_MAX];
	size_t length;

	(void)ctx;

	// The rename just landed the destination in its own directory. Flush that
	// directory so the entry survives a crash. A failure is a warning only: the
	// rename already happened.
	if (destination == NULL)
		return 0;

	strncpy(directory, destination, sizeof(directory) - 1);
	directory[sizeof(directory) - 1] = '\0';

	length = strlen(directory);
	while ((length > 0) && ((directory[length - 1] == '/') || (directory[length - 1] == '\\')))
	{
		directory[length - 1] = '\0';
		length--;
	}

	while ((length > 0) && (directory[length - 1] != '/') && (directory[length - 1] != '\\'))
	{
		directory[length - 1] = '\0';
		length--;
	}

	if (length == 0)
		return NativeFs_FlushDirectory(".");
	if (length == 1)
		return NativeFs_FlushDirectory(directory);

	directory[length - 1] = '\0'; // drop the trailing separator
	return NativeFs_FlushDirectory(directory);
}

const NativeDiscCopyOps g_nativeDiscCopyRealOps = {
    NativeDiscCopyReal_OpenRead,
    NativeDiscCopyReal_CreateTempExclusive,
    NativeDiscCopyReal_Read,
    NativeDiscCopyReal_Write,
    NativeDiscCopyReal_FlushToDisk,
    NativeDiscCopyReal_Close,
    NativeDiscCopyReal_RemoveOwned,
    NativeDiscCopyReal_Validate,
    NativeDiscCopyReal_Replace,
    NativeDiscCopyReal_FlushDirectory,
};
