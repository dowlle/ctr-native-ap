#include "platform/native_steam_route.h"

#include <stdio.h>
#include <string.h>

#include "platform/native_fs_utf8.h"

// Steam route for room links (issue #334, slice 4). See the header. No SDL and
// no engine headers; the host harness compiles this unit with
// platform/native_fs_utf8.c.

int NativeSteamRoute_ParseGameId(const char *text, unsigned long long *out)
{
	unsigned long long v = 0;
	size_t i;
	size_t n;

	if (text == NULL || out == NULL)
		return 0;
	n = strlen(text);
	if (n == 0 || n > 20)
		return 0;
	for (i = 0; i < n; i++)
	{
		const unsigned d = (unsigned)(unsigned char)text[i] - '0';
		if (d > 9)
			return 0;
		if (v > (18446744073709551615ULL - d) / 10ULL)
			return 0; // would overflow 64 bits
		v = v * 10ULL + d;
	}
	if (v == 0)
		return 0;
	*out = v;
	return 1;
}

int NativeSteamRoute_FormatUrl(unsigned long long id, char *out, size_t cap)
{
	int n;

	if (id == 0 || out == NULL || cap == 0)
		return 0;
	n = snprintf(out, cap, "steam://rungameid/%llu", id);
	return n > 0 && (size_t)n < cap;
}

static int steamRoutePath(const char *stateDir, const char *name, char *out, size_t cap)
{
	int n;

	if (stateDir == NULL)
		return 0;
	n = snprintf(out, cap, "%s/%s", stateDir, name);
	return n > 0 && (size_t)n < cap;
}

int NativeSteamRoute_Load(const char *stateDir, unsigned long long *out)
{
	char path[1024];
	char text[32];
	size_t n;
	FILE *f;

	if (out == NULL || !steamRoutePath(stateDir, NATIVE_STEAM_ROUTE_FILE, path, sizeof path))
		return 0;
	f = NativeFs_OpenRead(path);
	if (f == NULL)
		return 0;
	n = fread(text, 1, sizeof text - 1, f);
	fclose(f);
	// Exactly "<digits>\n".
	if (n < 2 || text[n - 1] != '\n')
		return 0;
	text[n - 1] = '\0';
	return NativeSteamRoute_ParseGameId(text, out);
}

int NativeSteamRoute_Save(const char *stateDir, unsigned long long id)
{
	char path[1024];
	char temp[1024];
	char text[32];
	FILE *f;
	int n;
	int ok;

	if (id == 0 || !steamRoutePath(stateDir, NATIVE_STEAM_ROUTE_FILE, path, sizeof path) ||
	    !steamRoutePath(stateDir, NATIVE_STEAM_ROUTE_FILE ".tmp", temp, sizeof temp))
		return 0;
	n = snprintf(text, sizeof text, "%llu\n", id);

	// Only the primary client saves, so a leftover temp file is from a crash.
	if (NativeFs_FileExists(temp))
		NativeFs_Delete(temp);
	f = NativeFs_CreateExclusive(temp);
	if (f == NULL)
		return 0;
	ok = fwrite(text, 1, (size_t)n, f) == (size_t)n;
	ok = NativeFs_FlushToDisk(f) && ok;
	ok = (fclose(f) == 0) && ok;
	if (ok)
		ok = NativeFs_Replace(temp, path);
	if (!ok)
	{
		NativeFs_Delete(temp);
		return 0;
	}
	NativeFs_FlushDirectory(stateDir);
	return 1;
}
