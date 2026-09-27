// Out-of-engine assertions for the whole-frame stall watchdog ([AP PERF] frame
// stall lines). Exercises the freestanding accounting, threshold, rate limiter,
// tags and line format in ap/ap_frame_watch_logic.h directly -- no SDL, no
// engine, no clock (timestamps are passed in).
//
//   cc -Wall -Wextra -I ap -o /tmp/test-frame-watch tools/test-frame-watch.c && /tmp/test-frame-watch
//
// Exit 0 = every assertion held; failing cases are printed otherwise.
//
// Covers:
//   * exclusive accounting: a nested bucket is charged to itself only, so the
//     buckets plus rest add up to the frame total
//   * an End matching an outer bucket closes inner ones left open by an early
//     return; an End with nothing open is ignored; Begin/End outside a frame is
//     ignored; nesting deeper than the stack is counted and skipped
//   * the 250 ms threshold (strictly greater)
//   * the rate limiter: one line per window, drops reported as suppressed=N
//   * mode, pause, load-stage and VSync tags, including a load that starts
//     inside the frame
//   * the exact line format

#include <stdio.h>
#include <string.h>

#include "ap_frame_watch_logic.h"

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

static int near(double a, double b)
{
	double d = a - b;
	return d < 1e-9 && d > -1e-9;
}

static void test_exclusive_accounting(void)
{
	AP_FrameWatch w;
	double total, sum = 0.0;
	int i;

	AP_FwFrameBegin(&w, 1000.0);
	AP_FwBegin(&w, AP_FW_AP, 1001.0);     // AP slice 1001..1011
	AP_FwBegin(&w, AP_FW_DUMP, 1003.0);   //   dump 1003..1008
	AP_FwBegin(&w, AP_FW_LOG, 1004.0);    //     a log line inside the dump 1004..1006
	AP_FwEnd(&w, AP_FW_LOG, 1006.0);
	AP_FwEnd(&w, AP_FW_DUMP, 1008.0);
	AP_FwEnd(&w, AP_FW_AP, 1011.0);
	AP_FwBegin(&w, AP_FW_RENDER, 1012.0); // render 1012..1030
	AP_FwBegin(&w, AP_FW_SUBMIT, 1014.0); //   submit 1014..1020
	AP_FwBegin(&w, AP_FW_SCENE, 1015.0);  //     scene 1015..1017
	AP_FwEnd(&w, AP_FW_SCENE, 1017.0);
	AP_FwEnd(&w, AP_FW_SUBMIT, 1020.0);
	AP_FwBegin(&w, AP_FW_VSYNC, 1021.0);  //   vsync 1021..1029
	AP_FwEnd(&w, AP_FW_VSYNC, 1029.0);
	AP_FwEnd(&w, AP_FW_RENDER, 1030.0);
	AP_FwBegin(&w, AP_FW_SWAP, 1031.0);   // swap 1031..1500
	AP_FwEnd(&w, AP_FW_SWAP, 1500.0);
	total = AP_FwFrameEnd(&w, 1502.0);

	expect(near(total, 502.0), "frame total is end - begin");
	expect(near(w.accum[AP_FW_AP], 10.0 - 5.0), "AP excludes the dump nested in it");
	expect(near(w.accum[AP_FW_DUMP], 5.0 - 2.0), "dump excludes the log line nested in it");
	expect(near(w.accum[AP_FW_LOG], 2.0), "log line charged to log only");
	expect(near(w.accum[AP_FW_RENDER], 18.0 - 6.0 - 8.0), "render excludes submit and vsync");
	expect(near(w.accum[AP_FW_SUBMIT], 6.0 - 2.0), "submit excludes scene");
	expect(near(w.accum[AP_FW_SCENE], 2.0), "scene");
	expect(near(w.accum[AP_FW_VSYNC], 8.0), "vsync");
	expect(near(w.accum[AP_FW_SWAP], 469.0), "swap");
	for (i = 0; i < AP_FW__COUNT; i++)
		sum += w.accum[i];
	expect(near(sum + AP_FwRest(&w, total), total), "buckets + rest == total");
	expect(near(AP_FwRest(&w, total), 1.0 + 1.0 + 1.0 + 2.0), "rest is the uncovered gaps");
	expect(!w.active, "frame closed");
}

