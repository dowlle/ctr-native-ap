// Production letter import/spawn/pickup integration, no display or game launch.
// cc -Wall -Wextra -m32 -DCTR_AP -DCTR_NATIVE -DBUILD=926 -I . -I include
//    -ffunction-sections -fdata-sections -Wl,--gc-sections
//    -o /tmp/test-trial-letters tools/test-trial-letters.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <common.h>
#include "../ap/ap_hooks.h"
#include "../game/231/RB_CtrLetter.c"
#include "../ap/ap_trial_letters.c"

struct sData sdata_static;
ctr_seed_config ctr_cfg;
static struct GameTracker gt;
static struct Driver driver;
static struct Instance instances[3], kart;
static struct Thread threads[3], kartThread;
static struct CtrLetter objects[3];
static struct Model models[3];
static int births, liveCfg=1, logCount, sounds, pickups, textureCalls;
static const char *bigfilePath;

int ctr_cfg_active(void) { return liveCfg; }
void AP_LogLine(const char *s) { (void)s; ++logCount; }
void ConvertRotToMatrix(MATRIX *m,const SVec3 *rot) { (void)m;(void)rot; }
void Vector_SpecLightSpin3D(struct Instance *i,const SVec3 *r,const SVec3 *l) { (void)i;(void)r;(void)l; }
void RB_Default_LInB(struct Instance *i) { (void)i; }
void RB_Fruit_GetScreenCoords(struct PushBuffer *p,struct Instance *i,s16 *pos) { (void)p;(void)i;pos[0]=42;pos[1]=43; }
int OtherFX_Play(unsigned int id,int flags) { assert(id==100);(void)flags;++sounds;return 1; }
int AP_LetterAvailable(int level,int letter) { assert(level==16||level==17); assert(letter>=0&&letter<3);return 1; }
void AP_LetterCollected(int level,int letter) { assert(level==16||level==17); assert(letter>=0&&letter<3);++pickups; }
void AP_LetterUnavailableTouched(int level,int letter) { (void)level;(void)letter;abort(); }
struct Thread *PROC_BirthWithObject(int flags,void *func,const char *name,struct Thread *parent)
{ (void)flags;(void)func;(void)name;(void)parent;abort(); }

struct Instance *INSTANCE_BirthWithThread(int id,const char *name,int pool,int bucket,void *func,int size,struct Thread *parent)
{
    int n=births++;
    assert(n<3 && id==STATIC_C+n && pool==SMALL && bucket==STATIC);
    assert(size==sizeof(struct CtrLetter) && parent==NULL && name[0]=='a');
    memset(&instances[n],0,sizeof(instances[n]));
    memset(&threads[n],0,sizeof(threads[n]));
    instances[n].thread=&threads[n];instances[n].model=gt.modelPtr[id];
    threads[n].inst=&instances[n];threads[n].object=&objects[n];
    threads[n].funcThTick=func;
    return &instances[n];
}

unsigned char *AP_RetailAsset_ReadSubfile(int entry,int dram,unsigned char *dst,int cap,int *size)
{
    FILE *f; unsigned char pair[8]; unsigned off,n;
    assert(!dram && (entry==24||entry==25));
    if (!bigfilePath) return NULL;
    f=fopen(bigfilePath,"rb"); assert(f);
    assert(!fseek(f,8+entry*8,SEEK_SET) && fread(pair,1,8,f)==8);
    off=AP_TrialU32(pair);n=AP_TrialU32(pair+4);assert(n<=(unsigned)cap);
    assert(!fseek(f,off*2048,SEEK_SET) && fread(dst,1,n,f)==n);fclose(f);
    *size=n;return dst;
}
// The tested importer validates every slot first. Same relocation arithmetic
// as LOAD_RunPtrMap, without unrelated loader/checkpoint dependencies.
void LOAD_RunPtrMap(char *body,int *slots,int count)
{ int i;for(i=0;i<count;i++) {unsigned *p=(unsigned *)(body+(slots[i]&~3));*p+=(unsigned)body;} }
TextureID NativeRenderer_CreateRGBATexture(int w,int h,u8 *rgba)
{
    assert(w==64&&h==2&&rgba[3]==255);++textureCalls;return 77;
}
void NativeGpu_SetTrialLetterTexture(unsigned texture,int w,int h) { assert(texture==77&&w==64&&h==2); }

static void reset(int level)
{
    int i;
    memset(&gt,0,sizeof(gt));memset(&driver,0,sizeof(driver));
    memset(&ctr_cfg,0,sizeof(ctr_cfg));
    sdata_static.gGT=&gt;sdata_static.Loading.stage=LOAD_IDLE;
    gt.levelID=level;gt.numPlyrCurrGame=1;
    gt.gameMode1=ADVENTURE_MODE;gt.gameMode2=TOKEN_RACE;
    gt.drivers[0]=&driver;driver.instSelf=&kart;kart.thread=&kartThread;kartThread.object=&driver;
    for(i=0;i<2;i++) {ctr_cfg.trial_track_valid[i]=1;ctr_cfg.trial_track_mode[i]=2;}
    for(i=0;i<3;i++) {models[i].id=STATIC_C+i;s_trialModels[i]=&models[i];}
    liveCfg=1;s_trialAssets=1;births=0;sounds=0;pickups=0;
}

