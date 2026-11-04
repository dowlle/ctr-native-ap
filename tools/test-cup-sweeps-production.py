"""Run the production Cup sweep callbacks against deterministic engine seams."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
modes=(root/'include/namespace_Main.h').read_text()
start=modes.index('enum GameMode1\n')
mode_enum=modes[start:modes.index('};',start)+2]
source=(root/'ap/ap_hooks_cup_sweeps.c').read_text()
fixture=r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ap_cup_sweeps.h"
#define CTR_AP 1
#define CTR_CUSTOM_TRACKS 1
#define CUP_ANY_KIND 0x10
#define TOKEN_RACE 0x8
#define AP_RESULT_PRODUCER_ADV_REWARD 0
struct Driver {int driverID,driverRank;};
struct GameTracker {unsigned gameMode1,gameMode2;int levelID;struct {int cupID,trackIndex;}cup;struct Driver *drivers[1];};
struct {struct GameTracker *gGT;}data,*sdata=&data;
AP_CupSweepConfig ctr_cup_sweeps;
static int active=1,displaced,blocked,custom,cortex,exists=1,sent,checked;
static long last;
static char seed[32]="seed",slot[32]="slot",endpoint[32]="room:38281";
int ctr_cfg_active(void){return active;}
int ctr_cfg_cup_displaced(int c){(void)c;return displaced;}
int ctr_cfg_cup_leg(int c,int l){return ctr_cup_sweeps.legs[c][l];}
int ap_net_seed_name(char *b,int n){snprintf(b,n,"%s",seed);return *seed!=0;}
int ap_net_slot_name(char *b,int n){snprintf(b,n,"%s",slot);return *slot!=0;}
int ap_net_room_endpoint(char *b,int n){snprintf(b,n,"%s",endpoint);return *endpoint!=0;}
int CustomTrack_ServingLoad(int a,int b,int c){(void)a;(void)b;(void)c;return custom;}
int CustomTrack_CupRaceRedirectActive(int a,int b){(void)a;(void)b;return custom;}
int AP_CortexTrackActive(void){return cortex;}
int AP_RaceAttempt_ProducerBlocked(int p){(void)p;return blocked;}
int ap_net_location_exists(long code){(void)code;return exists;}
int AP_EmitClassCheck(long code,int a,int b,int c,int d,const char *fmt,...){
 (void)a;(void)b;(void)c;(void)d;(void)fmt;if(!exists||checked)return 0;
 sent++;last=code;checked=1;return 1;
}
"""
tests=r"""
static void setup(struct GameTracker *g,int cup){
 active=exists=1;displaced=custom=cortex=blocked=checked=0;
 strcpy(seed,"seed");strcpy(slot,"slot");strcpy(endpoint,"room:38281");
 memset(&ctr_cup_sweeps,0,sizeof ctr_cup_sweeps);ctr_cup_sweeps.enabled=1;
 for(int c=0;c<5;c++){
  ctr_cup_sweeps.present[c]=1;ctr_cup_sweeps.locations[c]=35028300+c;
  // Four occurrences of one physical track must remain four distinct legs.
  for(int l=0;l<4;l++)ctr_cup_sweeps.legs[c][l]=0;
 }
 g->gameMode1=ADVENTURE_MODE|ADVENTURE_CUP;g->gameMode2=0;
 g->levelID=25;g->cup.cupID=cup;g->cup.trackIndex=0;g->drivers[0]->driverID=0;
 AP_CupSweepEnterFromHub();
}
static void leg(struct GameTracker *g,int l,int first){
 g->cup.trackIndex=l;AP_CupSweepOnLoadRequested(0);g->levelID=0;
 AP_CupSweepOnLevelStart(g);g->drivers[0]->driverRank=first?0:1;
 AP_CupSweepRecordLeg(g);AP_CupSweepRecordLeg(g);
}
static void finish(struct GameTracker *g){g->cup.trackIndex=4;AP_CupSweepComplete(g,4);}
int main(void){
 struct Driver driver={0};struct GameTracker g={0};g.drivers[0]=&driver;sdata->gGT=&g;
 for(int cup=0;cup<5;cup++)for(int pattern=0;pattern<16;pattern++){
  setup(&g,cup);int before=sent;
  for(int l=0;l<4;l++)leg(&g,l,(pattern>>l)&1);
  finish(&g);assert(sent==before+(pattern==15));
  if(pattern==15)assert(last==35028300+cup);
  finish(&g);assert(sent==before+(pattern==15));
 }
 for(int f=0;f<4;f++){
  setup(&g,0);int before=sent;
  for(int l=0;l<4;l++){blocked=l==f;leg(&g,l,1);}blocked=0;finish(&g);assert(sent==before);
 }
 for(int mutation=0;mutation<15;mutation++){
  setup(&g,0);leg(&g,0,1);int before=sent;
  switch(mutation){
   case 0:strcpy(seed,"other");break;
   case 1:strcpy(slot,"other");break;
   case 2:strcpy(endpoint,"room:38282");break;
   case 3:active=0;break;
   case 4:displaced=1;break;
   case 5:custom=1;break;
   case 6:ctr_cup_sweeps.legs[0][1]=1;break;
   case 7:driver.driverID=1;break;
   case 8:g.gameMode1=ARCADE_MODE;break;
   case 9:g.gameMode2=CUP_ANY_KIND;break;
   case 10:cortex=1;break;
   case 11:g.cup.cupID=1;break;
   case 12:exists=0;break;
   case 13:checked=1;break;
   case 14:ctr_cup_sweeps.enabled=0;break;
  }
  for(int l=1;l<4;l++){leg(&g,l,1);}
  finish(&g);assert(sent==before);
 }
 setup(&g,0);leg(&g,0,1);AP_CupSweepOnLoadRequested(25);assert(!ap_sweep_attempt.active);
 setup(&g,0);g.levelID=0;AP_CupSweepOnLevelStart(&g);AP_CupSweepOnLoadRequested(0);assert(!ap_sweep_attempt.active);
 setup(&g,0);leg(&g,0,1);AP_CupSweepEnterFromHub();assert(ap_sweep_attempt.nextLeg==0);
 // The logical Cortex alias accepts only its physical host with alias active.
 setup(&g,0);ctr_cup_sweeps.legs[0][0]=110;AP_CupSweepEnterFromHub();
 AP_CupSweepOnLoadRequested(13);g.levelID=13;cortex=1;AP_CupSweepOnLevelStart(&g);
 assert(ap_sweep_attempt.started);g.drivers[0]->driverRank=0;AP_CupSweepRecordLeg(&g);
 assert(ap_sweep_attempt.nextLeg==1);
 puts("Production Cup sweep dispatch: ordered repeats, per-leg losses, identity changes, restart/exit, membership and Cortex alias passed");
 return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
    src,exe=Path(tmp)/'sweeps.c',Path(tmp)/'sweeps'
    src.write_text(mode_enum+fixture+source+tests)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-fsanitize=undefined','-I',str(root/'ap'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
