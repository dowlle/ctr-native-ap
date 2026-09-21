#ifndef NATIVE_DISC_PICKER_H
#define NATIVE_DISC_PICKER_H

#include <stddef.h>
#include <stdlib.h>

#include "platform/native_disc_limits.h"

// Freestanding wait/abandon state for the first-run disc picker (issue #334,
// slice 2). SDL documents that the file-dialog callback is invoked once, for a
// completed selection, a cancellation or an error, but it gives no maximum
// completion time and no cancellation. A backend that never answers must not
// hang startup before the game window exists, so the wait is bounded and the
// invocation is abandoned on timeout.
//
// Ownership contract (the reason this lives in a header the host harness can
// drive with a fake backend, no SDL required):
//
//   * Each invocation owns a heap state. No state is shared between
//     invocations.
//   * The waiter releases the state only when the callback completed in time.
//   * On timeout the waiter marks the state abandoned and never releases it.
//     The late callback finds it abandoned and releases it instead. A callback
//     that never arrives leaves the state leaked on purpose: one small state
//     per timed-out pick is preferable to freeing memory a callback could
//     still write into, and the process is about to exit the no-disc path
//     anyway.
//   * The flag pair is always read and written under the caller's lock, because
//     SDL may invoke the callback on another thread.

// Bounded wait before a pick is abandoned: 120 seconds of wall clock.
#define NATIVE_DISC_PICK_TIMEOUT_MS 120000u

typedef struct NativeDiscPickerState
{
	int done;      // callback completed
	int abandoned; // waiter timed out and will not touch the state again
	int cancelled; // completed without a chosen file
	int failed;    // the backend could not start (no dialog available)
	char path[NATIVE_DISC_PATH_MAX];
} NativeDiscPickerState;

// Injected operations. Production wires SDL; the host harness wires a fake
// backend. lock/unlock guard the flag pair and may be no-ops on a single
// threaded backend.
typedef struct NativeDiscPickerOps
{
	// Hand the dialog to the backend. The backend eventually calls
	// NativeDiscPicker_Complete, possibly from another thread. Returns 0 when
	// no dialog could be started at all (the caller then completes immediately
	// as a failure).
	int (*start)(void *ctx, NativeDiscPickerState *state);
	// Process pending events so the dialog can make progress.
	void (*pump)(void *ctx);
	// Monotonic milliseconds.
	unsigned long long (*nowMs)(void *ctx);
	// Sleep for the given number of milliseconds.
	void (*sleep)(void *ctx, unsigned int ms);
	// Release a state this side owns (free() in production).
	void (*release)(void *ctx, NativeDiscPickerState *state);
	void (*lock)(void *ctx);
	void (*unlock)(void *ctx);
} NativeDiscPickerOps;

// Called by the backend exactly once per started dialog. path is NULL (or an
// empty string) for a cancellation or an error. Releases the state when the
// waiter has already abandoned it; otherwise it is the waiter's to release.
static inline void NativeDiscPicker_Complete(void *ctx, const NativeDiscPickerOps *ops, NativeDiscPickerState *state, const char *path)
{
	int abandoned;

	if ((state == NULL) || (ops == NULL))
		return;

	ops->lock(ctx);
	abandoned = state->abandoned;
	if (!abandoned)
	{
		if ((path == NULL) || (path[0] == '\0'))
			state->cancelled = 1;
		else
		{
			size_t i = 0;
			while ((path[i] != '\0') && (i + 1u < sizeof(state->path)))
			{
				state->path[i] = path[i];
				i++;
			}
			state->path[i] = '\0';
		}
		state->done = 1;
	}
	ops->unlock(ctx);

	if (abandoned)
		ops->release(ctx, state);
}

// Run one pick to completion or the bounded timeout. Returns 1 and fills outPath
// when the user chose a file, 0 on cancellation, backend failure or timeout.
// On a timeout the state is abandoned and never released by this function.
static inline int NativeDiscPicker_Wait(void *ctx, const NativeDiscPickerOps *ops, NativeDiscPickerState *state, unsigned int timeoutMs, char *outPath,
                                        size_t outPathSize)
{
	unsigned long long deadline;
	int answered = 0;
	int completed;

	if ((ops == NULL) || (state == NULL))
		return 0;

	if (outPath != NULL)
		outPath[0] = '\0';

	if (!ops->start(ctx, state))
	{
		// No backend: complete as a failure so the same ownership rules apply.
		NativeDiscPicker_Complete(ctx, ops, state, NULL);
		state->failed = 1;
		ops->release(ctx, state);
		return 0;
	}

	deadline = ops->nowMs(ctx) + (unsigned long long)timeoutMs;

	ops->lock(ctx);
	completed = state->done;
	ops->unlock(ctx);

	while (!completed)
	{
		if (ops->nowMs(ctx) >= deadline)
			break;

		ops->pump(ctx);
		ops->sleep(ctx, 10);

		ops->lock(ctx);
		completed = state->done;
		ops->unlock(ctx);
	}

	ops->lock(ctx);
	if (state->done)
	{
		// Callback completed in time: this side owns the state.
		answered = !state->cancelled;
		if (answered && (outPath != NULL) && (outPathSize > 0))
		{
			size_t i = 0;
			while ((state->path[i] != '\0') && (i + 1u < outPathSize))
			{
				outPath[i] = state->path[i];
				i++;
			}
			outPath[i] = '\0';
		}
		ops->unlock(ctx);
		ops->release(ctx, state);
	}
	else
	{
		// Timed out: abandon. A late callback owns the state from here.
		state->abandoned = 1;
		ops->unlock(ctx);
	}

	return answered;
}

#endif // NATIVE_DISC_PICKER_H
