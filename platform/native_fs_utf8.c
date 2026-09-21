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

#define NATIVE_FS_WIDE_MAX 4096

// Convert a UTF-8 path to UTF-16, replacing forward slashes and adding the
// \\?\ long-path prefix for absolute paths (including the \\?\UNC\ form for a
// UNC path). Relative paths are used unchanged: the \\?\ prefix disables the
// relative-path resolution and dot-segment handling we still want there.
static int NativeFs_Utf8ToWidePath(const char *utf8Path, wchar_t *out, size_t outCount)
{
	wchar_t wide[NATIVE_FS_WIDE_MAX];
	wchar_t *p;
	int need;

	if ((utf8Path == NULL) || (out == NULL) || (outCount == 0))
		return 0;

	need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Path, -1, wide, (int)(sizeof(wide) / sizeof(wide[0])));
	if ((need <= 0) || ((size_t)need > (int)(sizeof(wide)/sizeof(wide[0]))))
		return 0;

	for (p = wide; *p != L'\0'; p++)
	{
		if (*p == L'/')
			*p = L'\\';
	}

	if ((wide[0] == L'\\') && (wide[1] == L'\\') && ((wide[2] == L'?') || (wide[2] == L'.')) && (wide[3] == L'\\'))
	{
		if (wcslen(wide) + 1u > outCount)
			return 0;
		wcscpy(out, wide);
		return 1;
	}

	if ((wide[0] == L'\\') && (wide[1] == L'\\'))
	{
		// UNC: \\server\share -> \\?\UNC\server\share
		if (swprintf(out, outCount, L"\\\\?\\UNC\\%s", wide + 2) < 0)
			return 0;
		return 1;
	}

	if ((wide[1] == L':') && ((wide[2] == L'\\') || (wide[2] == L'\0')))
	{
		if (swprintf(out, outCount, L"\\\\?\\%s", wide) < 0)
			return 0;
		return 1;
	}

	if (wcslen(wide) + 1u > outCount)
		return 0;
	wcscpy(out, wide);
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

struct NativeFsDir
{
	HANDLE handle;
	WIN32_FIND_DATAW data;
	int first;
};

NativeFsDir *NativeFs_OpenDir(const char *utf8Path)
{
	char pattern[NATIVE_FS_WIDE_MAX];
	wchar_t wide[NATIVE_FS_WIDE_MAX];
	NativeFsDir *dir;
	int written;

	if (utf8Path == NULL)
		return NULL;

	written = snprintf(pattern, sizeof(pattern), "%s/*", utf8Path);
	if ((written <= 0) || ((size_t)written >= sizeof(pattern)))
		return NULL;

	if (!NativeFs_Utf8ToWidePath(pattern, wide, (int)(sizeof(wide)/sizeof(wide[0]))))
		return NULL;

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
