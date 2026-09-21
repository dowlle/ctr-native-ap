#ifndef NATIVE_DISC_COPY_H
#define NATIVE_DISC_COPY_H

#include <stddef.h>

// Filesystem copy state machine for the disc wizard (issue #334, slice 2).
//
// Copying a player-selected image into the game folder goes through a unique
// temporary sibling created exclusively by this process, is flushed to stable
// storage, validated as a disc image, then moved over the destination. Any
// failure leaves the destination untouched and removes only the temporary file
// this run created. The machine is freestanding over NativeDiscCopyOps so the
// host harness can drive the real production operations against temporary
// directories and inject write, flush, validation and replace failures.
//
// The machine never opens the destination for writing; that is the whole point
// of going through a temporary sibling, so a valid destination that is a hard
// link is replaced, never modified in place.

typedef enum
{
	NATIVE_DISC_COPY_OK = 0,
	NATIVE_DISC_COPY_BAD_ARGUMENT,
	NATIVE_DISC_COPY_OPEN_SOURCE_FAILED,
	NATIVE_DISC_COPY_TEMP_FAILED,
	NATIVE_DISC_COPY_READ_FAILED,
	NATIVE_DISC_COPY_WRITE_FAILED,
	NATIVE_DISC_COPY_FLUSH_FAILED,
	NATIVE_DISC_COPY_CLOSE_FAILED,
	NATIVE_DISC_COPY_VALIDATE_FAILED,
	NATIVE_DISC_COPY_REPLACE_FAILED,
	NATIVE_DISC_COPY_DIRECTORY_FLUSH_WARNING
} NativeDiscCopyResult;

static inline const char *NativeDiscCopy_ResultText(NativeDiscCopyResult result)
{
	switch (result)
	{
	case NATIVE_DISC_COPY_OK:
		return "ok";
	case NATIVE_DISC_COPY_BAD_ARGUMENT:
		return "invalid destination";
	case NATIVE_DISC_COPY_OPEN_SOURCE_FAILED:
		return "cannot open the selected disc image";
	case NATIVE_DISC_COPY_TEMP_FAILED:
		return "cannot create a temporary file beside the destination";
	case NATIVE_DISC_COPY_READ_FAILED:
		return "cannot read the selected disc image";
	case NATIVE_DISC_COPY_WRITE_FAILED:
		return "cannot write the temporary file";
	case NATIVE_DISC_COPY_FLUSH_FAILED:
		return "cannot flush the copied image to disk";
	case NATIVE_DISC_COPY_CLOSE_FAILED:
		return "cannot finish the temporary file";
	case NATIVE_DISC_COPY_VALIDATE_FAILED:
		return "the copied file is not a valid disc image";
	case NATIVE_DISC_COPY_REPLACE_FAILED:
		return "cannot replace the file in the game folder";
	case NATIVE_DISC_COPY_DIRECTORY_FLUSH_WARNING:
		return "the disc image was copied but the game folder could not be flushed to disk";
	}

	return "the disc could not be copied";
}

// Injected operations. The real ones (platform/native_disc_copy.c) use the
// UTF-8 filesystem layer; the harness wraps them to inject failures.
typedef struct NativeDiscCopyOps
{
	// Open source for reading. NULL on failure.
	void *(*openRead)(void *ctx, const char *path);
	// Create a fresh temporary sibling of destination, exclusively. On success
	// returns a handle and writes the path this run owns into tempPathOut.
	// Must fail rather than reuse an existing path.
	void *(*createTempExclusive)(void *ctx, const char *destination, char *tempPathOut, size_t tempPathOutSize);
	// Read up to size bytes. >0 bytes read, 0 at end, <0 on error.
	long (*read)(void *ctx, void *handle, void *buffer, size_t size);
	// Write exactly size bytes. 1 on success.
	int (*write)(void *ctx, void *handle, const void *buffer, size_t size);
	// Flush to stable storage. 1 on success.
	int (*flushToDisk)(void *ctx, void *handle);
	// Close a handle. 1 on success (a deferred write error must show up here).
	int (*close)(void *ctx, void *handle);
	// Remove the temporary path this run owns.
	void (*removeOwned)(void *ctx, const char *tempPath);
	// Validate a completed file. 1 when it is a usable disc image.
	int (*validate)(void *ctx, const char *path);
	// Move temp over destination atomically. 1 on success.
	int (*replace)(void *ctx, const char *tempPath, const char *destination);
	// Optional: flush the destination directory so the rename is durable. May
	// be NULL. Called only after a successful replace; a failure is reported as
	// the WARNING result below, which does not undo the already-completed
	// replace.
	int (*flushDirectory)(void *ctx, const char *destination);
} NativeDiscCopyOps;

