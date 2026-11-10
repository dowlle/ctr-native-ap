// cc -std=c99 -Wall -Wextra -I. tools/test-calmer-feed.c -o /tmp/test-calmer-feed && /tmp/test-calmer-feed
#include <assert.h>
#include <stdio.h>
#include "ap/ap_feed_lines_logic.h"
#include "ap/ap_trap_sched_logic.h"

static AP_FeedLines f;
static void trap(int effect, int slot, unsigned seq, int stage, const char *text, int race)
{
    AP_FeedLinesTrapGrouped(&f, effect, slot, seq, stage, text, 7, race);
}
static void pump(AP_TrapSched *s)
{
    AP_TrapEvent ev;
    while (AP_TrapSchedPopEvent(s, &ev)) {
        int stage = ev.kind == AP_TRAP_EV_ARMED ? AP_FEED_TRAP_ARMED :
                    ev.kind == AP_TRAP_EV_WARN ? AP_FEED_TRAP_INCOMING :
                    (ev.kind == AP_TRAP_EV_FIRE || ev.kind == AP_TRAP_EV_REFRESH) ? AP_FEED_TRAP_FIRED : -1;
        if (stage >= 0) trap(ev.effect, ev.slot, ev.seq, stage, "FORCED BOOST", 1);
    }
}
static void refreshing_copies(void)
{
    AP_TrapSched s;
    AP_TrapWorld race = {0};
    race.context = AP_TRAP_CTX_RACE;
    race.controlUnlocked = 1;
    race.elapsedMs = 100;
    race.mapEpoch = 1;
    AP_FeedLinesReset(&f);
    AP_TrapSchedReset(&s);
    AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
    AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
    for (int i=0;i<15;i++) { AP_TrapSchedStep(&s,&race);pump(&s); }
    assert(f.qcount==1 && f.queue[0].copies==2);
    AP_TrapSchedReceive(&s, AP_TRAP_BOOST); // refreshes without ARMED or WARN
    AP_TrapSchedStep(&s,&race);pump(&s);
    assert(f.qcount==1 && f.queue[0].copies==3);
    AP_TrapSchedMapChange(&s);pump(&s);
    AP_TrapSchedReceive(&s, AP_TRAP_BOOST);
    for (int i=0;i<15;i++) { AP_TrapSchedStep(&s,&race);pump(&s); }
    assert(f.qcount==1 && f.queue[0].copies==4);
}
int main(void)
{
    int i;
    refreshing_copies();
    assert(AP_TRAP_SCHED_CAP == 32);
    AP_FeedLinesReset(&f);
    trap(1, 0, 1, AP_FEED_TRAP_ARMED, "NITRO DROP ARMED", 1);
    trap(1, 1, 2, AP_FEED_TRAP_ARMED, "NITRO DROP ARMED", 1);
    assert(f.qcount == 0);
    trap(1, 0, 1, AP_FEED_TRAP_INCOMING, "NITRO DROP INCOMING", 1);
    assert(f.qcount == 1 && !strcmp(f.queue[0].text, "2 NITRO DROP INCOMING"));
    AP_FeedLinesPromote(&f);
    trap(1, 1, 2, AP_FEED_TRAP_FIRED, "NITRO DROP", 1);
    trap(1, 0, 1, AP_FEED_TRAP_FIRED, "NITRO DROP", 1);
    assert(f.vcount == 1 && !strcmp(f.vis[0].text, "2 NITRO DROP"));
    trap(1, 2, 3, AP_FEED_TRAP_ARMED, "NITRO DROP ARMED", 1);
    assert(!strcmp(f.vis[0].text, "3 NITRO DROP"));
    trap(2, 0, 4, AP_FEED_TRAP_ARMED, "FLATTEN ARMED", 0);
    assert(f.qcount == 1 && !strcmp(f.queue[f.qhead].text, "FLATTEN ARMED"));
    trap(1, 0, 5, AP_FEED_TRAP_INCOMING, "NITRO DROP INCOMING", 1);
    assert(!strcmp(f.vis[0].text, "4 NITRO DROP INCOMING"));
    for (i = 0; i < 240; ++i) AP_FeedLinesAge(&f);
    trap(1, 0, 5, AP_FEED_TRAP_FIRED, "NITRO DROP", 1);
    assert(f.qcount == 2 && f.queue[(f.qhead + 1) % 24].copies == 1);

    AP_FeedLinesReset(&f);
    AP_FeedLinesReceipt(&f, 10, 1, 1, "WUMPA FRUIT", 3);
    AP_FeedLinesPromote(&f);
    for (i = 0; i < 239; ++i) AP_FeedLinesReceiptTick(&f);
    AP_FeedLinesReceipt(&f, 10, 1, 1, "WUMPA FRUIT", 3);
    assert(f.qcount == 0 && !strcmp(f.vis[0].text, "2 WUMPA FRUIT"));
    AP_FeedLinesReceiptTick(&f);
    AP_FeedLinesReceipt(&f, 10, 1, 1, "WUMPA FRUIT", 3);
    assert(f.qcount == 1); // fixed window does not slide on the second receipt
    AP_FeedLinesReceipt(&f, 10, 2, 1, "WUMPA FRUIT FROM OTHER", 3);
    AP_FeedLinesReceipt(&f, 10, 1, 1, "WUMPA FRUIT", 3);
    assert(f.qcount == 3); // sender change breaks consecutive run
    AP_FeedLinesReceipt(&f, 11, 1, 0, "GREEN GEM", 1);
    AP_FeedLinesReceipt(&f, 11, 1, 0, "GREEN GEM", 1);
    AP_FeedLinesReceipt(&f, 12, 1, 0, "USEFUL ITEM", 2);
    AP_FeedLinesReceipt(&f, 12, 1, 0, "USEFUL ITEM", 2);
    assert(f.qcount == 7);
    AP_FeedLinesReceipt(&f, 10, 1, 1, "WUMPA FRUIT", 3);
    AP_FeedLinesReceipt(&f, 10, 1, 1, "WUMPA FRUIT", 3);
    assert(f.qcount == 8 && f.queue[(f.qhead + 7) % 24].copies == 2);
    AP_FeedLinesReset(&f);
    assert(f.receiptWindow == 0 && f.qcount == 0);
    puts("calmer feed: stacking, race/hub, identity reuse, expiry, filler window and classes PASS");
    return 0;
}
