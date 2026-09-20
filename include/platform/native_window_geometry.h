#ifndef NATIVE_WINDOW_GEOMETRY_H
#define NATIVE_WINDOW_GEOMETRY_H

// Freestanding decision logic for remembering the host window's position and
// size across sessions (the streaming-setup request: launch at the same size
// and place every time). Pulled out of platform/native_renderer.c so a host
// harness can pin the validation/decision rules without linking SDL, exactly
// the way ap_hit_policy.h splits decision from gather.
//
// NOTHING here touches SDL: the caller (NativeRenderer_InitialiseGLContext /
// NativeRenderer_CaptureWindowGeometry) supplies every fact and applies every
// decision. Both sides agree on the same header, so the harness and the game
// cannot drift.
//
// Persisted fields (platform/native_config.h, [State] section, config-file-only
// exactly like update_last_seen -- remembered state, not a user option):
//   window_x, window_y       last known windowed-mode top-left. UNSET
//                            (NATIVE_WINDOW_GEOMETRY_POS_UNSET) means "never
//                            saved"; 0,0 is a legitimate real position, so the
//                            unset marker has to live well outside any real
//                            display coordinate range instead.
//   window_w, window_h       last known windowed-mode client size. 0 means
//                            "never saved" (a real size is never zero).
//   window_maximized         whether that window was maximized, remembered
//                            separately so a maximized session does not
//                            overwrite the windowed rect underneath it.

#ifdef __cplusplus
extern "C" {
#endif

// Real display coordinates -- even spanning several monitors, even with one
// placed left of or above the origin -- stay far inside this range, so it is
// safe as the "no saved position" marker for window_x/window_y.
#define NATIVE_WINDOW_GEOMETRY_POS_UNSET (-1000000)

// Sane bounds for a saved/restored window client size: at least a usable
// minimum, at most a generous ceiling (comfortably past any real display),
// so a corrupted or absurd hand-edit cannot be applied.
#define NATIVE_WINDOW_GEOMETRY_MIN_SIZE 200
#define NATIVE_WINDOW_GEOMETRY_MAX_SIZE 16384

typedef struct
{
	int x, y, w, h;
} NativeWindowRect;

// The startup decision: what to hand SDL_CreateWindow / SDL_SetWindowPosition.
// usePosition is 0 when the saved position should not be applied (never
// saved, or its display is gone) -- the caller then leaves window placement
// to SDL's own default, matching today's behaviour exactly.
typedef struct
{
	int w, h;
	int x, y;
	int usePosition;
} NativeWindowGeometryPlan;

// What NativeRenderer_CaptureWindowGeometry should do with the window's
// current SDL state, decided on every save point (see native_config.c
// NativeConfig_Save call sites) and on a clean exit.
enum
{
	// Fullscreen or minimized: neither the rect nor the maximized flag is
	// this session's honest "windowed" state, so touch nothing and keep
	// whatever was last remembered.
	NATIVE_WINDOW_GEOMETRY_CAPTURE_NONE = 0,
	// Plain windowed: persist the live rect and clear the maximized flag.
	NATIVE_WINDOW_GEOMETRY_CAPTURE_RECT,
	// Maximized: keep the previously remembered windowed rect (it is still
	// underneath the maximize), but do remember that it was maximized.
	NATIVE_WINDOW_GEOMETRY_CAPTURE_MAXIMIZED_ONLY
};

// True when w/h look like a real, storable windowed client size.
static inline int NativeWindowGeometry_SizeValidPure(int w, int h)
{
	return w >= NATIVE_WINDOW_GEOMETRY_MIN_SIZE && w <= NATIVE_WINDOW_GEOMETRY_MAX_SIZE &&
	       h >= NATIVE_WINDOW_GEOMETRY_MIN_SIZE && h <= NATIVE_WINDOW_GEOMETRY_MAX_SIZE;
}

static inline int NativeWindowGeometry_PositionSavedPure(int x, int y)
{
	return x != NATIVE_WINDOW_GEOMETRY_POS_UNSET && y != NATIVE_WINDOW_GEOMETRY_POS_UNSET;
}

// True if the rect at x,y,w,h overlaps at least one of the given display
// rects by at least one pixel. An empty display list (query failure) always
// fails closed, the same as a disconnected monitor.
static inline int NativeWindowGeometry_PositionOnAnyDisplayPure(int x, int y, int w, int h,
                                                                 const NativeWindowRect *displays,
                                                                 int displayCount)
{
	int i;

	if (w <= 0 || h <= 0)
		return 0;

	for (i = 0; i < displayCount; i++)
	{
		const NativeWindowRect *d = &displays[i];
		int overlapsX = x < d->x + d->w && x + w > d->x;
		int overlapsY = y < d->y + d->h && y + h > d->y;

		if (overlapsX && overlapsY)
			return 1;
	}
	return 0;
}

// Startup plan: given the persisted geometry and the currently connected
// displays, decide the size and position to apply. defaultW/defaultH are the
// size the caller would have used with nothing saved (today's behaviour), and
// are exactly what a fresh install / cleared config.ini keeps producing.
//
//   * Invalid or never-saved size -> keep defaultW/defaultH.
//   * Valid size -> use it, regardless of position.
//   * Never-saved position, or a position whose display is gone -> leave
//     placement to SDL's default (usePosition = 0), but still apply a valid
//     saved size.
//   * Valid position that lands on a live display -> apply it.
static inline NativeWindowGeometryPlan NativeWindowGeometry_PlanStartupPure(
	int savedX, int savedY, int savedW, int savedH,
	int defaultW, int defaultH,
	const NativeWindowRect *displays, int displayCount)
{
	NativeWindowGeometryPlan plan;

	plan.w = defaultW;
	plan.h = defaultH;
	plan.x = 0;
	plan.y = 0;
	plan.usePosition = 0;

	if (NativeWindowGeometry_SizeValidPure(savedW, savedH))
	{
		plan.w = savedW;
		plan.h = savedH;
	}

	if (NativeWindowGeometry_PositionSavedPure(savedX, savedY) &&
	    NativeWindowGeometry_PositionOnAnyDisplayPure(savedX, savedY, plan.w, plan.h, displays, displayCount))
	{
		plan.x = savedX;
		plan.y = savedY;
		plan.usePosition = 1;
	}

	return plan;
}

// Save-point decision: given the window's current SDL_WINDOW_* state, what
// NativeRenderer_CaptureWindowGeometry should do with the persisted geometry.
static inline int NativeWindowGeometry_CaptureDecisionPure(int isFullscreen, int isMaximized, int isMinimized)
{
	if (isFullscreen || isMinimized)
		return NATIVE_WINDOW_GEOMETRY_CAPTURE_NONE;
	if (isMaximized)
		return NATIVE_WINDOW_GEOMETRY_CAPTURE_MAXIMIZED_ONLY;
	return NATIVE_WINDOW_GEOMETRY_CAPTURE_RECT;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NATIVE_WINDOW_GEOMETRY_H
