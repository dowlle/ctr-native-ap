// cc -m32 -DCTR_NATIVE -DCTR_AP -DBUILD=926 -ffunction-sections -fdata-sections
// -I include -I . tools/test-boss-mine-lifecycle.c -Wl,--gc-sections
// Production mine pool, hazard and weapon ticks with controlled terrain and effects.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <common.h>

struct sData sdata_static;
struct Data data;
struct rData rdata;
struct OverlayDATA_231 D231;
static struct GameTracker tracker;
static struct Level level;
static struct Driver owner;
static struct Instance kart;
static struct Thread kartThread;
static struct Model models[256];
static struct MineWeapon mines[16];
static struct Thread threads[16];
static struct Instance instances[16];
static struct Crate pickupCrate;
static struct Thread pickupCrateThread;
static struct Instance pickupCrateInst;
static u8 scratch[4096];
u8 *gCTRNativeScratchpadBase = scratch;

static int rulesLevel, expiryEnabled, ground, hurtCount, shatters, blasts, lastSound, doorSearches, genericExplosions;
int AP_BossRetailRulesLevel(int levelID) { return rulesLevel < 0 ? levelID : rulesLevel; }
int AP_BossMineExpiryNow(void) { return expiryEnabled && (tracker.gameMode1 & ADVENTURE_BOSS); }
int PickupBots_AdapterProtect(struct Driver *d) { (void)d; return 0; }
void PlaySound3D(u32 id, struct Instance *inst) { (void)inst; lastSound = id; }
void PROC_StartSearch_Self(struct ScratchpadStruct *sps)
{
    assert(sps->Union.ThBuckColl.funcCallback == RB_Potion_OnShatter_TeethCallback);
    ++doorSearches;
}
void RB_Blowup_Init(struct Instance *inst) { (void)inst; ++blasts; }
void RB_RainCloud_Init(struct Driver *d) { (void)d; assert(0); }
int VehPickState_NewState(struct Driver *d, int type, struct Driver *attacker, int reason)
{ (void)d; (void)type; (void)attacker; (void)reason; assert(0); return 0; }
u32 BOTS_ChangeState(struct Driver *d, int type, struct Driver *attacker, int reason)
{ assert(d == &owner && attacker == &owner && type == 1 && reason == 2); ++hurtCount; return 0; }
u16 INSTANCE_GetNumAnimFrames(struct Instance *inst, int anim) { (void)inst; (void)anim; return 1; }
void RB_MakeInstanceReflective(struct ScratchpadStruct *sps, struct Instance *inst) { (void)sps; (void)inst; }
void VehPhysForce_RotAxisAngle(MATRIX *m, s16 *axis, s16 angle) { (void)m; (void)axis; (void)angle; }
void RB_Teeth_OpenDoor(struct Instance *inst) { (void)inst; assert(0); }
void COLL_SearchBSP_CallbackQUADBLK(const SVec3 *a, const SVec3 *b, struct ScratchpadStruct *sps, s32 radius)
{
    (void)a; (void)b; (void)radius;
    sps->boolDidTouchHitbox = 0;
    sps->boolDidTouchQuadblock = ground;
    sps->Union.QuadBlockColl.hitPos.y = 0;
}
struct Instance *INSTANCE_BirthWithThread(int id, const char *name, int pool, int bucket,
                                          void *tick, int size, struct Thread *parent)
{
    (void)name; (void)pool; (void)bucket; (void)tick; (void)size; (void)parent;
    if (id == STATIC_SHOCKWAVE_GREEN || id == STATIC_SHOCKWAVE_RED)
        ++shatters;
    else {
        assert(id == 0x26);
        ++genericExplosions;
    }
    return NULL; // Exhaust the cosmetic instance pool, while keeping door search live.
}
void ThTick_SetAndExec(struct Thread *t, void (*tick)(struct Thread *)) { t->funcThTick = tick; tick(t); }
void ThTick_FastRET(struct Thread *t) { (void)t; }
void PROC_DestroyInstance(struct Thread *t) { (void)t; assert(0); }
struct Particle *Particle_Init(u32 num, struct IconGroup *group, struct ParticleEmitter *emit)
{ (void)num; (void)group; (void)emit; return NULL; }
void Particle_FuncPtr_PotionShatter(struct Particle *p) { (void)p; assert(0); }
void LHMatrix_Parent(struct Instance *dst, struct Instance *src, const SVec3 *v)
{ (void)dst; (void)src; (void)v; assert(0); }
int MixRNG_Scramble(void) { assert(0); return 0; }
void ConvertRotToMatrix(MATRIX *m, const SVec3 *v) { (void)m; (void)v; assert(0); }
void MatrixRotate(MATRIX *dst, MATRIX *src, MATRIX *rot)
{ (void)dst; (void)src; (void)rot; assert(0); }

