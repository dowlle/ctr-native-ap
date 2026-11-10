#!/usr/bin/env python3
"""Compile the production receipt and trap feed entry points with host stubs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / 'ap/ap_hooks_feed.c').read_text()
net = (root / 'ap/ap_hooks_net.c').read_text()
held = net[net.index('if (AP_ArcadeHoldsRoom())'):]
held = held[:held.index('}') + 1]
assert 'AP_FeedLinesReceiptTick(&ap_feed);' in src[src.index('void AP_FeedEndDrain'):]
surface = src[src.index('static void AP_FeedTickAndDraw'):]
surface = surface[surface.index('if (playableRace != 0)'):]
begin = surface.index('{')
depth, end = 1, begin + 1
while depth:
    depth += (surface[end] == '{') - (surface[end] == '}')
    end += 1
surface = surface[:end]

def function(name):
    start = src.index('void ' + name + '(')
    begin = src.index('{', start)
    depth = 1
    end = begin + 1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[start:end]

stubs = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ap/ap_feed_lines_logic.h"
#include "ap/ap_item_flags.h"
#define AP_FEED_ITEM_CAP 32
#define AP_FEED_PLAYER_CAP 24
#define AP_FEED_FMT_OWN "%s"
#define AP_FEED_FMT_FOREIGN "%s FROM %s"
#define AP_FEED_ITEM_UNKNOWN "AP ITEM"
#define CORTEX_RED 7
static AP_FeedLines ap_feed;
static int active=1, ap_feed_primed=1;
static long long ap_feed_hwm=-1;
static int ctr_cfg_active(void) { return active; }
static int AP_FeedConsumeSelfRung(long long item) { (void)item;return 0; }
static int ap_net_self_slot(void) { return 1; }
static int ap_net_item_text(long long item,int player,char *a,int ac,char *b,int bc) {
    (void)item;(void)player;snprintf(a,ac,"WUMPA FRUIT");snprintf(b,bc,"OTHER");return 1;
}
static void AP_CeremonySanitize(const char *s,char *d,int n) { snprintf(d,n,"%s",s); }
static int AP_ItemDisplayAlias(long long item,char *d,int n) { (void)item;(void)d;(void)n;return 0; }
static int AP_CeremonyNameIsBlank(const char *s) { return !*s; }
static int AP_ClassFontColor(unsigned flags) { return AP_ItemFlagsClass(flags); }
static int AP_ArcadeHoldsRoom(void) { return 1; }
#define TEXT_SLOT_FEED 100
static int reset_count;
static void DecalFont_ResetLineBox(int slot) { assert(slot>=100 && slot<105);++reset_count; }
'''
tests = r'''
int main(void) {
    AP_FeedLinesReset(&ap_feed);
    ap_feed_primed=0;
    AP_FeedOnItemReceived(10,1,0,0);
    assert(!ap_feed.qcount);
    ap_feed_primed=1;
    AP_FeedOnItemReceived(10,1,0,0);
    assert(!ap_feed.qcount);
    AP_FeedOnItemReceived(10,1,1,0);
    AP_FeedOnItemReceived(10,1,2,0);
    assert(ap_feed.qcount==1 && !strcmp(ap_feed.queue[0].text,"2 WUMPA FRUIT"));
    AP_FeedOnItemReceived(10,1,3,AP_ITEM_FLAG_PROGRESSION);
    AP_FeedOnItemReceived(10,1,4,AP_ITEM_FLAG_PROGRESSION);
    AP_FeedOnItemReceived(10,1,5,AP_ITEM_FLAG_USEFUL);
    AP_FeedOnItemReceived(10,1,6,AP_ITEM_FLAG_USEFUL);
    assert(ap_feed.qcount==5);
    AP_FeedOnItemReceived(10,1,7,0);
    assert(ap_feed.qcount==6); // progression/useful broke the filler run
    AP_FeedOnItemReceived(10,2,8,0);
    AP_FeedOnItemReceived(10,2,9,0);
    assert(ap_feed.qcount==7 && strstr(ap_feed.queue[6].text,"2 WUMPA FRUIT FROM OTHER"));
    AP_FeedLinesReset(&ap_feed);
    AP_FeedTrapLine(13,0,1,AP_FEED_TRAP_ARMED,"NITRO DROP ARMED",1);
    assert(!ap_feed.qcount);
    AP_FeedTrapLine(13,0,1,AP_FEED_TRAP_ARMED,"NITRO DROP ARMED",0);
    AP_FeedTrapLine(13,1,2,AP_FEED_TRAP_ARMED,"NITRO DROP ARMED",0);
    assert(ap_feed.qcount==1 && !strcmp(ap_feed.queue[0].text,"2 NITRO DROP ARMED"));
    AP_FeedTrapLine(13,0,1,AP_FEED_TRAP_INCOMING,"NITRO DROP INCOMING",1);
    assert(ap_feed.qcount==1 && !strcmp(ap_feed.queue[0].text,"2 NITRO DROP INCOMING"));
    active=0;
    AP_FeedOnItemReceived(10,1,10,0);
    AP_FeedTrapLine(13,2,3,AP_FEED_TRAP_FIRED,"NITRO DROP",1);
    assert(ap_feed.qcount==1 && ap_feed.queue[0].copies==2);
    active=1;
    AP_FeedLinesReset(&ap_feed);
    AP_FeedOnItemReceived(10,1,20,0);
    for (int i=0;i<239;i++) held_frame();
    AP_FeedOnItemReceived(10,1,21,0);
    assert(ap_feed.qcount==1 && ap_feed.queue[0].copies==2);
    held_frame();
    AP_FeedOnItemReceived(10,1,22,0);
    assert(ap_feed.qcount==2 && ap_feed.queue[1].copies==1);
    AP_FeedLinesReset(&ap_feed);
    AP_FeedTrapLine(13,0,1,AP_FEED_TRAP_ARMED,"FORCED USF ARMED",0);
    AP_FeedLinesPromote(&ap_feed);
    AP_FeedLinesReceipt(&ap_feed,10,1,1,"KEEP",3);
    ap_feed.stagger=0;AP_FeedLinesPromote(&ap_feed);
    AP_FeedTrapLine(14,1,2,AP_FEED_TRAP_ARMED,"OTHER ARMED",0);
    race_surface(0);
    assert(ap_feed.vcount==2 && ap_feed.qcount==1 && reset_count==0);
    race_surface(1);
    assert(ap_feed.vcount==1 && ap_feed.qcount==0 && !strcmp(ap_feed.vis[0].text,"KEEP"));
    assert(reset_count==5);
    AP_FeedTrapLine(13,0,3,AP_FEED_TRAP_ARMED,"FORCED USF ARMED",0);
    assert(ap_feed.qcount==1);
    race_surface(2); // results can be the first non-hub surface drawn
    assert(ap_feed.vcount==1 && ap_feed.qcount==0 && reset_count==5);
    puts("production receipt flags, replay, option off, sender and race/hub trap entry points PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path/'test.c').write_text(stubs + function('AP_FeedOnItemReceived') +
                              function('AP_FeedTrapLine') +
                              'static void held_frame(void) { ' + held + ' }' +
                              'static void race_surface(int playableRace) { ' + surface + ' }' + tests)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-I',str(root),
                    str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
