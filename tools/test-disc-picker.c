// Behavioral harness for the bounded disc-picker wait (issue #334, slice 2).
// It drives the freestanding wait/abandon state machine in
// include/platform/native_disc_picker.h with a fake dialog backend and a fake
// clock, so all three callback orders are pinned without SDL:
//
//   * callback before the timeout: the waiter owns and releases the state and
//     returns the chosen path;
//   * callback after the timeout: the waiter abandons the state and returns 0
//     without releasing it, and the late callback releases it exactly once;
//   * callback never: the waiter abandons the state and returns 0, and nothing
//     is released by the waiter (the one orphaned state is intentional).
//
// The harness also counts releases to prove no state is freed twice and no
// caller-visible state is touched by a late callback.
//
//   cc -Wall -Wextra -I . -I include -o /tmp/test-disc-picker tools/test-disc-picker.c
//
// Exit 0 = every assertion held; failures are printed otherwise.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/native_disc_picker.h"

static int g_checks;
static int g_failures;

static void expect(int condition, const char *name)
{
	g_checks++;
	if (!condition)
	{
		g_failures++;
		printf("FAIL: %s\n", name);
	}
}

// ── fake backend ────────────────────────────────────────────────────────────

enum FakeOrder
{
	ORDER_BEFORE,
	ORDER_AFTER,
	ORDER_NEVER
};

struct FakeCtx
{
	enum FakeOrder order;
	unsigned long long now;
	NativeDiscPickerState *started; // the state handed to start
	int releases;
	unsigned long long releaseTick;
	const char *completionPath;   // path the callback will report
	const char *lateCompletionPath;
};

static int fakeStart(void *ctx, NativeDiscPickerState *state)
{
	struct FakeCtx *c = (struct FakeCtx *)ctx;

	c->started = state;
	return 1;
}

static const NativeDiscPickerOps g_fakeOps;

static void fakePump(void *ctx)
{
	struct FakeCtx *c = (struct FakeCtx *)ctx;

	// Drive the fake clock forward so the bounded wait reaches its deadline.
	// A completion scheduled for "before" fires on the second tick, which is
	// well inside the 120 s budget; "after" fires far past it; "never" never
	// fires.
	c->now += 100;

	if ((c->order == ORDER_BEFORE) && (c->now >= 200))
	{
		NativeDiscPicker_Complete(c, &g_fakeOps, c->started, c->completionPath);
	}
	else if ((c->order == ORDER_AFTER) && (c->now >= 120200))
	{
		NativeDiscPicker_Complete(c, &g_fakeOps, c->started, c->lateCompletionPath);
	}
}

static unsigned long long fakeNow(void *ctx)
{
	return ((struct FakeCtx *)ctx)->now;
}

static void fakeSleep(void *ctx, unsigned int ms)
{
	(void)ctx;
	(void)ms;
}

static void fakeRelease(void *ctx, NativeDiscPickerState *state)
{
	struct FakeCtx *c = (struct FakeCtx *)ctx;

	c->releases++;
	c->releaseTick = c->now;
	free(state);
}

static void fakeLock(void *ctx)
{
	(void)ctx;
}

static void fakeUnlock(void *ctx)
{
	(void)ctx;
}

static const NativeDiscPickerOps g_fakeOps = {
    fakeStart,
    fakePump,
    fakeNow,
    fakeSleep,
    fakeRelease,
    fakeLock,
    fakeUnlock,
};

// ── tests ───────────────────────────────────────────────────────────────────

static void TestCallbackBeforeTimeout(void)
{
	struct FakeCtx ctx;
	NativeDiscPickerState *state = (NativeDiscPickerState *)calloc(1, sizeof(*state));
	char out[NATIVE_DISC_PATH_MAX];
	int answered;

	memset(&ctx, 0, sizeof(ctx));
	ctx.order = ORDER_BEFORE;
	ctx.completionPath = "/discs/chosen.bin";
	ctx.lateCompletionPath = "/discs/late.bin";

	answered = NativeDiscPicker_Wait(&ctx, &g_fakeOps, state, NATIVE_DISC_PICK_TIMEOUT_MS, out, sizeof(out));

	expect(answered == 1, "before: a completing callback yields an answer");
	expect(strcmp(out, "/discs/chosen.bin") == 0, "before: the chosen path is returned");
	expect(ctx.releases == 1, "before: the waiter releases the state exactly once");
	expect(state->abandoned == 0, "before: the state was not abandoned");
}

