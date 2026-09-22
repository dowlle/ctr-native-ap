#include "platform/native_link_register.h"

// Real per-user registry operations for ctr-ap:// link registration (issue
// #334, slice 4). Windows only; the decisions that use them are in
// platform/native_link_register.c. Everything is under HKEY_CURRENT_USER, so no
// administrator rights are involved and no other user's settings are touched.
// This layer only reads the tree and makes single changes; there is no
// recursive delete, and every decision (what to write, what to remove, how to
// undo) is in the portable unit the host harness covers.

#if defined(_WIN32)

#include <platform/native_win32.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <stdlib.h>

#define LINK_REG_CLASS_KEY L"Software\\Classes\\ctr-ap"
// Registry key paths are at most 255 characters per name; the full path is
// bounded by NATIVE_LINK_REG_PATH_MAX UTF-8 bytes under the class key.
#define LINK_REG_WIDE_PATH_MAX (NATIVE_LINK_REG_PATH_MAX + 64)
// Longest value name the registry allows, plus the terminator.
#define LINK_REG_WIDE_NAME_MAX 16384

static int linkRegUtf8ToWide(const char *in, wchar_t *out, int outCount)
{
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in, -1, out, outCount) > 0;
}

static int linkRegWideToUtf8(const wchar_t *in, char *out, int outCount)
{
	return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, in, -1, out, outCount, NULL, NULL) > 0;
}

// HKCU-relative path of a key given relative to the ctr-ap key.
static int linkRegFullPath(const char *path, wchar_t *out, int outCount)
{
	const size_t base = wcslen(LINK_REG_CLASS_KEY);

	if ((size_t)outCount < base + 2)
		return 0;
	memcpy(out, LINK_REG_CLASS_KEY, (base + 1) * sizeof(wchar_t));
	if (path[0] == '\0')
		return 1;
	out[base] = L'\\';
	return linkRegUtf8ToWide(path, out + base + 1, outCount - (int)base - 1);
}

// Buffers for the recursive read, on the heap (a value name alone may be 32 KiB).
typedef struct
{
	wchar_t name[LINK_REG_WIDE_NAME_MAX];
	unsigned char data[NATIVE_LINK_REG_DATA_MAX];
	char utf8[NATIVE_LINK_REG_NAME_MAX];
} LinkRegSnapBuffers;

static int linkRegSnapKey(HKEY key, const char *path, int depth, NativeLinkRegTree *t, LinkRegSnapBuffers *b)
{
	DWORD i;

	if (depth > NATIVE_LINK_REG_DEPTH_MAX || NativeLinkRegTree_AddKey(t, path) < 0)
		return 0;
	for (i = 0;; i++)
	{
		DWORD nameLen = LINK_REG_WIDE_NAME_MAX;
		DWORD type = 0;
		DWORD size = (DWORD)sizeof b->data;
		const LONG rc = RegEnumValueW(key, i, b->name, &nameLen, NULL, &type, b->data, &size);

		if (rc == ERROR_NO_MORE_ITEMS)
			break;
		// ERROR_MORE_DATA included: a value beyond the limits is never guessed at.
		if (rc != ERROR_SUCCESS || !linkRegWideToUtf8(b->name, b->utf8, (int)sizeof b->utf8) ||
		    !NativeLinkRegTree_SetValue(t, path, b->utf8, (unsigned long)type, b->data, (size_t)size))
			return 0;
	}
	for (i = 0;; i++)
	{
		wchar_t child[256];
		char childUtf8[NATIVE_LINK_REG_NAME_MAX];
		char childPath[NATIVE_LINK_REG_PATH_MAX];
		DWORD childLen = (DWORD)(sizeof child / sizeof child[0]);
		HKEY sub;
		LONG rc = RegEnumKeyExW(key, i, child, &childLen, NULL, NULL, NULL, NULL);
		int ok;

		if (rc == ERROR_NO_MORE_ITEMS)
			break;
		if (rc != ERROR_SUCCESS || !linkRegWideToUtf8(child, childUtf8, (int)sizeof childUtf8))
			return 0;
		if (snprintf(childPath, sizeof childPath, path[0] != '\0' ? "%s\\%s" : "%s%s", path, childUtf8) >=
		    (int)sizeof childPath)
			return 0;
		rc = RegOpenKeyExW(key, child, 0, KEY_READ, &sub);
		if (rc != ERROR_SUCCESS)
			return 0;
		ok = linkRegSnapKey(sub, childPath, depth + 1, t, b);
		RegCloseKey(sub);
		if (!ok)
			return 0;
	}
	return 1;
}

