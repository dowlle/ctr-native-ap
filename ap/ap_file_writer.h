#ifndef AP_FILE_WRITER_H
#define AP_FILE_WRITER_H

// Background file writer for the AP layer (ap/ap_file_writer.cpp, part of the
// isolated C++ ap_net library so the unity C build stays thread-free).
//
// WHY: field logs show whole-file rewrites on the game thread (ap-state.json
// every 60 frames, the replay-dedup file on every new item) stalling for 0.3 to
// 2 seconds on some Windows hosts. The game thread now builds the content in
// memory and hands a copy to this writer; the open/write/close happens on one
// worker thread and can be as slow as the host makes it without stopping the
// game.
//
// Model: a fixed number of slots, one per file. Each slot holds at most one
// pending job; submitting to a slot that still has a pending job replaces it
// (latest snapshot wins -- an older state file is never worth writing once a
// newer one exists). Jobs of different slots never replace each other. A job
// is a plain C function plus a byte payload copied at submit time, so the job
// never reads live game state from the worker thread.
//
// Submitting never waits for file I/O: it takes a mutex that the worker only
// holds to swap a job in or out. If the worker thread cannot be started, jobs
// run synchronously on the caller (the pre-writer behaviour).

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	AP_FILE_WRITER_SLOT_STATE  = 0, // ap-state.json
	AP_FILE_WRITER_SLOT_FXSEEN = 1, // ctr-ap-fxseen.txt (0.2.1 ledger; #299 migration row removal)
	AP_FILE_WRITER_SLOT_TURBOGRANT = 2, // ctr-ap-turbogrant.txt (same, for the Turbo Grant ledger)
	AP_FILE_WRITER_SLOT__COUNT = 3
};

// A job runs on the writer thread. It must only use its payload (and the file
// system). Returns nonzero on success; the result is kept per slot.
typedef int (*ap_file_writer_job)(const void *payload, size_t len);

// Queue job(copy of payload) on `slot`, replacing a job still pending there.
// Returns 1 when queued, 0 when it ran synchronously (no worker thread) or the
// slot is out of range.
int ap_file_writer_submit(int slot, ap_file_writer_job job, const void *payload, size_t len);

// Wait until every queued job has finished, at most timeout_ms. Returns 1 when
// the writer is idle. Used on clean exit and before reading a file back.
int ap_file_writer_flush(unsigned timeout_ms);

// Result of the last finished job of `slot`: 1 ok, 0 failed. 1 before any job
// ran.
int ap_file_writer_last_ok(int slot);

// Write `len` bytes as the complete new content of `path`, in text mode like
// the fopen("w") it replaces. Writes path + ".tmp" and moves it over `path`
// (MoveFileEx with replace on Windows, rename elsewhere), so a reader never sees
// a half-written file; if the move is refused (a reader holding the file open
// without delete sharing on Windows), falls back to overwriting `path` in
// place. Safe to call from any thread. Returns 1 on success.
int ap_file_writer_replace_file(const char *path, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // AP_FILE_WRITER_H