static void TestCallbackAfterTimeout(void)
{
	struct FakeCtx ctx;
	NativeDiscPickerState *state = (NativeDiscPickerState *)calloc(1, sizeof(*state));
	char out[NATIVE_DISC_PATH_MAX];
	int answered;

	memset(&ctx, 0, sizeof(ctx));
	ctx.order = ORDER_AFTER;
	ctx.completionPath = "/discs/early.bin";
	ctx.lateCompletionPath = "/discs/late.bin";

	answered = NativeDiscPicker_Wait(&ctx, &g_fakeOps, state, NATIVE_DISC_PICK_TIMEOUT_MS, out, sizeof(out));

	expect(answered == 0, "after: a late callback yields no answer");
	expect(out[0] == '\0', "after: no path is returned");
	expect(ctx.releases == 0, "after: the waiter does not release an abandoned state");
	expect(state->abandoned == 1, "after: the state is marked abandoned");

	// The late callback fires now (the fake pump advances the clock past the
	// new threshold). It releases the abandoned state itself, exactly once, and
	// never writes back into the caller's result.
	{
		int i;
		for (i = 0; (i < 5) && (ctx.releases == 0); i++)
			fakePump(&ctx);
	}
	expect(ctx.releases == 1, "after: the late callback releases the orphaned state exactly once");
	expect(ctx.releaseTick >= 120000, "after: the release happens after the timeout");
}

static void TestCallbackNever(void)
{
	struct FakeCtx ctx;
	NativeDiscPickerState *state = (NativeDiscPickerState *)calloc(1, sizeof(*state));
	char out[NATIVE_DISC_PATH_MAX];
	int answered;

	memset(&ctx, 0, sizeof(ctx));
	ctx.order = ORDER_NEVER;
	ctx.completionPath = NULL;
	ctx.lateCompletionPath = NULL;

	answered = NativeDiscPicker_Wait(&ctx, &g_fakeOps, state, NATIVE_DISC_PICK_TIMEOUT_MS, out, sizeof(out));

	expect(answered == 0, "never: a missing callback yields no answer");
	expect(state->abandoned == 1, "never: the state is marked abandoned");
	expect(ctx.releases == 0, "never: nothing is released by the waiter");

	// A never-arriving callback leaves the orphaned state allocated on purpose.
	// The harness owns it here and releases it once for cleanliness.
	free(state);
}

static int failingStart(void *ctx, NativeDiscPickerState *state)
{
	(void)ctx;
	(void)state;
	return 0;
}

static void TestNoBackendCompletes(void)
{
	struct FakeCtx ctx;
	NativeDiscPickerState *state = (NativeDiscPickerState *)calloc(1, sizeof(*state));
	NativeDiscPickerOps ops = {
	    failingStart,
	    fakePump,
	    fakeNow,
	    fakeSleep,
	    fakeRelease,
	    fakeLock,
	    fakeUnlock,
	};
	char out[NATIVE_DISC_PATH_MAX];
	int answered;

	memset(&ctx, 0, sizeof(ctx));
	ctx.order = ORDER_NEVER;

	// A start failure completes immediately as a failure and is released like
	// any other completed callback, so no state is left behind.
	answered = NativeDiscPicker_Wait(&ctx, &ops, state, NATIVE_DISC_PICK_TIMEOUT_MS, out, sizeof(out));

	expect(answered == 0, "nobackend: a failed start yields no answer");
	expect(ctx.releases == 1, "nobackend: the state is released exactly once");
	expect(state->abandoned == 0, "nobackend: a failed start is not abandoned");
}

int main(void)
{
	TestCallbackBeforeTimeout();
	TestCallbackAfterTimeout();
	TestCallbackNever();
	TestNoBackendCompletes();

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
