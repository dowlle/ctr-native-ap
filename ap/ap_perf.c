// Archipelago frame-stall watchdog. See ap_perf.h for the contract and the
// interpretation of each log line. Compiled into the C unity build
// (game/game_unity.h) AFTER ap_hooks.c, so AP_LogLine (the non-static log shim,
// ap_hooks.h), ap_net_is_connected (ap_net.h), g_config, sdata and LOAD_IDLE are
// already in scope -- same unity-scope arrangement the sibling ap_verify.c /
// ap_traps.c modules rely on.

#ifdef CTR_AP

#include "ap_perf.h"
#include "ap_hooks.h" // AP_LogLine (non-static log shim), same as ap_traps.c
#include "ap_frame_watch_logic.h"

#include <stdio.h> // snprintf

// Monotonic wall-clock in milliseconds (platform/native_platform.c, CTR_AP
// only). Declared here rather than including <SDL3/SDL.h> so this module stays
// free of SDL headers -- the same extern-in-consumer pattern ap_hooks.c uses for
// Platform_InputRawKeyDown (ap_hooks.c:51). The definition lands later in the
// unity translation unit (native_platform.c is #included after game_unity.h),
// exactly like Platform_InputRawKeyDown's definition in native_input.c.
double Platform_PerfNowMs(void);

// GL adapter strings cached at renderer init (platform/native_renderer.c).
// Plain C strings, so this module stays free of GL and SDL headers.
void NativeRenderer_GetAdapterInfo(const char **renderer, const char **vendor,
                                   const char **version);

// The mode tag in ap_frame_watch_logic.h reads these bits by value.
CTR_STATIC_ASSERT(AP_FW_GM1_PAUSE_ALL == PAUSE_ALL);
CTR_STATIC_ASSERT(AP_FW_GM1_MAIN_MENU == MAIN_MENU);
CTR_STATIC_ASSERT(AP_FW_GM1_ADVENTURE_ARENA == ADVENTURE_ARENA);
CTR_STATIC_ASSERT(AP_FW_GM1_GAME_CUTSCENE == GAME_CUTSCENE);
CTR_STATIC_ASSERT(AP_FW_GM1_LOADING == LOADING);
CTR_STATIC_ASSERT(AP_FW_LOAD_IDLE == LOAD_IDLE);

// ---------------------------------------------------------------------------
// State. All static, no allocation, no I/O except the log lines.
// ---------------------------------------------------------------------------

// AP-slice timing.
static double   s_frameBeginMs   = 0.0;
static int      s_frameLevelID   = -1;
static unsigned s_frameTimer     = 0; // gGT->timer, for the t= stamp

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
	AP_PERF_LINE_FRAME = 0, // "frame stall"
	AP_PERF_LINE_SLICE = 1, // "slow AP frame"
	AP_PERF_LINE__COUNT = 2
};
static AP_PerfRateLimit s_lineLimit[AP_PERF_LINE__COUNT];

// A "slow AP frame" line prepared by AP_PerfFrameEnd and written at the end of
// the frame, unless the frame turned out to be a stall (then the frame stall
// line carries the same breakdown and this one is dropped).
static int  s_slicePending = 0;
static char s_sliceLine[256];

// Whole-frame watchdog.
static AP_FrameWatch s_fw;
static int       s_fwLoadStageBegin = LOAD_IDLE;
static int       s_fwItems = 0;
static long long s_fwLastItem = 0;
static int       s_fwChecks = 0;
static int       s_fwDumped = 0;

// Emit one of this module's own lines without the LOG_IO section billing it to
// the current frame.
static void ap_perf_emit(const char *msg)
{
	s_emittingOwnLine = 1;
	AP_LogLine(msg);
	s_emittingOwnLine = 0;
}

static void ap_perf_emit_slice(double now)
{
	unsigned suppressed = 0;
	s_slicePending = 0;
	if (AP_PerfRateLimitTry(&s_lineLimit[AP_PERF_LINE_SLICE], now,
	                        AP_PERF_RATELIMIT_MS, &suppressed))
	{
		char msg[288];
		snprintf(msg, sizeof msg, "%s (suppressed=%u)\n", s_sliceLine, suppressed);
		ap_perf_emit(msg);
	}
}

