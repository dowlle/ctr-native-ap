"""A received DeathLink that a newer one replaces still gets its own popup.

Compiles the production receive bookkeeping from ap/ap_deathlink.c (the receive
state, AP_DeathLinkPopup, AP_DeathLinkDrop and AP_DeathLinkDrainInbound) against
the production inbox (ap/ap_deathlink_inbox.h, the store ap_net.cpp hands deaths
through) and the production receive decision and grace step
(ap/ap_race_attempt_logic.h). Frames are driven by a small model of
AP_DeathLinkTick's receive branch. Covered:

  * two deaths in one frame: the older shows "DEATHLINK IGNORED", the newer applies;
  * a second death while the first is in its grace window: same;
  * a second death while the first is held for a pause: same, and the newer one
    still applies on unpause in the same race;
  * which death applies is unchanged (always the newest), nothing is carried to a
    later frame, and DeathLink off drains silently;
  * the inbox keeps arrival order and its bound.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
dl = (root / 'ap/ap_deathlink.c').read_text()
net = (root / 'ap/ap_net.cpp').read_text()


def func(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


def block(source, first, last):
    start = source.index(first)
    end = source.index(last, start)
    return source[start:source.index('\n', end) + 1]


# ap_net.cpp must hand every bounce through the inbox (no single latch left).
assert 'AP_DeathLinkInboxPush(&g_dl_inbox' in net, 'bounced handler no longer pushes into the inbox'
take = func(net, 'extern "C" int ap_net_deathlink_take(')
assert 'AP_DeathLinkInboxTake(&g_dl_inbox' in take, 'ap_net_deathlink_take no longer reads the inbox'
assert 'g_dl_incoming' not in net, 'the old depth-1 latch is back'
# The tick drains through the function under test, and the feature-off branch
# drains the whole inbox.
tick = func(dl, 'void AP_DeathLinkTick(struct GameTracker *gGT)')
assert 'AP_DeathLinkDrainInbound();' in tick
assert 'AP_DeathLinkInboxTakeDropped(&g_dl_inbox)' in func(net, 'extern "C" int ap_net_deathlink_take_dropped('), 'ap_net no longer reports dropped deaths'
assert re.search(r'while \(ap_net_deathlink_take\(cause, sizeof cause, source, sizeof source\)\)\s*;', tick)

state = block(dl, 'static int  g_dl_pending_recv', 'static int  g_dl_hold_logged')
pieces = '\n'.join([
    state,
    func(dl, 'static void AP_DeathLinkPopup(int applied, const char *why)'),
    func(dl, 'static void AP_DeathLinkDrop(const char *why)'),
    func(dl, 'static void AP_DeathLinkDrainInbound(void)'),
])

fixture = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ap_race_attempt_logic.h"
#include "ap_deathlink_inbox.h"

enum { CTR_DL_OFF = 0, CTR_DL_MASK_RESET = 1, CTR_DL_RACE_LOSS = 3, CTR_DL_WEAPON_HIT = 4 };

/* Engine-side stubs: record what production shows and logs. */
static char feed[64][96];
static int feed_ignored[64];
static int feed_n;
static void AP_FeedDeathLinkLine(const char *text, int ignored)
{
	assert(feed_n < 64);
	snprintf(feed[feed_n], sizeof feed[feed_n], "%.28s", text);
	feed_ignored[feed_n++] = ignored;
}
static int logs;
static void AP_LogLine(const char *msg) { (void)msg; logs++; }
static int mode = CTR_DL_MASK_RESET;
static int AP_DeathLinkEffMode(void) { return mode; }

/* ap_net.cpp side: the same inbox and take the production client uses. */
static APDeathLinkInbox g_dl_inbox;
static int ap_net_deathlink_take(char *c, int cn, char *s, int sn)
{
	return AP_DeathLinkInboxTake(&g_dl_inbox, c, cn, s, sn);
}
static int ap_net_deathlink_take_dropped(void)
{
	return AP_DeathLinkInboxTakeDropped(&g_dl_inbox);
}
static void bounce(const char *source, const char *cause)
{
	AP_DeathLinkInboxPush(&g_dl_inbox, cause, source);
}

PRODUCTION

/* Model of AP_DeathLinkTick's receive branch for one frame. `land` says whether
 * the pending death can land this live frame (mask-grab gate open, kart under
 * control); when it lands it shows the applied popup, as every apply path does. */
static char applied_src[64];
static int applied_n;
static void frame(int window, int land)
{
	int action;
	AP_DeathLinkDrainInbound();
	action = AP_DeathLinkReceiveDecision(mode, g_dl_pending_recv, 1, window);
	if (action == AP_DL_RECV_DROP)
		AP_DeathLinkDrop("not in a race");
	else if (action == AP_DL_RECV_HOLD)
		;
	else if (action != AP_DL_RECV_WAIT && g_dl_pending_recv)
	{
		if (land)
		{
			snprintf(applied_src, sizeof applied_src, "%s", g_dl_pending_source);
			applied_n++;
			AP_DeathLinkPopup(1, 0);
			g_dl_pending_recv = 0;
			g_dl_pending_cause[0] = '\0';
			g_dl_pending_source[0] = '\0';
		}
		else if (AP_DeathLinkGraceStep(&g_dl_grace_left))
			AP_DeathLinkDrop("could not apply");
	}
}

static void reset(void)
{
	AP_DeathLinkInboxClear(&g_dl_inbox);
	g_dl_pending_recv = 0;
	g_dl_grace_left = 0;
	g_dl_pending_cause[0] = '\0';
	g_dl_pending_source[0] = '\0';
	feed_n = 0;
	applied_n = 0;
	applied_src[0] = '\0';
	mode = CTR_DL_MASK_RESET;
}

static int has_line(const char *text, int ignored)
{
	int i;
	for (i = 0; i < feed_n; i++)
		if (strcmp(feed[i], text) == 0 && feed_ignored[i] == ignored)
			return 1;
	return 0;
}

static void expect_replaced_then_newer_applies(const char *what)
{
	assert(has_line("DEATHLINK IGNORED", 1));
	assert(has_line("Alice: newer death", 1));
	assert(has_line("DEATHLINK: Bob", 0));
	assert(has_line("fell off", 0));
	assert(applied_n == 1 && strcmp(applied_src, "Bob") == 0);
	assert(!g_dl_pending_recv && g_dl_inbox.count == 0);
	printf("PASS %s\n", what);
}

int main(void)
{
	int m, i;
	char c[128], s[64];

	/* Inbox: arrival order, drained to empty, bounded (oldest discarded). */
	reset();
	bounce("A", "a1");
	bounce("B", "b1");
	assert(AP_DeathLinkInboxTake(&g_dl_inbox, c, sizeof c, s, sizeof s) && !strcmp(s, "A") && !strcmp(c, "a1"));
	assert(AP_DeathLinkInboxTake(&g_dl_inbox, c, sizeof c, s, sizeof s) && !strcmp(s, "B"));
	assert(!AP_DeathLinkInboxTake(&g_dl_inbox, c, sizeof c, s, sizeof s));
	for (i = 0; i < AP_DL_INBOX_CAP; i++)
		assert(!AP_DeathLinkInboxPush(&g_dl_inbox, "x", "s"));
	assert(AP_DeathLinkInboxPush(&g_dl_inbox, "last", "z") && g_dl_inbox.count == AP_DL_INBOX_CAP);
	for (i = 0; AP_DeathLinkInboxTake(&g_dl_inbox, c, sizeof c, s, sizeof s); i++)
		;
	assert(i == AP_DL_INBOX_CAP && !strcmp(c, "last"));
	assert(AP_DeathLinkInboxTake(&g_dl_inbox, 0, 0, 0, 0) == 0);
	puts("PASS inbox keeps arrival order and its bound");

	/* Two deaths in one frame, in every receive mode. */
	for (m = 1; m <= 4; m++)
	{
		reset();
		mode = m;
		bounce("Alice", "Alice was eaten");
		bounce("Bob", "Bob fell off");
		frame(AP_DL_WIN_LIVE, 1);
		expect_replaced_then_newer_applies("two deaths in one frame");
		/* the older popup comes first */
		assert(strcmp(feed[0], "DEATHLINK IGNORED") == 0 && strcmp(feed[1], "Alice: newer death") == 0);
	}

	/* Second death inside the first one's grace window. */
	for (m = 1; m <= 4; m++)
	{
		reset();
		mode = m;
		bounce("Alice", "Alice was eaten");
		frame(AP_DL_WIN_LIVE, 0);
		frame(AP_DL_WIN_LIVE, 0);
		assert(g_dl_pending_recv && feed_n == 0);
		bounce("Bob", "Bob fell off");
		frame(AP_DL_WIN_LIVE, 0);
		assert(has_line("Alice: newer death", 1) && g_dl_pending_recv &&
		       strcmp(g_dl_pending_source, "Bob") == 0 && g_dl_grace_left == AP_DL_GRACE_FRAMES - 1);
		frame(AP_DL_WIN_LIVE, 1);
		expect_replaced_then_newer_applies("second death in the grace window");
	}

	/* Second death while the first is held for a pause; applies on unpause. */
	for (m = 1; m <= 4; m++)
	{
		reset();
		mode = m;
		bounce("Alice", "Alice was eaten");
		frame(AP_DL_WIN_PAUSED, 1);
		assert(g_dl_pending_recv && feed_n == 0);
		bounce("Bob", "Bob fell off");
		frame(AP_DL_WIN_PAUSED, 1);
		assert(feed_n == 2 && has_line("Alice: newer death", 1) && g_dl_pending_recv &&
		       strcmp(g_dl_pending_source, "Bob") == 0);
		frame(AP_DL_WIN_PAUSED, 1);
		assert(feed_n == 2 && g_dl_pending_recv);
		frame(AP_DL_WIN_LIVE, 1);
		expect_replaced_then_newer_applies("second death during a pause hold");
	}

	/* Still never queued: replaced or not, a death outside a race drops at once
	 * and nothing reaches the next frame. Three deaths: two ignored as replaced,
	 * the newest ignored as not in a race. */
	reset();
	bounce("Alice", "a");
	bounce("Bob", "b");
	bounce("Carl", "c");
	frame(AP_DL_WIN_OUTSIDE, 1);
	assert(feed_n == 6 && has_line("Alice: newer death", 1) && has_line("Bob: newer death", 1) &&
	       has_line("Carl: not in a race", 1) && !g_dl_pending_recv && applied_n == 0);
	frame(AP_DL_WIN_LIVE, 1);
	assert(applied_n == 0 && feed_n == 6);
	puts("PASS three deaths outside a race: three ignored popups, nothing held");

	/* Review finding 12: more deaths than the inbox holds in one poll. The oldest
	 * are discarded unread, and one ignored popup says how many. */
	reset();
	for (i = 0; i < AP_DL_INBOX_CAP + 5; i++)
		bounce("Spam", "died");
	bounce("Last", "Last fell off");
	frame(AP_DL_WIN_LIVE, 1);
	assert(has_line("6 MORE: TOO MANY", 1));
	assert(strcmp(feed[0], "DEATHLINK IGNORED") == 0 && strcmp(feed[1], "6 MORE: TOO MANY") == 0); /* summary first */
	assert(has_line("DEATHLINK: Last", 0) && applied_n == 1 && strcmp(applied_src, "Last") == 0);
	assert(g_dl_inbox.count == 0 && g_dl_inbox.dropped == 0);
	frame(AP_DL_WIN_LIVE, 1);
	assert(!has_line("1 MORE: TOO MANY", 1)); /* reported once */
	puts("PASS inbox overflow shows one ignored popup with the count");

	/* Overflow with receive off stays silent. */
	reset();
	mode = CTR_DL_OFF;
	for (i = 0; i < AP_DL_INBOX_CAP + 3; i++)
		bounce("Spam", "died");
	frame(AP_DL_WIN_LIVE, 1);
	assert(feed_n == 0 && g_dl_inbox.dropped == 0);
	puts("PASS overflow with receive off is silent");

	/* A lone death is unchanged: no extra popup. */
	reset();
	bounce("Bob", "Bob fell off");
	frame(AP_DL_WIN_LIVE, 1);
	assert(feed_n == 2 && applied_n == 1 && !has_line("DEATHLINK IGNORED", 1));
	puts("PASS a single death shows only its own popup");

	/* DeathLink receive off: drained without popups. */
	reset();
	mode = CTR_DL_OFF;
	bounce("Alice", "a");
	bounce("Bob", "b");
	frame(AP_DL_WIN_LIVE, 1);
	assert(feed_n == 0 && !g_dl_pending_recv && g_dl_inbox.count == 0);
	puts("PASS receive off drains silently");
	return 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / 'dl.c', Path(tmp) / 'dl'
    src.write_text(fixture.replace('PRODUCTION', pieces))
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-format-truncation', '-fsanitize=undefined,address',
                    '-I', str(root / 'ap'), str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('DeathLink replaced-death popup harness passed')
