"""Compile the production R2 decision and trap query with the real scheduler."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_traps.c").read_text()
start = source.index("int AP_TrapReverseCamera(")
query = source[start:source.index("\n}\n", start) + 3]
camera = (root / "game/CAM.c").read_text()
start = camera.index("\t// backup flags", camera.index("void CAM_FollowDriver_Normal("))
decision = camera[start:camera.index("\n\t// 0 = forwards", start)]
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include "ap_trap_items.h"
#include "ap_trap_observe_logic.h"
#define CTR_AP 1
#define ACTION_BOT 1
#define START_OF_RACE 2
#define CAMERA_FLAG_REVERSE 4
#define CAMERA_FLAG_DIRECTION_CHANGED 8
struct Driver { int actionsFlagSet; };
struct Camera { int flags; };
struct Pad { int buttonsHeldCurrFrame; };
struct Game { int gameMode1; };
static struct Driver local;
static AP_TrapSched sched;
static unsigned char g_active[AP_TRAP_EFFECT_COUNT];
static int g_trap_paused;
static int AP_TrapIsLocal(struct Driver *d) { return d == &local; }
QUERY
static int view(struct Driver *d, int buttons, int mode, int previous) {
 struct Camera c = {previous}, *cDC = &c;
 struct Pad p = {buttons}, *pad = &p;
 struct Game g = {mode}, *gGT = &g;
 int backupFlags;
 DECISION
 return c.flags;
}
static void step(AP_TrapWorld *w, int ms) {
 w->elapsedMs = ms;
 AP_TrapSchedStep(&sched,w);
 AP_TrapEvent event;
 while (AP_TrapSchedPopEvent(&sched,&event)) {}
 for(int e=0;e<AP_TRAP_EFFECT_COUNT;e++) g_active[e]=AP_TrapSchedActive(&sched,e);
 g_trap_paused=w->paused;
}
static int remaining(void) {
 for(int i=0;i<AP_TRAP_SCHED_CAP;i++)
  if(sched.slots[i].effect==AP_TRAP_REVERSE_CAMERA &&
     sched.slots[i].state==AP_TRAP_SLOT_ACTIVE) return sched.slots[i].remainMs;
 return 0;
}
int main(void) {
 struct Driver other={0};
 AP_TrapWorld w={0}; w.controlUnlocked=1;
 const int contexts[]={AP_TRAP_CTX_RACE,AP_TRAP_CTX_CUP_LEG,
  AP_TRAP_CTX_TIME_TRIAL,AP_TRAP_CTX_CTR_CHALLENGE};
 for(unsigned i=0;i<sizeof(contexts)/sizeof(*contexts);i++) {
  AP_TrapSchedReset(&sched); w.context=contexts[i];
  AP_TrapSchedReceive(&sched,AP_TRAP_REVERSE_CAMERA);
  step(&w,32); assert(!AP_TrapReverseCamera(&local));
  step(&w,999); assert(!AP_TrapReverseCamera(&local));
  step(&w,1); assert(AP_TrapReverseCamera(&local));
  assert(remaining()==15000);
  assert(view(&local,0,0,0)==(CAMERA_FLAG_REVERSE|CAMERA_FLAG_DIRECTION_CHANGED));
  assert(view(&local,0,0,CAMERA_FLAG_REVERSE)==CAMERA_FLAG_REVERSE);
  assert(!AP_TrapReverseCamera(&other)); assert(view(&other,0,0,0)==0);
  assert(view(&other,0x200,0,0)&CAMERA_FLAG_REVERSE); // retail R2 still works
  assert(!(view(&local,0,START_OF_RACE,0)&CAMERA_FLAG_REVERSE));
  w.paused=1; step(&w,5000); assert(remaining()==15000);
  assert(!(view(&local,0,0,CAMERA_FLAG_REVERSE)&CAMERA_FLAG_REVERSE));
  w.paused=0; w.scripted=1; step(&w,5000);
  assert(remaining()==15000 && !AP_TrapReverseCamera(&local));
  w.scripted=0; step(&w,1000); assert(remaining()==14000);
  AP_TrapSchedReceive(&sched,AP_TRAP_REVERSE_CAMERA); step(&w,0);
  assert(remaining()==15000);
  AP_TrapSchedReceive(&sched,AP_TRAP_MIRROR_MODE); step(&w,1000);
  assert(!AP_TrapSchedActive(&sched,AP_TRAP_MIRROR_MODE));
  step(&w,14000); assert(!AP_TrapReverseCamera(&local));
  step(&w,1000); assert(AP_TrapSchedActive(&sched,AP_TRAP_MIRROR_MODE));
 }
 const int excluded[]={0,AP_TRAP_CTX_HUB,AP_TRAP_CTX_BATTLE,AP_TRAP_CTX_CRYSTAL};
 for(unsigned i=0;i<sizeof(excluded)/sizeof(*excluded);i++) {
  AP_TrapSchedReset(&sched);w.context=excluded[i];
  AP_TrapSchedReceive(&sched,AP_TRAP_REVERSE_CAMERA);step(&w,60000);step(&w,60000);
  assert(!AP_TrapReverseCamera(&local));
  assert(AP_TrapSchedArmedCount(&sched,AP_TRAP_REVERSE_CAMERA)==1);
 }
 AP_TrapSchedReset(&sched);w.context=AP_TRAP_CTX_RACE;
 AP_TrapSchedSetDuration(&sched,0); // Full Race
 AP_TrapSchedReceive(&sched,AP_TRAP_REVERSE_CAMERA);step(&w,32);step(&w,1000);
 step(&w,1000000);assert(AP_TrapReverseCamera(&local));
 AP_TrapSchedMapChange(&sched);step(&w,0);assert(!AP_TrapReverseCamera(&local));
 AP_TrapSchedReceive(&sched,AP_TRAP_REVERSE_CAMERA);step(&w,32);step(&w,1000);
 assert(AP_TrapReverseCamera(&local));
 AP_TrapSchedReset(&sched);step(&w,0);assert(!AP_TrapReverseCamera(&local));
 assert(AP_TrapEffectForItemId(35010207)==AP_TRAP_REVERSE_CAMERA);
 puts("PASS: production rear-view decision and scheduler lifecycle");
}
'''.replace("QUERY", query).replace("DECISION", decision)
with tempfile.TemporaryDirectory(prefix="ctr-reverse-camera-") as tmp:
    src = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    src.write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-I", str(root / "ap"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
