"""Production hub-ready gate: tunnel triggers and background loader ownership."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[1]
source = (root / 'ap/ap_charswap.c').read_text()
signature = 'static int ap_cs_hubReady(struct GameTracker *gGT)'
start = source.index(signature + '\n{')
end = source.index('\n}\n', source.index('static int ap_cs_hubTransitionIdle(')) + 3 if 'static int ap_cs_hubTransitionIdle(' in source else source.index('\n}\n', start) + 3
fixture = r'''
#include <assert.h>
#include <stdio.h>
#define ADVENTURE_ARENA 1
#define LOAD_IDLE -1
#define GEM_STONE_VALLEY 25
#define CITADEL_CITY 29
#define COLL_STEP_TRIGGER_HUB_LEVEL_ID_MASK 0x30
#define COLL_STEP_TRIGGER_HUB_SWAP_NOW_MASK 0xc0
typedef short s16;
struct Driver { unsigned stepFlagSet; struct { int x,y,z; } posCurr; struct { short y; } rotCurr; };
struct GameTracker { int gameMode1; struct Driver *drivers[1]; int bool_AdvHub_NeedToSwapLEV; int levelID; int activeMempackIndex,levID_in_each_mempack[3]; void *level2; };
struct State { int queueLength, queueReady, load_inProgress; struct { int stage; } Loading; struct { short characterID; } advProgress; void *ptrBigfile1; } state;
struct { short characterIDs[1]; } data;
static int ap_cs_savedPos[3],ap_cs_savedRotY,ap_cs_savedLevel,ap_cs_fromCharacter;
static int ap_cs_open,ap_cs_editFocus,ap_cs_pendingSwap,ap_cs_restorePos;
static int ap_cs_savedNeighborHub=-1;
static int persisted,requested,released;
static int preload,teleported;
static struct GameTracker *live;
void LOAD_Hub_ReadFile(void *file,int level,int pack) {
 assert(file && pack==3-live->activeMempackIndex);
 preload++; live->levID_in_each_mempack[pack]=level; live->level2=0;
 state.queueLength=3;
}
void VehBirth_TeleportSelf(struct Driver *d,int flag,int y) { teleported++; }
void AP_CharSwap_ApplyStatPackage(struct Driver *d) {}
void ap_cs_persistCharacter(int c) { persisted++; }
void AP_LogLine(const char *s) { }
void ap_cs_logPackage(const char *s,struct Driver *d,int c) { }
void ap_cs_setFreeze(struct GameTracker *g,int hold) { released++; }
void MainRaceTrack_RequestLoad(int level) { requested++; }
#define sdata (&state)
int LOAD_IsOpen_AdvHub(void) { return 1; }
'''
tests = r'''
int main(void) {
 struct Driver d={0}; struct GameTracker gt={ADVENTURE_ARENA,{&d},0,26};
 state.Loading.stage=LOAD_IDLE;
 state.queueReady=1;
 assert(ap_cs_hubReady(&gt));
 d.stepFlagSet=COLL_STEP_TRIGGER_HUB_SWAP_NOW_MASK;
 assert(!ap_cs_hubReady(&gt)); /* Restored swap-only tunnel, no preload queue. */
 d.stepFlagSet=COLL_STEP_TRIGGER_HUB_LEVEL_ID_MASK;
 assert(!ap_cs_hubReady(&gt));
 d.stepFlagSet=0; gt.bool_AdvHub_NeedToSwapLEV=1;
 assert(!ap_cs_hubReady(&gt));
 gt.bool_AdvHub_NeedToSwapLEV=0; state.queueLength=1;
 assert(!ap_cs_hubReady(&gt));
 state.queueLength=0; state.load_inProgress=1;
 assert(!ap_cs_hubReady(&gt)); /* Final file removed from queue, callback pending. */
 state.load_inProgress=0;
 state.queueReady=0;
 assert(!ap_cs_hubReady(&gt)); /* Final queued dispatch awaiting callback. */
 assert(ENGINE_HUB_READY(&gt)); /* Completion semantics must not change. */
 state.queueReady=1;
 assert(ap_cs_hubReady(&gt));
 state.Loading.stage=0; assert(!ap_cs_hubReady(&gt));
 state.Loading.stage=LOAD_IDLE; data.characterIDs[0]=13;
 d.stepFlagSet=COLL_STEP_TRIGGER_HUB_SWAP_NOW_MASK;
 ap_cs_requestSwap(&gt,7);
 assert(data.characterIDs[0]==13 && !persisted && !requested && !released);
 assert(!ap_cs_pendingSwap && !ap_cs_restorePos);
 d.stepFlagSet=0; ap_cs_requestSwap(&gt,7);
 assert(data.characterIDs[0]==7 && persisted==1 && requested==1 && released==1);
 assert(ap_cs_pendingSwap && ap_cs_restorePos);
 /* Neighbor was fully loaded at admission: no trigger and no queue. */
 live=&gt; gt.activeMempackIndex=1; gt.levID_in_each_mempack[2]=25;
 gt.level2=&gt; state.ptrBigfile1=&state;
 ap_cs_requestSwap(&gt,13); assert(ap_cs_savedNeighborHub==25);
 /* Real full reload invalidates neighbor and may flip active pack. */
 gt.activeMempackIndex=2; gt.levID_in_each_mempack[1]=-1; gt.level2=0;
 ap_cs_completeSwap(&gt);
 assert(preload==1 && !teleported && ap_cs_restorePos);
 assert(gt.levID_in_each_mempack[1]==25 && state.queueLength==3);
 ap_cs_completeSwap(&gt); assert(preload==1 && !teleported);
 state.queueLength=0; state.queueReady=0;
 ap_cs_completeSwap(&gt); assert(!teleported); /* final callback pending */
 gt.level2=&gt; state.queueReady=1;
 ap_cs_completeSwap(&gt);
 assert(teleported==1 && !ap_cs_restorePos && ap_cs_savedNeighborHub==-1);
 /* No neighbor: ordinary safe-hub swap does not preload anything. */
 ap_cs_savedLevel=26; ap_cs_restorePos=1; gt.level2=0;
 ap_cs_completeSwap(&gt); assert(preload==1 && teleported==2);
 /* Interrupted reload to a different hub must not restore or preload. */
 ap_cs_savedNeighborHub=25; ap_cs_restorePos=1; gt.levelID=28;
 ap_cs_completeSwap(&gt); assert(preload==1 && teleported==2);
 puts("Production tunnel/queue readiness regressions passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp)/'probe.c', Path(tmp)/'probe'
    probe = tests.replace('ap_cs_hubReady', 'ap_cs_hubTransitionIdle') if 'static int ap_cs_hubTransitionIdle(' in source else tests
    probe = probe.replace('ENGINE_HUB_READY', 'ap_cs_hubReady')
    commit_start=source.index('static void ap_cs_requestSwap(')
    commit_end=source.index('\n}\n',commit_start)+3
    neighbor_start=source.index('static int ap_cs_neighborHub(')
    neighbor_end=source.index('static void ap_cs_requestSwap(',neighbor_start)
    complete_source=Path(sys.argv[1]).read_text() if len(sys.argv)>1 else source
    complete_start=complete_source.index('static void ap_cs_completeSwap(')
    complete_end=complete_source.index('\n}\n',complete_start)+3
    src.write_text(fixture + source[start:end] + source[neighbor_start:neighbor_end] + source[commit_start:commit_end] + complete_source[complete_start:complete_end] + probe)
    subprocess.run(['cc', '-std=c11', '-fsanitize=undefined', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
