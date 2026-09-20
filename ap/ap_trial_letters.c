// #203: additive retail C/T/R mechanics for Slide Coliseum and Turbo Track.
#ifdef CTR_AP
#include <common.h>
#include <stdlib.h>
#include "ap_trial_letters.h"
#include "ap_trial_letter_logic.h"
#include "ap_retail_asset.h"
#include "ap_seedcfg.h"
#include "ap_hooks.h"
#include <platform/native_gpu.h>
#include <platform/native_renderer.h>
#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h>
#endif

// Process-owned, aligned storage, never MEMPACK or a borrowed track buffer.
static u32 s_trialLev[768000/4];
static struct Model *s_trialModels[3];
static struct BoundingBox s_trialLetterBoxOffset[3];
static struct BoundingBox s_trialLetterBox[3];
static int s_trialAssets; // 0 untried, 1 ready, -1 refused (logged once)

static int AP_TrialLetters_FindRetailBoxes(unsigned char *body, unsigned map)
{
    struct Level *level=(struct Level *)body;
    struct mesh_info *mesh=level->ptr_mesh_info;
    struct InstDef *defs=level->ptrInstDefs;
    struct InstDef *letterDefs[3]={0};
    uintptr_t lo=(uintptr_t)body, hi=lo+map;
    int found[3]={0}, i, j, k;
    if (!mesh || !defs || (uintptr_t)mesh<lo || (uintptr_t)(mesh+1)>hi ||
        (uintptr_t)defs<lo || level->numInstances>65536 ||
        (uintptr_t)(defs+level->numInstances)>hi || !mesh->bspRoot ||
        (uintptr_t)mesh->bspRoot<lo || mesh->numBspNodes<=0 || mesh->numBspNodes>65536 ||
        (uintptr_t)(mesh->bspRoot+mesh->numBspNodes)>hi) return 0;
    for (i=0; i<(int)level->numInstances; ++i)
        if (defs[i].modelID>=STATIC_C && defs[i].modelID<=STATIC_R)
            letterDefs[defs[i].modelID-STATIC_C]=&defs[i];
    for (i=0; i<3; ++i) if (!letterDefs[i]) return 0;
    for (i=0; i<mesh->numBspNodes; ++i) {
        struct BSP *node=&mesh->bspRoot[i], *hit;
        if (!(node->flag & BSP_NODE_FLAG_LEAF) || !node->data.leaf.bspHitboxArray) continue;
        hit=node->data.leaf.bspHitboxArray;
        if ((uintptr_t)hit<lo || (uintptr_t)(hit+1)>hi) return 0;
        for (j=0; j<2048 && (uintptr_t)(hit+j+1)<=hi && hit[j].flag; ++j) {
            if ((hit[j].flag>>8)!=BSP_HITBOX_CLASS_TOUCH) continue;
            for (k=0; k<3; ++k) if (!found[k] && hit[j].data.hitbox.instDef==letterDefs[k]) {
                s_trialLetterBoxOffset[k]=hit[j].box;
                s_trialLetterBoxOffset[k].min.x-=letterDefs[k]->pos.x;
                s_trialLetterBoxOffset[k].min.y-=letterDefs[k]->pos.y;
                s_trialLetterBoxOffset[k].min.z-=letterDefs[k]->pos.z;
                s_trialLetterBoxOffset[k].max.x-=letterDefs[k]->pos.x;
                s_trialLetterBoxOffset[k].max.y-=letterDefs[k]->pos.y;
                s_trialLetterBoxOffset[k].max.z-=letterDefs[k]->pos.z;
                found[k]=1;
            }
        }
    }
    return found[0] && found[1] && found[2];
}