void AP_PerfFrameBegin(int levelID, int loadStage, unsigned timer)
{
	const double now = Platform_PerfNowMs();
	int i;

	(void)loadStage; // the frame watchdog reads the load stage itself

	// Clear the section breakdown up front so an unbalanced End left over from a
	// prior frame cannot leak into this one.
	for (i = 0; i < AP_PERF_SEC__COUNT; i++)
	{
		s_secAccumMs[i] = 0.0;
		s_secOpen[i] = 0;
	}

	s_frameLevelID = levelID;
	s_frameTimer = timer;
	s_frameBeginMs = now;
	AP_FwBegin(&s_fw, AP_FW_AP, now);
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

	// File I/O is charged to its own frame bucket, wherever in the frame it runs.
	if (sec == AP_PERF_SEC_LOG_IO)
		AP_FwBegin(&s_fw, AP_FW_LOG, s_secBeginMs[sec]);
	else if (sec == AP_PERF_SEC_STATE_DUMP)
	{
		s_fwDumped = 1;
		AP_FwBegin(&s_fw, AP_FW_DUMP, s_secBeginMs[sec]);
	}
}

void AP_PerfSectionEnd(int sec)
{
	double now, elapsed;

	if (sec < 0 || sec >= AP_PERF_SEC__COUNT)
		return;
	if (!s_secOpen[sec]) // ignore an End with no matching Begin
		return;

	now = Platform_PerfNowMs();
	elapsed = now - s_secBeginMs[sec];

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

	if (sec == AP_PERF_SEC_LOG_IO)
		AP_FwEnd(&s_fw, AP_FW_LOG, now);
	else if (sec == AP_PERF_SEC_STATE_DUMP)
		AP_FwEnd(&s_fw, AP_FW_DUMP, now);
}

void AP_PerfFrameEnd(void)
{
	const double now = Platform_PerfNowMs();
	const double total = now - s_frameBeginMs;

	AP_FwEnd(&s_fw, AP_FW_AP, now);

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

		// The suppressed count is filled in when the line is written.
		snprintf(s_sliceLine, sizeof s_sliceLine,
		         "[AP PERF] slow AP frame: total=%dms poll=%dms verify=%dms item=%dms "
		         "dump=%dms logio=%dms rest=%dms lvl=%d t=%u",
		         (int)total, (int)poll, (int)verify, (int)item,
		         (int)dump, (int)logio, (int)rest,
		         s_frameLevelID, s_frameTimer);
		s_slicePending = 1;

		// Outside a watched frame (never the case from CTR_Main) nobody would
		// write it later, so write it now.
		if (!s_fw.active)
			ap_perf_emit_slice(now);
	}
}

// ---------------------------------------------------------------------------
// Whole-frame watchdog.
// ---------------------------------------------------------------------------

// enum NativePerfBucket -> frame bucket, -1 for buckets the watchdog skips. The
// high-frequency per-batch buckets (vertex upload, draw triangles, ...) map to
// -1 so the always-on cost stays at a handful of timer reads per frame.
static int ap_fw_bucket_for_native(int nativeBucket)
{
	switch (nativeBucket)
	{
	case NATIVE_PERF_BUCKET_GAME_LOGIC:           return AP_FW_LOGIC;
	case NATIVE_PERF_BUCKET_RENDER_FRAME:         return AP_FW_RENDER;
	case NATIVE_PERF_BUCKET_PLATFORM_BEGIN_SCENE: return AP_FW_SCENE;
	case NATIVE_PERF_BUCKET_RENDER_SUBMIT:        return AP_FW_SUBMIT;
	case NATIVE_PERF_BUCKET_FRAMEBUFFER_READBACK: return AP_FW_READBACK;
	case NATIVE_PERF_BUCKET_PLATFORM_END_SCENE:   return AP_FW_PRESENT;
	case NATIVE_PERF_BUCKET_SWAP_WINDOW:          return AP_FW_SWAP;
	case NATIVE_PERF_BUCKET_VSYNC_WAIT:           return AP_FW_VSYNC;
	default:                                      return -1;
	}
}

void AP_FrameWatchNativeBegin(int nativeBucket)
{
	int b;
	if (!s_fw.active)
		return;
	b = ap_fw_bucket_for_native(nativeBucket);
	if (b >= 0)
		AP_FwBegin(&s_fw, b, Platform_PerfNowMs());
}

void AP_FrameWatchNativeEnd(int nativeBucket)
{
	int b;
	if (!s_fw.active)
		return;
	b = ap_fw_bucket_for_native(nativeBucket);
	if (b >= 0)
		AP_FwEnd(&s_fw, b, Platform_PerfNowMs());
}

