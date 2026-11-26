// #203: additive retail C/T/R mechanics for Slide Coliseum and Turbo Track.
#ifdef CTR_AP
#include <common.h>
#include <stdlib.h>
#include "ap_trial_letters.h"
#include "ap_trial_letter_logic.h"
#include "ap_placed_letters.h"
#include "ap_content_plan.h"
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

/* Original block glyphs extruded from three convex strokes. Vertices in
 * each stroke run around its face in model X/Z order. */
static const u8 s_originStrokes[3][3][4][2] = {
    {{{25, 35}, {70, 35}, {70, 215}, {25, 215}},
     {{70, 35}, {225, 35}, {225, 80}, {70, 80}},
     {{70, 170}, {225, 170}, {225, 215}, {70, 215}}},
    {{{25, 35}, {70, 35}, {70, 215}, {25, 215}},
     {{70, 155}, {185, 35}, {185, 95}, {70, 215}},
     {{185, 35}, {230, 35}, {230, 215}, {185, 215}}},
    {{{25, 35}, {70, 35}, {70, 215}, {25, 215}},
     {{70, 100}, {177, 35}, {235, 35}, {70, 132}},
     {{70, 132}, {235, 215}, {174, 215}, {70, 164}}}
};

#define AP_GLYPH_VERTICES (3 * 6 * 2 * 3)
static struct {
    struct Model model;
    struct ModelHeader header;
    struct {
        struct ModelFrame header;
        u8 vertices[AP_GLYPH_VERTICES][3];
    } frame;
    u32 commands[AP_GLYPH_VERTICES + 2];
} s_originModels[3];

static void AP_TrialLetters_BuildOriginModels(void)
{
    static const u8 faces[6][4] = {
        {0, 1, 2, 3}, {7, 6, 5, 4}, {0, 4, 5, 1},
        {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}
    };
    static const u8 triangles[] = {0, 1, 2, 0, 2, 3};
    static const char glyphs[] = "CNK";
    static u32 colors[] = {0x00808080, 0x00585858};
    static struct TextureLayout *texture[1];
    if (texture[0]) return;
    texture[0] = s_trialModels[0]->headers[0].ptrTexLayout[0];
    for (int glyph = 0; glyph < 3; ++glyph) {
        struct Model *model = &s_originModels[glyph].model;
        struct ModelHeader *header = &s_originModels[glyph].header;
        struct ModelFrame *frame = &s_originModels[glyph].frame.header;
        unsigned cursor = 0;
        model->name[0] = glyphs[glyph];
        model->id = STATIC_C + glyph;
        model->numHeaders = 1;
        model->headers = header;
        header->name[0] = glyphs[glyph];
        header->maxDistanceLOD = 0x7fff;
        header->scale = s_trialModels[glyph]->headers[0].scale;
        header->ptrCommandList = (u32)(uintptr_t)s_originModels[glyph].commands;
        header->ptrFrameData = frame;
        header->ptrTexLayout = texture;
        header->ptrColors = colors;
        frame->pos = s_trialModels[0]->headers[0].ptrFrameData->pos;
        frame->vertexOffset = sizeof *frame;
        s_originModels[glyph].commands[0] = 2;
        for (int stroke = 0; stroke < 3; ++stroke) {
            for (int face = 0; face < 6; ++face) {
                for (int j = 0; j < 6; ++j) {
                    unsigned corner = faces[face][triangles[j]];
                    const u8 *point = s_originStrokes[glyph][stroke][corner % 4];
                    u8 *vertex = s_originModels[glyph].frame.vertices[cursor];
                    vertex[0] = point[0];
                    vertex[1] = corner < 4 ? 20 : 58;
                    vertex[2] = point[1] - (point[0] >= 128);
                    s_originModels[glyph].commands[++cursor] =
                        (j % 3 == 0 ? 0x80000000u : 0) | ((j % 3 + 1u) << 16) |
                        (face < 2 ? 0 : 1u << 9) | 1u;
                }
            }
        }
        s_originModels[glyph].commands[cursor + 1] = 0xffffffffu;
    }
}

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

// A content-plan CTR Challenge on Slide Coliseum or Turbo Track (#133 PR 3)
// uses the same authored letters as the legacy trial CTR Challenge.
static int AP_TrialLetters_ContentCtr(struct GameTracker *gGT)
{
#ifdef CTR_CUSTOM_TRACKS
    const struct CustomTrackRaceContext *race=CustomTrack_StandaloneContext(gGT->levelID);
    return race && race->retail && race->mode==AP_CONTENT_MODE_CTR && race->hostLevelID==gGT->levelID;
#else
    (void)gGT;
    return 0;
#endif
}

static int AP_TrialLetters_Active(struct GameTracker *gGT)
{
    int track=gGT->levelID-16;
    if (track<0 || track>=2 || !ctr_cfg_active() ||
        (!AP_TrialLetters_ContentCtr(gGT) &&
         (!ctr_cfg.trial_track_valid[track] || ctr_cfg.trial_track_mode[track]!=2)) ||
        gGT->numPlyrCurrGame!=1 || !(gGT->gameMode1 & ADVENTURE_MODE) ||
        !(gGT->gameMode2 & TOKEN_RACE) ||
        (gGT->gameMode1 & (ADVENTURE_ARENA|ADVENTURE_CUP|RELIC_RACE|MAIN_MENU|BATTLE_MODE|GAME_CUTSCENE))) return 0;
#ifdef CTR_CUSTOM_TRACKS
    if (CustomTrack_ServingLoad(gGT->levelID,0,gGT->cup.cupID) || CustomTrack_ServeFaultReason()) return 0;
#endif
    return 1;
}

