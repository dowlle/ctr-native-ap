#ifndef AP_PERF_H
#define AP_PERF_H
#ifdef CTR_AP

// Archipelago frame-stall watchdog for CTR Native.
//
// Compiled ONLY when CTR_AP is defined (CMake: -DCTR_AP=ON), same as the rest of
// the AP layer, so the vanilla decomp build is untouched. Mirrors the ap_verify.c
// / ap_traps.c convention: this header is only ever seen inside a CTR_AP build
// (game/game_unity.h #includes ap_perf.c inside its #ifdef CTR_AP block), so the
// whole file lives under the guard and needs no non-CTR_AP no-op stubs.
//
// WHY: players report intermittent stutters and occasional multi-second hangs in
// online multiworld sessions, but release builds ship no telemetry (the engine's
// profiler in platform/native_perf.c is CTR_INTERNAL-only). The AP layer runs
// fully synchronous on the single game thread -- AP_OnFrame (ap_hooks.c) is
// called every frame, and apclientpp's poll() fires all network handlers inline
// during ap_net_poll(). This watchdog is an always-on, allocation-free, I/O-free
// (bar the log lines) probe that writes rate-limited attribution lines to the AP
// log so field sessions tell us WHERE a stall was:
//
//   "stall outside AP"  -- the hang was in the engine/renderer/vsync/OS between
//                          our per-frame slices (gap from the previous frame's
//                          AP-slice end to this frame's AP-slice start). A level
//                          load legitimately blocks for seconds and is NOT
//                          reported (see the LOAD_IDLE gate below).
//   "slow AP frame"     -- the hang was in OUR layer this frame; the dominant
//                          section names the suspect:
//                            poll   = network handlers (item batches + the
//                                     connect-time datapackage sync)
//                            verify = the seed-completability sweep (ap_verify.c)
//                            rest   = everything else in AP_OnFrame (item apply,
//                                     traps, state dump, ...)

// Per-frame AP-slice sections. Room to grow: add new IDs before
// AP_PERF_SEC__COUNT (and account for them in ap_perf.c's "rest" arithmetic).
enum
{
	AP_PERF_SEC_POLL   = 0, // apclientpp poll() -- all network handlers fire inline
	AP_PERF_SEC_VERIFY = 1, // seed-completability sweep (ap_verify.c)
	AP_PERF_SEC__COUNT = 2
};

// Thresholds and rate limit, in milliseconds.
//   GAP_WARN   -- a between-slices gap over this is a candidate "stall outside AP".
//   SLICE_WARN -- an AP-slice total over this is a "slow AP frame". 50ms is ~3
//                 frames at 60fps: below the "hang" bar players report but well
//                 above a healthy slice, so the log stays quiet in normal play.
//   RATELIMIT  -- at most one line of each type per this window; occurrences
//                 dropped in between are counted and reported as suppressed=N on
//                 the next line of that type (a burst stays one line, not a flood).
#define AP_PERF_GAP_WARN_MS   250.0
#define AP_PERF_SLICE_WARN_MS 50.0
#define AP_PERF_RATELIMIT_MS  1000.0

// Called at the very top of AP_OnFrame with the live levelID and load stage.
void AP_PerfFrameBegin(int levelID, int loadStage);

// Bracket a section around a call site inside the AP slice. Depth-1 only (the
// call sites do not nest); an unbalanced End (no matching Begin) is ignored.
void AP_PerfSectionBegin(int sec);
void AP_PerfSectionEnd(int sec);

// Called at the very bottom of AP_OnFrame (after the body, incl. its early
// returns). Emits the "slow AP frame" line if the slice ran long, then stamps
// the slice-end time the next frame's "stall outside AP" gap is measured from.
void AP_PerfFrameEnd(void);

#endif // CTR_AP
#endif // AP_PERF_H
