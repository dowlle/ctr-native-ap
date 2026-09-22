#include "platform/native_link_register.h"

// Real per-user registry operations for ctr-ap:// link registration (issue
// #334, slice 4). Windows only; the decisions that use them are in
// platform/native_link_register.c. Everything is under HKEY_CURRENT_USER, so no
// administrator rights are involved and no other user's settings are touched.

#if defined(_WIN32)

#include <platform/native_win32.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define LINK_REG_CLASS_KEY L"Software\\Classes\\ctr-ap"
#define LINK_REG_COMMAND_KEY L"shell\\open\\command"
#define LINK_REG_URL_PROTOCOL L"URL Protocol"
#define LINK_REG_OWNER L"CtrApClient"
#define LINK_REG_WIDE_MAX NATIVE_LINK_REG_TEXT_MAX
#define LINK_REG_TREE_DEPTH_MAX 8

static int linkRegUtf8ToWide(const char *in, wchar_t *out, int outCount)
{
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in, -1, out, outCount) > 0;
}

static int linkRegWideToUtf8(const wchar_t *in, char *out, int outCount)
{
	return WideCharToMultiByte(CP_UTF8, 0, in, -1, out, outCount, NULL, NULL) > 0;
}

// A string value as UTF-8. Missing gives "". A value that is not a string or
// does not fit reads as "?": present, and never equal to this client's text.
static void linkRegReadString(HKEY key, const wchar_t *name, char *out, size_t cap)
{
	wchar_t wide[LINK_REG_WIDE_MAX];
	DWORD type = 0;
	DWORD bytes = sizeof(wide) - sizeof(wchar_t);
	LONG rc = RegQueryValueExW(key, name, NULL, &type, (LPBYTE)wide, &bytes);

	out[0] = '\0';
	if (rc == ERROR_FILE_NOT_FOUND)
		return;
	if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
	{
		snprintf(out, cap, "?");
		return;
	}
	wide[bytes / sizeof(wchar_t)] = L'\0';
	if (!linkRegWideToUtf8(wide, out, (int)cap))
		snprintf(out, cap, "?");
}

static int linkRegValueExists(HKEY key, const wchar_t *name)
{
	return RegQueryValueExW(key, name, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
}

static int linkRegWinRead(void *ctx, NativeLinkRegState *out)
{
	HKEY key;
	HKEY command;
	LONG rc;

	(void)ctx;
	memset(out, 0, sizeof *out);
	rc = RegOpenKeyExW(HKEY_CURRENT_USER, LINK_REG_CLASS_KEY, 0, KEY_READ, &key);
	if (rc == ERROR_FILE_NOT_FOUND)
		return 1;
	if (rc != ERROR_SUCCESS)
		return 0;

	out->present = 1;
	out->urlProtocol = linkRegValueExists(key, LINK_REG_URL_PROTOCOL);
	out->owned = linkRegValueExists(key, LINK_REG_OWNER);
	linkRegReadString(key, NULL, out->description, sizeof out->description);

	rc = RegOpenKeyExW(key, LINK_REG_COMMAND_KEY, 0, KEY_READ, &command);
	if (rc == ERROR_SUCCESS)
	{
		linkRegReadString(command, NULL, out->command, sizeof out->command);
		RegCloseKey(command);
	}
	else if (rc != ERROR_FILE_NOT_FOUND)
	{
		snprintf(out->command, sizeof out->command, "?");
	}
	RegCloseKey(key);
	return 1;
}

static int linkRegSetString(HKEY key, const wchar_t *name, const char *utf8)
{
	wchar_t wide[LINK_REG_WIDE_MAX];
	LONG rc;

	if (utf8[0] == '\0')
	{
		rc = RegDeleteValueW(key, name);
		return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
	}
	if (!linkRegUtf8ToWide(utf8, wide, LINK_REG_WIDE_MAX))
		return 0;
	return RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)wide,
	                      (DWORD)((wcslen(wide) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

static int linkRegSetFlag(HKEY key, const wchar_t *name, int on, const char *value)
{
	if (on)
	{
		wchar_t wide[8];
		if (!linkRegUtf8ToWide(value, wide, 8))
			return 0;
		return RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)wide,
		                      (DWORD)((wcslen(wide) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
	}
	{
		const LONG rc = RegDeleteValueW(key, name);
		return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
	}
}

static int linkRegWinWrite(void *ctx, const NativeLinkRegState *s)
{
	HKEY key;
	HKEY command;
	int ok = 1;

	(void)ctx;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, LINK_REG_CLASS_KEY, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &key, NULL) !=
	    ERROR_SUCCESS)
		return 0;

	ok = linkRegSetString(key, NULL, s->description) && ok;
	ok = linkRegSetFlag(key, LINK_REG_URL_PROTOCOL, s->urlProtocol, "") && ok;
	ok = linkRegSetFlag(key, LINK_REG_OWNER, s->owned, "1") && ok;

	if (s->command[0] != '\0')
	{
		if (RegCreateKeyExW(key, LINK_REG_COMMAND_KEY, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &command, NULL) ==
		    ERROR_SUCCESS)
		{
			ok = linkRegSetString(command, NULL, s->command) && ok;
			RegCloseKey(command);
		}
		else
		{
			ok = 0;
		}
	}
	else if (RegOpenKeyExW(key, LINK_REG_COMMAND_KEY, 0, KEY_READ | KEY_WRITE, &command) == ERROR_SUCCESS)
	{
		ok = linkRegSetString(command, NULL, "") && ok;
		RegCloseKey(command);
	}

	RegCloseKey(key);
	return ok;
}

// RegDeleteKeyW refuses a key with subkeys, so delete depth-first.
static LONG linkRegDeleteTree(HKEY parent, const wchar_t *name, int depth)
{
	HKEY key;
	wchar_t child[256];
	DWORD length;
	LONG rc;

	if (depth > LINK_REG_TREE_DEPTH_MAX)
		return ERROR_ACCESS_DENIED;
	rc = RegOpenKeyExW(parent, name, 0, KEY_READ | KEY_WRITE, &key);
	if (rc == ERROR_FILE_NOT_FOUND)
		return ERROR_SUCCESS;
	if (rc != ERROR_SUCCESS)
		return rc;
	for (;;)
	{
		length = (DWORD)(sizeof(child) / sizeof(child[0]));
		rc = RegEnumKeyExW(key, 0, child, &length, NULL, NULL, NULL, NULL);
		if (rc == ERROR_NO_MORE_ITEMS)
			break;
		if (rc == ERROR_SUCCESS)
			rc = linkRegDeleteTree(key, child, depth + 1);
		if (rc != ERROR_SUCCESS)
		{
			RegCloseKey(key);
			return rc;
		}
	}
	RegCloseKey(key);
	return RegDeleteKeyW(parent, name);
}

static int linkRegWinRemove(void *ctx)
{
	const LONG rc = linkRegDeleteTree(HKEY_CURRENT_USER, LINK_REG_CLASS_KEY, 0);
	(void)ctx;
	return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

void NativeLinkReg_WinOps(NativeLinkRegOps *ops)
{
	ops->ctx = NULL;
	ops->read = linkRegWinRead;
	ops->write = linkRegWinWrite;
	ops->remove = linkRegWinRemove;
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
