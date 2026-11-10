"""Run production ShootNow and the player choke point with real 32-bit structs.

Engine allocation, rendering, sound and collision are seams. This tests fire
initialization, not subsequent weapon physics, which need a loaded client.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
headers = "\n".join((ROOT / name).read_text() for name in
                    ("include/functions.h", "include/psx/libgte.h", "include/psx/inline_c.h"))
# These engine services have no effect on the initialization assertions below.
noop = """OtherFX_Play_Echo PROC_DestroyInstance SetRotMatrix SetTransMatrix
ConvertRotToMatrix MATH_HitboxMatrix MixRNG_Scramble GAMEPAD_ShockFreq
GAMEPAD_ShockForce1 VehPhysForce_RotAxisAngle PROC_DestroyTracker
RB_Hazard_ThCollide_Missile CTR_MatrixToRot PlaySound3D RB_GetThread_ClosestTracker
Voiceline_RequestPlay RB_GenericMine_ThTick RB_Hazard_ThCollide_Generic
RB_MinePool_Add RB_Hazard_CollLevInst RB_GenericMine_ThDestroy
RB_MakeInstanceReflective RB_Follower_Init RB_ShieldDark_ThTick_Grow
OtherFX_Play RB_Hazard_HurtDriver RB_MaskWeapon_ThTick RB_MovingExplosive_ThTick""".split()
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <common.h>
#include "ap/ap_itemsanity_logic.h"
void AP_LogLine(const char *s) { (void)s; }
void AP_NavRec_NoteItemFire(struct Driver *d, int id) { (void)d; (void)id; }
static int useId, useCount, boost;
void AP_ItemsanityOnUse(struct Driver *d, int id) { (void)d; useId=id; useCount++; }
#include "game/Vehicle/VehPickupItem.c"
struct sData sdata_static;
struct Data data;
struct rData rdata;
static u8 scratch[4096];
u8 *gCTRNativeScratchpadBase = scratch;
static struct GameTracker gt;
static struct Level level;
static struct Driver driver;
static struct GamepadSystem pads;
static struct Instance kart, births[4];
static struct Thread kartTh, threads[4];
static struct Model models[256];
static struct ModelHeader modelHeaders[256];
static union { struct TrackerWeapon tracker; struct MineWeapon mine;
               struct Shield shield; struct MaskHeadWeapon mask; } objects[4];
static int born, failBirth;
int LOAD_IsOpen_RacingOrBattle(void) { return 1; }
void VehFire_Increment(struct Driver *d,int reserves,u32 type,int fire) {
    assert(d==&driver && reserves==0x960 && type==9); boost=fire;
}
struct Instance *INSTANCE_BirthWithThread(int id,const char *name,int pool,int bucket,
                                        void *tick,int size,struct Thread *parent) {
    (void)name;(void)pool;(void)bucket; assert(size <= sizeof objects[0]);
    if(failBirth) return NULL;
    assert(born<4);int n=born++;
    births[n].thread=&threads[n];births[n].model=&models[id];
    threads[n].inst=&births[n];threads[n].object=&objects[n];
    threads[n].funcThTick=tick;threads[n].parentThread=parent;threads[n].modelIndex=id;
    if(parent) parent->childThread=&threads[n];
    return &births[n];
}
struct Instance *INSTANCE_Birth3D(struct Model *m,const char *name,struct Thread *parent) {
    return INSTANCE_BirthWithThread(m->id,name,0,0,NULL,0,parent);
}
void COLL_SearchBSP_CallbackQUADBLK(const SVec3 *a,const SVec3 *b,struct ScratchpadStruct *s,s32 r) {
    (void)a;(void)b;(void)r;s->boolDidTouchHitbox=0;s->boolDidTouchQuadblock=0;
}
// No opponent exists. Any projection or Warp Ball path read is a regression.
void MTC2(unsigned int v,int r) { (void)v;(void)r;assert(!"opponent projection"); }
unsigned int MFC2(int r) { (void)r;assert(0);return 0; }
unsigned int CFC2(int r) { (void)r;assert(0);return 0; }
int doCOP2(int op) { (void)op;assert(0);return 0; }
void RB_Warpball_ThTick(struct Thread *t) { (void)t;assert(0); }
void RB_Warpball_SeekDriver(struct TrackerWeapon *t,u32 n,struct Driver *d) { (void)t;(void)n;(void)d;assert(0); }
struct Driver *RB_Warpball_GetDriverTarget(struct TrackerWeapon *t,struct Instance *i) { (void)t;(void)i;assert(0);return NULL; }
void RB_Warpball_SetTargetDriver(struct TrackerWeapon *t) { (void)t;assert(0); }
void RB_Warpball_Start(struct TrackerWeapon *t) { (void)t;assert(0); }
struct CheckpointNode *RB_Warpball_NewPathNode(struct CheckpointNode *c,struct Driver *d) { (void)c;(void)d;assert(0);return NULL; }
struct Particle *Particle_Init(u32 n,struct IconGroup *i,struct ParticleEmitter *e) { (void)n;(void)i;(void)e;assert(0);return NULL; }
int VehPickState_NewState(struct Driver *d,int type,struct Driver *a,int r) { (void)d;(void)type;(void)a;(void)r;assert(0);return 0; }
'''
for name in noop:
    match = re.search(r"(?:^|\n)([^\n;{}]*\b" + name + r"\([^;{}]*\));", headers)
    assert match, name
    signature = re.sub(r"^extern\s+", "", match[1].strip())
    body = "" if signature.startswith("void ") else "return 0;"
    fixture += signature + " { " + body + " }\n"
fixture += r'''
static void setup(int arena,int fruit,int mode) {
    memset(&gt,0,sizeof gt);memset(&driver,0,sizeof driver);
    memset(&level,0,sizeof level);memset(&kart,0,sizeof kart);
    memset(&kartTh,0,sizeof kartTh);memset(births,0,sizeof births);
    memset(threads,0,sizeof threads);memset(objects,0xa5,sizeof objects);
    memset(scratch,0,sizeof scratch);born=failBirth=boost=useCount=0;useId=-1;
    sdata->gGT=&gt;sdata->gGamepads=&pads;
    gt.level1=&level;gt.levelID=arena;gt.numPlyrCurrGame=1;
    gt.gameMode1=ADVENTURE_MODE|mode;gt.drivers[0]=&driver;
    driver.instSelf=&kart;driver.numWumpas=fruit;
    kart.thread=&kartTh;kartTh.modelIndex=DYNAMIC_PLAYER;
    kart.matrix.m[0][0]=kart.matrix.m[1][1]=kart.matrix.m[2][2]=0x1000;
    for(unsigned i=0;i<sizeof gt.modelPtr/sizeof gt.modelPtr[0];i++){
        models[i].id=i;models[i].headers=&modelHeaders[i];gt.modelPtr[i]=&models[i];
    }
}
static void fire(int id) {
    if(id==9) gt.gameMode1|=WARPBALL_HELD;
    driver.heldItemID=id;driver.actionsFlagSet|=ACTION_WEAPON_FIRE_REQUEST;
    VehPickupItem_ShootOnCirclePress(&driver);
    assert(!(driver.actionsFlagSet&ACTION_WEAPON_FIRE_REQUEST));
    assert(useId==id && useCount==1);
    VehPickupItem_ShootOnCirclePress(&driver);assert(useCount==1);
}
int main(void) {
    const int ids[]={0,1,2,3,4,6,7,8,10,11,12,13,9};
    int cases=0;
    for(int arena=18;arena<=24;arena++) for(int fruit=0;fruit<=10;fruit+=10)
    for(int m=0;m<2;m++) for(unsigned i=0;i<sizeof ids/sizeof ids[0];i++) {
        int id=ids[i];setup(arena,fruit,m?BATTLE_MODE:CRYSTAL_CHALLENGE);fire(id);cases++;
        if(id==0) assert(boost==(fruit?0x100:0x80));
        if(id==1||id==2||id==10||id==11||id==9) {
            int bomb=id==1||id==10;
            assert(born==1 && gt.numMissiles==1);
            assert(births[0].model->id==(bomb?DYNAMIC_BOMB:DYNAMIC_ROCKET));
            assert(objects[0].tracker.driverTarget==NULL);
            assert(objects[0].tracker.driverParent==&driver && objects[0].tracker.timeAlive==0);
            assert(objects[0].tracker.frameCount_DontHurtParent==60);
            assert((objects[0].tracker.flags&1)==(fruit?1:0));
            assert(threads[0].funcThTick==RB_MovingExplosive_ThTick);
            assert(!(gt.gameMode1&WARPBALL_HELD));
        }
        if(id==3||id==4) {
            assert(born==1 && objects[0].mine.driverTarget==NULL);
            assert(objects[0].mine.velocity.x==0 && objects[0].mine.velocity.y==0);
            assert(threads[0].funcThTick==RB_GenericMine_ThTick);
            assert(births[0].model->id==(id==3?(fruit?PU_EXPLOSIVE_CRATE:STATIC_CRATE_TNT):(fruit?STATIC_BEAKER_RED:STATIC_BEAKER_GREEN)));
        }
        if(id==6) {
            assert(born==3 && driver.instBubbleHold==&births[0]);
            assert(objects[0].shield.instColor==&births[1]);
            assert(fruit?(objects[0].shield.flags&4):objects[0].shield.duration==0x2d00);
        }
        if(id==7) assert(born==2 && objects[0].mask.duration==(fruit?0x2d00:0x1e00));
        if(id==8) assert(born==0 && driver.clockSend==0x1e && driver.clockReceive==0);
        if(id==12) assert(driver.invisibleTimer==(fruit?0x2d00:0x1e00));
        if(id==13) assert(driver.superEngineTimer==(fruit?0x2d00:0x1e00));
    }
    printf("PASS: %d normal arena fire initializations\n",cases);fflush(stdout);
    // A full mine pool must not turn a consumed bomb or missile into a crash.
    const int allocating[]={1,2,3,4,6,7,9,10,11};
    for(int arena=18;arena<=24;arena++) for(int fruit=0;fruit<=10;fruit+=10)
    for(unsigned i=0;i<sizeof allocating/sizeof allocating[0];i++) {
        setup(arena,fruit,CRYSTAL_CHALLENGE);failBirth=1;fire(allocating[i]);
        assert(born==0 && gt.numMissiles==0 && driver.numTimesMissileLaunched==0);
        assert(!(gt.gameMode1&WARPBALL_HELD));
    }
    // The global projectile cap remains a harmless consumed shot.
    setup(18,10,CRYSTAL_CHALLENGE);gt.numMissiles=12;fire(2);
    assert(born==0 && gt.numMissiles==12);
    printf("PASS: %d arena fire initializations, allocation failure and projectile cap\n",cases);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "fire.c").write_text(fixture)
    subprocess.run(["cc", "-m32", "-DCTR_NATIVE", "-DCTR_AP", "-DBUILD=926",
                    "-I", str(ROOT / "include"), "-I", str(ROOT),
                    str(path / "fire.c"), "-o", str(path / "fire")], check=True)
    subprocess.run([str(path / "fire")], check=True)
