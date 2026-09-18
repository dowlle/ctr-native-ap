// Out-of-engine assertions for remembering the host window's position and
// size between sessions (the streaming-setup request: launch at the same
// window every time). Exercises the freestanding decision logic in
// include/platform/native_window_geometry.h directly -- no SDL, no engine.
//
//   cc -Wall -Wextra -I include -o /tmp/test-window-geometry tools/test-window-geometry.c && /tmp/test-window-geometry
//
// Exit 0 = every assertion held; failing cases are printed otherwise.
//
// Covers:
//   * size validity bounds (too small, too large, zero/negative, in range)
//   * position-on-display overlap, including multi-monitor and a negative
//     (left-of-origin) display
//   * the startup plan: nothing saved keeps the caller's defaults byte-for-
//     byte; a valid saved rect on a live display is applied; a saved size
//     survives even when the saved position's display has been disconnected;
//     an invalid saved size falls back to the default size
//   * the save-point capture decision for every SDL_WINDOW_* combination that
//     matters: plain windowed, maximized, fullscreen, minimized

#include <stdio.h>

#include "platform/native_window_geometry.h"

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

static void TestSizeValid(void)
{
	expect(NativeWindowGeometry_SizeValidPure(1280, 720), "1280x720 is valid");
	expect(NativeWindowGeometry_SizeValidPure(NATIVE_WINDOW_GEOMETRY_MIN_SIZE, NATIVE_WINDOW_GEOMETRY_MIN_SIZE),
	       "minimum size is valid");
	expect(NativeWindowGeometry_SizeValidPure(NATIVE_WINDOW_GEOMETRY_MAX_SIZE, NATIVE_WINDOW_GEOMETRY_MAX_SIZE),
	       "maximum size is valid");
	expect(!NativeWindowGeometry_SizeValidPure(NATIVE_WINDOW_GEOMETRY_MIN_SIZE - 1, 720), "just-under-minimum width rejected");
	expect(!NativeWindowGeometry_SizeValidPure(1280, NATIVE_WINDOW_GEOMETRY_MIN_SIZE - 1), "just-under-minimum height rejected");
	expect(!NativeWindowGeometry_SizeValidPure(NATIVE_WINDOW_GEOMETRY_MAX_SIZE + 1, 720), "just-over-maximum width rejected");
	expect(!NativeWindowGeometry_SizeValidPure(0, 0), "zero size (never-saved sentinel) rejected");
	expect(!NativeWindowGeometry_SizeValidPure(-1, 720), "negative width rejected");
	expect(!NativeWindowGeometry_SizeValidPure(1280, -1), "negative height rejected");
}

static void TestPositionSaved(void)
{
	expect(!NativeWindowGeometry_PositionSavedPure(NATIVE_WINDOW_GEOMETRY_POS_UNSET, 100), "unset x is not saved");
	expect(!NativeWindowGeometry_PositionSavedPure(100, NATIVE_WINDOW_GEOMETRY_POS_UNSET), "unset y is not saved");
	expect(NativeWindowGeometry_PositionSavedPure(0, 0), "0,0 is a real saved position, not unset");
	expect(NativeWindowGeometry_PositionSavedPure(-1920, 0), "a negative left-of-origin position is a real saved position");
}

static void TestPositionOnDisplay(void)
{
	// A single 1920x1080 primary display at the origin.
	NativeWindowRect single[1] = {{0, 0, 1920, 1080}};
	expect(NativeWindowGeometry_PositionOnAnyDisplayPure(100, 100, 1280, 720, single, 1),
	       "fully inside the only display");
	expect(NativeWindowGeometry_PositionOnAnyDisplayPure(-50, -50, 1280, 720, single, 1),
	       "partially off the top-left edge still overlaps");
	expect(NativeWindowGeometry_PositionOnAnyDisplayPure(1900, 1060, 1280, 720, single, 1),
	       "partially off the bottom-right edge still overlaps");
	expect(!NativeWindowGeometry_PositionOnAnyDisplayPure(5000, 5000, 1280, 720, single, 1),
	       "far outside every display does not overlap");
	expect(!NativeWindowGeometry_PositionOnAnyDisplayPure(100, 100, 1280, 720, single, 0),
	       "an empty display list (query failure) fails closed");

	// Two monitors, the second placed to the LEFT of the origin (negative x),
	// the kind of layout that rules out a naive "position >= 0" validity check.
	NativeWindowRect dual[2] = {{0, 0, 1920, 1080}, {-1920, 0, 1920, 1080}};
	expect(NativeWindowGeometry_PositionOnAnyDisplayPure(-1000, 100, 800, 600, dual, 2),
	       "lands on the negative-x secondary display");
	expect(NativeWindowGeometry_PositionOnAnyDisplayPure(100, 100, 800, 600, dual, 2),
	       "lands on the primary display in a multi-monitor list");
	expect(!NativeWindowGeometry_PositionOnAnyDisplayPure(-4000, 100, 800, 600, dual, 2),
	       "past both displays does not overlap");

	// Degenerate size never overlaps, even inside a display rect.
	expect(!NativeWindowGeometry_PositionOnAnyDisplayPure(100, 100, 0, 600, single, 1), "zero width never overlaps");
	expect(!NativeWindowGeometry_PositionOnAnyDisplayPure(100, 100, 800, 0, single, 1), "zero height never overlaps");
}

