#include "platform/native_fs_utf8.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#include <platform/native_win32.h>

#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <sys/stat.h>
#include <wchar.h>

#include "platform/native_fs_winpath.h"

#define NATIVE_FS_WIDE_MAX 4096

// Convert a UTF-8 path to the UTF-16 extended-length form. The classification
// and prefixing are pure and freestanding (platform/native_fs_winpath.h), so the
// host harness tests them without Windows; this function only does the UTF-8
// conversion and hands the code unit array over. The converted length is known
// before any indexing, so short paths are never read out of bounds.
static int NativeFs_Utf8ToWidePath(const char *utf8Path, wchar_t *out, size_t outCount)
{
	NativeWinPathUnit wide[NATIVE_FS_WIDE_MAX];
	NativeWinPathUnit prefixed[NATIVE_FS_WIDE_MAX + 16]; // prefix adds at most 6 units
	int need;
	size_t length;
	size_t i;

	if ((utf8Path == NULL) || (out == NULL) || (outCount == 0))
		return 0;

	need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Path, -1, (wchar_t *)wide, (int)(sizeof(wide) / sizeof(wide[0])));
	if ((need <= 0) || (need > (int)(sizeof(wide) / sizeof(wide[0]))))
		return 0;

	length = (size_t)need - 1u; // drop the terminator MultiByteToWideChar counted

	if (!NativeWinPath_Prefix(wide, length, prefixed, sizeof(prefixed) / sizeof(prefixed[0])))
		return 0;

	{
		size_t prefixedLength = NativeWinPath_PrefixedLength(wide, length);

		if (prefixedLength + 1u > outCount)
			return 0;

		for (i = 0; i < prefixedLength; i++)
			out[i] = (wchar_t)prefixed[i];
		out[prefixedLength] = L'\0';
	}

	return 1;
}

static int NativeFs_WideToUtf8(const wchar_t *wide, char *out, size_t outCount)
{
	int need = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);

	if ((need <= 0) || ((size_t)need > outCount))
		return 0;

	return WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)outCount, NULL, NULL) > 0;
}