static const struct AP_PlacedLetterSet *AP_TrialLetters_Custom(
    struct GameTracker *gGT, const ctr_content_pad **out)
{
#ifdef CTR_CUSTOM_TRACKS
    const struct CustomTrackRaceContext *race = CustomTrack_StandaloneContext(gGT->levelID);
    if (race && !race->retail && !race->cortex && !race->cup &&
        race->mode == AP_CONTENT_MODE_CTR && !CustomTrack_ServeFaultReason() &&
        gGT->numPlyrCurrGame == 1 && (gGT->gameMode1 & ADVENTURE_MODE) &&
        (gGT->gameMode2 & TOKEN_RACE) && ap_content_plan_active() &&
        race->seedEpoch == ap_content_plan_epoch()) {
        const ctr_content_pad *pad = ap_content_plan_pad(race->physicalPad);
        if (pad && pad->placed_letters && !strcmp(pad->track_id, race->trackID)) {
            *out = pad;
            return AP_PlacedLetters_Find(NULL, race->levSha256, race->vrmSha256);
        }
    }
#else
    (void)gGT;
    (void)out;
#endif
    return NULL;
}

int AP_TrialLetters_ReplaceBaked(struct GameTracker *gGT)
{
    const ctr_content_pad *pad = NULL;
    return s_trialAssets == 1 && AP_TrialLetters_Custom(gGT, &pad) != NULL;
}

void AP_TrialLetters_Register(struct GameTracker *gGT)
{
    const ctr_content_pad *pad = NULL;
    const struct AP_PlacedLetterSet *custom = AP_TrialLetters_Custom(gGT, &pad);
    if ((!custom && !AP_TrialLetters_Active(gGT)) || !AP_TrialLetters_Prepare()) return;
    int origin = custom && pad->letter_glyphs[0];
    if (origin) AP_TrialLetters_BuildOriginModels();
    // MainInit, BEFORE UI_INSTANCE_InitAll, including same-level restart.
    for (int i = 0; i < 3; ++i)
        gGT->modelPtr[STATIC_C+i] = origin ? &s_originModels[i].model : s_trialModels[i];
}

static void AP_TrialLetters_Tick(struct Thread *t)
{
    struct GameTracker *gGT=sdata->gGT;
    struct Driver *d;
    const ctr_content_pad *pad = NULL;
    SVec3 prev, curr;
    int letter=t->inst->model->id-STATIC_C;
    if (t->flags & THREAD_FLAG_DEAD) return;
    RB_CtrLetter_ThTick(t);
    if (sdata->Loading.stage!=LOAD_IDLE ||
        (!AP_TrialLetters_Active(gGT) && !AP_TrialLetters_Custom(gGT, &pad)) ||
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
    const ctr_content_pad *pad = NULL;
    const struct AP_PlacedLetterSet *custom = AP_TrialLetters_Custom(gGT, &pad);
    int i,axis,track=gGT->levelID-16;
    if ((!custom && !AP_TrialLetters_Active(gGT)) || s_trialAssets!=1) return;
    for (i=0; i<3; ++i) {
        const short *anchor = custom ? custom->anchors[i] : AP_TRIAL_LETTER_POS[track][i];
        struct Instance *inst=INSTANCE_BirthWithThread(STATIC_C+i,names[i],SMALL,STATIC,
            AP_TrialLetters_Tick,sizeof(struct CtrLetter),NULL);
        struct CtrLetter *obj;
        if (!inst) {
            AP_LogLine("[AP TRIAL LETTERS] instance/thread allocation failed; restart this challenge\n");
            continue;
        }
        obj=inst->thread->object;
        memset(obj,0,sizeof(*obj));
        obj->rot.y=custom ? anchor[3] : AP_TRIAL_LETTER_YAW[track][i];
        ConvertRotToMatrix(&inst->matrix,&obj->rot);
        for (axis=0; axis<3; ++axis) inst->matrix.t[axis]=anchor[axis];
        s_trialLetterBox[i].min.x=s_trialLetterBoxOffset[i].min.x+anchor[0];
        s_trialLetterBox[i].min.y=s_trialLetterBoxOffset[i].min.y+anchor[1];
        s_trialLetterBox[i].min.z=s_trialLetterBoxOffset[i].min.z+anchor[2];
        s_trialLetterBox[i].max.x=s_trialLetterBoxOffset[i].max.x+anchor[0];
        s_trialLetterBox[i].max.y=s_trialLetterBoxOffset[i].max.y+anchor[1];
        s_trialLetterBox[i].max.z=s_trialLetterBoxOffset[i].max.z+anchor[2];
        inst->scale=(SVec3){{0x1800,0x1800,0x1800}};
        inst->colorRGBA=0xffc8000;
        inst->flags=DRAW_COLLISION_MASK|DRAW_TRANSPARENT|USE_SPECULAR_LIGHT;
        inst->thread->funcThCollide=(void (*)(struct Thread *))RB_CtrLetter_ThCollide;
    }
    // No retained instance/thread handles: the engine owns and clears all
    // three on load/restart, and the next MainInit births a fresh complete set.
    AP_LogLine(custom ? "[AP LETTER] custom letter set spawned for this attempt\n"
                      : "[AP TRIAL LETTERS] authored C/T/R set spawned for this attempt\n");
}
#endif