static void test_unbalanced(void)
{
	AP_FrameWatch w;

	// Begin/End outside a frame: ignored.
	memset(&w, 0, sizeof w);
	AP_FwBegin(&w, AP_FW_SWAP, 5.0);
	AP_FwEnd(&w, AP_FW_SWAP, 9.0);
	expect(w.depth == 0 && near(w.accum[AP_FW_SWAP], 0.0), "no accounting outside a frame");

	// Early return: submit is still open when render closes.
	AP_FwFrameBegin(&w, 0.0);
	AP_FwBegin(&w, AP_FW_RENDER, 0.0);
	AP_FwBegin(&w, AP_FW_SUBMIT, 2.0);
	AP_FwEnd(&w, AP_FW_RENDER, 7.0);
	expect(w.depth == 0, "closing the outer bucket pops the inner one");
	expect(near(w.accum[AP_FW_RENDER], 2.0), "render charged until submit opened");
	expect(near(w.accum[AP_FW_SUBMIT], 5.0), "open inner bucket charged up to the outer End");

	// End with nothing matching: ignored, no time moves.
	AP_FwEnd(&w, AP_FW_VSYNC, 9.0);
	expect(near(w.accum[AP_FW_VSYNC], 0.0) && w.depth == 0, "stray End ignored");

	// A bucket still open at frame end is charged up to the end.
	AP_FwBegin(&w, AP_FW_LOGIC, 10.0);
	expect(near(AP_FwFrameEnd(&w, 30.0), 30.0), "total");
	expect(near(w.accum[AP_FW_LOGIC], 20.0), "open bucket closed at frame end");

	// Out-of-range bucket ids: ignored.
	AP_FwFrameBegin(&w, 0.0);
	AP_FwBegin(&w, -1, 1.0);
	AP_FwBegin(&w, AP_FW__COUNT, 1.0);
	expect(w.depth == 0, "bad bucket ids ignored");
}

static void test_overflow(void)
{
	AP_FrameWatch w;
	int i;

	AP_FwFrameBegin(&w, 0.0);
	for (i = 0; i < AP_FW_STACK_MAX; i++)
		AP_FwBegin(&w, AP_FW_LOGIC, (double)i);
	AP_FwBegin(&w, AP_FW_SWAP, 100.0); // one too deep: counted, skipped
	expect(w.overflow == 1 && w.depth == AP_FW_STACK_MAX, "overflow counted");
	AP_FwEnd(&w, AP_FW_SWAP, 200.0);   // its End consumes the overflow count
	expect(w.overflow == 0 && w.depth == AP_FW_STACK_MAX, "overflowed End skipped");
	expect(near(w.accum[AP_FW_SWAP], 0.0), "overflowed bucket not charged");
	AP_FwFrameEnd(&w, 300.0);
	expect(near(w.accum[AP_FW_LOGIC], 300.0), "all time charged to the innermost logged bucket");
}

static void test_threshold(void)
{
	expect(!AP_FwIsStall(33.4), "a normal frame is not a stall");
	expect(!AP_FwIsStall(250.0), "exactly the threshold is not a stall");
	expect(AP_FwIsStall(250.5), "over the threshold is a stall");
	expect(AP_FwIsStall(1000.0), "a one-second freeze is a stall");
}

static void test_ratelimit(void)
{
	AP_PerfRateLimit rl;
	unsigned sup = 99;

	memset(&rl, 0, sizeof rl);
	expect(AP_PerfRateLimitTry(&rl, 5000.0, AP_FW_RATELIMIT_MS, &sup) && sup == 0,
	       "first line goes out, suppressed=0");
	expect(!AP_PerfRateLimitTry(&rl, 5300.0, AP_FW_RATELIMIT_MS, &sup), "burst: dropped");
	expect(!AP_PerfRateLimitTry(&rl, 5999.0, AP_FW_RATELIMIT_MS, &sup), "still inside the window");
	sup = 99;
	expect(AP_PerfRateLimitTry(&rl, 6000.0, AP_FW_RATELIMIT_MS, &sup) && sup == 2,
	       "next line after the window reports the two drops");
	sup = 99;
	expect(AP_PerfRateLimitTry(&rl, 9000.0, AP_FW_RATELIMIT_MS, &sup) && sup == 0,
	       "counter reset after it was reported");
}

