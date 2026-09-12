"""Compile actual network door functions, game gates and late/birth pose helper.

Server ordering: MultiServer Connected reply includes initial ReceivedItems;
the subsequent Get response is a barrier, including for empty inventory.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
def extract(path, signature):
    source = (root / path).read_text()
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end]

# Single-line functions must not use the brace-delimited extractor.
net_source = (root/'ap/ap_net.cpp').read_text()
net = extract('ap/ap_net.cpp', 'static void ap_doors_flush()\n{')
net += extract('ap/ap_net.cpp', 'extern "C" int ap_net_doors_ready(void)')
for name in ('history', 'session'):
    net += next(line for line in net_source.splitlines(True)
                if line.startswith('extern "C" unsigned ap_net_doors_' + name + '('))
net += extract('ap/ap_net.cpp', 'extern "C" void ap_net_doors_record(unsigned bit)')
fixture = r'''
#include <cassert>
#include <deque>
#include <cstdio>
#include "ap_door_history.h"
void AP_LogLine(const char*) {}
#define AP_NET_GUARD(label, ...) __VA_ARGS__
struct APClient {
 struct DataStorageOperation {std::string operation; nlohmann::json value;};
 unsigned sent=0; int calls=0; bool success=true;
 bool Set(const std::string&,int d,bool reply,std::initializer_list<DataStorageOperation> ops) {
  assert(d==0 && reply); assert(ops.begin()->operation=="or");
  calls++; sent=ops.begin()->value.get<unsigned>(); return success;
 }
};
static APDoorHistory g_doors;
static unsigned g_doors_sent;
static APClient client, *g_ap=&client;
static bool g_connected=true,g_recv_reset=false;
static struct {int schema_newer=0;} ctr_cfg;
static int configActive=1;
int ctr_cfg_active(){return configActive;}
static std::deque<int> g_items;
'''
tests = r'''
int main() {
 g_doors.connect("host","seed:a",0,1);
 assert(!ap_net_doors_ready());
 ap_net_doors_record(1); assert(g_doors.pending==1 && client.calls==0);
 g_doors.retrieved(nullptr); g_items.push_back(1);
 assert(!ap_net_doors_ready()); g_items.clear();
 g_recv_reset=true; assert(!ap_net_doors_ready()); g_recv_reset=false;
 assert(ap_net_doors_ready()); ap_doors_flush(); assert(client.calls==1 && client.sent==1);
 ap_doors_flush(); assert(client.calls==1);
 ap_net_doors_record(2); assert(client.calls==2 && client.sent==2);
 g_doors.reply(1); g_doors_sent &= g_doors.pending;
 assert(g_doors.pending==2 && ap_net_doors_history()==3);
 /* Disconnect before second ack: only pending bits retry in same room. */
 g_connected=false; g_doors.disconnected(); g_doors_sent=0;
 assert(!ap_net_doors_ready());
 ap_net_doors_record(8); assert(g_doors.pending==10);
 g_connected=true; g_doors.connect("host","seed:a",0,1);
 g_doors.retrieved(1); ap_doors_flush(); assert(client.sent==10);
 g_doors.reply(11); g_doors_sent &= g_doors.pending;
 assert(!g_doors.pending && ap_net_doors_history()==11);
 assert(ap_net_doors_session()==0); /* Replay did not count as completion. */
 g_doors.session=11; g_connected=false; g_doors.disconnected();
 assert(ap_net_doors_session()==11); /* Acknowledged doors survive a drop. */
 g_connected=true; g_doors.connect("host","seed:a",0,1);
 assert(!ap_net_doors_session() && !ap_net_doors_ready());
 g_doors.record(4); g_doors.connect("host","seed:a",0,1);
 assert(g_doors.pending==4 && !ap_net_doors_session());
 /* Delayed initial receipt before Get: pending is not session history. */
 g_items.push_back(1); assert(!ap_net_doors_ready() && !ap_net_doors_session());
 g_items.clear(); assert(!ap_net_doors_ready() && !ap_net_doors_session());
 g_doors.retrieved(11); assert(ap_net_doors_ready());
 /* Stale Retrieved must not regress acknowledged history. */
 g_doors.retrieved(1); assert(ap_net_doors_history()==15);
 for (auto v : {nlohmann::json(-1),nlohmann::json(16),nlohmann::json(1.5),
                nlohmann::json(true),nlohmann::json("3"),nlohmann::json::array(),
                nlohmann::json(18446744073709551615ULL)}) {
  g_doors.retrieved(v); assert(!ap_net_doors_ready());
 }
 g_doors.retrieved(11); assert(ap_net_doors_ready());
 /* Failed Set retains pending and leaves it eligible for the next poll. */
 g_doors.reply(15); g_doors_sent &= g_doors.pending;
 assert(!g_doors.pending);
 g_doors.connect("host","failure-room",0,1); g_doors_sent=0;
 g_doors.retrieved(nullptr); client.success=false;
 ap_net_doors_record(2); assert(g_doors.pending==2 && !g_doors_sent);
 client.success=true; ap_doors_flush(); assert(g_doors_sent==2);
 ctr_cfg.schema_newer=1; ap_net_doors_record(8);
 assert(g_doors.pending==10 && g_doors_sent==2);
 ctr_cfg.schema_newer=0; configActive=0; ap_doors_flush(); assert(g_doors_sent==2);
 configActive=1; ap_doors_flush(); assert(g_doors_sent==10);
 /* Another client ORs a different bit before our acknowledgement. */
 g_doors.reply(14); g_doors_sent &= g_doors.pending;
 assert(g_doors.history==14 && !g_doors.pending && !g_doors_sent);
 auto key=g_doors.key;
 g_doors.connect("host","seed:a",1,1); assert(g_doors.key!=key && !g_doors.history);
 g_doors.record(1); g_doors.connect("host","seed:b",1,1); assert(!g_doors.pending);
 g_doors.record(1); g_doors.connect("other","seed:b",1,1); assert(!g_doors.pending);
 g_doors.record(1); g_doors.connect("other","seed:b",1,2); assert(!g_doors.pending);
 /* Ambiguous concatenations cannot collide. */
 assert(APDoorHistory::part("ab")+APDoorHistory::part("c") !=
        APDoorHistory::part("a")+APDoorHistory::part("bc"));
 assert(AP_DoorBitPure(26,4)==AP_DoorBitPure(27,0));
 assert(AP_DoorStoryPure(15)==0x1f0);
 assert(!AP_DoorBitPure(29,0) && !AP_DoorBitPure(26,3));
}
'''

gates = ''.join(extract('ap/ap_hooks.c', signature) for signature in (
    'int AP_DoorHistoryEnabled(void)', 'int AP_DoorHistoryReady(void)', 'int AP_DoorHistoryOpen(int level, int door)',
    'void AP_DoorHistoryRecord(int level, int door)',
    'void AP_DoorHistoryReconcile(int level, int door)'))
pose = extract('game/232/AH_Door.c', 'static void AH_Door_ApplyHistoryPose(')
game_fixture = r'''
#include <cassert>
#include "ap_door_history.h"
struct {int schema_newer;} ctr_cfg;
static int active=1,ready=0,keys=0;
static unsigned history=0,session=0;
int ctr_cfg_active(){return active;}
int ap_net_doors_ready(){return ready;}
unsigned ap_net_doors_history(){return history|session;}
unsigned ap_net_doors_session(){return session;}
void ap_net_doors_record(unsigned bit){session|=bit;}
void ap_net_doors_mark_session(unsigned bit){session|=bit;}
#define AP_IDX_KEY 1
int AP_GateCount(int){return keys;}
struct SVec3 {int x,y,z;};
struct Matrix {int angle;};
struct Def {SVec3 rot;};
struct Instance {Matrix matrix; Def *instDef;};
struct WoodDoor {Instance *otherDoor; unsigned camFlags; SVec3 doorRot; int doorID;};
struct GT {int levelID;};
struct State {GT *gGT; struct {unsigned storyFlags;} advProgress;};
static GT gt={26}; static State state={&gt,{0x800001f0}}; static State *sdata=&state;
#define WdCam_CutscenePlaying 1
static int poseCalls=0;
void ConvertRotToMatrix(Matrix *m,SVec3 *r){poseCalls++;m->angle=r->y;}
'''
game_tests = r'''
int main(){
 Def def={{0,100,0}}; Instance left={{100},&def},right={{100},&def};
 WoodDoor door={&right,0,{0,0,0},4};
 /* Cold saved flags cannot open or be imported; unrelated flags survive. */
 AH_Door_ApplyHistoryPose(&left,&door);
 assert(!door.doorRot.y && state.advProgress.storyFlags==0x80000130);
 history=2; keys=2; AH_Door_ApplyHistoryPose(&left,&door); assert(!door.doorRot.y);
 ready=1; keys=1; AH_Door_ApplyHistoryPose(&left,&door); assert(!door.doorRot.y);
 keys=2; AH_Door_ApplyHistoryPose(&left,&door);
 assert(door.doorRot.y==0x400 && left.matrix.angle==1124 && right.matrix.angle==-924);
 auto calls=poseCalls; AH_Door_ApplyHistoryPose(&left,&door); assert(poseCalls==calls);
 assert((state.advProgress.storyFlags&0xc0)==0xc0);
 /* Shared opposite door restores, but 3-Key and cup gates stay distinct. */
 assert(AP_DoorHistoryOpen(27,0)); assert(!AP_DoorHistoryOpen(28,0));
 history=8; assert(AP_DoorHistoryOpen(25,0)); keys=1; assert(!AP_DoorHistoryOpen(25,0));
 /* Completion before Get still creates session history. */
 session=0; ready=0; keys=2; AP_DoorHistoryRecord(26,4); assert(session==2);
 assert(AP_DoorHistoryOpen(26,4));
 /* Reconnect clears session, pending storage is unavailable during partial sync. */
 session=0; ready=0; history=15; keys=2;
 assert(!AP_DoorHistoryOpen(26,4) && !AP_DoorHistoryOpen(25,0));
 /* Malformed response keeps ready false; cached server history cannot restore. */
 assert(!AP_DoorHistoryOpen(26,5));
 session=0; door.camFlags=1; history=0;
 AH_Door_ApplyHistoryPose(&left,&door); assert(door.doorRot.y==0x400);
 door.camFlags=0; AH_Door_ApplyHistoryPose(&left,&door); assert(!door.doorRot.y);
 ctr_cfg.schema_newer=1; AP_DoorHistoryRecord(26,4); assert(!session);
 active=0; auto flags=state.advProgress.storyFlags;
 AP_DoorHistoryReconcile(26,4); assert(flags==state.advProgress.storyFlags);
}
'''
# Wiring assertions complement extracted production behavior.
door_source=(root/'game/232/AH_Door.c').read_text()
assert door_source.count('AH_Door_ApplyHistoryPose(')==3
assert door_source.index('AP_DoorHistoryRecord(lev, doorID);') > door_source.index('// == Door is fully open ==')
assert 'return AP_DoorHistoryOpen(levelID, doorID);' in door_source
with tempfile.TemporaryDirectory() as tmp:
    for name, code in [('net',fixture+net+tests),('game',game_fixture+gates+pose+game_tests)]:
        src=Path(tmp)/(name+'.cpp'); exe=Path(tmp)/name
        src.write_text(code)
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-fsanitize=undefined',
                        '-I',str(root/'ap'),'-I',str(root/'ap/vendor/json/include'),
                        str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
print('Production door network/gates/pose regressions passed')
