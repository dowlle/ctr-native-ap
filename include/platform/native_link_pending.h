#ifndef NATIVE_LINK_PENDING_H
#define NATIVE_LINK_PENDING_H

// Pending one-click-connect request store (issue #334, implementation slice 3).
//
// A room-page link starts a second copy of the client. That copy must never
// load the game, write config.ini or open a socket; when a client is already
// running for this install it only publishes the parsed request here and exits.
// The running client (the "primary") claims it and decides what to do with it
// (see native_link_handoff.h).
//
// The file carries the same non-secret fields as the ctr-ap URI plus a schema
// line, a non-secret token and the creation time. It never carries a password:
// the URI grammar has no password key, and a record is rebuilt from a parsed
// NativeLaunchRequest, never copied from the command line.
//
// All filesystem work goes through NativeLinkFsOps so the protocol below can be
// driven by a host harness against a simulated filesystem (crash injection) as
// well as against the real one (native_link_host.c, multi-process tests).
//
// Protocol, every step under the store lock:
//   publish  remove a stale temp file, read the current pending file; the same
//            identity (not expired) is kept as it is, so the oldest creation time
//            survives repeated clicks; anything else is written to the temp file,
//            flushed and atomically moved over the pending file.
//   claim    a consuming file left by a crashed claim is recovered first (and
//            dropped if a newer pending file exists); otherwise the pending file
//            is renamed to the fixed consuming name, read, and that consuming
//            file (which only the lock holder can create) is deleted.
// Diagnostics are the fixed strings of NativeLinkPending_StatusText only.

#include <stddef.h>

#include "platform/native_launch_request.h"

#ifdef __cplusplus
extern "C" {
#endif

// Expiry, checked when a request is claimed, before a prompt is shown and again
// before it is accepted.
#define NATIVE_LINK_PENDING_TTL_MS (15LL * 60LL * 1000LL)
// A creation time this far in the future is a clock that went backwards; the
// request is treated as expired rather than as fresh forever.
#define NATIVE_LINK_PENDING_FUTURE_SKEW_MS (5LL * 1000LL)

#define NATIVE_LINK_PENDING_FILE "pending.req"
#define NATIVE_LINK_CONSUMING_FILE "consuming.req"
#define NATIVE_LINK_TEMP_FILE "pending.tmp"

// Record text is bounded: schema + token + created + one encoded URI.
#define NATIVE_LINK_RECORD_MAX 3072

typedef struct
{
	NativeLaunchRequest request;
	unsigned long long token;  // non-secret, distinguishes two publishes
	long long createdUnixMs;   // publisher wall clock
} NativeLinkRecord;

typedef enum
{
	NATIVE_LINK_PENDING_OK = 0,
	NATIVE_LINK_PENDING_PUBLISHED,   // written (no pending file before)
	NATIVE_LINK_PENDING_REPLACED,    // a different or expired request was replaced
	NATIVE_LINK_PENDING_COALESCED,   // the same identity was already pending; kept
	NATIVE_LINK_PENDING_CLAIMED,     // a request was claimed into *out
	NATIVE_LINK_PENDING_NONE,        // nothing pending
	NATIVE_LINK_PENDING_BUSY,        // the lock is held elsewhere; try again later
	NATIVE_LINK_PENDING_EXPIRED,     // claimed but too old (or from the future); dropped
	NATIVE_LINK_PENDING_INVALID,     // an unreadable or tampered file; dropped
	NATIVE_LINK_PENDING_ERR_ARG,
	NATIVE_LINK_PENDING_ERR_LOCK,    // the lock could not be taken in time
	NATIVE_LINK_PENDING_ERR_IO       // a write, flush, rename or delete failed
} NativeLinkPendingStatus;

// Filesystem primitives. Names are the fixed file names above, relative to the
// per-install state directory the ops were built for. Each returns 1 on success
// and 0 on failure unless noted.
typedef struct
{
	void *ctx;
	// Take the store lock. blocking != 0 waits a bounded time; 0 tries once.
	int (*lock)(void *ctx, int blocking);
	void (*unlock)(void *ctx);
	// 1 read (length in *len), 0 missing, -1 error or larger than cap.
	int (*read)(void *ctx, const char *name, char *buf, size_t cap, size_t *len);
	// Create name exclusively, write data, flush it to stable storage.
	int (*write_new)(void *ctx, const char *name, const char *data, size_t len);
	// Atomically move from over to (replacing to).
	int (*replace)(void *ctx, const char *from, const char *to);
	// Delete name. A missing file counts as success.
	int (*remove)(void *ctx, const char *name);
	// 1 if name exists. Used only as a cheap pre-check before locking.
	int (*exists)(void *ctx, const char *name);
} NativeLinkFsOps;

// Identity of a request: (host, port, slot, room). Host compares ASCII
// case-insensitively (DNS names are case-insensitive); slot and room exactly.
// An empty room on either side means "room not known" (a session dialed from
// the saved connection rather than from a link): the room is then not compared,
// because only one room can listen on a host and port at a time.
int NativeLinkRequest_SameIdentity(const NativeLaunchRequest *a, const NativeLaunchRequest *b);

// Encode a request as the canonical ctr-ap URI (every byte outside the
// unreserved set percent-encoded). Returns 1 on success. The result parses back
// to the same request with NativeLaunchRequest_Parse.
int NativeLinkRequest_Encode(const NativeLaunchRequest *request, char *out, size_t cap);

// Record text. Format returns the length written (0 on failure); Parse
// validates the schema and every field through NativeLaunchRequest_Parse and
// leaves *out untouched on failure.
size_t NativeLinkRecord_Format(const NativeLinkRecord *record, char *out, size_t cap);
int NativeLinkRecord_Parse(const char *text, size_t len, NativeLinkRecord *out);

// 1 when a record created at createdUnixMs is expired at nowUnixMs.
int NativeLinkRecord_Expired(long long createdUnixMs, long long nowUnixMs);

NativeLinkPendingStatus NativeLinkPending_Publish(const NativeLinkFsOps *ops, const NativeLinkRecord *record,
                                                  long long nowUnixMs);
NativeLinkPendingStatus NativeLinkPending_Claim(const NativeLinkFsOps *ops, NativeLinkRecord *out,
                                                long long nowUnixMs);

// Fixed diagnostic for a status. Never contains a request field.
const char *NativeLinkPending_StatusText(NativeLinkPendingStatus status);

#ifdef __cplusplus
}
#endif

#endif // NATIVE_LINK_PENDING_H
