#ifndef AP_FRAME_WATCH_LOGIC_H
#define AP_FRAME_WATCH_LOGIC_H

// Freestanding logic for the whole-frame stall watchdog ([AP PERF] frame stall
// lines in ctr-ap.log). The runtime glue lives in ap/ap_perf.c; this header
// holds everything that can be decided without the engine, so the host harness
// tools/test-frame-watch.c pins the same code the game runs:
//
//   * exclusive-time accounting of the frame's buckets (a small stack, so a
//     bucket nested inside another is charged to itself only and the parts
//     plus "rest" add up to the frame total),
//   * the stall threshold,
//   * the rate limiter shared with the older "slow AP frame" line (one line per
//     window, the rest counted into suppressed=N on the next line),
//   * the tags that make a line readable on its own (game mode, load stage,
//     VSync option) and the line format itself.
//
// Nothing here reads a clock, touches a file or allocates. The caller passes
// timestamps in milliseconds and owns the output buffer.

#include <stdio.h>
#include <string.h>

// A frame (one pass of the main loop) longer than this is written to the log.
// 250 ms is about 15 frames at 60 Hz: far above any healthy frame, well below
// the one-second freezes players report.
#define AP_FW_STALL_MS 250.0

// Same window the older [AP PERF] lines use: at most one line per line type per
// second, later occurrences counted into suppressed=N.
#define AP_FW_RATELIMIT_MS 1000.0

// Deepest bucket nesting the accounting follows. The real maximum is four
// (render -> submit -> scene -> log); anything deeper is counted and ignored.
#define AP_FW_STACK_MAX 8

// Frame buckets. Every bucket is EXCLUSIVE: time spent in a nested bucket is
// charged to the nested one only.
enum
{
	AP_FW_AP       = 0,  // AP_OnFrame slice, minus the file I/O inside it
	AP_FW_DUMP     = 1,  // ap-state.json dump (game-thread part)
	AP_FW_LOG      = 2,  // log writes: ctr-ap.log lines + the renderer log flush
	AP_FW_LOGIC    = 3,  // MainFrame_GameLogic
	AP_FW_RENDER   = 4,  // MainFrame_RenderFrame CPU work not covered below
	AP_FW_SCENE    = 5,  // Platform_BeginScene: swap interval, scene begin, clear
	AP_FW_SUBMIT   = 6,  // RenderSubmit: DrawOTag, split draws, VRAM upload
	AP_FW_READBACK = 7,  // synchronous framebuffer readback (glReadPixels)
	AP_FW_PRESENT  = 8,  // Platform_EndScene: framebuffer store + present
	AP_FW_SWAP     = 9,  // SDL_GL_SwapWindow
	AP_FW_VSYNC    = 10, // software VBlank pacer wait
	AP_FW__COUNT   = 11
};

typedef struct AP_FrameWatch
{
	int    active;       // between AP_FwFrameBegin and AP_FwFrameEnd
	double frameBeginMs;
	double segBeginMs;   // start of the running segment of the innermost bucket
	double accum[AP_FW__COUNT];
	int    stack[AP_FW_STACK_MAX];
	int    depth;
	int    overflow;     // Begins ignored because the stack was full
} AP_FrameWatch;

static inline void AP_FwFrameBegin(AP_FrameWatch *w, double now)
{
	memset(w, 0, sizeof *w);
	w->active = 1;
	w->frameBeginMs = now;
	w->segBeginMs = now;
}

// Charge the running segment to the innermost open bucket.
static inline void AP_FwChargeTop(AP_FrameWatch *w, double now)
{
	if (w->depth > 0)
	{
		double d = now - w->segBeginMs;
		if (d > 0.0)
			w->accum[w->stack[w->depth - 1]] += d;
	}
	w->segBeginMs = now;
}

static inline void AP_FwBegin(AP_FrameWatch *w, int bucket, double now)
{
	if (!w->active || bucket < 0 || bucket >= AP_FW__COUNT)
		return;
	if (w->depth >= AP_FW_STACK_MAX)
	{
		w->overflow++;
		return;
	}
	AP_FwChargeTop(w, now);
	w->stack[w->depth++] = bucket;
}

