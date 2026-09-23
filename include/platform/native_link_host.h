#ifndef NATIVE_LINK_HOST_H
#define NATIVE_LINK_HOST_H

// Real filesystem side of the one-click-connect store (issue #334, slice 3).
//
// Everything lives in one per-install state directory, "ctr-ap-link" under the
// base directory the game runs from, so a Stable and a Testing install on one
// machine never see each other's requests:
//
//   primary.lock   held for its whole life by the running client that owns room
//                  links for this install (the "primary")
//   store.lock     held briefly around every publish and claim
//   pending.req / consuming.req / pending.tmp   see native_link_pending.h
//
// Both locks are NativeFs_TryLockFile locks, which the OS releases when the
// holder exits or crashes, so a dead client never blocks the next one.

#include "platform/native_fs_utf8.h"
#include "platform/native_link_pending.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_LINK_STATE_DIR "ctr-ap-link"
#define NATIVE_LINK_PRIMARY_LOCK_FILE "primary.lock"
#define NATIVE_LINK_STORE_LOCK_FILE "store.lock"

// How long a publisher waits for the store lock before giving up. A claim never
// waits: the running client simply tries again on a later frame.
#define NATIVE_LINK_STORE_LOCK_WAIT_MS 3000

#define NATIVE_LINK_HOST_PATH_MAX 1024

typedef struct
{
	char dir[NATIVE_LINK_HOST_PATH_MAX];
	NativeFsLock *storeLock;
	NativeFsLock *primaryLock;
} NativeLinkHost;

// Point host at stateDir and create the directory when missing. Returns 1 when
// the directory is usable.
int NativeLinkHost_Init(NativeLinkHost *host, const char *stateDir);

// Fill ops with the real filesystem primitives for host.
void NativeLinkHost_Ops(NativeLinkHost *host, NativeLinkFsOps *ops);

// Try to become the primary client for this install. 1 = this process now holds
// the primary lock (until NativeLinkHost_ReleasePrimary or exit); 0 = another
// process holds it (or the lock file cannot be opened).
int NativeLinkHost_TryPrimary(NativeLinkHost *host);
void NativeLinkHost_ReleasePrimary(NativeLinkHost *host);
int NativeLinkHost_IsPrimary(const NativeLinkHost *host);

// Wall-clock milliseconds since the Unix epoch (record creation and expiry).
long long NativeLinkHost_NowUnixMs(void);

// A non-secret token that tells two publishes apart. Not random enough to be a
// secret and never used as one.
unsigned long long NativeLinkHost_NewToken(void);

#ifdef __cplusplus
}
#endif

#endif // NATIVE_LINK_HOST_H
