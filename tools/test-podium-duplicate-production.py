"""Exercise production podium birth, script selection and camera visibility.

PODIUM_SOURCE may select pre-fix CS_Thread.c to verify regression detection.
Engine allocation and script decoding are stubbed; actual engine types are used.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = Path(os.environ.get('PODIUM_SOURCE', root / 'game/233/CS_Thread.c')).read_text()
start = source.find('static int CS_PodiumRank(')
rank = source[start:source.index('\n}\n', start) + 3] if start >= 0 else ''
start = source.index('struct Thread *CS_Thread_Init(')
birth = source[start:source.index('\nafter_opcode:', start)] + '\nafter_opcode:\nreturn t;\n}\n'
start = source.index('\tif (instance != 0)', source.index('int CS_Thread_UseOpcode('))
end = source.index('\n\t\tif ((cs->flags & 0x80)', start)
visibility = 'static void visibility(struct Instance *instance) {\nstruct GameTracker *gGT=sdata->gGT;\n' + source[start:end] + '\n;\n}\n}\n'

fixture = r'''
#include <assert.h>
#include <common.h>
struct sData sdata_static;
struct Data data;
struct OverlayDATA_233 D233;
static char winScript[16], loseScript[16];
const struct OverlayRDATA_233 R233 = {
 .s_first="first", .s_second="second", .s_third="third",
 .danceFirstScripts={[0 ... 15]=winScript},
 .danceOtherScripts={[0 ... 15]=loseScript}
};
static struct Thread threads[8];
static struct Instance instances[8];
static struct CutsceneObj objects[8];
static int births, failBirth;
static char *selectedScript;
void CS_Thread_ThTick(struct Thread *t) { (void)t; }
void PROC_DestroyInstance(struct Thread *t) { (void)t; }
struct Thread *PROC_BirthWithObject(int f,void *fn,const char *n,struct Thread *p)
{ (void)f;(void)fn;(void)n;(void)p;return NULL; }
struct Instance *INSTANCE_BirthWithThread(int id,const char *name,int pool,int bucket,
 void *fn,int size,struct Thread *parent)
{
 (void)pool;(void)bucket;(void)fn;(void)size;(void)parent;
 if (failBirth) return NULL;
 int n=births++;
 memset(&threads[n],0,sizeof threads[n]); memset(&instances[n],0,sizeof instances[n]);
 threads[n].object=&objects[n]; threads[n].name=name; threads[n].inst=&instances[n];
 instances[n].thread=&threads[n]; instances[n].model=sdata->gGT->modelPtr[id];
 return &instances[n];
}
void CS_ScriptCmd_OpcodeAt(struct CutsceneObj *cs,char *script)
{ (void)cs;selectedScript=script; }
#include "game/Podium.c"
'''
tests = r'''
int main(void) {
 static struct GameTracker gt;
 sdata->gGT=&gt;
 const char *names[]={R233.s_first,R233.s_second,R233.s_third};
 struct CsThreadInitData init={0};
 int cases=0;
 /* Result gathering must use finish rank, including a boss in first place. */
 struct Driver drivers[3]={{0}};
 for(int winnerSeat=0;winnerSeat<2;winnerSeat++) {
  for(int i=0;i<3;i++) {
   gt.drivers[i]=&drivers[i];drivers[i].driverID=i;
   drivers[i].driverRank=i<2?(i==winnerSeat?0:1):2;
   data.characterIDs[i]=2;
  }
  Podium_InitModels(&gt);
  assert(gt.podium_modelIndex_First==STATIC_CRASHDANCE+2);
  assert(gt.podium_modelIndex_Second==STATIC_CRASHDANCE+2);
  assert(gt.podium_modelIndex_Third==STATIC_CRASHDANCE+2);
 }
 /* Two racers cover boss podiums; three cover ordinary race duplicates. */
 for(int racer=0;racer<16;racer++) for(int count=2;count<=3;count++) {
  int id=STATIC_CRASHDANCE+racer;
  struct Model models[3]={{0}};
  gt.podium_modelIndex_First=id; gt.podium_modelIndex_Second=id; gt.podium_modelIndex_Third=count==3?id:0;
  for(int i=0;i<count;i++) models[i].id=id;
  data.podiumModel_firstPlace=racer==15?NULL:&models[0];
  data.podiumModel_secondPlace=&models[1]; data.podiumModel_thirdPlace=count==3?&models[2]:NULL;
  gt.modelPtr[id]=&models[count-1]; /* LOAD stage 8: last equal ID wins. */
  births=0;
  for(int place=count-1;place>=0;place--) {
   struct Thread *t=CS_Thread_Init(id,names[place],&init,0,NULL);
   assert(t);
   assert(t->inst->model==(place==0&&racer==15?gt.modelPtr[id]:&models[place]));
   assert(selectedScript==(place==0?winScript:loseScript));
   for(int frame=0;frame<240;frame++) {
    D233.PodiumInitUnk2=frame; visibility(t->inst);
    int hidden=place==0?(frame>=131&&frame<236):place==1?(frame>=101&&frame<236):0;
    assert(!!(t->inst->flags&HIDE_MODEL)==hidden); cases++;
   }
   /* A matching loser must not repeatedly hide/unhide the winner and
      subtract depth bias on every tick of the overlapping camera windows. */
   u8 bias=(u8)(place==0?-6:place==1?-2:0);
   assert(t->inst->depthBiasNormal==bias);
   assert(t->inst->depthBiasSecondary==bias);
  }
 }
 struct Model winner={0},loser={0}; winner.id=STATIC_CRASHDANCE;loser.id=STATIC_CRASHDANCE+1;
 gt.podium_modelIndex_First=winner.id;gt.podium_modelIndex_Second=loser.id;
 data.podiumModel_firstPlace=&winner;data.podiumModel_secondPlace=&loser;
 gt.modelPtr[winner.id]=&winner;gt.modelPtr[loser.id]=&loser;births=0;
 assert(CS_Thread_Init(winner.id,R233.s_first,&init,0,NULL)->inst->model==&winner);
 assert(selectedScript==winScript);
 assert(CS_Thread_Init(loser.id,R233.s_second,&init,0,NULL)->inst->model==&loser);
 assert(selectedScript==loseScript);
 struct Thread *dancer=CS_Thread_Init(winner.id,R233.s_g_dancer,&init,0,NULL);
 assert(selectedScript==winScript);
 D233.PodiumInitUnk2=150;visibility(dancer->inst);assert(dancer->inst->flags&HIDE_MODEL);
 failBirth=1;assert(CS_Thread_Init(winner.id,R233.s_first,&init,0,NULL)==NULL);
 printf("podium duplicate production: %d visibility cases plus model/script/fallback checks passed\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='ctr-podium-') as tmp:
    c, binary = Path(tmp) / 'test.c', Path(tmp) / 'test'
    c.write_text(fixture + rank + birth + visibility + tests)
    subprocess.run(['cc', '-m32', '-std=gnu99', '-DCTR_NATIVE', '-DBUILD=926',
                    '-I', str(root / 'include'), '-I', str(root), str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