// Close `bucket`. An End that matches an inner bucket left open by an early
// return closes everything above it too; an End with no open match is ignored.
static inline void AP_FwEnd(AP_FrameWatch *w, int bucket, double now)
{
	int i;
	if (!w->active || bucket < 0 || bucket >= AP_FW__COUNT)
		return;
	if (w->overflow > 0)
	{
		w->overflow--;
		return;
	}
	for (i = w->depth - 1; i >= 0; i--)
		if (w->stack[i] == bucket)
			break;
	if (i < 0)
		return;
	AP_FwChargeTop(w, now);
	w->depth = i;
}

// Close the frame: anything still open is charged up to `now`. Returns the
// frame total in milliseconds.
static inline double AP_FwFrameEnd(AP_FrameWatch *w, double now)
{
	if (!w->active)
		return 0.0;
	AP_FwChargeTop(w, now);
	w->depth = 0;
	w->overflow = 0;
	w->active = 0;
	return now - w->frameBeginMs;
}

// Frame time no bucket covers (loop overhead, input, the load queue, the
// fullscreen re-sync, the parts of the AP layer outside AP_OnFrame).
static inline double AP_FwRest(const AP_FrameWatch *w, double total)
{
	double rest = total;
	int i;
	for (i = 0; i < AP_FW__COUNT; i++)
		rest -= w->accum[i];
	return rest < 0.0 ? 0.0 : rest;
}

static inline int AP_FwIsStall(double totalMs)
{
	return totalMs > AP_FW_STALL_MS;
}

// ---------------------------------------------------------------------------
// Rate limiter (shared with the "slow AP frame" line in ap_perf.c).
// ---------------------------------------------------------------------------

typedef struct AP_PerfRateLimit
{
	double   lastEmitMs;
	int      emitted;    // 0 until the first emission
	unsigned suppressed; // dropped since the last emission
} AP_PerfRateLimit;

// Green light if the window has passed since the last emission (or there was
// none). On green it stamps the time, hands back the drops since the last line
// through *outSuppressed and resets the count. On red it counts the drop.
static inline int AP_PerfRateLimitTry(AP_PerfRateLimit *rl, double now,
                                      double windowMs, unsigned *outSuppressed)
{
	if (rl->emitted && (now - rl->lastEmitMs) < windowMs)
	{
		rl->suppressed++;
		return 0;
	}
	*outSuppressed = rl->suppressed;
	rl->suppressed = 0;
	rl->lastEmitMs = now;
	rl->emitted = 1;
	return 1;
}

// ---------------------------------------------------------------------------
// Tags.
// ---------------------------------------------------------------------------

// GameTracker.gameMode1 bits the mode tag reads (include/namespace_Main.h;
// ap_perf.c asserts they still match).
#define AP_FW_GM1_PAUSE_ALL       0x0000000Fu
#define AP_FW_GM1_MAIN_MENU       0x00002000u
#define AP_FW_GM1_ADVENTURE_ARENA 0x00100000u
#define AP_FW_GM1_GAME_CUTSCENE   0x20000000u
#define AP_FW_GM1_LOADING         0x40000000u

// Loading.stage values (include/namespace_Main.h).
#define AP_FW_LOAD_IDLE (-1)

static inline const char *AP_FwModeName(unsigned gameMode1, int loadStage)
{
	if (loadStage != AP_FW_LOAD_IDLE || (gameMode1 & AP_FW_GM1_LOADING) != 0)
		return "load";
	if ((gameMode1 & AP_FW_GM1_MAIN_MENU) != 0)
		return "menu";
	if ((gameMode1 & AP_FW_GM1_GAME_CUTSCENE) != 0)
		return "cutscene";
	if ((gameMode1 & AP_FW_GM1_ADVENTURE_ARENA) != 0)
		return "hub";
	return "race";
}

static inline int AP_FwPaused(unsigned gameMode1)
{
	return (gameMode1 & AP_FW_GM1_PAUSE_ALL) != 0;
}

// Loading.stage as a word: idle, vlc, restart, requested, finished, stageN for
// the TenStages pipeline, the raw number for anything else.
static inline void AP_FwLoadStageName(int stage, char *buf, size_t cap)
{
	const char *name = NULL;
	switch (stage)
	{
	case -6: name = "vlc"; break;
	case -5: name = "restart"; break;
	case -4: name = "requested"; break;
	case -2: name = "finished"; break;
	case -1: name = "idle"; break;
	default: break;
	}
	if (name != NULL)
		snprintf(buf, cap, "%s", name);
	else if (stage >= 0)
		snprintf(buf, cap, "stage%d", stage);
	else
		snprintf(buf, cap, "%d", stage);
}

