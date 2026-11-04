// #259: additive real Crystal Challenge objects. The existing swept collision
// helper also serves the additive trial letters; retail owns pickup/HUD/death.
#ifdef CTR_AP
#include <common.h>
#include "ap_hooks.h"
#include "ap_content_plan.h"
#include "ap_arena_content.h"
#include "ap_trial_letter_logic.h"
#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h>
static struct InstDef ap_arcade_defs[20];
static struct BoundingBox ap_arcade_boxes[20];
static uint64_t ap_arcade_epoch;
static int ap_arcade_level=-1, ap_arcade_failed;

int AP_ContentCrystalIntent(void)
{
    struct GameTracker *g=sdata->gGT;
    const struct CustomTrackRaceContext *c=g?CustomTrack_StandaloneContext(g->levelID):NULL;
    return c && c->crystal;
}

const ctr_content_pad *AP_ContentCrystalPad(void)
{
    struct GameTracker *g=sdata->gGT;
    const struct CustomTrackRaceContext *c;
    const ctr_content_pad *p;
    if(!g || !ctr_cfg_active() || !ap_content_plan_active() ||
       (g->gameMode1 & (ADVENTURE_MODE|CRYSTAL_CHALLENGE)) != (ADVENTURE_MODE|CRYSTAL_CHALLENGE) ||
       (g->gameMode1 & (ARCADE_MODE|ADVENTURE_CUP|ADVENTURE_BOSS|ADVENTURE_ARENA|RELIC_RACE|MAIN_MENU|BATTLE_MODE)) ||
       (g->gameMode2 & TOKEN_RACE) ||
       g->numPlyrCurrGame!=1) return NULL;
    c=CustomTrack_StandaloneContext(g->levelID);
    if(!c || !c->crystal || !c->retail || !c->seedEpoch || c->seedEpoch!=ap_content_plan_epoch())return NULL;
    p=ap_content_plan_pad(c->physicalPad);
    if(!p || !p->occupied || !p->crystal || p->retail_id!=g->levelID ||
       c->hostLevelID!=p->retail_id || c->returnHub!=p->hub || c->laps!=p->laps || c->slot!=p->custom_slot ||
       c->trophyLocation!=p->trophy_location || c->timeLimitTicks!=p->time_limit_ticks ||
       strcmp(c->entryID,p->entry_id) || strcmp(c->trackID,p->track_id))return NULL;
    return p;
}

static int AP_ArcadeCrystalIndex(struct Instance *inst)
{
    int i;
    if(ap_arcade_failed || !ap_arcade_epoch || ap_arcade_epoch!=ap_content_plan_epoch() ||
       sdata->gGT->levelID!=ap_arcade_level || !inst)return -1;
    for(i=0;i<20;++i)if(inst->instDef==&ap_arcade_defs[i] && ap_arcade_defs[i].ptrInstance==inst)return i;
    return -1;
}

static void AP_ArcadeCrystalTick(struct Thread *t)
{
    struct GameTracker *g=sdata->gGT;
    struct Driver *d;
    SVec3 prev,curr;
    int i=AP_ArcadeCrystalIndex(t->inst);
    if(t->flags & THREAD_FLAG_DEAD)return;
    if(i<0 || !AP_ContentCrystalPad())return;
    RB_Crystal_ThTick(t);
    if(sdata->Loading.stage!=LOAD_IDLE || (g->gameMode1 & (PAUSE_ALL|START_OF_RACE|END_OF_RACE)))return;
    d=g->drivers[0];
    if(!d || !d->instSelf || !d->instSelf->thread ||
       (d->actionsFlagSet & (ACTION_RACE_FINISHED|ACTION_WARP)))return;
    prev.x=(s16)CTR_MipsAddLo(CTR_MipsSra(d->posPrev.x,8),d->originToCenter.x);
    prev.y=(s16)CTR_MipsAddLo(CTR_MipsSra(d->posPrev.y,8),d->originToCenter.y);
    prev.z=(s16)CTR_MipsAddLo(CTR_MipsSra(d->posPrev.z,8),d->originToCenter.z);
    curr.x=(s16)CTR_MipsAddLo(CTR_MipsSra(d->posCurr.x,8),d->originToCenter.x);
    curr.y=(s16)CTR_MipsAddLo(CTR_MipsSra(d->posCurr.y,8),d->originToCenter.y);
    curr.z=(s16)CTR_MipsAddLo(CTR_MipsSra(d->posCurr.z,8),d->originToCenter.z);
    if(AP_TrialLetterHit(&prev,&curr,&ap_arcade_boxes[i])) {
        struct ScratchpadStruct collision={0};
        collision.Input1.modelID=DYNAMIC_PLAYER;
        RB_Crystal_LInC(t->inst,d->instSelf->thread,&collision);
    }
}