static void production_cases(void)
{
    int level,i,axis;
    for(level=16;level<=17;level++) {
        reset(level);AP_TrialLetters_Register(&gt);AP_TrialLetters_Spawn(&gt);
        assert(births==3);
        for(i=0;i<3;i++) {
            assert(gt.modelPtr[STATIC_C+i]==&models[i]);
            assert(objects[i].rot.y==AP_TRIAL_LETTER_YAW[level-16][i]);
            for(axis=0;axis<3;axis++) {
                int anchor=AP_TRIAL_LETTER_POS[level-16][i][axis];
                assert(instances[i].matrix.t[axis]==anchor);
                driver.posPrev.v[axis]=driver.posCurr.v[axis]=anchor*256;
            }
            assert(instances[i].flags & DRAW_COLLISION_MASK);
            driver.posPrev.x-=100*256;driver.posCurr.x+=100*256;
            sdata_static.Loading.stage=1;AP_TrialLetters_Tick(&threads[i]);assert(pickups==i);
            sdata_static.Loading.stage=LOAD_IDLE;
            gt.gameMode1|=PAUSE_1;AP_TrialLetters_Tick(&threads[i]);assert(pickups==i);gt.gameMode1&=~PAUSE_1;
            AP_TrialLetters_Tick(&threads[i]);
            assert(pickups==i+1&&driver.PickupLetterHUD.numCollected==i+1);
            assert(threads[i].flags&THREAD_FLAG_DEAD);
            assert(instances[i].thread==NULL&&instances[i].scale.x==0);
            assert(driver.PickupLetterHUD.modelID==STATIC_C+i&&driver.PickupLetterHUD.cooldown==10);
            AP_TrialLetters_Tick(&threads[i]);assert(pickups==i+1);
        }
        assert(sounds==3);
        // Same-map pool reset/restart: no static handles survive; fresh births.
        reset(level);AP_TrialLetters_Register(&gt);AP_TrialLetters_Spawn(&gt);assert(births==3);
        assert(driver.PickupLetterHUD.numCollected==0);
    }
    reset(15);AP_TrialLetters_Spawn(&gt);assert(!births);
    reset(18);AP_TrialLetters_Spawn(&gt);assert(!births);
    reset(16);gt.gameMode2=0;AP_TrialLetters_Spawn(&gt);assert(!births);
    reset(16);gt.gameMode1|=RELIC_RACE;AP_TrialLetters_Spawn(&gt);assert(!births);
    reset(16);gt.gameMode1|=ADVENTURE_CUP;AP_TrialLetters_Spawn(&gt);assert(!births);
    reset(16);gt.numPlyrCurrGame=2;AP_TrialLetters_Spawn(&gt);assert(!births);
    reset(16);liveCfg=0;AP_TrialLetters_Spawn(&gt);assert(!births);
    reset(16);ctr_cfg.trial_track_mode[0]=1;AP_TrialLetters_Spawn(&gt);assert(!births);
    reset(16);ctr_cfg.trial_track_valid[0]=0;AP_TrialLetters_Spawn(&gt);assert(!births);
    reset(16);s_trialAssets=-1;AP_TrialLetters_Spawn(&gt);assert(!births);
}

int main(void)
{
    unsigned char bad[64]={0},rgba[512]; unsigned out;
    double a[3]={-100,0,0},b[3]={100,0,0};short anchor[3]={0,0,0};
    production_cases();
    assert(AP_TrialLetterHit(a,b,anchor));
    a[1]=b[1]=57;assert(!AP_TrialLetterHit(a,b,anchor));
    a[1]=b[1]=0;a[0]=-1000;b[0]=1000;assert(!AP_TrialLetterHit(a,b,anchor));
    b[0]=0;assert(AP_TrialLetterHit(a,b,anchor));
    assert(!AP_TrialPointerMapValid(NULL,0));assert(!AP_TrialPointerMapValid(bad,sizeof(bad)));
    bad[0]=32;bad[36]=4;bad[40]=0;bad[4]=4;assert(AP_TrialPointerMapValid(bad,sizeof(bad)));
    bad[40]=32;assert(!AP_TrialPointerMapValid(bad,sizeof(bad)));
    bad[40]=0;bad[4]=32;assert(!AP_TrialPointerMapValid(bad,sizeof(bad)));
    assert(!AP_TrialVramWord(bad,sizeof(bad),0,0,&out));assert(!AP_TrialTextureDecode(bad,sizeof(bad),rgba));
    assert(!AP_TrialDigest(bad,sizeof(bad),AP_TRIAL_LEV_SHA));
    s_trialAssets=0;sdata_static.ptrBigfile1=(struct BigHeader *)bad;
    assert(!AP_TrialLetters_Prepare()&&s_trialAssets==-1&&!textureCalls);
    bigfilePath=getenv("CTR_TEST_BIGFILE");
    if(bigfilePath) {
        int i,j; s_trialAssets=0;
        assert(AP_TrialLetters_Prepare()&&textureCalls==1);
        for(i=0;i<3;i++) {
            assert(s_trialModels[i]->id==STATIC_C+i&&s_trialModels[i]->numHeaders==1);
            for(j=0;j<(int)AP_TRIAL_LAYOUT_COUNT[i];j++) {
                struct TextureLayout *t=s_trialModels[i]->headers[0].ptrTexLayout[j];
                assert(t->v0<2&&t->v1<2&&t->v2<2&&t->v3<2);
                assert((t->tpage&AP_TPAGE_SIDELOAD_MASK)==AP_TPAGE_TRIAL_LETTER_BIT);
            }
        }
        assert(AP_TrialLetters_Prepare()&&textureCalls==1);
        puts("PASS: real NTSC-U asset import, model graph and texture remap");
    } else puts("SKIP: optional real asset import (set CTR_TEST_BIGFILE)");
    puts("PASS: production trial-letter spawn, pickup, lifecycle, mode gates, sweep and refusal");
    return 0;
}
