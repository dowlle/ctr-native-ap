"""Execute production authored-crystal construction/dispatch with engine seams."""
from pathlib import Path
import json
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
data=json.loads((root/'tools/fixtures/arcade-arenas-v1.json').read_text())
header=(root/'ap/ap_arena_content.h').read_text().split('ap_arcade_anchors[3][20][3] = {',1)[1]
anchors=[list(map(int,p)) for p in re.findall(r'\{(-?\d+),(-?\d+),(-?\d+)\}',header)]
assert anchors==[p for row in data['arenas'] for p in row['positions']], 'runtime anchors differ from the accepted placement fixture'
source=(root/'ap/ap_arcade_crystals.c').read_text()
source='\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
modes=(root/'include/namespace_Main.h').read_text()
start=modes.index('enum GameMode1\n')
mode_enum=modes[start:modes.index('};',start)+2]
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "ap_content_plan.h"
#include "ap_crystal_checks.h"
#include "ap_arena_content.h"
#define ADVENTURE_BOSS 0x80000000u
typedef int16_t s16;
#define CTR_AP 1
#define CTR_CUSTOM_TRACKS 1
#define TOKEN_RACE 1
#define THREAD_FLAG_DEAD 1
#define LOAD_IDLE 0
#define ACTION_RACE_FINISHED 1
#define ACTION_WARP 2
#define STATIC_CRYSTAL 96
#define DYNAMIC_PLAYER 0
#define AP_RESULT_PRODUCER_ADV_REWARD 0
#define CTR_MipsSra(a,b) ((a)>>(b))
#define CTR_MipsAddLo(a,b) ((a)+(b))
typedef struct {union {struct {short x,y,z;};short v[3];};} SVec3;
struct BoundingBox {SVec3 min,max;};
struct Thread;
struct Model {int id;};
struct InstDef {char name[16];struct Model *model;int modelID;SVec3 pos,scale;struct Instance *ptrInstance;};
struct Instance {struct InstDef *instDef;struct Thread *thread;SVec3 scale;struct {int t[3];}matrix;};
struct Thread {int flags;struct Instance *inst;void (*funcThTick)(struct Thread *);};
struct Driver {struct Instance *instSelf;int actionsFlagSet,numCrystals;struct {int x,y,z;}posPrev,posCurr;SVec3 originToCenter;};
struct Level {int numInstances;struct InstDef *ptrInstDefs;};
struct GameTracker {unsigned gameMode1,gameMode2;int levelID,numPlyrCurrGame,numCrystalsInLEV;struct Driver *drivers[1];struct Model *modelPtr[97];struct Level *level1;};
struct Small {struct GameTracker *gGT;struct {int stage;struct {unsigned RemBitsConfig0,AddBitsConfig0;}OnBegin;}Loading;} data,*sdata=&data;
struct CustomTrackRaceContext {int physicalPad,hostLevelID,returnHub,laps,slot,retail,crystal,timeLimitTicks;uint64_t seedEpoch;int64_t trophyLocation;char entryID[65],trackID[65];}context;
struct ScratchpadStruct {struct {int modelID;}Input1;};
static ctr_content_pad pad;
static uint64_t epoch=5;
static int active=1,bound=1,blocked,exists=1,checked[256],sent,perf,feed,ap_state_gen,loaded=-1,born,fail_at=-1,fail_thread_at=-1,ticks,collided;
static long lastCode;
static struct Instance instances[20];
static struct Thread threads[20];
int ctr_cfg_active(void){return active;}
int ap_content_plan_active(void){return bound;}
uint64_t ap_content_plan_epoch(void){return bound?epoch:0;}
const ctr_content_pad *ap_content_plan_pad(int id){return bound && id==pad.physical?&pad:NULL;}
const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int level){return level==context.hostLevelID?&context:NULL;}
int ap_net_location_exists(long code){(void)code;return exists;}
int ap_net_location_checked(long code){return checked[code%256];}
void ap_net_send_location(long code){sent++;lastCode=code;checked[code%256]=1;}
void AP_PerfNoteCheckSent(void){perf++;}
void AP_FeedOnLocationSent(long code){assert(code==lastCode);feed++;}
int AP_RaceAttempt_ProducerBlocked(int producer){(void)producer;return blocked;}
int AP_EmitClassCheck(long code,int a,int b,int c,int d,const char *fmt,...){(void)a;(void)b;(void)c;(void)d;(void)fmt;if(!exists || ap_net_location_checked(code))return 0;ap_net_send_location(code);return 1;}
void AP_LogLine(const char *line){(void)line;}
void MainRaceTrack_RequestLoad(int level){loaded=level;}
struct Instance *INSTANCE_Birth3D(struct Model *m,const char *name,struct Thread *t){(void)m;(void)name;(void)t;if(born==fail_at)return NULL;assert(born<20);return &instances[born++];}
void RB_Crystal_LInB(struct Instance *inst){int i=inst-instances;if(i==fail_thread_at)return;inst->thread=&threads[i];threads[i].inst=inst;}
void RB_Crystal_ThTick(struct Thread *t){(void)t;ticks++;}
int AP_TrialLetterHit(const SVec3 *p,const SVec3 *c,const struct BoundingBox *b){return p->x>=b->min.x-25&&p->x<=b->max.x+25&&c->y>=b->min.y-25&&c->y<=b->max.y+25&&c->z>=b->min.z-25&&c->z<=b->max.z+25;}
void AP_NotifyContentCrystalPickup(struct Instance *,int);
int RB_Crystal_LInC(struct Instance *i,struct Thread *d,struct ScratchpadStruct *s){(void)d;assert(s->Input1.modelID==DYNAMIC_PLAYER);collided++;AP_NotifyContentCrystalPickup(i,0);i->thread->flags|=THREAD_FLAG_DEAD;return 1;}
'''
tests=r'''
static void setup(struct GameTracker *g,int level){
 memset(&pad,0,sizeof pad);memset(&context,0,sizeof context);memset(instances,0,sizeof instances);memset(threads,0,sizeof threads);memset(checked,0,sizeof checked);
 pad.physical=100;pad.hub=25;pad.occupied=pad.crystal=1;pad.retail_id=level;pad.laps=1;pad.target=20;pad.crystal_count=20;
 pad.time_limit_ticks=AP_ArenaTimeLimit(level);pad.trophy_location=AP_ArenaCompletionCode(level);
 strcpy(pad.entry_id,"entry/arena:test");strcpy(pad.track_id,"arena:test");
 for(int n=0;n<20;n++)pad.crystal_locations[n]=AP_ArenaPickupCode(level,n);
 context.physicalPad=100;context.hostLevelID=level;context.returnHub=25;context.laps=1;context.retail=context.crystal=1;context.seedEpoch=epoch;
 context.trophyLocation=pad.trophy_location;context.timeLimitTicks=pad.time_limit_ticks;strcpy(context.entryID,pad.entry_id);strcpy(context.trackID,pad.track_id);
 g->gameMode1=ADVENTURE_MODE|CRYSTAL_CHALLENGE;g->levelID=level;g->numPlyrCurrGame=1;g->drivers[0]->numCrystals=0;
 active=bound=exists=1;born=0;fail_at=fail_thread_at=-1;loaded=-1;blocked=0;sdata->Loading.stage=LOAD_IDLE;
}
int main(void){
 struct Model model={96};struct Instance kart={0};struct Thread driverTh={0};struct Driver driver={0};struct GameTracker g={0};
 driver.instSelf=&kart;kart.thread=&driverTh;struct Level arcadeLev={0};g.level1=&arcadeLev;g.drivers[0]=&driver;g.modelPtr[96]=&model;sdata->gGT=&g;
 for(int a=0;a<3;a++){
  setup(&g,20+2*a);AP_ArcadeCrystals_Spawn(&g);assert(born==20&&g.numCrystalsInLEV==20);
  for(int n=0;n<20;n++){
   struct Instance *i=&instances[n];int before=sent;
   assert(i->instDef==&ap_arcade_defs[n]);assert(i->matrix.t[0]==ap_arcade_anchors[a][n][0]);
   driver.posPrev.x=driver.posCurr.x=i->matrix.t[0]*256;
   driver.posPrev.y=driver.posCurr.y=i->matrix.t[1]*256;
   driver.posPrev.z=driver.posCurr.z=i->matrix.t[2]*256;
   i->thread->funcThTick(i->thread);assert(sent==before+1&&lastCode==AP_ArenaPickupCode(g.levelID,n));
   i->thread->funcThTick(i->thread);assert(sent==before+1);
  }
  assert(!AP_NotifyContentCrystalComplete());driver.numCrystals=20;blocked=1;assert(!AP_NotifyContentCrystalComplete());blocked=0;
  assert(AP_NotifyContentCrystalComplete());assert(lastCode==AP_ArenaCompletionCode(g.levelID));assert(!AP_NotifyContentCrystalComplete());
 }
 assert(collided==60&&perf==60&&feed==60&&ap_state_gen==60);
 for(int phase=0;phase<2;phase++)for(int fail=0;fail<20;fail++){
  setup(&g,20);if(phase)fail_thread_at=fail;else fail_at=fail;AP_ArcadeCrystals_Spawn(&g);assert(ap_arcade_failed&&g.numCrystalsInLEV==20);
  for(int n=0;n<born;n++)assert(instances[n].scale.x==0&&(!instances[n].thread || (threads[n].flags&THREAD_FLAG_DEAD)));
  AP_ArcadeCrystals_OnFrame(&g);assert(loaded==25);
 }
 setup(&g,20);g.modelPtr[96]=NULL;AP_ArcadeCrystals_Spawn(&g);assert(ap_arcade_failed&&born==0&&g.numCrystalsInLEV==20);AP_ArcadeCrystals_OnFrame(&g);assert(loaded==25);g.modelPtr[96]=&model;
 setup(&g,20);AP_ArcadeCrystals_Spawn(&g);
 #define DENY(statement,restore) do{int before=sent;statement;AP_NotifyContentCrystalPickup(&instances[0],0);assert(sent==before);assert(!AP_ContentCrystalPad());restore;}while(0)
 DENY(context.seedEpoch++,context.seedEpoch--);
 DENY(context.physicalPad=1,context.physicalPad=100);
 DENY(context.returnHub=26,context.returnHub=25);
 DENY(context.timeLimitTicks=1,context.timeLimitTicks=pad.time_limit_ticks);
 DENY(context.entryID[0]='x',context.entryID[0]='e');
 DENY(g.gameMode1=CRYSTAL_CHALLENGE,g.gameMode1=ADVENTURE_MODE|CRYSTAL_CHALLENGE);
 DENY(bound=0,bound=1);
 {int before=sent;AP_NotifyContentCrystalPickup(&instances[0],1);assert(sent==before);}
 for(int a=0;a<4;a++){
  int levels[]={21,19,23,18};struct InstDef defs[80]={0};struct Level lev={80,defs};struct Instance crystal={0};
  setup(&g,levels[a]);g.level1=&lev;g.numCrystalsInLEV=20;
  for(int n=0;n<20;n++){int before=sent;crystal.instDef=&defs[AP_CrystalInstanceBase(a,1)+n];AP_NotifyContentCrystalPickup(&crystal,0);assert(sent==before+1&&lastCode==AP_ArenaPickupCode(levels[a],n));}
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src,exe=Path(tmp)/'arcade.c',Path(tmp)/'arcade'
    src.write_text(mode_enum+fixture+source+tests)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-fsanitize=undefined','-I',str(root/'ap'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Arcade production construction/collision/dispatch passed: 60 authored + 80 retail identities, 40 instance/thread allocation failure points and missing model, context guards')
