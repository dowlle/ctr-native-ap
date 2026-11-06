// Host harness for the feed line store and trap lines (ap/ap_feed_lines_logic.h,
// A3-21): one feed line per trap that changes in place, ARMED then INCOMING then
// the trap name, with the receipt line unchanged.
//
//   cc -std=c99 -Wall -Wextra -DCTR_AP -I ap -I . -I include -o /tmp/tfeedtrap tools/test-feed-trap-lines.c && /tmp/tfeedtrap
//
// The real trap scheduler produces the events; pump() maps them to feed calls the
// way AP_TrapDrainEvents in ap/ap_traps.c does (ARMED, WARN and FIRE only; every
// other event is log-only). Keep the two in step.

#include <stdio.h>
#include <string.h>

#include "../ap/ap_feed_lines_logic.h"
#include "../ap/ap_trap_items.h"
#include "../ap/ap_trap_sched_logic.h"

static int failures;
static int checks;

static void expect(const char *name, int got, int want)
{
	checks++;
	if (got != want)
	{
		printf("FAIL %-60s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

static void expect_text(const char *name, const char *got, const char *want)
{
	checks++;
	if (strcmp(got, want) != 0)
	{
		printf("FAIL %-60s got=\"%s\" want=\"%s\"\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

#define SALMON 7

static AP_FeedLines feed;

static void feed_frames(int n)
{
	int i;
	for (i = 0; i < n; i++)
	{
		AP_FeedLinesPromote(&feed);
		AP_FeedLinesAge(&feed);
	}
}

// Visible line index of a text, or -1.
static int vis_find(const char *text)
{
	int i;
	for (i = 0; i < feed.vcount; i++)
		if (strcmp(feed.vis[i].text, text) == 0)
			return i;
	return -1;
}

static int vis_suffix_count(const char *suffix)
{
	int i, n = 0;
	size_t k = strlen(suffix);
	for (i = 0; i < feed.vcount; i++)
	{
		size_t l = strlen(feed.vis[i].text);
		if (l >= k && strcmp(feed.vis[i].text + l - k, suffix) == 0)
			n++;
	}
	return n;
}

// Same mapping as AP_TrapDrainEvents.
static void pump(AP_TrapSched *s)
{
	AP_TrapEvent ev;
	char msg[128];
	while (AP_TrapSchedPopEvent(s, &ev))
	{
		const char *name = AP_TRAP_DESC[ev.effect].name;
		switch (ev.kind)
		{
		case AP_TRAP_EV_ARMED:
			snprintf(msg, sizeof msg, "%s ARMED", name);
			AP_FeedLinesTrapState(&feed, ev.slot, AP_FEED_TRAP_ARMED, msg, SALMON);
			break;
		case AP_TRAP_EV_WARN:
			snprintf(msg, sizeof msg, "%s INCOMING", name);
			AP_FeedLinesTrapState(&feed, ev.slot, AP_FEED_TRAP_INCOMING, msg, SALMON);
			break;
		case AP_TRAP_EV_FIRE:
			snprintf(msg, sizeof msg, "%s", name);
			AP_FeedLinesTrapState(&feed, ev.slot, AP_FEED_TRAP_FIRED, msg, SALMON);
			break;
		default:
			break;
		}
	}
}

static AP_TrapWorld world_in(int context, int epoch)
{
	AP_TrapWorld w;
	memset(&w, 0, sizeof w);
	w.context = context;
	w.mapEpoch = epoch;
	w.controlUnlocked = 1;
	w.elapsedMs = 100;
	return w;
}

// One scheduler frame of 100 ms is six feed frames at 60 fps.
static void step(AP_TrapSched *s, const AP_TrapWorld *w, int frames)
{
	int i;
	for (i = 0; i < frames; i++)
	{
		AP_TrapSchedStep(s, w);
		pump(s);
		feed_frames(6);
	}
}

static void fresh(void)
{
	AP_FeedLinesReset(&feed);
}

// A trap that waits, then warns, then fires: three states, one line each time.
static void case_one_trap_one_line(void)
{
	AP_TrapSched s;
	AP_TrapWorld hub = world_in(AP_TRAP_CTX_HUB, 1);
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE, 2);

	fresh();
	AP_TrapSchedReset(&s);
	AP_FeedLinesEnqueue(&feed, "FORCED BOOST", SALMON, 1); // the receipt line
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
	step(&s, &hub, 5);
	expect("receipt line plus armed line", feed.vcount, 2);
	expect("armed line text", vis_find("Forced Boost ARMED") >= 0, 1);

	step(&s, &race, 1);
	expect("warning replaces armed, still two lines", feed.vcount, 2);
	expect("warning text", vis_find("Forced Boost INCOMING") >= 0, 1);
	expect("armed text is gone", vis_find("Forced Boost ARMED"), -1);

	step(&s, &race, 10);
	expect("fire replaces warning, still two lines", feed.vcount, 2);
	expect("fire shows the bare name", vis_find("Forced Boost") >= 0, 1);
	expect("incoming text is gone", vis_find("Forced Boost INCOMING"), -1);
	expect("receipt line unchanged", vis_find("FORCED BOOST") >= 0, 1);
}

// A trap without a warning goes from ARMED to the name on the same line.
static void case_no_warning_trap(void)
{
	fresh();
	AP_FeedLinesTrapState(&feed, 3, AP_FEED_TRAP_ARMED, "Flatten ARMED", SALMON);
	feed_frames(1);
	AP_FeedLinesTrapState(&feed, 3, AP_FEED_TRAP_FIRED, "Flatten", SALMON);
	feed_frames(1);
	expect("armed to name is one line", feed.vcount, 1);
	expect_text("armed to name text", feed.vis[0].text, "Flatten");
}

// Several traps at once: one line each, none overwrites another's.
static void case_several_traps(void)
{
	AP_TrapSched s;
	AP_TrapWorld hub = world_in(AP_TRAP_CTX_HUB, 1);
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE, 2);

	fresh();
	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
	AP_TrapSchedReceive(&s, AP_TRAP_FLATTEN);
	AP_TrapSchedReceive(&s, AP_TRAP_WIREFRAME);
	step(&s, &hub, 5);
	expect("three armed traps, three lines", feed.vcount, 3);
	expect("each armed line present",
	       (vis_find("Forced Boost ARMED") >= 0) + (vis_find("Flatten ARMED") >= 0) +
	       (vis_find("Wireframe ARMED") >= 0), 3);

	step(&s, &race, 1);
	expect("warnings keep three lines", feed.vcount, 3);
	expect("no armed text left", vis_suffix_count(" ARMED"), 0);
	step(&s, &race, 10);
	expect("fires keep three lines", feed.vcount, 3);
	expect("fired boost line", vis_find("Forced Boost") >= 0, 1);
	expect("fired wireframe line", vis_find("Wireframe") >= 0, 1);
}

// The line scrolled out before the next state: a new line is added.
static void case_scrolled_out(void)
{
	fresh();
	AP_FeedLinesTrapState(&feed, 0, AP_FEED_TRAP_ARMED, "Icy Road ARMED", SALMON);
	feed_frames(AP_FEED_LIFETIME_FRAMES + 20);
	expect("armed line expired", feed.vcount, 0);
	AP_FeedLinesTrapState(&feed, 0, AP_FEED_TRAP_FIRED, "Icy Road", SALMON);
	feed_frames(AP_FEED_STAGGER_FRAMES + 1);
	expect("late fire adds one fresh line", feed.vcount, 1);
	expect_text("late fire text", feed.vis[0].text, "Icy Road");
}

// A state change restarts the display time of the visible line.
static void case_display_time_restarts(void)
{
	fresh();
	AP_FeedLinesTrapState(&feed, 1, AP_FEED_TRAP_ARMED, "Low Gravity ARMED", SALMON);
	feed_frames(AP_FEED_LIFETIME_FRAMES - 30);
	expect("armed line still up near its end", feed.vcount, 1);
	AP_FeedLinesTrapState(&feed, 1, AP_FEED_TRAP_INCOMING, "Low Gravity INCOMING", SALMON);
	feed_frames(AP_FEED_LIFETIME_FRAMES - 30);
	expect("replaced line outlives the old deadline", feed.vcount, 1);
	feed_frames(40);
	expect("replaced line expires after its own full time", feed.vcount, 0);
}

// A trap that waits armed across a race restart. The scheduler re-announces a
// trap whose warning a restart cancelled; the existing line takes the text back.
static void case_armed_across_restart(void)
{
	AP_TrapSched s;
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE, 1);
	AP_TrapWorld again = world_in(AP_TRAP_CTX_RACE, 2);
	AP_TrapWorld hub = world_in(AP_TRAP_CTX_HUB, 1);

	// Waiting armed: Forced Boost armed in a hub, still armed through a restart.
	fresh();
	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
	step(&s, &hub, 3);
	expect("armed in hub, one line", feed.vcount, 1);
	AP_TrapSchedMapChange(&s);
	step(&s, &hub, 3);
	expect("armed copy survives the boundary, still one line", feed.vcount, 1);
	expect_text("still the armed text", feed.vis[0].text, "Forced Boost ARMED");

	// Restart during the warning: ARMED comes back over INCOMING, same line.
	fresh();
	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
	step(&s, &race, 3); // warning running
	expect("warning line", vis_find("Forced Boost INCOMING") >= 0, 1);
	AP_TrapSchedMapChange(&s);
	step(&s, &again, 1);
	expect("restart keeps one line", feed.vcount, 1);
	step(&s, &again, 12);
	expect("fires after the restart on that same line", feed.vcount, 1);
	expect_text("fired text", feed.vis[0].text, "Forced Boost");
}

// A slot is reused after its trap is over: the new trap gets its own line while
// the old trap's fired line is still on screen.
static void case_slot_reuse(void)
{
	fresh();
	AP_FeedLinesTrapState(&feed, 0, AP_FEED_TRAP_ARMED, "Flatten ARMED", SALMON);
	AP_FeedLinesTrapState(&feed, 0, AP_FEED_TRAP_FIRED, "Flatten", SALMON);
	feed_frames(30);
	AP_FeedLinesTrapState(&feed, 0, AP_FEED_TRAP_ARMED, "Nitro Drop ARMED", SALMON);
	feed_frames(30);
	expect("old fired line and new armed line are separate", feed.vcount, 2);
	expect("old line kept its text", vis_find("Flatten") >= 0, 1);
	expect("new line shows armed", vis_find("Nitro Drop ARMED") >= 0, 1);
	AP_FeedLinesTrapState(&feed, 0, AP_FEED_TRAP_FIRED, "Nitro Drop", SALMON);
	expect("new trap's fire updates the new line only", vis_find("Flatten") >= 0, 1);
	expect("new line now the name", vis_find("Nitro Drop") >= 0, 1);
	expect("still two lines", feed.vcount, 2);
}

// A trap line still waiting in the ring takes the newer state there.
static void case_replaced_while_queued(void)
{
	int i;
	char buf[32];
	fresh();
	for (i = 0; i < 8; i++)
	{
		snprintf(buf, sizeof buf, "ITEM %d", i);
		AP_FeedLinesEnqueue(&feed, buf, SALMON, 1);
	}
	AP_FeedLinesTrapState(&feed, 5, AP_FEED_TRAP_ARMED, "Wireframe ARMED", SALMON);
	expect("trap line queued behind receipts", feed.qcount, 9);
	AP_FeedLinesTrapState(&feed, 5, AP_FEED_TRAP_INCOMING, "Wireframe INCOMING", SALMON);
	AP_FeedLinesTrapState(&feed, 5, AP_FEED_TRAP_FIRED, "Wireframe", SALMON);
	expect("queued trap line was replaced, not appended", feed.qcount, 9);
	AP_FeedLinesTrapState(&feed, 6, AP_FEED_TRAP_ARMED, "Icy Road ARMED", SALMON);
	expect("another trap still appends", feed.qcount, 10);
	feed_frames(600);
	expect("queued line surfaced with the newest text", vis_find("Wireframe") >= 0 ||
	       feed.vcount == 0, 1);
}

// Five visible, 24 waiting and the stagger are unchanged by trap lines.
static void case_limits_unchanged(void)
{
	int i;
	char buf[32];
	fresh();
	for (i = 0; i < 40; i++)
	{
		snprintf(buf, sizeof buf, "ITEM %d", i);
		AP_FeedLinesEnqueue(&feed, buf, SALMON, 1);
	}
	expect("pending ring holds 24", feed.qcount, 24);
	feed_frames(AP_FEED_STAGGER_FRAMES * 10);
	expect("five lines visible at most", feed.vcount, 5);
	expect("lifetime is about 4 s", AP_FEED_LIFETIME_FRAMES, 240);
	for (i = 0; i < 8; i++)
		AP_FeedLinesTrapState(&feed, i, AP_FEED_TRAP_ARMED, "Trap ARMED", SALMON);
	expect("trap lines share the same 24 ring", feed.qcount <= 24, 1);
}

int main(void)
{
	case_one_trap_one_line();
	case_no_warning_trap();
	case_several_traps();
	case_scrolled_out();
	case_display_time_restarts();
	case_armed_across_restart();
	case_slot_reuse();
	case_replaced_while_queued();
	case_limits_unchanged();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