#include "game/LIST.c"
#include "game/231/RB_MinePool.c"
#include "game/231/RB_GenericMine.c"
#include "game/231/RB_Hazard.c"
#include "game/231/RB_Explosion.c"
#include "game/231/RB_Potion.c"
#include "game/231/RB_TNT.c"

static void setup(int venue, int garageHome)
{
    memset(&tracker, 0, sizeof tracker);
    memset(&level, 0, sizeof level);
    memset(&owner, 0, sizeof owner);
    memset(&kart, 0, sizeof kart);
    memset(mines, 0, sizeof mines);
    memset(threads, 0, sizeof threads);
    memset(instances, 0, sizeof instances);
    memset(scratch, 0, sizeof scratch);
    rulesLevel = garageHome;
    expiryEnabled = ground = hurtCount = shatters = blasts = lastSound = doorSearches = genericExplosions = 0;
    sdata->gGT = &tracker;
    tracker.levelID = venue;
    tracker.level1 = &level;
    tracker.gameMode1 = ADVENTURE_BOSS;
    tracker.numPlyrCurrGame = 1;
    tracker.elapsedTimeMS = 32;
    tracker.drivers[0] = &owner;
    owner.instSelf = &kart;
    owner.actionsFlagSet = ACTION_BOT;
    owner.kartState = KS_NORMAL;
    kart.thread = &kartThread;
    kartThread.object = &owner;
    kart.matrix.t[0] = 500;
    RB_MinePool_Init();
}

static void add(int i, int model, int thrown)
{
    models[model].id = model;
    instances[i].model = &models[model];
    instances[i].thread = &threads[i];
    threads[i].inst = &instances[i];
    threads[i].object = &mines[i];
    threads[i].funcThTick = RB_GenericMine_ThTick;
    mines[i].instParent = &kart;
    mines[i].frameCount_DontHurtParent = 10;
    if (thrown) {
        mines[i].extraFlags = 2;
        mines[i].velocity.y = 0x30;
    }
    RB_MinePool_Add(&mines[i]);
}

static void moveAtCheckpoint(int x, int remaining, int checkpoint)
{
    const SVec3 backward = {.x = -4096};
    owner.posCurr.x = x * 256;
    RB_MinePool_BossProgress(&owner, remaining, 800, checkpoint, &backward);
}

static void move(int x, int remaining)
{
    moveAtCheckpoint(x, remaining, 0);
}

static void bossDrop(int i, int model, int x, int thrown)
{
    RB_MinePool_BossShot(&owner);
    add(i, model, thrown);
    instances[i].matrix.t[0] = x;
    RB_MinePool_BossMine(&mines[i], &instances[i]);
    RB_MinePool_BossShot(NULL);
}

static void blockPickupCrate(int i)
{
    pickupCrate.boolPauseCooldown = 1;
    pickupCrateThread.object = &pickupCrate;
    models[PU_RANDOM_CRATE].id = PU_RANDOM_CRATE;
    pickupCrateInst.model = &models[PU_RANDOM_CRATE];
    pickupCrateInst.thread = &pickupCrateThread;
    mines[i].crateInst = &pickupCrateInst;
}

