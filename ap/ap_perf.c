// Archipelago frame-stall watchdog. See ap_perf.h for the contract and the
// interpretation of each log line. Compiled into the C unity build
// (game/game_unity.h) AFTER ap_hooks.c, so AP_LogLine (the non-static log shim,
// ap_hooks.h) and LOAD_IDLE (namespace_Main.h) are already in scope -- same
// unity-scope arrangement the sibling ap_verify.c / ap_traps.c modules rely on.

#ifdef CTR_AP

#include "ap_perf.h"
#include "ap_hooks.h" // AP_LogLine (non-static log shim), same as ap_traps.c

#include <stdio.h> // snprintf

// Monotonic wall-clock in milliseconds (platform/native_platform.c, CTR_AP
// only). Declared here rather than including <SDL3/SDL.h> so this module stays
// free of SDL headers -- the same extern-in-consumer pattern ap_hooks.c uses for
// Platform_InputRawKeyDown (ap_hooks.c:51). The definition lands later in the
// unity translation unit (native_platform.c is #included after game_unity.h),
// exactly like Platform_InputRawKeyDown's definition in native_input.c.
double Platform_PerfNowMs(void);

// ---------------------------------------------------------------------------
// State. All static, no allocation, no I/O except the log lines.
// ---------------------------------------------------------------------------

// Slice timing. s_lastFrameEndMs < 0 means "no valid previous frame end yet"
// (first call), which disables the "stall outside AP" gap until we have a real
// prior slice to measure from.
static double   s_lastFrameEndMs = -1.0;
static double   s_frameBeginMs   = 0.0;
static int      s_frameLevelID   = -1;
static unsigned s_frameTimer     = 0; // gGT->timer, for the t= stamp

// Previous frame's load stage. The "stall outside AP" gap must be suppressed if
// EITHER the previous or the current frame was loading (a level load blocks for
// seconds by design), so the previous stage has to be remembered across frames.
static int    s_prevLoadStage  = LOAD_IDLE;

// Per-frame section breakdown (ms). s_secOpen is the depth-1 nesting guard.
// s_secLogIoAtBeginMs is the LOG_IO running total captured when the section
// opened: the difference at End is the log I/O charged INSIDE this section, and
// it is subtracted so the sections stay disjoint (see AP_PerfSectionEnd).
static double s_secAccumMs[AP_PERF_SEC__COUNT];
static double s_secBeginMs[AP_PERF_SEC__COUNT];
static double s_secLogIoAtBeginMs[AP_PERF_SEC__COUNT];
static int    s_secOpen[AP_PERF_SEC__COUNT];

// Set while this module is writing one of its own log lines. The LOG_IO section
// wraps AP_AppendLog, so without this the watchdog would bill its own emission
// to the frame it is reporting on.
static int s_emittingOwnLine = 0;

// Rate limiting, per line type.
enum
{
	AP_PERF_LINE_GAP   = 0, // "stall outside AP"
	AP_PERF_LINE_SLICE = 1, // "slow AP frame"
	AP_PERF_LINE__COUNT = 2
};
static double   s_lineLastEmitMs[AP_PERF_LINE__COUNT];
static int      s_lineEmitted[AP_PERF_LINE__COUNT];    // 0 until first emission
static unsigned s_lineSuppressed[AP_PERF_LINE__COUNT]; // dropped since last emit

// Green-light this line type if at least AP_PERF_RATELIMIT_MS has passed since
// its last emission (or it has never been emitted). On a green light it stamps
// the emit time, hands back the number suppressed since the last one via
// *outSuppressed, and resets that counter. On a red light it just counts the
// drop and returns 0.
static int ap_perf_ratelimit(int line, double now, unsigned *outSuppressed)
{
	if (s_lineEmitted[line] &&
	    (now - s_lineLastEmitMs[line]) < AP_PERF_RATELIMIT_MS)
	{
		s_lineSuppressed[line]++;
		return 0;
	}
	*outSuppressed = s_lineSuppressed[line];
	s_lineSuppressed[line] = 0;
	s_lineLastEmitMs[line] = now;
	s_lineEmitted[line] = 1;
	return 1;
}

// Emit one of this module's own lines without the LOG_IO section billing it to
// the current frame.
static void ap_perf_emit(const char *msg)
{
	s_emittingOwnLine = 1;
	AP_LogLine(msg);
	s_emittingOwnLine = 0;
}

