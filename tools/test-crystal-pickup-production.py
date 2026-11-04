"""Compile the production crystal dispatch with isolated game/network state."""
from pathlib import Path
import subprocess
import tempfile
import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
source = ap_hooks_unit.read()
start = source.index('void AP_NotifyCrystalPickup(struct Instance *inst, int driverID)\n{')
body = source[start:source.index('\n}\n', start) + 3]
modes = (root/'include/namespace_Main.h').read_text()
mode_start = modes.index('enum GameMode1\n')
mode_enum = modes[mode_start:modes.index('};', mode_start)+2]
fixture = r'''
#include <assert.h>
#include <stddef.h>
#include "ap_crystal_checks.h"
#define CTR_CUSTOM_TRACKS 1
struct InstDef { int unused; };
struct Level { struct InstDef *ptrInstDefs; int numInstances; };
struct Instance { struct InstDef *instDef; };
struct GameTracker { unsigned gameMode1; struct Level *level1; int levelID, numPlyrCurrGame; struct { int cupID; } cup; };
struct Small { struct GameTracker *gGT; } data, *sdata=&data;
AP_CrystalChecks ctr_crystal_checks;
static int active=1, custom=0, exists=1, checked=0, sent, feed, perf, ap_state_gen;
static long sentCode;
int ctr_cfg_active(void){return active;}
int ap_content_plan_present(void){return 0;}
int AP_ContentCrystalIntent(void){return 0;}
void AP_NotifyContentCrystalPickup(struct Instance *inst,int driverID){(void)inst;(void)driverID;}
int CustomTrack_ServingLoad(int level,int cup,int id){(void)level;(void)cup;(void)id;return custom;}
int ap_net_location_exists(long code){(void)code;return exists;}
int ap_net_location_checked(long code){(void)code;return checked;}
void ap_net_send_location(long code){sent++;sentCode=code;checked=1;}
void AP_PerfNoteCheckSent(void){perf++;}
void AP_FeedOnLocationSent(long code){assert(code==sentCode);feed++;}
'''
tests = r'''
int main(void){
 struct InstDef defs[80],foreign;
 struct Level lev={defs,80};
 struct GameTracker gt={ADVENTURE_MODE|CRYSTAL_CHALLENGE,&lev,21,1};
 struct Instance inst={&defs[8]};
 int levels[]={21,19,23,18},a,p,n;
 sdata->gGT=&gt;ctr_crystal_checks.state=1;
 for(a=0;a<4;a++){
  ctr_crystal_checks.present[a]=1;
  for(n=0;n<20;n++)ctr_crystal_checks.locations[a][n]=35028400+20*a+n;
 }
 for(a=0;a<4;a++)for(p=1;p<=4;p++)for(n=0;n<20;n++){
  int before=sent;
  gt.levelID=levels[a];gt.numPlyrCurrGame=p;
  inst.instDef=&defs[AP_CrystalInstanceBase(a,p)+n];checked=0;
  AP_NotifyCrystalPickup(&inst,0);
  assert(sent==before+1&&sentCode==35028400+20*a+n);
  AP_NotifyCrystalPickup(&inst,0);assert(sent==before+1);
 }
 assert(sent==320&&feed==sent&&perf==sent&&ap_state_gen==sent);
 gt.levelID=21;gt.numPlyrCurrGame=1;inst.instDef=&defs[8];checked=0;
 #define NO_SEND(statement) do { int before=sent;statement;AP_NotifyCrystalPickup(&inst,0);assert(sent==before); } while(0)
 NO_SEND(active=0);active=1;
 NO_SEND(custom=1);custom=0;
 NO_SEND(exists=0);exists=1;
 NO_SEND(checked=1);checked=0;
 NO_SEND(ctr_crystal_checks.state=-1);ctr_crystal_checks.state=1;
 NO_SEND(ctr_crystal_checks.present[0]=0);ctr_crystal_checks.present[0]=1;
 NO_SEND(gt.gameMode1=ADVENTURE_MODE|ADVENTURE_ARENA); /* hub is not challenge */
 NO_SEND(gt.gameMode1=CRYSTAL_CHALLENGE); /* no AP checks outside Adventure */
 gt.gameMode1=ADVENTURE_MODE|CRYSTAL_CHALLENGE;
 NO_SEND(gt.levelID=20);gt.levelID=21;
 NO_SEND(gt.numPlyrCurrGame=0);gt.numPlyrCurrGame=1;
 NO_SEND(inst.instDef=&foreign);
 NO_SEND(inst.instDef=NULL);inst.instDef=&defs[8];
 NO_SEND(gt.level1=NULL);gt.level1=&lev;
 NO_SEND(lev.ptrInstDefs=NULL);lev.ptrInstDefs=defs;
 NO_SEND(sdata->gGT=NULL);sdata->gGT=&gt;
 {int before=sent;AP_NotifyCrystalPickup(&inst,1);AP_NotifyCrystalPickup(NULL,0);assert(sent==before);}
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp)/'pickup.c', Path(tmp)/'pickup'
    src.write_text(mode_enum+fixture+body+tests)
    subprocess.run(['cc','-std=c11','-fsanitize=undefined','-I',str(root/'ap'),
                    '-I',str(root/'include'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

# Dispatch precedes the deferred HUD counter and belongs to the player branch.
collision = (root/'game/231/RB_Crystal.c').read_text()
branch = collision.index('if (modelID == DYNAMIC_PLAYER)')
hook = collision.index('AP_NotifyCrystalPickup(crystalInst, driver->driverID);')
hud = collision.index('driver->PickupWumpaHUD.numCollected++;')
assert branch < hook < hud
print('Crystal pickup production dispatch passed (320 authored identities + guards)')