static void testHalfLap(void)
{
    setup(TURBO_TRACK, PAPU_PYRAMID);
    expiryEnabled = 1;
    move(0, 100);
    bossDrop(0, STATIC_BEAKER_GREEN, -20, 0);
    bossDrop(1, STATIC_CRATE_TNT, 0, 0);
    blockPickupCrate(1);
    bossDrop(2, PU_EXPLOSIVE_CRATE, 0, 0);
    add(3, STATIC_BEAKER_RED, 0);  // Ordinary shot, outside the boss firing path.
    move(80, 20);
    move(110, 790);  // Over the finish while still lap zero.
    move(90, 10);    // Reverse across the same line.
    move(200, 700);
    move(379, 521);
    assert(D231.minePoolTaken.count == 4);
    move(380, 520);
    assert(mines[0].boolDestroyed && !mines[1].boolDestroyed && !mines[2].boolDestroyed);
    move(400, 500);
    assert(mines[1].boolDestroyed && mines[2].boolDestroyed && !mines[3].boolDestroyed);
    assert(!pickupCrate.boolPauseCooldown && !mines[1].crateInst);
    assert(D231.minePoolTaken.count == 1 && D231.minePoolTaken.first == &mines[3].weaponSlot231->item);
    kart.matrix.t[0] = -20;
    mines[0].frameCount_DontHurtParent = 0;
    RB_GenericMine_ThTick(&threads[0]);
    assert(shatters == 1 && lastSound == 0x3f && doorSearches == 1 && hurtCount == 0);
    RB_GenericMine_ThTick(&threads[1]);
    assert(blasts == 1 && lastSound == 0x3d);
    RB_GenericMine_ThTick(&threads[2]);
    assert(blasts == 2 && lastSound == 0x3f);
}

static void testScopeAndDiscontinuities(void)
{
    setup(PAPU_PYRAMID, PAPU_PYRAMID);
    move(0, 800);
    bossDrop(0, STATIC_BEAKER_GREEN, 0, 0);
    move(200, 600);
    move(400, 400);
    assert(!mines[0].boolDestroyed);  // Home venue with retail kit.
    expiryEnabled = 1;
    tracker.gameMode1 = ARCADE_MODE;
    bossDrop(1, STATIC_BEAKER_GREEN, 400, 0);
    move(500, 300);
    assert(!mines[1].boolDestroyed);

    setup(TURBO_TRACK, PAPU_PYRAMID);
    expiryEnabled = 1;
    move(0, 799);
    bossDrop(0, STATIC_BEAKER_GREEN, 0, 0);
    move(100, 699);
    move(100, 399);  // An irregular checkpoint changes the projection without movement.
    move(100, 699);  // Neither direction of the jump counts.
    owner.kartState = KS_MASK_GRABBED;
    move(9000, 150);
    owner.kartState = KS_NORMAL;
    move(10000, 750);  // The recovery sample is behind the old reference.
    move(10050, 700);
    move(10200, 550);
    assert(!mines[0].boolDestroyed);
    RB_MinePool_BossProgress(&owner, 0, 0, -1, NULL);
    move(10201, 500);  // First valid sample after the missing checkpoint.
    assert(!mines[0].boolDestroyed);
    owner.actionsFlagSet |= ACTION_RACE_FINISHED;  // The player is still racing.
    move(10300, 401);
    assert(!mines[0].boolDestroyed);
    move(10301, 400);
    assert(mines[0].boolDestroyed);

    setup(TURBO_TRACK, PAPU_PYRAMID);
    expiryEnabled = 1;
    move(0, 799);
    bossDrop(0, STATIC_BEAKER_GREEN, 0, 0);
    move(100, 699);
    const struct BossMineState saved = s_bossMineState;
    RB_MinePool_ResetBossProgress();
    s_bossMineState = saved;
    move(300, 499);
    move(399, 400);
    assert(!mines[0].boolDestroyed);
    move(400, 399);
    assert(mines[0].boolDestroyed);

}

static void testShortCustomCheckpointGaps(void)
{
    setup(TURBO_TRACK, PAPU_PYRAMID);
    expiryEnabled = 1;
    struct CheckpointNode nodes[5];
    memset(nodes, 0xff, sizeof nodes);
    nodes[0].nextIndex_forward = 1;
    nodes[1].nextIndex_left = 2;
    nodes[2].nextIndex_forward = 3;
    level.cnt_restart_points = 5;
    level.ptr_restart_points = nodes;

    moveAtCheckpoint(0, 799, 0);
    bossDrop(0, STATIC_BEAKER_GREEN, 0, 0);
    moveAtCheckpoint(30, 649, 1);  // The reported 150-unit change is larger than the world movement.
    moveAtCheckpoint(60, 799, 0);  // The road label flickers backwards while the boss drives forward.
    moveAtCheckpoint(90, 649, 1);
    assert(s_bossMineState.progress[owner.driverID].distance == 150); // The transition counts once.
    moveAtCheckpoint(120, 499, 4);  // An unrelated checkpoint cannot claim the jump.
    moveAtCheckpoint(150, 649, 1);
    moveAtCheckpoint(180, 499, 2);
    moveAtCheckpoint(150, 649, 1);  // Connected reverse motion does subtract the distance.
    moveAtCheckpoint(180, 499, 2);
    assert(!mines[0].boolDestroyed);
    moveAtCheckpoint(210, 399, 3);
    assert(mines[0].boolDestroyed);
}