int AP_TrialLetters_Prepare(void)
{
    unsigned char *raw=(unsigned char *)s_trialLev, *vrm;
    unsigned char rgba[64*2*4];
    unsigned texture;
    int size=0, i, j;
    if (s_trialAssets) return s_trialAssets==1;
    // Don't latch a failure before the directory has been read at startup.
    if (!sdata->ptrBigfile1) return 0;
    s_trialAssets=-1;
    // Raw read first, including DRAM header; authenticate BEFORE relocation.
    if (!AP_RetailAsset_ReadSubfile(25,0,raw,sizeof(s_trialLev),&size) ||
        size!=AP_TRIAL_LEV_SIZE || !AP_TrialDigest(raw,size,AP_TRIAL_LEV_SHA) ||
        !AP_TrialPointerMapValid(raw,size)) goto refused;
    vrm=(unsigned char *)malloc(460800);
    if (!vrm) goto refused;
    i=AP_RetailAsset_ReadSubfile(24,0,vrm,460800,&size)!=NULL &&
      size==AP_TRIAL_VRM_SIZE && AP_TrialDigest(vrm,size,AP_TRIAL_VRM_SHA) &&
      AP_TrialTextureDecode(vrm,size,rgba);
    free(vrm);
    if (!i) goto refused;
    texture=(unsigned)NativeRenderer_CreateRGBATexture(64,2,rgba);
    if (!texture) goto refused;
    NativeGpu_SetTrialLetterTexture(texture,64,2);
    {
        unsigned char *body=raw+4;
        unsigned map=AP_TrialU32(raw);
        LOAD_RunPtrMap((char *)body,(int *)(body+map+4),AP_TrialU32(body+map)/4);
        for (i=0; i<3; ++i) {
            struct Model *m=(struct Model *)(body+AP_TRIAL_MODEL_OFFSET[i]);
            s_trialModels[i]=m;
            // Audited texture tables: C 72, T 27, R 68 triangles; both
            // palette ramps use separate halves of this tiny owned atlas.
            for (j=0; j<(int)AP_TRIAL_LAYOUT_COUNT[i]; ++j) {
                struct TextureLayout *t=m->headers[0].ptrTexLayout[j];
                t->v0-=244; t->v1-=244; t->v2-=244; t->v3-=244;
                t->tpage=(t->tpage & 0x60) | AP_TPAGE_TRIAL_LETTER_BIT;
            }
        }
        if (!AP_TrialLetters_FindRetailBoxes(body,map)) goto refused;
    }
    s_trialAssets=1;
    AP_LogLine("[AP TRIAL LETTERS] retail C/T/R models and independent texture ready\n");
    return 1;
refused:
    AP_LogLine("[AP TRIAL LETTERS] CTR Challenge unavailable: retail letter assets could not be verified/loaded; Trophy and Relic remain available\n");
    return 0;
}

static int AP_TrialLetters_Active(struct GameTracker *gGT)
{
    int track=gGT->levelID-16;
    if (track<0 || track>=2 || !ctr_cfg_active() ||
        !ctr_cfg.trial_track_valid[track] || ctr_cfg.trial_track_mode[track]!=2 ||
        gGT->numPlyrCurrGame!=1 || !(gGT->gameMode1 & ADVENTURE_MODE) ||
        !(gGT->gameMode2 & TOKEN_RACE) ||
        (gGT->gameMode1 & (ADVENTURE_ARENA|ADVENTURE_CUP|RELIC_RACE|MAIN_MENU|BATTLE_MODE|GAME_CUTSCENE))) return 0;
#ifdef CTR_CUSTOM_TRACKS
    if (CustomTrack_ServingLoad(gGT->levelID,0,gGT->cup.cupID) || CustomTrack_ServeFaultReason()) return 0;
#endif
    return 1;
}

void AP_TrialLetters_Register(struct GameTracker *gGT)
{
    int i;
    if (!AP_TrialLetters_Active(gGT) || !AP_TrialLetters_Prepare()) return;
    // MainInit, BEFORE UI_INSTANCE_InitAll, including same-level restart.
    for (i=0; i<3; ++i) gGT->modelPtr[STATIC_C+i]=s_trialModels[i];
}