void AP_FrameWatchLogFlushBegin(void)
{
	if (s_fw.active)
		AP_FwBegin(&s_fw, AP_FW_LOG, Platform_PerfNowMs());
}

void AP_FrameWatchLogFlushEnd(void)
{
	if (s_fw.active)
		AP_FwEnd(&s_fw, AP_FW_LOG, Platform_PerfNowMs());
}

void AP_PerfNoteItemsReceived(int count, long long lastItemId)
{
	if (count <= 0)
		return;
	s_fwItems += count;
	s_fwLastItem = lastItemId;
}

void AP_PerfNoteCheckSent(void)
{
	s_fwChecks++;
}

void AP_PerfAnnounce(void)
{
	const char *renderer = NULL, *vendor = NULL, *version = NULL;
	char msg[640]; // the three adapter strings are capped at 160 + 96 + 160

	NativeRenderer_GetAdapterInfo(&renderer, &vendor, &version);
	snprintf(msg, sizeof msg,
	         "[AP PERF] frame watchdog on: threshold=%dms adapter=\"%s\" vendor=\"%s\" "
	         "gl=\"%s\" vsync_opt=%s fullscreen=%d\n",
	         (int)AP_FW_STALL_MS,
	         renderer ? renderer : "?", vendor ? vendor : "?", version ? version : "?",
	         AP_FwVsyncName(g_config.vsync), g_config.fullscreen ? 1 : 0);
	ap_perf_emit(msg);
}

void AP_FrameWatchBegin(void)
{
	int i;
	// A frame that never reaches AP_OnFrame (the load-complete and reset states)
	// must not report the previous slice's sections.
	for (i = 0; i < AP_PERF_SEC__COUNT; i++)
		if (!s_secOpen[i])
			s_secAccumMs[i] = 0.0;
	s_fwItems = 0;
	s_fwLastItem = 0;
	s_fwChecks = 0;
	s_fwDumped = 0;
	s_slicePending = 0;
	s_fwLoadStageBegin = (int)sdata->Loading.stage;
	AP_FwFrameBegin(&s_fw, Platform_PerfNowMs());
}

void AP_FrameWatchEnd(void)
{
	const double now = Platform_PerfNowMs();
	const double total = AP_FwFrameEnd(&s_fw, now);

	// StateZero (mainGameState 0) is the one-time boot initialisation: memory
	// card, CD directory, first language file. Long by design and not a freeze.
	if (AP_FwIsStall(total) && sdata->mainGameState != 0)
	{
		unsigned suppressed = 0;
		s_slicePending = 0; // the frame line carries the AP breakdown
		if (AP_PerfRateLimitTry(&s_lineLimit[AP_PERF_LINE_FRAME], now,
		                        AP_FW_RATELIMIT_MS, &suppressed))
		{
			struct GameTracker *gGT = sdata->gGT;
			AP_FwContext c;
			char msg[768];

			memset(&c, 0, sizeof c);
			c.gameMode1 = (unsigned)gGT->gameMode1;
#ifdef CTR_CUSTOM_PACKAGES
			c.levelID = MainRaceTrack_IdentityLevelID();
#else
			c.levelID = (int)gGT->levelID;
#endif
			c.loadStageBegin = s_fwLoadStageBegin;
			c.loadStageEnd = (int)sdata->Loading.stage;
			c.mainGameState = (int)sdata->mainGameState;
			c.timer = (unsigned)gGT->timer;
			c.itemsReceived = s_fwItems;
			c.lastItemId = s_fwLastItem;
			c.checksSent = s_fwChecks;
			c.stateDumped = s_fwDumped;
			c.connected = ap_net_is_connected();
			c.vsyncOption = g_config.vsync;
			c.fullscreen = g_config.fullscreen ? 1 : 0;
			c.apPollMs = s_secAccumMs[AP_PERF_SEC_POLL];
			c.apVerifyMs = s_secAccumMs[AP_PERF_SEC_VERIFY];
			c.apItemMs = s_secAccumMs[AP_PERF_SEC_ITEM_APPLY];

			AP_FwFormatStallLine(msg, sizeof msg, total, &s_fw, &c, suppressed);
			ap_perf_emit(msg);
		}
	}

	if (s_slicePending)
		ap_perf_emit_slice(now);
}

#endif // CTR_AP
