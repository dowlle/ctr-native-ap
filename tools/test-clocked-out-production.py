"""Execute the production Clocked Out effect with real scheduler and engine seams."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'ap/ap_traps.c').read_text()
start=source.index('static int AP_TrapHeldItemResolved(')
held=source[start:source.index('\n}\n',start)+3]
fixture=r"""
#include <assert.h>
#include <stdio.h>
#include "ap_trap_sched_logic.h"
#include "ap_trap_observe_logic.h"
#define LOAD_IDLE 0
#define PAUSE_ALL 1
#define ACTION_MASK_WEAPON 2
struct Driver {int kartState,squishTimer,invincibleTimer,actionsFlagSet,pendingDamageType;
 int clockReceive,clockFlash,numWumpas,heldItemID,itemRollTimer,noItemTimer;void *instBubbleHold;};
struct GameTracker {unsigned gameMode1;};
static struct {struct {int stage;}Loading;} data,*sdata=&data;
static struct Driver *local;
static int context=AP_TRAP_CTX_RACE,control=1,scripted,finished,suppress,hits,sounds,accept=1;
static AP_TrapSched g_sched;
static int AP_TrapIsLocal(struct Driver *d){return d==local;}
static int AP_TrapContextOf(struct GameTracker *g,int load){(void)g;return load?0:context;}
static int AP_TrapControlUnlocked(struct GameTracker *g,struct Driver *d){(void)g;(void)d;return control;}
static int AP_TrapScripted(struct GameTracker *g,struct Driver *d){(void)g;(void)d;return scripted;}
static int AP_TrapFinishOrPodium(struct GameTracker *g,struct Driver *d){(void)g;(void)d;return finished;}
static void AP_DeathLinkSuppressSelfInflicted(int on){assert(on!=suppress);suppress=on;}
static int RB_Hazard_HurtDriver(struct Driver *d,int damage,void *attacker,int reason){
 assert(d==local&&damage==1&&attacker==NULL&&reason==0&&suppress);hits++;return accept;
}
static void OtherFX_Play(int sound,int once){assert(sound==0x44&&once==1);sounds++;}
#include "ap_trap_clocked_out_runtime.h"
HELD_FUNCTION
static void reset(struct Driver *d,struct GameTracker *g){
 AP_TrapClockClear(&g_trap_clocked);AP_TrapSchedReset(&g_sched);
 *d=(struct Driver){0};d->kartState=AP_TRAP_KS_NORMAL;local=d;g->gameMode1=0;
 context=AP_TRAP_CTX_RACE;control=accept=1;scripted=finished=sdata->Loading.stage=0;
}
static void step(struct GameTracker *g,int ms){
 AP_TrapWorld w={0};AP_TrapEvent e;
 AP_TrapPollClockedOut(g,local);
 w.context=context;w.controlUnlocked=control;w.paused=(g->gameMode1&PAUSE_ALL)!=0;
 w.scripted=scripted;w.finishOrPodium=finished;w.elapsedMs=ms;
 AP_TrapSchedStep(&g_sched,&w);
 while(AP_TrapSchedPopEvent(&g_sched,&e))if(e.kind==AP_TRAP_EV_FIRE&&e.effect==AP_TRAP_CLOCKED_OUT)AP_TrapStartClockedOut(g,local);
}
int main(void){
 struct Driver d={0},other={0};struct GameTracker g={0};
 for(int wumpa=0;wumpa<=10;wumpa++){
  reset(&d,&g);d.numWumpas=wumpa;int before=hits;
  AP_TrapStartClockedOut(&g,&d);
  assert(d.clockReceive==7680&&d.clockFlash==4&&hits==before+1&&!suppress);
  assert(other.clockReceive==0&&other.clockFlash==0);
 }
 for(int guard=0;guard<14;guard++){
  reset(&d,&g);int before=hits;
  switch(guard){
   case 0:d.instBubbleHold=&other;break;
   case 1:d.invincibleTimer=1;break;
   case 2:d.actionsFlagSet=ACTION_MASK_WEAPON;break;
   case 3:d.kartState=99;break;
   case 4:d.pendingDamageType=1;break;
   case 5:d.clockReceive=1000;break;
   case 6:g.gameMode1=PAUSE_ALL;break;
   case 7:control=0;break;
   case 8:scripted=1;break;
   case 9:finished=1;break;
   case 10:context=AP_TRAP_CTX_HUB;break;
   case 11:sdata->Loading.stage=1;break;
   case 12:d.squishTimer=1;break;
   case 13:context=0;break;
  }
  AP_TrapStartClockedOut(&g,&d);assert(hits==before&&g_trap_clocked.pending&&!g_trap_clocked.applied);
 }
 reset(&d,&g);assert(!AP_TrapApplyClockedOut(&g,&other));
 // Rejected engine dispatch remains pending, then exactly one accepted hit.
 reset(&d,&g);accept=0;AP_TrapSchedReceive(&g_sched,AP_TRAP_CLOCKED_OUT);
 step(&g,1000);step(&g,1000);assert(g_trap_clocked.pending&&d.clockReceive==0&&!suppress);
 accept=1;step(&g,32);assert(g_trap_clocked.applied&&d.clockReceive==7680);
 // Two copies never refresh one timer. Paused/scripted polls preserve it.
 AP_TrapSchedReceive(&g_sched,AP_TRAP_CLOCKED_OUT);int before=hits;
 for(int i=0;i<10;i++)step(&g,1000);
 assert(hits==before&&AP_TrapSchedArmedCount(&g_sched,AP_TRAP_CLOCKED_OUT)==1);
 g.gameMode1=PAUSE_ALL;step(&g,1000);assert(d.clockReceive==7680&&g_trap_clocked.applied);
 g.gameMode1=0;scripted=1;step(&g,1000);assert(g_trap_clocked.applied);
 scripted=0;d.clockReceive=0;step(&g,32);step(&g,1000);step(&g,1000);
 assert(hits==before+1&&d.clockReceive==7680);
 // Applied harm clears on map/reset; a protected pending copy is still owed.
 AP_TrapClockedOutReset(&d);assert(!d.clockReceive&&!d.clockFlash&&!g_trap_clocked.applied);
 reset(&d,&g);d.instBubbleHold=&other;AP_TrapSchedReceive(&g_sched,AP_TRAP_CLOCKED_OUT);
 step(&g,1000);step(&g,1000);assert(AP_TrapClockOwed(&g_trap_clocked,AP_TrapSchedPollGate(&g_sched,AP_TRAP_CLOCKED_OUT)));
 d.clockReceive=123;AP_TrapClockedOutReset(&d);assert(d.clockReceive==123); // retail clock untouched
 AP_TrapSchedMapChange(&g_sched);AP_TrapSchedReceive(&g_sched,AP_TRAP_CLOCKED_OUT);
 d.clockReceive=0;d.instBubbleHold=NULL;step(&g,1000);step(&g,1000);assert(d.clockReceive==7680);
 for(int ctx=AP_TRAP_CTX_TIME_TRIAL;ctx<=AP_TRAP_CTX_TIME_TRIAL;ctx++){
  reset(&d,&g);context=ctx;AP_TrapStartClockedOut(&g,&d);assert(d.clockReceive==7680);
 }
 d.heldItemID=6;d.clockReceive=1;assert(!AP_TrapHeldItemResolved(&d));
 d.clockReceive=0;assert(AP_TrapHeldItemResolved(&d));
 assert(sounds>0&&!suppress);puts("Clocked Out production effect, retry, local isolation, fixed duration, pause and serialization passed");
 return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
    src,exe=Path(tmp)/'clock.c',Path(tmp)/'clock'
    src.write_text(fixture.replace('HELD_FUNCTION',held))
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-fsanitize=undefined','-I',str(root/'ap'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

# The verifier's dense item range ends before its sparse custom-letter slots.
# Exercise its actual own-item banking function: Clocked Out must not grant a
# custom letter merely because its raw item index is 206.
import re
verify=(root/'ap/ap_verify.c').read_text()
begin=verify.index('static void ap_vf_bank_own(')
bank=verify[begin:verify.index('\n}\n',begin)+3]
bound=re.search(r'^#define AP_VF_ITEM_COUNT +\d+', (root/'ap/ap_verify_logic.h').read_text(), re.M).group()
bank_fixture=r"""
#include <assert.h>
#include "ap_lettersanity.h"
BOUND
#define AP_ITEM_BASE 35010000
static long long scouted;
static struct{struct{int slot;}custom_track;}ctr_cfg;
static int AP_CustomLetterNamedBlock(void){return -1;} // generic slot path
static int ap_net_self_slot(void){return 1;}
static int ap_net_scout_known(long code,long long *item,int *player,unsigned *flags){
 (void)code;*item=scouted;*player=1;*flags=0;return 1;
}
BANK
int main(void){
 int counts[AP_CUSTOM_LETTER_VERIFY_FIRST+AP_CUSTOM_LETTER_VERIFY_COUNT]={0};
 scouted=35010206LL;ap_vf_bank_own(1,counts);
 for(unsigned i=0;i<sizeof(counts)/sizeof(counts[0]);i++)assert(counts[i]==0);
 scouted=35010207LL;ap_vf_bank_own(1,counts);
 for(unsigned i=0;i<sizeof(counts)/sizeof(counts[0]);i++)assert(counts[i]==0);
 for(int i=0;i<9;i++){
  scouted=35021000LL+i;ap_vf_bank_own(1,counts);
  assert(counts[AP_CUSTOM_LETTER_VERIFY_FIRST+i]==1);
 }
 scouted=35010206LL;ap_vf_bank_own(1,counts);
 assert(counts[206]==1); // only the actual custom letter was banked
 return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
    src,exe=Path(tmp)/'bank.c',Path(tmp)/'bank'
    src.write_text(bank_fixture.replace('BOUND',bound).replace('BANK',bank))
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-fsanitize=undefined','-I',str(root/'ap'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print("Clocked Out cannot bank a custom-letter verifier item")