#define NATIVE_DISC_COPY_BUFFER_SIZE 65536u

static inline NativeDiscCopyResult NativeDiscCopy_Run(const NativeDiscCopyOps *ops, void *ctx, const char *source, const char *destination,
                                                      char *tempPathOut, size_t tempPathOutSize)
{
	char buffer[NATIVE_DISC_COPY_BUFFER_SIZE];
	NativeDiscCopyResult result = NATIVE_DISC_COPY_OK;
	void *in;
	void *out;
	long got;

	if ((ops == NULL) || (source == NULL) || (source[0] == '\0') || (destination == NULL) || (destination[0] == '\0') || (tempPathOut == NULL) ||
	    (tempPathOutSize == 0))
	{
		return NATIVE_DISC_COPY_BAD_ARGUMENT;
	}

	tempPathOut[0] = '\0';

	in = ops->openRead(ctx, source);
	if (in == NULL)
		return NATIVE_DISC_COPY_OPEN_SOURCE_FAILED;

	out = ops->createTempExclusive(ctx, destination, tempPathOut, tempPathOutSize);
	if (out == NULL)
	{
		ops->close(ctx, in);
		return NATIVE_DISC_COPY_TEMP_FAILED;
	}

	for (;;)
	{
		got = ops->read(ctx, in, buffer, sizeof(buffer));
		if (got < 0)
		{
			result = NATIVE_DISC_COPY_READ_FAILED;
			break;
		}
		if (got == 0)
			break;
		if (!ops->write(ctx, out, buffer, (size_t)got))
		{
			result = NATIVE_DISC_COPY_WRITE_FAILED;
			break;
		}
	}

	if ((result == NATIVE_DISC_COPY_OK) && !ops->flushToDisk(ctx, out))
		result = NATIVE_DISC_COPY_FLUSH_FAILED;

	if (!ops->close(ctx, out) && (result == NATIVE_DISC_COPY_OK))
		result = NATIVE_DISC_COPY_CLOSE_FAILED;

	if ((result == NATIVE_DISC_COPY_OK) && !ops->validate(ctx, tempPathOut))
		result = NATIVE_DISC_COPY_VALIDATE_FAILED;

	if ((result == NATIVE_DISC_COPY_OK) && !ops->replace(ctx, tempPathOut, destination))
		result = NATIVE_DISC_COPY_REPLACE_FAILED;

	// The rename already happened, so the copy has succeeded. Flushing the
	// destination directory only adds durability for a crash right after; a
	// failure here is a warning result, never a failed copy, and the temporary
	// file must not be removed (it is already the destination).
	if ((result == NATIVE_DISC_COPY_OK) && (ops->flushDirectory != NULL) && !ops->flushDirectory(ctx, destination))
		result = NATIVE_DISC_COPY_DIRECTORY_FLUSH_WARNING;

	ops->close(ctx, in);

	if ((result != NATIVE_DISC_COPY_OK) && (result != NATIVE_DISC_COPY_DIRECTORY_FLUSH_WARNING))
		ops->removeOwned(ctx, tempPathOut);

	return result;
}

// Production implementation, used by platform/native_disc_resolution.c and
// driven against real temporary directories by tools/test-disc-copy.c.
extern const NativeDiscCopyOps g_nativeDiscCopyRealOps;

#endif // NATIVE_DISC_COPY_H