void AP_ArcadeCrystals_Spawn(struct GameTracker *g)
{
    const ctr_content_pad *p;
    int a,i,axis;
    ap_arcade_epoch=0;ap_arcade_level=-1;ap_arcade_failed=0;
    memset(ap_arcade_defs,0,sizeof ap_arcade_defs);
    p=AP_ContentCrystalPad();
    if(!p || (a=AP_ArcadeArenaIndex(g->levelID))<0)return;
    // Even a failed allocation must never become an automatic zero-crystal win.
    g->numCrystalsInLEV=20;
    if(!g->modelPtr[STATIC_CRYSTAL])goto refused;
    for(i=0;i<20;++i) {
        struct InstDef *def=&ap_arcade_defs[i];
        struct Instance *inst;
        snprintf(def->name,sizeof def->name,"ap-crystal-%02d",i+1);
        def->modelID=STATIC_CRYSTAL;def->model=g->modelPtr[STATIC_CRYSTAL];
        for(axis=0;axis<3;++axis) {def->pos.v[axis]=ap_arcade_anchors[a][i][axis];def->scale.v[axis]=0x1000;}
        inst=INSTANCE_Birth3D(def->model,def->name,NULL);
        if(!inst)goto refused;
        def->ptrInstance=inst;inst->instDef=def;inst->scale=def->scale;
        for(axis=0;axis<3;++axis)inst->matrix.t[axis]=def->pos.v[axis];
        RB_Crystal_LInB(inst);
        if(!inst->thread)goto refused;
        inst->thread->funcThTick=AP_ArcadeCrystalTick;
        // Centered retail crystal touch envelope, including its vertical bob.
        ap_arcade_boxes[i].min=(SVec3){{def->pos.x-46,def->pos.y-86,def->pos.z-46}};
        ap_arcade_boxes[i].max=(SVec3){{def->pos.x+46,def->pos.y+178,def->pos.z+46}};
    }
    ap_arcade_epoch=ap_content_plan_epoch();ap_arcade_level=g->levelID;
    AP_LogLine("[AP ARENA] authored twenty-crystal set ready; timer is provisional\n");
    return;
refused:
    ap_arcade_failed=1;
    for(i=0;i<20;++i)if(ap_arcade_defs[i].ptrInstance) {
        struct Instance *inst=ap_arcade_defs[i].ptrInstance;
        inst->scale=(SVec3){{0,0,0}};
        if(inst->thread)inst->thread->flags |= THREAD_FLAG_DEAD;
    }
    AP_LogLine("[AP ARENA] crystal construction failed; returning without a reward\n");
}

void AP_ArcadeCrystals_OnFrame(struct GameTracker *g)
{
    (void)g;
    if(ap_arcade_failed && sdata->Loading.stage==LOAD_IDLE) {
        const ctr_content_pad *p=AP_ContentCrystalPad();
        ap_arcade_failed=0;
        sdata->Loading.OnBegin.RemBitsConfig0 |= CRYSTAL_CHALLENGE;
        sdata->Loading.OnBegin.AddBitsConfig0 |= ADVENTURE_ARENA;
        MainRaceTrack_RequestLoad(p?p->hub:26);
    }
}

void AP_NotifyContentCrystalPickup(struct Instance *inst,int driverID)
{
    const ctr_content_pad *p=AP_ContentCrystalPad();
    struct GameTracker *g=sdata->gGT;
    int i,index=-1;
    long code;
    if(!p || p->crystal_count!=20 || driverID!=0 || !inst || !inst->instDef || !g->level1)return;
    if(AP_ArcadeArenaIndex(g->levelID)>=0)index=AP_ArcadeCrystalIndex(inst);
    else if(g->level1->ptrInstDefs) {
        int base=AP_CrystalInstanceBase(AP_CrystalArenaIndex(g->levelID),g->numPlyrCurrGame);
        for(i=0;i<(int)g->level1->numInstances;++i)if(inst->instDef==&g->level1->ptrInstDefs[i]) {index=i-base;break;}
    }
    if(index<0 || index>=20)return;
    code=(long)p->crystal_locations[index];
    if(!ap_net_location_exists(code) || ap_net_location_checked(code))return;
    ap_net_send_location(code);AP_PerfNoteCheckSent();AP_FeedOnLocationSent(code);ap_state_gen++;
}

int AP_NotifyContentCrystalComplete(void)
{
    const ctr_content_pad *p=AP_ContentCrystalPad();
    struct GameTracker *g=sdata->gGT;
    if(!p || !g->drivers[0] || g->numCrystalsInLEV!=20 || g->drivers[0]->numCrystals<20 ||
       (AP_ArcadeArenaIndex(g->levelID)>=0 && (ap_arcade_failed || ap_arcade_epoch!=ap_content_plan_epoch())) ||
       AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_ADV_REWARD))return 0;
    return AP_EmitClassCheck((long)p->trophy_location,0,-1,-1,1,
        "[AP CHECK] arena entry=%s track=%s slot=%d location=%ld\n",p->entry_id,p->track_id,0,(long)p->trophy_location);
}
#else
int AP_ContentCrystalIntent(void) {return 0;}
const ctr_content_pad *AP_ContentCrystalPad(void) {return NULL;}
void AP_ArcadeCrystals_Spawn(struct GameTracker *g) {(void)g;}
void AP_ArcadeCrystals_OnFrame(struct GameTracker *g) {(void)g;}
void AP_NotifyContentCrystalPickup(struct Instance *i,int d) {(void)i;(void)d;}
int AP_NotifyContentCrystalComplete(void) {return 0;}
#endif
#endif