void AP_PerfFrameBegin(int levelID, int loadStage, unsigned timer)
{
	const double now = Platform_PerfNowMs();
	int i;

	// Clear the section breakdown up front so an unbalanced End left over from a
	// prior frame cannot leak into this one.
	for (i = 0; i < AP_PERF_SEC__COUNT; i++)
	{
		s_secAccumMs[i] = 0.0;
		s_secOpen[i] = 0;
	}

	// "stall outside AP": the time the engine/renderer/vsync/OS spent between our
	// slices (previous slice END -> this slice START). Only meaningful once we
	// have a valid previous end AND neither the previous nor the current frame was
	// loading -- a level load legitimately blocks for seconds and must not be
	// reported (mirrors the LOAD_IDLE gate at ap_hooks.c:3267).
	if (s_lastFrameEndMs >= 0.0 &&
	    loadStage == LOAD_IDLE && s_prevLoadStage == LOAD_IDLE)
	{
		const double gap = now - s_lastFrameEndMs;
		if (gap > AP_PERF_GAP_WARN_MS)
		{
			unsigned suppressed = 0;
			if (ap_perf_ratelimit(AP_PERF_LINE_GAP, now, &suppressed))
			{
				char msg[128];
				snprintf(msg, sizeof msg,
				         "[AP PERF] stall outside AP: gap=%dms lvl=%d t=%u (suppressed=%u)\n",
				         (int)gap, levelID, timer, suppressed);
				ap_perf_emit(msg);
			}
		}
	}

	s_prevLoadStage = loadStage;
	s_frameLevelID = levelID;
	s_frameTimer = timer;
	s_frameBeginMs = now;
}

void AP_PerfSectionBegin(int sec)
{
	if (sec < 0 || sec >= AP_PERF_SEC__COUNT)
		return;
	if (sec == AP_PERF_SEC_LOG_IO && s_emittingOwnLine)
		return; // our own line: the matching End is then ignored as unbalanced
	if (s_secOpen[sec]) // depth-1 only: ignore a re-entrant Begin
		return;
	s_secOpen[sec] = 1;
	s_secLogIoAtBeginMs[sec] = s_secAccumMs[AP_PERF_SEC_LOG_IO];
	s_secBeginMs[sec] = Platform_PerfNowMs();
}

void AP_PerfSectionEnd(int sec)
{
	if (sec < 0 || sec >= AP_PERF_SEC__COUNT)
		return;
	if (!s_secOpen[sec]) // ignore an End with no matching Begin
		return;

	double elapsed = Platform_PerfNowMs() - s_secBeginMs[sec];

	// Log I/O nests inside every other section (poll handlers, the item drain and
	// the state dump all write log lines), so charge it to LOG_IO only. Without
	// this, "rest = total - every section" would subtract those milliseconds
	// twice and read low.
	if (sec != AP_PERF_SEC_LOG_IO)
	{
		elapsed -= s_secAccumMs[AP_PERF_SEC_LOG_IO] - s_secLogIoAtBeginMs[sec];
		if (elapsed < 0.0)
			elapsed = 0.0;
	}

	s_secAccumMs[sec] += elapsed;
	s_secOpen[sec] = 0;
}

void AP_PerfFrameEnd(void)
{
	const double now = Platform_PerfNowMs();
	const double total = now - s_frameBeginMs;

	if (total > AP_PERF_SLICE_WARN_MS)
	{
		const double poll   = s_secAccumMs[AP_PERF_SEC_POLL];
		const double verify = s_secAccumMs[AP_PERF_SEC_VERIFY];
		const double item   = s_secAccumMs[AP_PERF_SEC_ITEM_APPLY];
		const double dump   = s_secAccumMs[AP_PERF_SEC_STATE_DUMP];
		const double logio  = s_secAccumMs[AP_PERF_SEC_LOG_IO];

		// The sections are disjoint (AP_PerfSectionEnd keeps nested log I/O out of
		// its enclosing section), so this is a true remainder: whatever is left is
		// AP work no section covers yet.
		double rest = total - poll - verify - item - dump - logio;
		if (rest < 0.0) // section timing can round just past the total; clamp
			rest = 0.0;

		unsigned suppressed = 0;
		if (ap_perf_ratelimit(AP_PERF_LINE_SLICE, now, &suppressed))
		{
			char msg[256];
			snprintf(msg, sizeof msg,
			         "[AP PERF] slow AP frame: total=%dms poll=%dms verify=%dms item=%dms "
			         "dump=%dms logio=%dms rest=%dms lvl=%d t=%u (suppressed=%u)\n",
			         (int)total, (int)poll, (int)verify, (int)item,
			         (int)dump, (int)logio, (int)rest,
			         s_frameLevelID, s_frameTimer, suppressed);
			ap_perf_emit(msg);
		}
	}

	s_lastFrameEndMs = now;
}

#endif // CTR_AP
