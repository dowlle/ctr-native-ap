#ifndef NATIVE_FS_UTF8_H
#define NATIVE_FS_UTF8_H

#include <stddef.h>
#include <stdio.h>

// UTF-8 filesystem helpers for the disc feature (issue #334, slice 2).
//
// Every path argument is a UTF-8 C string. On Windows the helpers convert it to
// UTF-16 and call the wide Win32 API, normalizing absolute paths to the Win32
// extended long-path form, so a selected path with non-ASCII characters or
// beyond MAX_PATH still works; on POSIX the bytes pass through to the ordinary
// calls.
// This module is deliberately small and is used by the new disc
// open/exists/scan/temp/delete/replace sites only. It does not change how the
// rest of the engine opens its files.
//
// Durability boundary: NativeFs_FlushToDisk does not return success until the
// written bytes have reached stable storage (fsync on POSIX, _commit on
// Windows). NativeFs_Replace is atomic on POSIX (rename) and is issued with
// MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH on Windows.

int NativeFs_FileExists(const char *utf8Path);
FILE *NativeFs_OpenRead(const char *utf8Path);

// Create the file exclusively: fails (NULL) when the path already exists,
// whether a regular file, a hard link, a symlink or a reparse point. The caller
// owns exactly the path it passed and may delete only that path.
FILE *NativeFs_CreateExclusive(const char *utf8Path);

// fflush + fsync/_commit. Returns 0 on failure, 1 on success.
int NativeFs_FlushToDisk(FILE *file);

int NativeFs_Delete(const char *utf8Path);

// Atomically move src over dst. Returns 0 on failure.
int NativeFs_Replace(const char *utf8Source, const char *utf8Destination);

// Directory enumeration. NativeFs_ReadDir returns 1 and a UTF-8 name, or 0 at
// the end of the directory. "." and ".." are skipped.
typedef struct NativeFsDir NativeFsDir;
NativeFsDir *NativeFs_OpenDir(const char *utf8Path);
int NativeFs_ReadDir(NativeFsDir *dir, char *nameOut, size_t nameOutSize);
void NativeFs_CloseDir(NativeFsDir *dir);

#endif // NATIVE_FS_UTF8_H