// "idle", or "idle>stage0" when the frame started and ended in different stages.
static inline void AP_FwLoadTag(int beginStage, int endStage, char *buf, size_t cap)
{
	char a[16], b[16];
	AP_FwLoadStageName(beginStage, a, sizeof a);
	if (beginStage == endStage)
	{
		snprintf(buf, cap, "%s", a);
		return;
	}
	AP_FwLoadStageName(endStage, b, sizeof b);
	snprintf(buf, cap, "%s>%s", a, b);
}

// config.ini [Video & QoL] vsync (include/platform/native_vsync.h).
static inline const char *AP_FwVsyncName(int option)
{
	switch (option)
	{
	case 0: return "off";
	case 1: return "on";
	case 2: return "adaptive";
	default: return "?";
	}
}

// Everything the stall line says besides the bucket times.
typedef struct AP_FwContext
{
	unsigned gameMode1;
	int      levelID;
	int      loadStageBegin;
	int      loadStageEnd;
	int      mainGameState;
	unsigned timer;          // gGT->timer, the t= clock of the other AP lines
	int      itemsReceived;  // received items drained this frame
	long long lastItemId;    // AP id of the last of them (0 when none)
	int      checksSent;     // location checks sent this frame
	int      stateDumped;    // the ap-state.json dump ran this frame
	int      connected;      // AP room connection
	int      vsyncOption;    // config.ini vsync
	int      fullscreen;     // config.ini fullscreen (kept in step with the window)
	double   apPollMs;       // AP slice sections (ap_perf.c), exclusive of log I/O
	double   apVerifyMs;
	double   apItemMs;
} AP_FwContext;

static inline int AP_FwMs(double ms)
{
	return ms < 0.0 ? 0 : (int)ms;
}

// Format the one stall line. Returns snprintf's result.
static inline int AP_FwFormatStallLine(char *buf, size_t cap, double totalMs,
                                       const AP_FrameWatch *w,
                                       const AP_FwContext *c, unsigned suppressed)
{
	char load[40];
	const double *a = w->accum;
	AP_FwLoadTag(c->loadStageBegin, c->loadStageEnd, load, sizeof load);
	return snprintf(buf, cap,
	                "[AP PERF] frame stall: total=%dms ap=%dms (poll=%d verify=%d item=%d) "
	                "io=%dms (dump=%d log=%d) logic=%dms render=%dms scene=%dms "
	                "submit=%dms readback=%dms present=%dms swap=%dms vsync=%dms rest=%dms | "
	                "mode=%s paused=%d load=%s gm1=0x%08x main=%d lvl=%d t=%u "
	                "items=%d last_item=%lld checks=%d state_write=%d net=%s vsync_opt=%s fullscreen=%d "
	                "(suppressed=%u)\n",
	                AP_FwMs(totalMs), AP_FwMs(a[AP_FW_AP]),
	                AP_FwMs(c->apPollMs), AP_FwMs(c->apVerifyMs), AP_FwMs(c->apItemMs),
	                AP_FwMs(a[AP_FW_DUMP] + a[AP_FW_LOG]),
	                AP_FwMs(a[AP_FW_DUMP]), AP_FwMs(a[AP_FW_LOG]),
	                AP_FwMs(a[AP_FW_LOGIC]), AP_FwMs(a[AP_FW_RENDER]),
	                AP_FwMs(a[AP_FW_SCENE]), AP_FwMs(a[AP_FW_SUBMIT]),
	                AP_FwMs(a[AP_FW_READBACK]), AP_FwMs(a[AP_FW_PRESENT]),
	                AP_FwMs(a[AP_FW_SWAP]), AP_FwMs(a[AP_FW_VSYNC]),
	                AP_FwMs(AP_FwRest(w, totalMs)),
	                AP_FwModeName(c->gameMode1, c->loadStageEnd),
	                AP_FwPaused(c->gameMode1), load, c->gameMode1,
	                c->mainGameState, c->levelID, c->timer,
	                c->itemsReceived, c->lastItemId, c->checksSent, c->stateDumped,
	                c->connected ? "connected" : "offline",
	                AP_FwVsyncName(c->vsyncOption), c->fullscreen ? 1 : 0,
	                suppressed);
}

#endif // AP_FRAME_WATCH_LOGIC_H