static void test_tags(void)
{
	char buf[40];

	expect(!strcmp(AP_FwModeName(0x00100000u | 0x00080000u, -1), "hub"), "adventure hub");
	expect(!strcmp(AP_FwModeName(0x00080000u, -1), "race"), "adventure race");
	expect(!strcmp(AP_FwModeName(0x00400000u, -1), "race"), "arcade race");
	expect(!strcmp(AP_FwModeName(0x00002000u, -1), "menu"), "main menu");
	expect(!strcmp(AP_FwModeName(0x20000000u, -1), "cutscene"), "cutscene");
	expect(!strcmp(AP_FwModeName(0x00100000u, 3), "load"), "a load stage wins over the mode bits");
	expect(!strcmp(AP_FwModeName(0x40000000u, -1), "load"), "LOADING flag");
	expect(AP_FwPaused(0x1u) && !AP_FwPaused(0x00100000u), "pause bits");

	AP_FwLoadStageName(-1, buf, sizeof buf);
	expect(!strcmp(buf, "idle"), "idle");
	AP_FwLoadStageName(-6, buf, sizeof buf);
	expect(!strcmp(buf, "vlc"), "vlc");
	AP_FwLoadStageName(-4, buf, sizeof buf);
	expect(!strcmp(buf, "requested"), "requested");
	AP_FwLoadStageName(4, buf, sizeof buf);
	expect(!strcmp(buf, "stage4"), "TenStages stage");
	AP_FwLoadStageName(-3, buf, sizeof buf);
	expect(!strcmp(buf, "-3"), "unnamed stage keeps its number");
	AP_FwLoadTag(-1, -1, buf, sizeof buf);
	expect(!strcmp(buf, "idle"), "same stage prints once");
	AP_FwLoadTag(-1, 0, buf, sizeof buf);
	expect(!strcmp(buf, "idle>stage0"), "a load starting inside the frame shows both");

	expect(!strcmp(AP_FwVsyncName(0), "off") && !strcmp(AP_FwVsyncName(1), "on") &&
	       !strcmp(AP_FwVsyncName(2), "adaptive") && !strcmp(AP_FwVsyncName(7), "?"),
	       "vsync option names");
}

static void test_line_format(void)
{
	AP_FrameWatch w;
	AP_FwContext c;
	char line[768];
	double total;

	AP_FwFrameBegin(&w, 0.0);
	AP_FwBegin(&w, AP_FW_AP, 1.0);
	AP_FwEnd(&w, AP_FW_AP, 3.5);
	AP_FwBegin(&w, AP_FW_LOGIC, 4.0);
	AP_FwEnd(&w, AP_FW_LOGIC, 9.0);
	AP_FwBegin(&w, AP_FW_SWAP, 10.0);
	AP_FwEnd(&w, AP_FW_SWAP, 1030.0);
	total = AP_FwFrameEnd(&w, 1043.0);

	memset(&c, 0, sizeof c);
	c.gameMode1 = 0x00080000u; // adventure race
	c.levelID = 11;
	c.loadStageBegin = -1;
	c.loadStageEnd = -1;
	c.mainGameState = 3;
	c.timer = 183495u;
	c.itemsReceived = 1;
	c.lastItemId = 35010075LL;
	c.checksSent = 0;
	c.stateDumped = 1;
	c.connected = 1;
	c.vsyncOption = 0;
	c.fullscreen = 1;
	c.apPollMs = 0.4;
	c.apItemMs = 1.9;
	AP_FwFormatStallLine(line, sizeof line, total, &w, &c, 2);

	expect(!strcmp(line,
	               "[AP PERF] frame stall: total=1043ms ap=2ms (poll=0 verify=0 item=1) "
	               "io=0ms (dump=0 log=0) logic=5ms render=0ms scene=0ms "
	               "submit=0ms readback=0ms present=0ms swap=1020ms vsync=0ms rest=15ms | "
	               "mode=race paused=0 load=idle gm1=0x00080000 main=3 lvl=11 t=183495 "
	               "items=1 last_item=35010075 checks=0 state_write=1 net=connected "
	               "vsync_opt=off fullscreen=1 (suppressed=2)\n"),
	       "stall line format");
	if (g_failures)
		printf("line was: %s", line);
	expect(strlen(line) < 400, "line fits the emit buffer with room to spare");
}

int main(void)
{
	test_exclusive_accounting();
	test_unbalanced();
	test_overflow();
	test_threshold();
	test_ratelimit();
	test_tags();
	test_line_format();

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
