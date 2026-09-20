// Out-of-engine assertions for the VSync graphics option (community request:
// "vsync as a graphics option"). Exercises the freestanding option -> SDL
// swap-interval mapping in include/platform/native_vsync.h directly -- no
// SDL, no engine.
//
//   cc -Wall -Wextra -I include -o /tmp/test-vsync tools/test-vsync.c && /tmp/test-vsync
//
// Exit 0 = every assertion held; failing cases are printed otherwise.
//
// Covers:
//   * the requested SDL swap interval for Off/On/Adaptive, and that an
//     out-of-range (hand-edited) option value falls back to Off's interval
//   * the fallback interval when a requested interval is rejected by the
//     driver: Adaptive (-1) falls back to On (1); Off/On have no fallback,
//     they are returned unchanged

#include <stdio.h>

#include "platform/native_vsync.h"

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

static void TestRequestedInterval(void)
{
	expect(NativeVsync_RequestedIntervalPure(NATIVE_VSYNC_OFF) == 0, "Off requests swap interval 0");
	expect(NativeVsync_RequestedIntervalPure(NATIVE_VSYNC_ON) == 1, "On requests swap interval 1");
	expect(NativeVsync_RequestedIntervalPure(NATIVE_VSYNC_ADAPTIVE) == -1, "Adaptive requests swap interval -1");
	expect(NativeVsync_RequestedIntervalPure(99) == 0, "an out-of-range hand-edited option requests Off's interval");
	expect(NativeVsync_RequestedIntervalPure(-1) == 0, "a negative out-of-range option requests Off's interval");
}

static void TestFallbackInterval(void)
{
	expect(NativeVsync_FallbackIntervalPure(-1) == 1, "a failed Adaptive request falls back to On");
	expect(NativeVsync_FallbackIntervalPure(1) == 1, "a failed On request has no fallback, stays On");
	expect(NativeVsync_FallbackIntervalPure(0) == 0, "a failed Off request has no fallback, stays Off");
}

int main(void)
{
	TestRequestedInterval();
	TestFallbackInterval();

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures != 0;
}
