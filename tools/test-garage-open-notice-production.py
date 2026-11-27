"""FN1: the feed's GARAGE OPEN line appears only when the garage can be entered.

Compiles the production unlock-reason watch (AP_UnlockWatchCapture,
AP_UnlockReasonsUpdate and the reconnect hooks) out of the ap_hooks unit with
stubbed gates, and drives the orderings a player meets: the garage's own
requirement before its hub Keys, the Keys before the requirement, both on one
item, a same-room reconnect with the Keys arriving while away, a reconnect
and a fresh start that must not repeat a line, and the feed set to OFF.
"""
from pathlib import Path
import subprocess
import tempfile

import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
source = ap_hooks_unit.read()


def region(start_marker, end_marker):
    start = source.index(start_marker)
    return source[start:source.index(end_marker, start)]


fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum { AP_ITEM_FEED_OFF, AP_ITEM_FEED_ALL, AP_ITEM_FEED_IMPORTANT }; // native_config.h
#include "ap/ap_content_plan.h"
#include "ap/ap_door_history.h"
#include "ap/ap_feed_lines_logic.h"
#include "ap/ap_wumpa_shop.h"
#include "ap/ap_unlock_reasons_logic.h"
#define LIGHT_GREEN 1
#define AP_IDX_KEY 14
#define AP_BOSS_VENUE_GARAGES 4
static AP_FeedLines ap_feed;
static int ap_feed_primed;
static int feedMode = AP_ITEM_FEED_ALL;
static int ap_recv_count[15];
static int bossOwn[4], oxideOwn;
static AP_WumpaShopConfig ctr_wumpa_shop;
static int ctr_cfg_active(void) { return 1; }
int ap_content_plan_active(void) { return 1; }
const ctr_content_pad *ap_content_plan_pad(int phys) { (void)phys; return NULL; }
static int ctr_cfg_warp_unlocked(int phys) { (void)phys; return 0; }
static int ctr_cfg_racer_lock_met(int phys) { (void)phys; return 1; }
static int ctr_cfg_warp_stage2_unlocked(int phys) { (void)phys; return 0; }
static int AP_ContentPadRacerMet(const ctr_content_pad *p) { (void)p; return 1; }
static int AP_GateCount(int idx) { return ap_recv_count[idx]; }
static int AP_BossGarageOpen(int g) { return bossOwn[g]; }
static int AP_OxideGarageOpen(void) { return oxideOwn; }
static int ap_net_wumpa_ready(void) { return 0; }
static long long ap_net_wumpa_lifetime(void) { return 0; }
static int AP_ItemFeedMode(void) { return feedMode; }
static void AP_FeedEnqueueHeld(const char *text, int suffixOffset, int color, int playCue)
{
    AP_FeedLinesEnqueueNotice(&ap_feed, text, color, playCue, 1)->suffixOffset = suffixOffset;
}
static const char *AP_UnlockPadName(int phys) { (void)phys; return "PAD"; }
'''

tests = r'''
static int lines(const char *prefix)
{
    int n = 0;
    for (int i = 0; i < ap_feed.qcount; ++i)
        if (!strncmp(ap_feed.queue[(ap_feed.qhead + i) % AP_FEED_QUEUE_MAX].text, prefix, strlen(prefix)))
            n++;
    return n;
}

static void clear_feed(void) { AP_FeedLinesReset(&ap_feed); }

// One received item, then the frame's batch end, then some quiet frames.
static void item(void)
{
    AP_UnlockReasonsAfterItem();
    AP_UnlockReasonsEndDrain(1);
    for (int i = 0; i < 5; ++i)
        AP_UnlockReasonsEndDrain(0);
}

static void quiet(int frames)
{
    for (int i = 0; i < frames; ++i)
        AP_UnlockReasonsEndDrain(0);
}

static void fresh(void)
{
    memset(ap_recv_count, 0, sizeof ap_recv_count);
    memset(bossOwn, 0, sizeof bossOwn);
    oxideOwn = 0;
    AP_UnlockReasonsConnectReset(0, 0);
    ap_feed_primed = 0;
    quiet(3);
    ap_feed_primed = 1;
    quiet(3);
    clear_feed();
}

