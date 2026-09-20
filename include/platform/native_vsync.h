#ifndef NATIVE_VSYNC_H
#define NATIVE_VSYNC_H

// Freestanding decision logic for the VSync graphics option (community
// request: "vsync as a graphics option"). Pulled out of
// platform/native_renderer.c so a host harness can pin the option -> SDL
// swap-interval mapping without linking SDL, exactly the way
// native_window_geometry.h splits decision from gather.
//
// NOTHING here touches SDL: the caller (NativeRenderer_UpdateSwapIntervalState)
// supplies the option value and, when a requested interval fails, the fact
// that it failed. Both sides agree on the same header, so the harness and the
// game cannot drift.
//
// Persisted field (platform/native_config.h, [Video & QoL] section,
// g_config.vsync, CFG_ENUM):
//   vsync   0 = Off (default), 1 = On, 2 = Adaptive. See
//           NATIVE_VSYNC_OFF/ON/ADAPTIVE below.
//
// Off is the default because CTR already throttles through the retail
// VSync/draw-sync path (Native_WaitUntilVBlankTarget); a second SDL swap wait
// on top of that is double throttling, and some GL drivers charge that wait
// to the next frame's first clear instead of SDL_GL_SwapWindow, which some
// profiling surfaces would misattribute. See the NOTE above
// NativeRenderer_UpdateSwapIntervalState's call site in
// platform/native_platform.c Platform_BeginScene.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	NATIVE_VSYNC_OFF = 0,
	NATIVE_VSYNC_ON = 1,
	NATIVE_VSYNC_ADAPTIVE = 2
} NativeVsyncOption;

// The SDL_GL_SetSwapInterval() argument to request for a given option, before
// checking whether the driver actually honors it (adaptive is speculative
// until SDL reports success). An out-of-range option (a hand-edited
// config.ini) renders and behaves as Off, the safe default.
static inline int NativeVsync_RequestedIntervalPure(int option)
{
	if (option == NATIVE_VSYNC_ON)
		return 1;
	if (option == NATIVE_VSYNC_ADAPTIVE)
		return -1;
	return 0;
}

// What to request next when SDL_GL_SetSwapInterval(requestedInterval) just
// returned false. Only adaptive (-1) has a fallback -- plain On (1) -- since
// every driver that runs at all is expected to honor 0 or 1.
static inline int NativeVsync_FallbackIntervalPure(int requestedInterval)
{
	if (requestedInterval == -1)
		return 1;
	return requestedInterval;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NATIVE_VSYNC_H
