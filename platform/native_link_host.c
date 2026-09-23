#include "platform/native_link_host.h"

#include <stdio.h>
#include <string.h>

// Real filesystem primitives for the one-click-connect store (issue #334,
// slice 3). No SDL and no engine headers: the host harness compiles this unit
// with platform/native_fs_utf8.c and drives it from several processes.

#if defined(_WIN32)
#include <platform/native_win32.h>
#else
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#endif

static int linkHostPath(const NativeLinkHost *host, const char *name, char *out, size_t cap)
{
	const int n = snprintf(out, cap, "%s/%s", host->dir, name);
	return n > 0 && (size_t)n < cap;
}

static void linkHostSleepMs(int ms)
{
#if defined(_WIN32)
	Sleep((DWORD)ms);
#else
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
#endif
}

static int linkHostLock(void *ctx, int blocking)
{
	NativeLinkHost *host = (NativeLinkHost *)ctx;
	char path[NATIVE_LINK_HOST_PATH_MAX];
	int waitedMs = 0;

	if (host->storeLock != NULL || !linkHostPath(host, NATIVE_LINK_STORE_LOCK_FILE, path, sizeof path))
		return 0;

	for (;;)
	{
		host->storeLock = NativeFs_TryLockFile(path);
		if (host->storeLock != NULL)
			return 1;
		if (!blocking || waitedMs >= NATIVE_LINK_STORE_LOCK_WAIT_MS)
			return 0;
		linkHostSleepMs(5);
		waitedMs += 5;
	}
}

static void linkHostUnlock(void *ctx)
{
	NativeLinkHost *host = (NativeLinkHost *)ctx;
	NativeFs_UnlockFile(host->storeLock);
	host->storeLock = NULL;
}

static int linkHostRead(void *ctx, const char *name, char *buf, size_t cap, size_t *len)
{
	NativeLinkHost *host = (NativeLinkHost *)ctx;
	char path[NATIVE_LINK_HOST_PATH_MAX];
	FILE *f;
	size_t n;
	char extra;

	if (!linkHostPath(host, name, path, sizeof path))
		return -1;
	if (!NativeFs_FileExists(path))
		return 0;
	f = NativeFs_OpenRead(path);
	if (f == NULL)
		return NativeFs_FileExists(path) ? -1 : 0;

	n = fread(buf, 1, cap, f);
	if (ferror(f) || (n == cap && fread(&extra, 1, 1, f) == 1))
	{
		fclose(f);
		return -1;
	}
	fclose(f);
	*len = n;
	return 1;
}

static int linkHostWriteNew(void *ctx, const char *name, const char *data, size_t len)
{
	NativeLinkHost *host = (NativeLinkHost *)ctx;
	char path[NATIVE_LINK_HOST_PATH_MAX];
	FILE *f;
	int ok;

	if (!linkHostPath(host, name, path, sizeof path))
		return 0;
	f = NativeFs_CreateExclusive(path);
	if (f == NULL)
		return 0;

	ok = fwrite(data, 1, len, f) == len;
	ok = NativeFs_FlushToDisk(f) && ok;
	ok = (fclose(f) == 0) && ok;
	if (!ok)
		NativeFs_Delete(path);
	return ok;
}

static int linkHostReplace(void *ctx, const char *from, const char *to)
{
	NativeLinkHost *host = (NativeLinkHost *)ctx;
	char fromPath[NATIVE_LINK_HOST_PATH_MAX];
	char toPath[NATIVE_LINK_HOST_PATH_MAX];

	if (!linkHostPath(host, from, fromPath, sizeof fromPath) || !linkHostPath(host, to, toPath, sizeof toPath))
		return 0;
	if (!NativeFs_Replace(fromPath, toPath))
		return 0;
	NativeFs_FlushDirectory(host->dir); // best effort; the rename already happened
	return 1;
}

static int linkHostRemove(void *ctx, const char *name)
{
	NativeLinkHost *host = (NativeLinkHost *)ctx;
	char path[NATIVE_LINK_HOST_PATH_MAX];

	if (!linkHostPath(host, name, path, sizeof path))
		return 0;
	if (!NativeFs_FileExists(path))
		return 1;
	return NativeFs_Delete(path);
}

static int linkHostExists(void *ctx, const char *name)
{
	NativeLinkHost *host = (NativeLinkHost *)ctx;
	char path[NATIVE_LINK_HOST_PATH_MAX];

	return linkHostPath(host, name, path, sizeof path) && NativeFs_FileExists(path);
}

int NativeLinkHost_Init(NativeLinkHost *host, const char *stateDir)
{
	const size_t n = (stateDir != NULL) ? strlen(stateDir) : 0;

	if (host == NULL)
		return 0;
	memset(host, 0, sizeof *host);
	// Room for "/" + the longest file name below.
	if (n == 0 || n + 32 >= sizeof host->dir)
		return 0;
	memcpy(host->dir, stateDir, n + 1);
	return NativeFs_MakeDirectory(host->dir);
}

void NativeLinkHost_Ops(NativeLinkHost *host, NativeLinkFsOps *ops)
{
	ops->ctx = host;
	ops->lock = linkHostLock;
	ops->unlock = linkHostUnlock;
	ops->read = linkHostRead;
	ops->write_new = linkHostWriteNew;
	ops->replace = linkHostReplace;
	ops->remove = linkHostRemove;
	ops->exists = linkHostExists;
}

int NativeLinkHost_TryPrimary(NativeLinkHost *host)
{
	char path[NATIVE_LINK_HOST_PATH_MAX];

	if (host == NULL)
		return 0;
	if (host->primaryLock != NULL)
		return 1;
	if (!linkHostPath(host, NATIVE_LINK_PRIMARY_LOCK_FILE, path, sizeof path))
		return 0;
	host->primaryLock = NativeFs_TryLockFile(path);
	return host->primaryLock != NULL;
}

void NativeLinkHost_ReleasePrimary(NativeLinkHost *host)
{
	if (host == NULL)
		return;
	NativeFs_UnlockFile(host->primaryLock);
	host->primaryLock = NULL;
}

int NativeLinkHost_IsPrimary(const NativeLinkHost *host)
{
	return host != NULL && host->primaryLock != NULL;
}

long long NativeLinkHost_NowUnixMs(void)
{
#if defined(_WIN32)
	FILETIME ft;
	ULARGE_INTEGER t;
	GetSystemTimeAsFileTime(&ft);
	t.LowPart = ft.dwLowDateTime;
	t.HighPart = ft.dwHighDateTime;
	// 100 ns ticks since 1601-01-01; 11644473600 s separate that from 1970.
	return (long long)(t.QuadPart / 10000ULL) - 11644473600000LL;
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (long long)tv.tv_sec * 1000LL + (long long)(tv.tv_usec / 1000);
#endif
}

unsigned long long NativeLinkHost_NewToken(void)
{
	static unsigned long long counter;
	unsigned long long pid;
	unsigned long long x;

#if defined(_WIN32)
	pid = (unsigned long long)GetCurrentProcessId();
#else
	pid = (unsigned long long)getpid();
#endif
	// splitmix64 over time, process id and a per-process counter.
	x = (unsigned long long)NativeLinkHost_NowUnixMs() ^ (pid << 40) ^ (++counter << 20);
	x += 0x9E3779B97F4A7C15ULL;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
	return x ^ (x >> 31);
}