int NativeFs_FileExists(const char *utf8Path)
{
	wchar_t wide[NATIVE_FS_WIDE_MAX];
	DWORD attributes;

	if (!NativeFs_Utf8ToWidePath(utf8Path, wide, (int)(sizeof(wide)/sizeof(wide[0]))))
		return 0;

	attributes = GetFileAttributesW(wide);
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return 0;

	return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

FILE *NativeFs_OpenRead(const char *utf8Path)
{
	wchar_t wide[NATIVE_FS_WIDE_MAX];

	if (!NativeFs_Utf8ToWidePath(utf8Path, wide, (int)(sizeof(wide)/sizeof(wide[0]))))
		return NULL;

	return _wfopen(wide, L"rb");
}

FILE *NativeFs_CreateExclusive(const char *utf8Path)
{
	wchar_t wide[NATIVE_FS_WIDE_MAX];
	HANDLE handle;
	int fd;
	FILE *file;

	if (!NativeFs_Utf8ToWidePath(utf8Path, wide, (int)(sizeof(wide)/sizeof(wide[0]))))
		return NULL;

	// CREATE_NEW fails with ERROR_FILE_EXISTS when anything already occupies the
	// name, so a pre-existing temporary path is never truncated or written
	// through.
	handle = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
		return NULL;

	fd = _open_osfhandle((intptr_t)handle, _O_WRONLY | _O_BINARY);
	if (fd == -1)
	{
		CloseHandle(handle);
		return NULL;
	}

	file = _fdopen(fd, "wb");
	if (file == NULL)
	{
		_close(fd);
		return NULL;
	}

	return file;
}

int NativeFs_FlushToDisk(FILE *file)
{
	if ((file == NULL) || (fflush(file) != 0))
		return 0;

	return _commit(_fileno(file)) == 0;
}

int NativeFs_Delete(const char *utf8Path)
{
	wchar_t wide[NATIVE_FS_WIDE_MAX];

	if (!NativeFs_Utf8ToWidePath(utf8Path, wide, (int)(sizeof(wide)/sizeof(wide[0]))))
		return 0;

	return DeleteFileW(wide) != 0;
}

int NativeFs_Replace(const char *utf8Source, const char *utf8Destination)
{
	wchar_t source[NATIVE_FS_WIDE_MAX];
	wchar_t destination[NATIVE_FS_WIDE_MAX];

	if (!NativeFs_Utf8ToWidePath(utf8Source, source, (int)(sizeof(source)/sizeof(source[0]))))
		return 0;
	if (!NativeFs_Utf8ToWidePath(utf8Destination, destination, (int)(sizeof(destination)/sizeof(destination[0]))))
		return 0;

	return MoveFileExW(source, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

int NativeFs_FlushDirectory(const char *utf8DirectoryPath)
{
	// MOVEFILE_WRITE_THROUGH already flushes the destination volume on Windows,
	// and there is no portable way to open and flush a directory handle here.
	// Nothing to do.
	(void)utf8DirectoryPath;
	return 1;
}

struct NativeFsDir
{
	HANDLE handle;
	WIN32_FIND_DATAW data;
	int first;
};

NativeFsDir *NativeFs_OpenDir(const char *utf8Path)
{
	char *pattern;
	wchar_t wide[NATIVE_FS_WIDE_MAX];
	NativeFsDir *dir;
	size_t length;

	if (utf8Path == NULL)
		return NULL;

	// Size the "<dir>/*" staging buffer from the actual input length rather than
	// a fixed maximum, so a long UTF-8 path is not rejected before conversion.
	length = strlen(utf8Path);
	pattern = (char *)malloc(length + 3u); // "/*" + NUL
	if (pattern == NULL)
		return NULL;

	memcpy(pattern, utf8Path, length);
	pattern[length] = '/';
	pattern[length + 1u] = '*';
	pattern[length + 2u] = '\0';

	if (!NativeFs_Utf8ToWidePath(pattern, wide, (int)(sizeof(wide) / sizeof(wide[0]))))
	{
		free(pattern);
		return NULL;
	}

	free(pattern);

	dir = (NativeFsDir *)calloc(1, sizeof(*dir));
	if (dir == NULL)
		return NULL;

	dir->handle = FindFirstFileW(wide, &dir->data);
	if (dir->handle == INVALID_HANDLE_VALUE)
	{
		free(dir);
		return NULL;
	}

	dir->first = 1;
	return dir;
}

int NativeFs_ReadDir(NativeFsDir *dir, char *nameOut, size_t nameOutSize)
{
	if (dir == NULL)
		return 0;

	for (;;)
	{
		if (dir->first)
			dir->first = 0;
		else if (!FindNextFileW(dir->handle, &dir->data))
			return 0;

		if ((wcscmp(dir->data.cFileName, L".") == 0) || (wcscmp(dir->data.cFileName, L"..") == 0))
			continue;

		return NativeFs_WideToUtf8(dir->data.cFileName, nameOut, nameOutSize);
	}
}

void NativeFs_CloseDir(NativeFsDir *dir)
{
	if (dir == NULL)
		return;

	if (dir->handle != INVALID_HANDLE_VALUE)
		FindClose(dir->handle);

	free(dir);
}

#else // POSIX

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int NativeFs_FileExists(const char *utf8Path)
{
	struct stat info;

	if ((utf8Path == NULL) || (stat(utf8Path, &info) != 0))
		return 0;

	return S_ISREG(info.st_mode);
}

FILE *NativeFs_OpenRead(const char *utf8Path)
{
	if (utf8Path == NULL)
		return NULL;

	return fopen(utf8Path, "rb");
}

FILE *NativeFs_CreateExclusive(const char *utf8Path)
{
	int fd;
	FILE *file;

	if (utf8Path == NULL)
		return NULL;

	// O_EXCL makes creation fail when the name is already taken by a regular
	// file, hard link or symlink, so a pre-existing temporary path is never
	// truncated or written through.
	fd = open(utf8Path, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd == -1)
		return NULL;

	file = fdopen(fd, "wb");
	if (file == NULL)
		close(fd);

	return file;
}

int NativeFs_FlushToDisk(FILE *file)
{
	if ((file == NULL) || (fflush(file) != 0))
		return 0;

	return fsync(fileno(file)) == 0;
}

int NativeFs_Delete(const char *utf8Path)
{
	if (utf8Path == NULL)
		return 0;

	return unlink(utf8Path) == 0;
}

int NativeFs_Replace(const char *utf8Source, const char *utf8Destination)
{
	if ((utf8Source == NULL) || (utf8Destination == NULL))
		return 0;

	return rename(utf8Source, utf8Destination) == 0;
}

int NativeFs_FlushDirectory(const char *utf8DirectoryPath)
{
	int fd;

	if (utf8DirectoryPath == NULL)
		return 0;

	// Open with O_RDONLY (O_DIRECTORY is not POSIX; a plain open of a directory
	// is enough to fsync it) and fsync so the rename into this directory is
	// durable. Closing is best-effort.
	fd = open(utf8DirectoryPath, O_RDONLY);
	if (fd == -1)
		return 0;

	if (fsync(fd) != 0)
	{
		close(fd);
		return 0;
	}

	close(fd);
	return 1;
}

struct NativeFsDir
{
	DIR *dir;
};

NativeFsDir *NativeFs_OpenDir(const char *utf8Path)
{
	NativeFsDir *dir;

	if (utf8Path == NULL)
		return NULL;

	dir = (NativeFsDir *)calloc(1, sizeof(*dir));
	if (dir == NULL)
		return NULL;

	dir->dir = opendir(utf8Path);
	if (dir->dir == NULL)
	{
		free(dir);
		return NULL;
	}

	return dir;
}

int NativeFs_ReadDir(NativeFsDir *dir, char *nameOut, size_t nameOutSize)
{
	struct dirent *entry;
	size_t length;

	if (dir == NULL)
		return 0;

	for (;;)
	{
		entry = readdir(dir->dir);
		if (entry == NULL)
			return 0;

		if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0))
			continue;

		length = strlen(entry->d_name);
		if (length + 1u > nameOutSize)
			continue;

		memcpy(nameOut, entry->d_name, length + 1u);
		return 1;
	}
}

void NativeFs_CloseDir(NativeFsDir *dir)
{
	if (dir == NULL)
		return;

	if (dir->dir != NULL)
		closedir(dir->dir);

	free(dir);
}

#endif
