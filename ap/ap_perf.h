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
// WHY: players report intermittent stutters and one-second whole-game freezes,
// but release builds ship no per-frame telemetry by default (the engine's CSV
// profiler in platform/native_perf.c only runs under --perf). The AP layer runs
// fully synchronous on the single game thread -- AP_OnFrame (ap_hooks.c) is
// called every frame, and apclientpp's poll() fires all network handlers inline
// during ap_net_poll(). This watchdog is an always-on, allocation-free, I/O-free
// (bar the log lines) probe that writes rate-limited attribution lines to the AP
// log so field sessions tell us WHERE a stall was:
//
//   "frame stall"       -- one pass of the main loop (CTR_Main, MainMain.c) took
//                          longer than AP_FW_STALL_MS. Bracketed at the top and
//                          bottom of the loop body, so input, game logic, the AP
//                          slice, render submit, present, swap and the vsync wait
//                          are all inside it. The line splits the frame into
//                          exclusive buckets (ap_frame_watch_logic.h) and tags it
//                          with the game mode, load stage, level, AP traffic this
//                          frame, connection state and the VSync / fullscreen
//                          options. Loads are NOT hidden: a load-screen stall is
//                          written with its load= stage so it can be told apart.
//                          Supersedes the older "stall outside AP" gap line.
//   "slow AP frame"     -- the AP slice alone ran over AP_PERF_SLICE_WARN_MS but
//                          the frame as a whole stayed under the stall threshold.
//                          When the frame is a stall, the frame stall line carries
//                          the AP breakdown instead, so one freeze is one line.
//                          The dominant section names the suspect:
//                            poll   = network handlers (item batches + the
//                                     connect-time datapackage sync)
//                            verify = the seed-completability sweep (ap_verify.c)
//                            item   = the received-item drain (runs AFTER poll
//                                     closes, so an item burst used to land in
//                                     "rest")
//                            dump   = AP_DumpState rewriting the whole state
//                                     file, every 60 frames
//                            logio  = the log writes, summed across every line
//                                     the frame wrote
//                            rest   = everything else in AP_OnFrame (traps,
//                                     DeathLink, the adventure poll, ...)

// Per-frame AP-slice sections. Room to grow: add new IDs before
// AP_PERF_SEC__COUNT (and account for them in ap_perf.c's "rest" arithmetic).
//
// The sections are DISJOINT by construction, so "rest" is a real remainder.
// LOG_IO is the only one that nests inside the others (every section can write
// log lines), so ap_perf.c subtracts the log-io charged inside a section from
// that section's own total -- see AP_PerfSectionEnd.
enum
{
	AP_PERF_SEC_POLL       = 0, // apclientpp poll() -- all network handlers fire inline
	AP_PERF_SEC_VERIFY     = 1, // seed-completability sweep (ap_verify.c)
	AP_PERF_SEC_ITEM_APPLY = 2, // received-item drain + per-item bookkeeping (ap_hooks.c)
	AP_PERF_SEC_STATE_DUMP = 3, // AP_DumpState: full state-file rewrite (ap_hooks.c)
	AP_PERF_SEC_LOG_IO     = 4, // AP_AppendLog: one fopen/fputs/fclose per log line
	AP_PERF_SEC__COUNT     = 5
};

// Thresholds and rate limit, in milliseconds. The frame stall threshold is
// AP_FW_STALL_MS in ap_frame_watch_logic.h.
//   SLICE_WARN -- an AP-slice total over this is a "slow AP frame". 50ms is ~3
//                 frames at 60fps: below the "hang" bar players report but well
//                 above a healthy slice, so the log stays quiet in normal play.
//   RATELIMIT  -- at most one line of each type per this window; occurrences
//                 dropped in between are counted and reported as suppressed=N on
//                 the next line of that type (a burst stays one line, not a flood).
#define AP_PERF_SLICE_WARN_MS 50.0
#define AP_PERF_RATELIMIT_MS  1000.0

// Called at the very top of AP_OnFrame with the live levelID, load stage, and
// frame timer. timer is gGT->timer, the same value the t= stamp on the [AP
// ITEM] / [AP HUB] / [AP RACE] lines carries, so a slow-frame line can be lined
// up against the game-side lines around it.
void AP_PerfFrameBegin(int levelID, int loadStage, unsigned timer);

// Bracket a section around a call site inside the AP slice. Depth-1 per section
// (a section never re-enters itself); an unbalanced End (no matching Begin) is
// ignored. LOG_IO may open inside any other section -- that is expected and
// accounted for, see the enum above.
void AP_PerfSectionBegin(int sec);
void AP_PerfSectionEnd(int sec);

// Called at the very bottom of AP_OnFrame (after the body, incl. its early
// returns). If the slice ran long it prepares the "slow AP frame" line; the line
// is written at the end of the frame by AP_FrameWatchEnd, unless the frame turns
// out to be a stall, in which case the frame stall line carries the breakdown.
void AP_PerfFrameEnd(void);

// ---------------------------------------------------------------------------
// Whole-frame watchdog.
// ---------------------------------------------------------------------------

// Top and bottom of the main loop body (CTR_Main, game/MAIN/MainMain.c). End
// reads the game mode, level and load stage itself and writes the frame stall
// line when the frame ran over AP_FW_STALL_MS.
void AP_FrameWatchBegin(void);
void AP_FrameWatchEnd(void);

// Engine buckets, forwarded from NativePerf_BeginScope / NativePerf_EndScope
// (platform/native_perf.c) whether or not --perf is on. `nativeBucket` is an
// enum NativePerfBucket; buckets the watchdog does not use cost one table
// lookup and return.
void AP_FrameWatchNativeBegin(int nativeBucket);
void AP_FrameWatchNativeEnd(int nativeBucket);

// The renderer log flush (Platform_LogFlush in Platform_BeginScene), charged to
// the frame's log I/O.
void AP_FrameWatchLogFlushBegin(void);
void AP_FrameWatchLogFlushEnd(void);

// AP traffic this frame, for the stall line's context.
void AP_PerfNoteItemsReceived(int count, long long lastItemId);
void AP_PerfNoteCheckSent(void);

// One line at client start naming the watchdog threshold and the video adapter,
// so every log identifies the GPU and driver. Called once, after the run-start
// marker.
void AP_PerfAnnounce(void);

#endif // CTR_AP
#endif // AP_PERF_H