static void TestStartupPlan(void)
{
	NativeWindowRect single[1] = {{0, 0, 1920, 1080}};

	// Nothing ever saved (a fresh config.ini, or one predating this feature):
	// the plan must be byte-for-byte the caller's own defaults, with placement
	// left to SDL.
	{
		NativeWindowGeometryPlan plan = NativeWindowGeometry_PlanStartupPure(
			NATIVE_WINDOW_GEOMETRY_POS_UNSET, NATIVE_WINDOW_GEOMETRY_POS_UNSET, 0, 0,
			1024, 768, single, 1);
		expect(plan.w == 1024 && plan.h == 768, "no saved geometry keeps the default size");
		expect(plan.usePosition == 0, "no saved geometry leaves placement to SDL");
	}

	// A fully valid saved rect on a live display is applied verbatim.
	{
		NativeWindowGeometryPlan plan = NativeWindowGeometry_PlanStartupPure(
			200, 150, 1600, 900, 1024, 768, single, 1);
		expect(plan.w == 1600 && plan.h == 900, "valid saved size is applied");
		expect(plan.usePosition == 1 && plan.x == 200 && plan.y == 150, "valid saved position is applied");
	}

	// The saved monitor is gone: fall back to default placement, but the saved
	// size still applies because it is independently valid.
	{
		NativeWindowRect noDisplays[1] = {{0, 0, 0, 0}};
		NativeWindowGeometryPlan plan = NativeWindowGeometry_PlanStartupPure(
			5000, 5000, 1600, 900, 1024, 768, noDisplays, 0);
		expect(plan.w == 1600 && plan.h == 900, "saved size survives a disconnected saved monitor");
		expect(plan.usePosition == 0, "saved position is dropped when its monitor is gone");
	}

	// A corrupted/hand-edited size (e.g. 0 from an old config, or an absurd
	// value) falls back to the caller's default size; a valid position is
	// still applied against the DEFAULT size, not the rejected saved one.
	{
		NativeWindowGeometryPlan plan = NativeWindowGeometry_PlanStartupPure(
			100, 100, 0, 0, 1024, 768, single, 1);
		expect(plan.w == 1024 && plan.h == 768, "invalid saved size falls back to the default size");
		expect(plan.usePosition == 1, "a valid saved position still applies alongside a fallback size");
	}
	{
		NativeWindowGeometryPlan plan = NativeWindowGeometry_PlanStartupPure(
			100, 100, 999999, 999999, 1024, 768, single, 1);
		expect(plan.w == 1024 && plan.h == 768, "absurdly large saved size falls back to the default size");
	}
}

static void TestCaptureDecision(void)
{
	expect(NativeWindowGeometry_CaptureDecisionPure(0, 0, 0) == NATIVE_WINDOW_GEOMETRY_CAPTURE_RECT,
	       "plain windowed captures the rect");
	expect(NativeWindowGeometry_CaptureDecisionPure(1, 0, 0) == NATIVE_WINDOW_GEOMETRY_CAPTURE_NONE,
	       "fullscreen leaves the remembered rect untouched");
	expect(NativeWindowGeometry_CaptureDecisionPure(0, 1, 0) == NATIVE_WINDOW_GEOMETRY_CAPTURE_MAXIMIZED_ONLY,
	       "maximized keeps the rect, remembers only the flag");
	expect(NativeWindowGeometry_CaptureDecisionPure(0, 0, 1) == NATIVE_WINDOW_GEOMETRY_CAPTURE_NONE,
	       "minimized leaves the remembered rect untouched");
	// Fullscreen wins over a (nonsensical, but SDL-reportable) maximized bit.
	expect(NativeWindowGeometry_CaptureDecisionPure(1, 1, 0) == NATIVE_WINDOW_GEOMETRY_CAPTURE_NONE,
	       "fullscreen+maximized is still treated as fullscreen");
}

int main(void)
{
	TestSizeValid();
	TestPositionSaved();
	TestPositionOnDisplay();
	TestStartupPlan();
	TestCaptureDecision();

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures != 0;
}