static int linkRegWinSnapshot(void *ctx, NativeLinkRegTree *out)
{
	LinkRegSnapBuffers *b;
	HKEY key;
	LONG rc;
	int ok;

	(void)ctx;
	NativeLinkRegTree_Clear(out);
	rc = RegOpenKeyExW(HKEY_CURRENT_USER, LINK_REG_CLASS_KEY, 0, KEY_READ, &key);
	if (rc == ERROR_FILE_NOT_FOUND)
		return 1;
	if (rc != ERROR_SUCCESS)
		return 0;
	b = (LinkRegSnapBuffers *)malloc(sizeof *b);
	ok = b != NULL && linkRegSnapKey(key, "", 0, out, b);
	free(b);
	RegCloseKey(key);
	return ok;
}

static int linkRegWinCreateKey(void *ctx, const char *path)
{
	wchar_t full[LINK_REG_WIDE_PATH_MAX];
	HKEY key;

	(void)ctx;
	if (!linkRegFullPath(path, full, LINK_REG_WIDE_PATH_MAX) ||
	    RegCreateKeyExW(HKEY_CURRENT_USER, full, 0, NULL, 0, KEY_READ, NULL, &key, NULL) != ERROR_SUCCESS)
		return 0;
	RegCloseKey(key);
	return 1;
}

static int linkRegWinSetValue(void *ctx, const char *path, const char *name, unsigned long type, const void *data,
                              size_t size)
{
	wchar_t full[LINK_REG_WIDE_PATH_MAX];
	wchar_t wideName[LINK_REG_WIDE_PATH_MAX];
	HKEY key;
	LONG rc;

	(void)ctx;
	if (size > NATIVE_LINK_REG_DATA_MAX || !linkRegFullPath(path, full, LINK_REG_WIDE_PATH_MAX) ||
	    !linkRegUtf8ToWide(name, wideName, LINK_REG_WIDE_PATH_MAX) ||
	    RegOpenKeyExW(HKEY_CURRENT_USER, full, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
		return 0;
	rc = RegSetValueExW(key, wideName[0] != L'\0' ? wideName : NULL, 0, (DWORD)type, (const BYTE *)data,
	                    (DWORD)size);
	RegCloseKey(key);
	return rc == ERROR_SUCCESS;
}

static int linkRegWinDeleteValue(void *ctx, const char *path, const char *name)
{
	wchar_t full[LINK_REG_WIDE_PATH_MAX];
	wchar_t wideName[LINK_REG_WIDE_PATH_MAX];
	HKEY key;
	LONG rc;

	(void)ctx;
	if (!linkRegFullPath(path, full, LINK_REG_WIDE_PATH_MAX) ||
	    !linkRegUtf8ToWide(name, wideName, LINK_REG_WIDE_PATH_MAX))
		return 0;
	rc = RegOpenKeyExW(HKEY_CURRENT_USER, full, 0, KEY_SET_VALUE, &key);
	if (rc == ERROR_FILE_NOT_FOUND)
		return 1;
	if (rc != ERROR_SUCCESS)
		return 0;
	rc = RegDeleteValueW(key, wideName[0] != L'\0' ? wideName : NULL);
	RegCloseKey(key);
	return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

// RegDeleteKeyW would delete a key's values along with it; check that there
// are none (and no subkeys) first.
static int linkRegWinDeleteEmptyKey(void *ctx, const char *path)
{
	wchar_t full[LINK_REG_WIDE_PATH_MAX];
	DWORD subkeys = 0;
	DWORD values = 0;
	HKEY key;
	LONG rc;

	(void)ctx;
	if (!linkRegFullPath(path, full, LINK_REG_WIDE_PATH_MAX))
		return 0;
	rc = RegOpenKeyExW(HKEY_CURRENT_USER, full, 0, KEY_READ, &key);
	if (rc == ERROR_FILE_NOT_FOUND)
		return 1;
	if (rc != ERROR_SUCCESS)
		return 0;
	rc = RegQueryInfoKeyW(key, NULL, NULL, NULL, &subkeys, NULL, NULL, &values, NULL, NULL, NULL, NULL);
	RegCloseKey(key);
	if (rc != ERROR_SUCCESS || subkeys != 0 || values != 0)
		return 0;
	rc = RegDeleteKeyW(HKEY_CURRENT_USER, full);
	return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

void NativeLinkReg_WinOps(NativeLinkRegOps *ops)
{
	ops->ctx = NULL;
	ops->snapshot = linkRegWinSnapshot;
	ops->createKey = linkRegWinCreateKey;
	ops->setValue = linkRegWinSetValue;
	ops->deleteValue = linkRegWinDeleteValue;
	ops->deleteEmptyKey = linkRegWinDeleteEmptyKey;
}

int NativeLinkReg_WinExePath(char *out, size_t cap)
{
	static wchar_t wide[32768]; // the longest path Windows returns
	const DWORD n = GetModuleFileNameW(NULL, wide, (DWORD)(sizeof(wide) / sizeof(wide[0])));

	if (n == 0 || n >= (DWORD)(sizeof(wide) / sizeof(wide[0])))
		return 0;
	wide[n] = L'\0';
	return linkRegWideToUtf8(wide, out, (int)cap);
}

#endif // _WIN32
