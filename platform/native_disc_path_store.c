// The remembered external disc image path, stored in its own file (issue #334,
// implementation slice 2). See include/platform/native_disc_path_store.h for
// the contract and for why this is not a config.ini key.

#include "platform/native_disc_path_store.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "platform/native_disc_limits.h"
#include "platform/native_disc_resolution.h"
#include "platform/native_fs_utf8.h"

// Room above the longest storable path for the decoration a legitimate file may
// carry: a 3-byte UTF-8 BOM and a short run of trailing CR/LF/space/tab. A read
// that fills the whole buffer therefore cannot be a path that fits, so it is
// rejected as overlong without reading (or allocating for) the rest.
#define NATIVE_DISC_PATH_STORE_SLACK 16
#define NATIVE_DISC_PATH_STORE_READ_MAX (NATIVE_DISC_PATH_CONTENT_MAX + NATIVE_DISC_PATH_STORE_SLACK + 1)

// How many candidate temporary names to try before giving up, matching the disc
// copy unit: every candidate carries the process id and an attempt counter, and
// the exclusive create guarantees this run owns exactly the file it made.
#define NATIVE_DISC_PATH_STORE_TEMP_ATTEMPTS 64

static long NativeDiscPathStore_ProcessId(void)
{
#if defined(_WIN32)
	return (long)_getpid();
#else
	return (long)getpid();
#endif
}

static int NativeDiscPathStore_MakeTempName(int attempt, char *out, size_t outSize)
{
	int written;

	if (attempt <= 0)
		written = snprintf(out, outSize, "%s.tmp.%ld", NATIVE_DISC_PATH_STORE_FILE, NativeDiscPathStore_ProcessId());
	else
		written = snprintf(out, outSize, "%s.tmp.%ld.%d", NATIVE_DISC_PATH_STORE_FILE, NativeDiscPathStore_ProcessId(), attempt);

	return (written > 0) && ((size_t)written < outSize);
}

int NativeDiscPathStore_Load(char *out, size_t outSize)
{
	char buffer[NATIVE_DISC_PATH_STORE_READ_MAX];
	const char *content = buffer;
	size_t length;
	size_t got;
	FILE *file;
	NativeDiscPathStatus status;

	if ((out == NULL) || (outSize == 0))
		return 0;

	out[0] = '\0';

	// Binary mode throughout: no text translation may come between the bytes
	// that were written and the bytes that are read back.
	file = NativeFs_OpenRead(NATIVE_DISC_PATH_STORE_FILE);
	if (file == NULL)
	{
		// No remembered disc yet. This is the ordinary first run, so it is
		// silent.
		return 0;
	}

	got = fread(buffer, 1, sizeof(buffer), file);
	if ((got == 0) && (ferror(file) != 0))
	{
		fclose(file);
		fprintf(stderr, "[CTR Native] remembered disc path ignored: the file could not be read\n");
		return 0;
	}
	fclose(file);

	if (got == sizeof(buffer))
	{
		fprintf(stderr, "[CTR Native] remembered disc path ignored: %s\n", NativeDiscPath_StatusText(NATIVE_DISC_PATH_TOO_LONG));
		return 0;
	}

	length = got;

	// One optional UTF-8 BOM, then the trailing line ending and any padding a
	// text editor may have left. Leading whitespace is not stripped: a path may
	// legitimately begin with a space, and NativeDiscPath_Validate decides.
	if ((length >= 3u) && ((unsigned char)content[0] == 0xEF) && ((unsigned char)content[1] == 0xBB) && ((unsigned char)content[2] == 0xBF))
	{
		content += 3;
		length -= 3u;
	}

	while ((length > 0u) &&
	       ((content[length - 1u] == '\n') || (content[length - 1u] == '\r') || (content[length - 1u] == ' ') || (content[length - 1u] == '\t')))
	{
		length--;
	}

	if (length == 0u)
	{
		fprintf(stderr, "[CTR Native] remembered disc path ignored: the file is empty\n");
		return 0;
	}

	if (memchr(content, '\0', length) != NULL)
	{
		fprintf(stderr, "[CTR Native] remembered disc path ignored: it contains an embedded NUL byte\n");
		return 0;
	}

	status = NativeDiscPath_Validate(content, length);
	if (status != NATIVE_DISC_PATH_OK)
	{
		fprintf(stderr, "[CTR Native] remembered disc path ignored: %s\n", NativeDiscPath_StatusText(status));
		return 0;
	}

	// Never truncate: a path that does not fit the caller's buffer is absent.
	if ((length + 1u) > outSize)
	{
		fprintf(stderr, "[CTR Native] remembered disc path ignored: %s\n", NativeDiscPath_StatusText(NATIVE_DISC_PATH_TOO_LONG));
		return 0;
	}

	memcpy(out, content, length);
	out[length] = '\0';
	return 1;
}

int NativeDiscPathStore_Save(const char *path)
{
	char tempPath[sizeof(NATIVE_DISC_PATH_STORE_FILE) + 64];
	FILE *file = NULL;
	size_t length;
	int attempt;
	NativeDiscPathStatus status;

	if ((path == NULL) || (path[0] == '\0'))
		return 0;

	length = strlen(path);

	// Only a path that survives a round trip through the file unchanged is
	// written; anything else is reported and left unsaved rather than stored
	// lossily.
	status = NativeDiscPath_Validate(path, length);
	if (status != NATIVE_DISC_PATH_OK)
	{
		fprintf(stderr, "[CTR Native] disc path not saved: %s\n", NativeDiscPath_StatusText(status));
		return 0;
	}

	for (attempt = 0; attempt < NATIVE_DISC_PATH_STORE_TEMP_ATTEMPTS; attempt++)
	{
		if (!NativeDiscPathStore_MakeTempName(attempt, tempPath, sizeof(tempPath)))
		{
			fprintf(stderr, "[CTR Native] disc path not saved: no temporary name available\n");
			return 0;
		}

		file = NativeFs_CreateExclusive(tempPath);
		if (file != NULL)
			break;
	}

	if (file == NULL)
	{
		fprintf(stderr, "[CTR Native] disc path not saved: no temporary file could be created\n");
		return 0;
	}

	// Write the path and exactly one newline, flush the bytes to stable storage,
	// and only then put the finished file in place. A crash at any point leaves
	// either the previous file or the new one, never a half-written path.
	if ((fwrite(path, 1, length, file) != length) || (fputc('\n', file) == EOF) || !NativeFs_FlushToDisk(file))
	{
		fclose(file);
		NativeFs_Delete(tempPath);
		fprintf(stderr, "[CTR Native] disc path not saved: the temporary file could not be written\n");
		return 0;
	}

	if (fclose(file) != 0)
	{
		NativeFs_Delete(tempPath);
		fprintf(stderr, "[CTR Native] disc path not saved: the temporary file could not be closed\n");
		return 0;
	}

	if (!NativeFs_Replace(tempPath, NATIVE_DISC_PATH_STORE_FILE))
	{
		NativeFs_Delete(tempPath);
		fprintf(stderr, "[CTR Native] disc path not saved: the temporary file could not be put in place\n");
		return 0;
	}

	// The store file is relative to the working directory, so the directory to
	// flush is that directory. Same rule as the disc copy: the rename already
	// happened, so a failure here is a warning and not a failed save.
	if (!NativeFs_FlushDirectory("."))
		fprintf(stderr, "[CTR Native] disc path warning: the game folder could not be flushed to disk\n");

	return 1;
}