static void show(const char *label)
{
    printf("%s:", label);
    for (int i = 0; i < ap_feed.qcount; ++i)
        printf(" [%s]", ap_feed.queue[(ap_feed.qhead + i) % AP_FEED_QUEUE_MAX].text);
    printf("\n");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    // 1. Own requirement first, hub Keys later (the report: Papu Papu's
    //    garage is in The Lost Ruins, behind the one-Key door).
    fresh();
    bossOwn[1] = 1;               // the garage's own requirement is met
    item();
    show("requirement first, no Key");
    assert(lines("Papu Papu GARAGE OPEN") == 0);
    quiet(30);
    assert(ap_feed.qcount == 0);
    ap_recv_count[AP_IDX_KEY] = 1; // the Key that opens The Lost Ruins
    item();
    show("then the Key");
    assert(lines("Papu Papu GARAGE OPEN") == 1);
    assert(!strcmp(ap_feed.queue[ap_feed.qhead].text, "Papu Papu GARAGE OPEN"));
    assert(lines("Gem Stone Valley DOOR OPEN") == 1); // the door keeps its own summary line
    quiet(30);
    item();
    assert(lines("Papu Papu GARAGE OPEN") == 1); // never twice

    // 2. Hub Keys first, own requirement later (Komodo Joe, two Keys).
    fresh();
    ap_recv_count[AP_IDX_KEY] = 2;
    item();
    clear_feed();
    bossOwn[2] = 1;
    item();
    show("Keys first, then requirement");
    assert(ap_feed.qcount == 1 && lines("Komodo Joe GARAGE OPEN") == 1);

    // 3. Both on the same item (Pinstripe, three Keys; the third Key is the
    //    last term of the garage requirement too).
    fresh();
    ap_recv_count[AP_IDX_KEY] = 2;
    item();
    clear_feed();
    ap_recv_count[AP_IDX_KEY] = 3;
    bossOwn[3] = 1;
    item();
    show("both at once");
    assert(lines("Pinstripe GARAGE OPEN") == 1);
    assert(lines("Citadel City DOOR OPEN") == 1);
    assert(ap_feed.qcount == 2);

    // Review round 1, finding 1: one Key releases a held garage and completes
    // another garage's own gate. Each garage gets its own named line.
    fresh();
    ap_recv_count[AP_IDX_KEY] = 2;
    bossOwn[3] = 1;               // Pinstripe held behind the three-Key door
    item();
    clear_feed();
    ap_recv_count[AP_IDX_KEY] = 3;
    bossOwn[0] = 1;               // the same Key completes Ripper Roo's gate
    item();
    show("two garages on one Key");
    assert(lines("Ripper Roo GARAGE OPEN") == 1 && lines("Pinstripe GARAGE OPEN") == 1);

    // Ripper Roo's garage needs no Key: its own requirement is enough.
    fresh();
    bossOwn[0] = 1;
    item();
    assert(lines("Ripper Roo GARAGE OPEN") == 1);

    // Oxide's garage stands in Gem Stone Valley, behind the one-Key door.
    fresh();
    oxideOwn = 1;
    item();
    assert(ap_feed.qcount == 0);
    ap_recv_count[AP_IDX_KEY] = 1;
    item();
    assert(lines("Oxide GARAGE OPEN") == 1);

    // 4. Reconnect to the same room after the line was shown: no repeat.
    fresh();
    ap_recv_count[AP_IDX_KEY] = 1;
    bossOwn[1] = 1;
    item();
    assert(lines("Papu Papu GARAGE OPEN") == 1);
    clear_feed();
    AP_UnlockReasonsConnectReset(1, 0);
    ap_feed_primed = 0;
    quiet(5);                     // the inventory absorb
    AP_UnlockReasonsResolveReconnect(1);
    ap_feed_primed = 1;
    quiet(30);
    show("reconnect after the line");
    assert(ap_feed.qcount == 0);

    // 5. Held garage, Key arrives while the client is away: shown once after
    //    the reconnect, and not again on a second reconnect.
    fresh();
    bossOwn[1] = 1;
    item();
    assert(ap_feed.qcount == 0);
    AP_UnlockReasonsConnectReset(1, 0);
    ap_feed_primed = 0;
    ap_recv_count[AP_IDX_KEY] = 1; // received while away, absorbed silently
    quiet(5);
    AP_UnlockReasonsResolveReconnect(1);
    ap_feed_primed = 1;
    quiet(30);
    show("Key while away");
    assert(lines("Papu Papu GARAGE OPEN") == 1 && ap_feed.qcount == 1);
    clear_feed();
    AP_UnlockReasonsConnectReset(1, 0);
    ap_feed_primed = 0;
    quiet(5);
    AP_UnlockReasonsResolveReconnect(1);
    ap_feed_primed = 1;
    quiet(30);
    show("second reconnect");
    assert(ap_feed.qcount == 0);

    // 5b. Same, but the room check answers after the feed is live again.
    fresh();
    bossOwn[2] = 1;
    ap_recv_count[AP_IDX_KEY] = 1; // opens Gem Stone Valley, not Glacier Park
    item();
    assert(lines("Komodo Joe") == 0);
    clear_feed();
    AP_UnlockReasonsConnectReset(1, 0);
    ap_feed_primed = 0;
    ap_recv_count[AP_IDX_KEY] = 2;
    quiet(5);
    ap_feed_primed = 1;
    quiet(10);
    assert(ap_feed.qcount == 0);
    AP_UnlockReasonsResolveReconnect(1);
    quiet(10);
    assert(lines("Komodo Joe GARAGE OPEN") == 1 && ap_feed.qcount == 1);

    // 5c. A different room (fresh server) never inherits the held garage.
    fresh();
    bossOwn[1] = 1;
    item();
    AP_UnlockReasonsConnectReset(1, 0);
    ap_feed_primed = 0;
    ap_recv_count[AP_IDX_KEY] = 1;
    quiet(5);
    AP_UnlockReasonsResolveReconnect(0);
    ap_feed_primed = 1;
    quiet(30);
    assert(ap_feed.qcount == 0);

    // Review round 1, finding 2: a garage line still waiting in the queue
    // when the connection drops survives the reconnect (held notice).
    fresh();
    for (int i = 0; i < 4; ++i)
        AP_FeedLinesEnqueue(&ap_feed, "FILLER", 0, 0);
    bossOwn[1] = 1;
    ap_recv_count[AP_IDX_KEY] = 1;
    item();
    assert(lines("Papu Papu GARAGE OPEN") == 1);
    {
        AP_FeedLines saved = ap_feed;
        AP_FeedLinesReconnect(&saved);   // what AP_FeedConnectReset keeps
        ap_feed = saved;
    }
    show("queued line after a drop");
    assert(lines("Papu Papu GARAGE OPEN") == 1 && lines("FILLER") == 0);

    // Review round 1, finding 3: a garage that closes and reopens (N. Oxide's
    // garage after the first challenge) is not announced again by a reconnect.
    fresh();
    ap_recv_count[AP_IDX_KEY] = 1;
    oxideOwn = 1;
    item();
    assert(lines("Oxide GARAGE OPEN") == 1);
    clear_feed();
    oxideOwn = 0;                 // first challenge won, final still locked
    quiet(5);
    AP_UnlockReasonsConnectReset(1, 0);
    ap_feed_primed = 0;
    quiet(5);
    AP_UnlockReasonsResolveReconnect(1);
    ap_feed_primed = 1;
    quiet(5);
    oxideOwn = 1;                 // the last Final Challenge relic
    item();
    assert(lines("Oxide GARAGE OPEN") == 0);

    // Review round 1, finding 4: a held garage whose own gate is briefly
    // shut after the reconnect (venue not ready yet) is still announced once
    // when the gate returns on a quiet frame.
    fresh();
    bossOwn[1] = 1;
    item();
    AP_UnlockReasonsConnectReset(1, 0);
    ap_feed_primed = 0;
    ap_recv_count[AP_IDX_KEY] = 1;
    bossOwn[1] = 0;               // venue not ready
    quiet(5);
    AP_UnlockReasonsResolveReconnect(1);
    ap_feed_primed = 1;
    quiet(10);
    clear_feed();
    bossOwn[1] = 1;               // venue ready, no item involved
    quiet(10);
    assert(lines("Papu Papu GARAGE OPEN") == 1);

    // ... and a second drop while the room check is pending keeps the hold.
    fresh();
    bossOwn[2] = 1;
    ap_recv_count[AP_IDX_KEY] = 1;
    item();
    clear_feed();
    AP_UnlockReasonsConnectReset(1, 0);
    ap_feed_primed = 1;           // live again before the room check answers
    quiet(5);
    AP_UnlockReasonsConnectReset(1, 1);
    ap_feed_primed = 0;
    ap_recv_count[AP_IDX_KEY] = 2;
    quiet(5);
    AP_UnlockReasonsResolveReconnect(1);
    ap_feed_primed = 1;
    quiet(10);
    assert(lines("Komodo Joe GARAGE OPEN") == 1);

    // Review round 2, finding 1: the feed is live again before the room
    // check answers, a garage opens, and the connection drops a second time
    // before the check. The garage is held again, not lost.
    fresh();
    bossOwn[1] = 1;
    item();
    clear_feed();
    AP_UnlockReasonsConnectReset(1, 0);
    ap_feed_primed = 1;
    quiet(5);
    ap_recv_count[AP_IDX_KEY] = 1;
    item();
    assert(lines("Papu Papu GARAGE OPEN") == 1);
    clear_feed();                 // the feed drops lines queued while pending
    AP_UnlockReasonsConnectReset(1, 1);
    ap_feed_primed = 0;
    quiet(5);
    AP_UnlockReasonsResolveReconnect(1);
    ap_feed_primed = 1;
    quiet(10);
    show("second drop while pending");
    assert(lines("Papu Papu GARAGE OPEN") == 1);

    // Review round 2, finding 2: N. Oxide's garage reopens while the room
    // check is pending. It stays quiet in the same room...
    fresh();
    ap_recv_count[AP_IDX_KEY] = 1;
    oxideOwn = 1;
    item();
    clear_feed();
    oxideOwn = 0;
    quiet(5);
    AP_UnlockReasonsConnectReset(1, 0);
    ap_feed_primed = 1;
    quiet(5);
    oxideOwn = 1;
    item();
    assert(lines("Oxide GARAGE OPEN") == 0);
    AP_UnlockReasonsResolveReconnect(1);
    quiet(10);
    assert(lines("Oxide GARAGE OPEN") == 0);
    // ... and is announced once when the check says it is another room.
    fresh();
    ap_recv_count[AP_IDX_KEY] = 1;
    oxideOwn = 1;
    item();
    clear_feed();
    oxideOwn = 0;
    quiet(5);
    AP_UnlockReasonsConnectReset(1, 0);
    ap_feed_primed = 1;
    quiet(5);
    oxideOwn = 1;
    item();
    assert(lines("Oxide GARAGE OPEN") == 0);
    AP_UnlockReasonsResolveReconnect(0);
    quiet(10);
    assert(lines("Oxide GARAGE OPEN") == 1);

    // 6. A reload (fresh client, nothing to resume) with an enterable garage
    //    adopts it silently: no repeat of a line shown in an earlier session.
    fresh();
    ap_recv_count[AP_IDX_KEY] = 3;
    memset(bossOwn, 1, sizeof bossOwn);
    oxideOwn = 1;
    AP_UnlockReasonsConnectReset(0, 0);
    ap_feed_primed = 0;
    quiet(5);
    ap_feed_primed = 1;
    quiet(30);
    assert(ap_feed.qcount == 0);

    // 7. Feed OFF: nothing shows, and turning it back on does not replay it.
    fresh();
    bossOwn[1] = 1;
    item();
    feedMode = AP_ITEM_FEED_OFF;
    ap_recv_count[AP_IDX_KEY] = 1;
    item();
    assert(ap_feed.qcount == 0);
    feedMode = AP_ITEM_FEED_ALL;
    quiet(30);
    item();
    assert(ap_feed.qcount == 0);

    // IMPORTANT mode keeps garage lines.
    fresh();
    feedMode = AP_ITEM_FEED_IMPORTANT;
    bossOwn[0] = 1;
    item();
    assert(lines("Ripper Roo GARAGE OPEN") == 1);
    feedMode = AP_ITEM_FEED_ALL;

    puts("PASS garage open notice waits for an enterable garage");
    return 0;
}
'''

def through_function(start_marker, signature):
    start = source.index(start_marker)
    end = source.index("\n}\n", source.index(signature, start)) + 3
    return source[start:end]


body = (region("static AP_UnlockWatch ap_unlock_watch;", "// What a pad leads to this seed")
        + through_function("// The hub a door leads to", "void AP_UnlockReasonsEndDrain(int drainedThisFrame)"))

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / "fixture.c"
    exe = Path(tmp) / "fixture"
    src.write_text(fixture + body + tests)
    subprocess.run(["cc", "-std=gnu11", "-fsanitize=undefined,address", "-I", str(root), str(src), "-o", str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