static void testBlastBeforeMineTick(void)
{
    setup(TURBO_TRACK, PAPU_PYRAMID);
    ground = 1;
    add(0, STATIC_CRATE_TNT, 0);
    add(1, STATIC_BEAKER_GREEN, 1);
    add(2, STATIC_CRATE_TNT, 1);
    RB_GenericMine_ThTick(&threads[1]);
    RB_GenericMine_ThTick(&threads[2]);

    // A collision callback killed all three neighbors before their bucket ticks.
    for (int i = 0; i < 3; ++i)
        RB_Hazard_ThCollide_Generic(&threads[i]);
    assert(genericExplosions == 2 && D231.minePoolTaken.count == 0);
    for (int i = 0; i < 3; ++i)
        threads[i].funcThTick(&threads[i]);
    assert(!blasts && !shatters && !doorSearches && genericExplosions == 2);
}

static void testAirborneExpiry(void)
{
    setup(TURBO_TRACK, PAPU_PYRAMID);
    expiryEnabled = ground = 1;
    move(0, 799);
    bossDrop(0, STATIC_BEAKER_GREEN, 0, 1);
    bossDrop(1, STATIC_CRATE_TNT, 0, 1);
    RB_GenericMine_ThTick(&threads[0]);
    RB_GenericMine_ThTick(&threads[1]);
    assert(threads[0].funcThTick == RB_Potion_ThTick_InAir);
    assert(threads[1].funcThTick == RB_TNT_ThTick_ThrowOffHead);

    // The beaker flew behind its drop point, and the TNT is still airborne.
    instances[0].matrix.t[0] = -10;
    move(195, 604);
    move(390, 409);
    assert(mines[0].boolDestroyed && !mines[1].boolDestroyed);
    threads[0].funcThTick(&threads[0]);
    assert(shatters == 1 && lastSound == 0x3f && doorSearches == 1);
    move(400, 399);
    assert(mines[1].boolDestroyed && D231.minePoolTaken.count == 0);
    threads[1].funcThTick(&threads[1]);
    assert(blasts == 1 && lastSound == 0x3d && (threads[1].flags & THREAD_FLAG_DEAD));
}

