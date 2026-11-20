"""Run the production letter feed with actual plan mode gates."""
from pathlib import Path
import subprocess
import tempfile

import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
source = ap_hooks_unit.read()


def function(signature):
    start = source.index(signature + "\n{")
    return source[start:source.index("\n}\n", start) + 3]


fixture = r'''
#include <assert.h>
#include <string.h>
#include "ap/ap_content_modes.h"
#include "ap/ap_feed_lines_logic.h"
#include "ap/ap_lettersanity.h"
#include "ap/ap_unlock_reasons_logic.h"
#define LIGHT_GREEN 1
#define AP_FEED_PRIME_QUIET_FRAMES 30
static AP_FeedLines ap_feed;
static uint64_t ap_feed_room, room = 1;
static uint64_t ap_feed_endpoint, server = 1;
static unsigned ap_feed_letters_held, ap_feed_letters_announced, received;
static int ap_feed_letters_baseline, ap_feed_primed, ap_feed_quiet, ap_feed_self_rung_count;
static int feedOn = 1, racerMet = 1, firstWin;
static long long ap_feed_hwm;
static int counts[15];
static ctr_content_pad pad;
static ctr_content_checks checks;
int ap_content_plan_active(void) { return 1; }
uint64_t ap_content_plan_room_identity(void) { return room; }
static uint64_t ap_net_feed_endpoint_identity(void) { return server; }
static int ap_net_is_connected(void) { return 1; }
static void AP_FeedOxideFinalOpenUpdate(void) {}
static void AP_UnlockReasonsConnectReset(int sameRoom) { (void)sameRoom; }
const ctr_content_pad *ap_content_plan_pad(int phys) { return phys == pad.physical ? &pad : NULL; }
const ctr_content_checks *ap_content_plan_checks(void) { return &checks; }
static int AP_ContentLetterItemReceived(int64_t code) { return code > 0 && (received & (1u << code)); }
static int AP_ContentPadRacerMet(const ctr_content_pad *p) { assert(p == &pad); return racerMet; }
static int checked(int64_t code) { return firstWin && code == pad.trophy_location; }
static int AP_ContentModeOpen(const ctr_content_pad *p, int mode) {
    return ap_content_mode_open(p, mode, counts, checked);
}
static int AP_HubFeedOn(void) { return feedOn; }
static const char *AP_UnlockPadName(int phys) { assert(phys == 16); return "Shuffled track"; }
'''
tests = r'''
static void update(int drained) {
    AP_FeedEndDrain(drained);
    if (ap_feed_primed)
        AP_FeedLetterReadyUpdates();
}

static void settle(void) {
    for (int i = 0; i < AP_FEED_PRIME_QUIET_FRAMES; ++i)
        update(0);
}

int main(void) {
    checks.lettersanity = 2;
    pad.physical = 16; pad.occupied = 1; pad.keys = 1;
    pad.mode_count = 2; pad.modes[0] = AP_CONTENT_MODE_TROPHY;
    pad.modes[1] = AP_CONTENT_MODE_CTR; pad.stages[1] = 2;
    pad.first_race_rule = 1; pad.trophy_location = 100; pad.has_stage2 = 1;
    pad.stage2_count = 1; pad.stage2_mask = 1u;
    pad.letter_locations[0] = 1; pad.letter_locations[2] = 3;
    pad.letter_items[0] = 1; pad.letter_items[2] = 3;
    AP_FeedConnectReset();
    received = (1u << 1) | (1u << 3);
    update(2);
    assert(!ap_feed_letters_held && !ap_feed.qcount);
    settle();
    assert(ap_feed_letters_held && !ap_feed.qcount);
    counts[14] = 1; counts[0] = 1;
    update(1);
    assert(!ap_feed.qcount); // first race still needs to be won
    firstWin = 1; racerMet = 0;
    update(0);
    assert(!ap_feed.qcount);
    AP_FeedConnectReset();
    update(2);
    racerMet = 1;
    settle();
    assert(ap_feed.qcount == 1 && !ap_feed_letters_held);
    assert(!strcmp(ap_feed.queue[ap_feed.qhead].text, "Shuffled track: Got CTR letters"));
    AP_FeedLinesEnqueue(&ap_feed, "ordinary", 0, 0);
    AP_FeedConnectReset();
    settle();
    assert(ap_feed.qcount == 1);
    assert(!strcmp(ap_feed.queue[ap_feed.qhead].text, "Shuffled track: Got CTR letters"));
    AP_FeedLinesPromote(&ap_feed);
    AP_FeedConnectReset();
    settle();
    assert(!ap_feed.qcount && !ap_feed.vcount);

    AP_FeedEnqueueHeld("old room", 0, 0, 0);
    ++server;
    AP_FeedConnectReset();
    settle();
    assert(!ap_feed.qcount && !ap_feed_letters_held && ap_feed_letters_announced);

    ++room;
    checks.lettersanity = 3;
    pad.letter_locations[0] = pad.letter_locations[2] = 0;
    pad.letter_items[1] = 2;
    AP_FeedConnectReset();
    settle();
    assert(!ap_feed_letters_announced);
    received |= 1u << 2;
    update(1);
    assert(ap_feed.qcount == 1);

    ++room;
    checks.lettersanity = 1;
    AP_FeedConnectReset();
    settle();
    assert(!ap_feed_letters_announced && !ap_feed.qcount);
    ++room;
    checks.lettersanity = 3;
    received = 0;
    AP_FeedConnectReset();
    settle();
    feedOn = 0;
    received = (1u << 1) | (1u << 2) | (1u << 3);
    update(1);
    feedOn = 1;
    update(0);
    assert(!ap_feed.qcount);
    puts("Production letter readiness, plan gates, async baseline and reconnect passed");
}
'''
signatures = (
    "static void AP_FeedEnqueueHeld(const char *text, int suffixOffset, int color, int playCue)",
    "void AP_FeedConnectReset(void)",
    "void AP_FeedEndDrain(int drainedThisFrame)",
    "static void AP_ContentLetterState(const ctr_content_pad *pad, long codes[3], unsigned char received[3])",
    "static int AP_PlanLettersRequiredCount(const ctr_content_pad *pad)",
    "static int AP_PlanLettersRequiredMet(const ctr_content_pad *pad)",
    "static void AP_FeedLetterReadyUpdates(void)",
)
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / "fixture.c"
    exe = Path(tmp) / "fixture"
    src.write_text(fixture + "\n".join(function(sig) for sig in signatures) + tests)
    subprocess.run(["cc", "-std=c11", "-fsanitize=undefined", "-I", str(root), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

    # Compile the transport-facing endpoint key separately to exercise URI
    # equivalence and preserve case-sensitive WebSocket resources.
    net = (root / "ap/ap_net.cpp").read_text()
    start = net.index("static uint64_t ap_net_feed_endpoint_of(")
    end = net.index("\n}\n", start) + 3
    source_cpp = src.with_suffix(".cpp")
    source_cpp.write_text(r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <string>
''' + net[start:end] + r'''
int main() {
    assert(ap_net_feed_endpoint_of("Example.Test") == ap_net_feed_endpoint_of("wss://example.test:38281/"));
    assert(ap_net_feed_endpoint_of("ws://example.test/Room") == ap_net_feed_endpoint_of("ws://EXAMPLE.test:38281/Room"));
    assert(ap_net_feed_endpoint_of("ws://example.test/Room") != ap_net_feed_endpoint_of("ws://example.test/room"));
    assert(ap_net_feed_endpoint_of("ws://example.test/room") != ap_net_feed_endpoint_of("ws://example.test/room/"));
    assert(ap_net_feed_endpoint_of("example.test:38282") != ap_net_feed_endpoint_of("example.test"));
}
''')
    subprocess.run(["c++", "-std=c++17", str(source_cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