static void AP_TrialLetters_Tick(struct Thread *t)
{
    struct GameTracker *gGT=sdata->gGT;
    struct Driver *d;
    SVec3 prev, curr;
    int letter=t->inst->model->id-STATIC_C;
    if (t->flags & THREAD_FLAG_DEAD) return;
    RB_CtrLetter_ThTick(t);
    if (sdata->Loading.stage!=LOAD_IDLE || !AP_TrialLetters_Active(gGT) ||
        (gGT->gameMode1 & (PAUSE_ALL|START_OF_RACE|END_OF_RACE))) return;
    d=gGT->drivers[0];
    if (!d || !d->instSelf || !d->instSelf->thread ||
        (d->actionsFlagSet & (ACTION_RACE_FINISHED|ACTION_WARP))) return;
    prev.x=(s16)CTR_MipsAddLo(CTR_MipsSra(d->posPrev.x,8),d->originToCenter.x);
    prev.y=(s16)CTR_MipsAddLo(CTR_MipsSra(d->posPrev.y,8),d->originToCenter.y);
    prev.z=(s16)CTR_MipsAddLo(CTR_MipsSra(d->posPrev.z,8),d->originToCenter.z);
    curr.x=(s16)CTR_MipsAddLo(CTR_MipsSra(d->posCurr.x,8),d->originToCenter.x);
    curr.y=(s16)CTR_MipsAddLo(CTR_MipsSra(d->posCurr.y,8),d->originToCenter.y);
    curr.z=(s16)CTR_MipsAddLo(CTR_MipsSra(d->posCurr.z,8),d->originToCenter.z);
    if (AP_TrialLetterHit(&prev,&curr,&s_trialLetterBox[letter])) {
        struct ScratchpadStruct collision={0};
        collision.Input1.modelID=DYNAMIC_PLAYER;
        // Shared retail collision owns item gating, check dispatch, real HUD
        // animation, sound, count and thread death for admitted trial letters.
        RB_CtrLetter_ThCollide(t,d->instSelf->thread,t->funcThCollide,&collision);
    }
}

void AP_TrialLetters_Spawn(struct GameTracker *gGT)
{
    static const char names[3][16]={"ap-trial-c","ap-trial-t","ap-trial-r"};
    int i,axis,track=gGT->levelID-16;
    if (!AP_TrialLetters_Active(gGT) || s_trialAssets!=1) return;
    for (i=0; i<3; ++i) {
        struct Instance *inst=INSTANCE_BirthWithThread(STATIC_C+i,names[i],SMALL,STATIC,
            AP_TrialLetters_Tick,sizeof(struct CtrLetter),NULL);
        struct CtrLetter *obj;
        if (!inst) {
            AP_LogLine("[AP TRIAL LETTERS] instance/thread allocation failed; restart this challenge\n");
            continue;
        }
        obj=inst->thread->object;
        memset(obj,0,sizeof(*obj));
        obj->rot.y=AP_TRIAL_LETTER_YAW[track][i];
        ConvertRotToMatrix(&inst->matrix,&obj->rot);
        for (axis=0; axis<3; ++axis) inst->matrix.t[axis]=AP_TRIAL_LETTER_POS[track][i][axis];
        s_trialLetterBox[i].min.x=s_trialLetterBoxOffset[i].min.x+AP_TRIAL_LETTER_POS[track][i][0];
        s_trialLetterBox[i].min.y=s_trialLetterBoxOffset[i].min.y+AP_TRIAL_LETTER_POS[track][i][1];
        s_trialLetterBox[i].min.z=s_trialLetterBoxOffset[i].min.z+AP_TRIAL_LETTER_POS[track][i][2];
        s_trialLetterBox[i].max.x=s_trialLetterBoxOffset[i].max.x+AP_TRIAL_LETTER_POS[track][i][0];
        s_trialLetterBox[i].max.y=s_trialLetterBoxOffset[i].max.y+AP_TRIAL_LETTER_POS[track][i][1];
        s_trialLetterBox[i].max.z=s_trialLetterBoxOffset[i].max.z+AP_TRIAL_LETTER_POS[track][i][2];
        inst->scale=(SVec3){{0x1800,0x1800,0x1800}};
        inst->colorRGBA=0xffc8000;
        inst->flags=DRAW_COLLISION_MASK|DRAW_TRANSPARENT|USE_SPECULAR_LIGHT;
        inst->thread->funcThCollide=(void (*)(struct Thread *))RB_CtrLetter_ThCollide;
    }
    // No retained instance/thread handles: the engine owns and clears all
    // three on load/restart, and the next MainInit births a fresh complete set.
    AP_LogLine("[AP TRIAL LETTERS] authored C/T/R set spawned for this attempt\n");
}
#endif