int main(void)
{
    testHalfLap();
    testScopeAndDiscontinuities();
    testShortCustomCheckpointGaps();
    testAirborneExpiry();
    testBlastBeforeMineTick();
    puts("PASS half-lap expiry by mine position, finish wrap, reverse, invalid samples and race scope");
    setup(TURBO_TRACK, PAPU_PYRAMID);
    assert(D231.minePoolFree.count == 10);
    setup(TURBO_TRACK, DRAGON_MINES);
    assert(D231.minePoolFree.count == 3);
    setup(TURBO_TRACK, ROO_TUBES);
    assert(D231.minePoolFree.count == 7);
    puts("PASS forced Turbo Track garage rules: Papu=10, Komodo=3, Roo=7");

    setup(TURBO_TRACK, DRAGON_MINES);
    add(0, STATIC_BEAKER_GREEN, 0);
    for (int frame = 0; frame < 10; ++frame) RB_GenericMine_ThTick(&threads[0]);
    kart.matrix.t[0] = 0;
    kart.matrix.t[1] = 80;
    RB_GenericMine_ThTick(&threads[0]);
    assert(!hurtCount && !(threads[0].flags & THREAD_FLAG_DEAD));
    kart.matrix.t[1] = 79;
    RB_GenericMine_ThTick(&threads[0]);
    assert(hurtCount == 1 && shatters == 1 && (threads[0].flags & THREAD_FLAG_DEAD));
    assert(D231.minePoolFree.count == 3);
    puts("PASS settled green beaker: owner hit/shatter at 79 vertical units, miss at 80");

    setup(TURBO_TRACK, DRAGON_MINES);
    for (int i = 0; i < 4; ++i) add(i, STATIC_BEAKER_RED, 0);
    assert(mines[0].boolDestroyed && !mines[0].weaponSlot231);
    RB_GenericMine_ThTick(&threads[0]);
    assert(shatters == 1 && (threads[0].flags & THREAD_FLAG_DEAD) && D231.minePoolTaken.count == 3);
    puts("PASS settled mine overflow: drop 4 evicts and shatters drop 1; three slots remain occupied");

    setup(DRAGON_MINES, DRAGON_MINES);
    for (int i = 0; i < 4; ++i) add(i, STATIC_BEAKER_GREEN, 0);
    kart.matrix.t[0] = 0;
    mines[0].frameCount_DontHurtParent = 0;
    RB_GenericMine_ThTick(&threads[0]);
    assert(hurtCount == 1 && shatters == 1 && (threads[0].flags & THREAD_FLAG_DEAD));
    puts("PASS settled retail overflow still checks for kart contact before shattering");

    setup(TURBO_TRACK, DRAGON_MINES);
    ground = 1;
    add(0, STATIC_BEAKER_GREEN, 1);
    RB_GenericMine_ThTick(&threads[0]);
    assert(threads[0].funcThTick == RB_Potion_ThTick_InAir);
    for (int i = 1; i < 4; ++i) add(i, STATIC_BEAKER_GREEN, 1);
    assert(mines[0].boolDestroyed && !mines[0].weaponSlot231);
    kart.matrix.t[0] = 0;
    kart.matrix.t[1] = 80;
    threads[0].funcThTick(&threads[0]);
    assert((threads[0].flags & THREAD_FLAG_DEAD) && !hurtCount && shatters == 1 && doorSearches == 1);
    puts("PASS airborne beaker eviction shatters on the next tick and searches for the Tiger Temple door");

    setup(TURBO_TRACK, DRAGON_MINES);
    add(0, STATIC_CRATE_TNT, 1);
    blockPickupCrate(0);
    for (int frame = 0; frame < 100 && !(threads[0].flags & THREAD_FLAG_DEAD); ++frame)
        threads[0].funcThTick(&threads[0]);
    assert((threads[0].flags & THREAD_FLAG_DEAD) && blasts == 1 && !mines[0].weaponSlot231);
    assert(D231.minePoolTaken.count == 0 && !pickupCrate.boolPauseCooldown && !mines[0].crateInst);
    memset(&mines[0], 0, sizeof mines[0]);
    threads[0].flags = 0;
    add(0, STATIC_BEAKER_GREEN, 0);  /* Model stack allocator reuse of the same object. */
    add(1, STATIC_BEAKER_GREEN, 0);
    add(2, STATIC_BEAKER_GREEN, 0);
    assert(D231.minePoolTaken.count == 3 && !mines[0].boolDestroyed);
    add(3, STATIC_BEAKER_GREEN, 0);
    assert(mines[0].boolDestroyed && !mines[0].weaponSlot231);
    assert(D231.minePoolTaken.count == 3 && D231.minePoolTaken.last == &mines[1].weaponSlot231->item);
    puts("PASS thrown TNT returns its slot, and recycled storage keeps normal eviction order");

    setup(TURBO_TRACK, DRAGON_MINES);
    add(0, STATIC_CRATE_TNT, 0);
    RB_MinePool_Remove(&mines[0]);  // The TNT moved onto a kart's head.
    mines[0].driverTarget = &owner;
    mines[0].velocity.y = 48;
    owner.instTntRecv = &instances[0];
    RB_TNT_ThTick_ThrowOffHead(&threads[0]);
    assert(!(threads[0].flags & THREAD_FLAG_DEAD) && !blasts);
    mines[0].velocity.y = -96;
    RB_TNT_ThTick_ThrowOffHead(&threads[0]);
    assert((threads[0].flags & THREAD_FLAG_DEAD) && blasts == 1 && !owner.instTntRecv);
    assert(D231.minePoolTaken.count == 0 && D231.minePoolFree.count == 3);
    puts("PASS TNT thrown off a kart's head still explodes on landing");
}
