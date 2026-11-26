// Native fill spawn, freeze credit, and instance-pool restore.
// cc -m32 -DCTR_NATIVE -DCTR_AP -DCTR_CUSTOM_TRACKS -DBUILD=926 -I . -I include
//    -ffunction-sections -fdata-sections -Wl,--gc-sections
//    -o /tmp/test-track-fill tools/test-track-fill.c
#include <assert.h>
#include <common.h>
#include "../ap/ap_hooks.h"
#include "../ap/ap_traps.h"
#include "../ap/ap_tizi.h"
#include "../game/231/RB_Crate.c"
#include "../ap/ap_track_fill.c"
#include "../game/MAIN/MainFrame_RaceClock.h"

struct sData sdata_static;
struct Data data;
static struct GameTracker tracker;
static struct Driver driver;
static struct Instance instances[AP_CONTENT_MAX_FILL_CRATES], kart;
static struct Thread threads[8], kart_thread;
static struct Crate crate_objects[8];
static struct Model models[3], weapon_model;
static struct BigHeader bigfile;
static struct CustomTrackRaceContext context;
static ctr_content_pad pad;
static ctr_content_package package;
static int born, thread_count;
static int asset_reads;
static const char *asset_dir;

uint64_t ap_content_plan_epoch(void) { return 1; }
const ctr_content_pad *ap_content_plan_pad(int index) { assert(index == 3); return &pad; }
const ctr_content_package *ap_content_plan_package(int index) { assert(index >= 0); return index == 0 ? &package : NULL; }
int ap_content_plan_active(void) { return 1; }
static const char *boss_venue_lev;
int CustomTrack_BossVenuePins(int level, int boss, int active, const char **lev, const char **vrm)
{
	(void)level; (void)boss; (void)active;
	*lev = boss_venue_lev; *vrm = "v";
	return boss_venue_lev != NULL;
}
const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int index)
{
	assert(index == 3);
	return &context;
}
void AP_LogLine(const char *line) { fputs(line, stdout); }
void ConvertRotToMatrix(MATRIX *m, const SVec3 *r)
{
	(void)r;
	memset(m->m, 0, sizeof m->m);
	for (int axis = 0; axis < 3; ++axis)
		m->m[axis][axis] = 0x1000;
}
void MatrixRotate(MATRIX *d, MATRIX *s, MATRIX *r) { (void)d; (void)s; (void)r; }
void PlaySound3D(u32 id, struct Instance *i) { assert(id == 0x3c); (void)i; }
int OtherFX_Play_LowLevel(u32 id, u8 anti_spam, u32 flags)
{
	assert(id == 0x40 && !anti_spam && (flags == 0x8c9080 || flags == 0x8c8880));
	return 0;
}
void Voiceline_RequestPlay(u32 id, u32 who, u32 arg) { assert(id == 0x13); (void)who; (void)arg; }
void RB_Fruit_GetScreenCoords(struct PushBuffer *pb, struct Instance *i, s16 *xy)
{
	(void)pb; (void)i;
	xy[0] = xy[1] = 0;
}
static int sanity_pickups;
int AP_SanityPickup(struct Instance *inst, struct Driver *d) { (void)inst; assert(d == &driver); return ++sanity_pickups; }
int AP_TrapSuppressCrateReward(struct Driver *d) { (void)d; return 0; }
void AP_TiziOnWeaponBoxHit(struct Driver *d, struct Instance *inst) { (void)d; (void)inst; }
int OtherFX_Play(u32 id, int flags) { assert(id == 0x5d); (void)flags; return 0; }
u16 INSTANCE_GetNumAnimFrames(struct Instance *inst, int index) { (void)inst; (void)index; return 0; }
struct Instance *INSTANCE_BirthWithThread(int id, const char *name, int pool, int bucket,
                                        void *tick, int size, struct Thread *relative)
{
	// The retail sound and freeze still happen if its cosmetic burst cannot allocate.
	assert(id == 0x26);
	(void)name; (void)pool; (void)bucket; (void)tick; (void)size; (void)relative;
	return NULL;
}
struct Thread *PROC_BirthWithObject(int flags, void *tick, const char *name, struct Thread *parent)
{
	(void)flags; (void)name; (void)parent;
	assert(thread_count < 8);
	struct Thread *t = &threads[thread_count];
	memset(t, 0, sizeof *t);
	t->funcThTick = tick;
	t->object = &crate_objects[thread_count++];
	return t;
}
struct Instance *INSTANCE_Birth3D(struct Model *model, const char *name, struct Thread *t)
{
	assert(born < AP_CONTENT_MAX_FILL_CRATES && t == NULL &&
	       (strcmp(name, "ap-fill-crate") == 0 || strcmp(name, "ap-fill-weapon") == 0));
	struct Instance *inst = &instances[born++];
	memset(inst, 0, sizeof *inst);
	memset(inst->matrix.m, 0x7a, sizeof inst->matrix.m);
	inst->model = model;
	inst->scale = (SVec3){{0x1000, 0x1000, 0x1000}};
	inst->flags = DRAW_COLLISION_MASK;
	inst->next = (struct Instance *)tracker.JitPools.instance.taken.first;
	tracker.JitPools.instance.taken.first = (struct Item *)inst;
	return inst;
}
void INSTANCE_Death(struct Instance *inst)
{
	assert(tracker.JitPools.instance.taken.first == (struct Item *)inst);
	tracker.JitPools.instance.taken.first = (struct Item *)inst->next;
}
u8 *AP_RetailAsset_ReadSubfile(int entry, int dram, u8 *dst, int cap, int *size)
{
	assert(!dram && (entry == 0 || entry == 1 || entry == 6 || entry == 7));
	asset_reads++;
	if (!asset_dir)
		return NULL;
	char file[512];
	snprintf(file, sizeof file, "%s/%d.bin", asset_dir, entry);
	FILE *stream = fopen(file, "rb");
	assert(stream);
	*size = fread(dst, 1, cap, stream);
	assert(!ferror(stream));
	fclose(stream);
	return dst;
}
void LOAD_RunPtrMap(char *body, int *slots, int count)
{
	for (int i = 0; i < count; ++i)
		*(u32 *)(body + (slots[i] & ~3)) += (uintptr_t)body;
}
TextureID NativeRenderer_CreateRGBATextureChecked(int w, int h, u8 *rgba)
{
	assert(w == 256 && h == 256 && rgba[3] == 255 && rgba[4 * (128 * 256) + 3] == 255);
	static int attempts;
	return ++attempts == 1 ? 0 : 42;
}
void NativeGpu_SetTimeCrateTexture(unsigned id, int w, int h) { assert(id == 42 && w == 256 && h == 256); }

// WF1b: the retail leaf tests are what the game runs over the filled crates'
// hitboxes; here they only capture the leaf AP_TrackFillWeaponSearch passes.
static struct BSP *captured_leaf;
static int captured_kind;
void COLL_FIXED_BSPLEAF_TestInstance(struct BSP *node, struct ScratchpadStruct *sps)
{
	(void)sps;
	captured_leaf = node;
	captured_kind = 1;
}
void COLL_FIXED_BSPLEAF_TestQuadblocks(struct BSP *node, struct ScratchpadStruct *sps) { (void)node; (void)sps; assert(0); }
void COLL_MOVED_BSPLEAF_TestQuadblocks(struct BSP *node, struct ScratchpadStruct *sps) { (void)node; (void)sps; assert(0); }
void PROC_PerBspLeaf_CheckInstances(struct BSP *node, struct ScratchpadStruct *sps)
{
	(void)sps;
	captured_leaf = node;
	captured_kind = 2;
}
static struct BSP level_bsp[2];
// Read from Dingo Canyon's crates on the pinned disc (CTR_TEST_FILL_ASSETS run).
static const struct BSP retail_weapon_hitbox = {.flag = 0x04c0, .data.hitbox = {.radius = 76, .unk18 = 76 * 76}};
static struct mesh_info level_mesh;
static struct Level level;

// The filled crate's hitbox as one search sees it, or NULL.
void RB_Burst_CollLevInst(struct ScratchpadStruct *sps, void *hit) { (void)sps; (void)hit; }
void RB_Burst_CollThBucket(struct ScratchpadStruct *sps, void *hit) { (void)sps; (void)hit; }
static const struct BoundingBox everywhere = {.min = {{-32768, -32768, -32768}}, .max = {{32767, 32767, 32767}}};

static struct BSP *weapon_hitbox(const struct Instance *inst, CollBspLeafCallback callback, int searchFlags, int kind)
{
	struct ScratchpadStruct sps;
	memset(&sps, 0, sizeof sps);
	if (callback == PROC_PerBspLeaf_CheckInstances) {
		sps.Union.ThBuckColl.funcCallback = RB_Burst_CollLevInst;
		sps.Union.ThBuckColl.bbox = everywhere;
	} else {
		sps.Union.QuadBlockColl.searchFlags = searchFlags;
		sps.bbox = everywhere;
	}
	captured_leaf = NULL;
	captured_kind = 0;
	AP_TrackFillWeaponSearch(level_bsp, callback, &sps);
	if (!captured_leaf)
		return NULL;
	assert(captured_kind == kind && (captured_leaf->flag & BSP_NODE_FLAG_LEAF));
	for (struct BSP *hit = captured_leaf->data.leaf.bspHitboxArray; hit->flag; ++hit)
		if (hit->data.hitbox.instDef && hit->data.hitbox.instDef->ptrInstance == inst)
			return hit;
	return NULL;
}

// What RB_Hazard_CollLevInst, CollMoved_PlayerSearch_RunHitboxLInC and the
// bot search do with a touched hitbox: skip an instance without collision
// bits, then run the LInC of the model's metadata (8: RB_CrateWeapon_LInC).
static int break_with(const struct Instance *inst, int modelID, struct Thread *breaker)
{
	struct BSP *hit = weapon_hitbox(inst, COLL_FIXED_BSPLEAF_TestInstance, 0, 1);
	assert(hit && (hit->flag & BSP_HITBOX_COLLIDABLE));
	struct InstDef *def = hit->data.hitbox.instDef;
	assert(def->modelID == PU_RANDOM_CRATE && def->ptrInstance->model->id == PU_RANDOM_CRATE &&
	       def->model == apFillWeaponModel);
	if (!(def->ptrInstance->flags & DRAW_COLLISION_MASK))
		return 0;
	struct ScratchpadStruct sps;
	memset(&sps, 0, sizeof sps);
	sps.Input1.modelID = modelID;
	sps.bspHitbox = hit;
	return RB_CrateWeapon_LInC(def->ptrInstance, breaker, &sps);
}

int main(void)
{
	const ctr_content_fill_crate crates[] = {
		{{1000, 0, 0}, 1}, {{2000, 0, 0}, 2}, {{3000, 0, 0}, 3},
	};
	sdata_static.gGT = &tracker;
	sdata_static.ptrBigfile1 = &bigfile;
	sdata_static.Loading.stage = LOAD_IDLE;
	tracker.levelID = 3;
	tracker.numPlyrCurrGame = 1;
	tracker.gameMode1 = RELIC_RACE;
	tracker.drivers[0] = &driver;
	level_mesh.bspRoot = level_bsp;
	level.ptr_mesh_info = &level_mesh;
	tracker.level1 = &level;
	driver.instSelf = &kart;
	kart.thread = &kart_thread;
	kart_thread.object = &driver;
	context.seedEpoch = 1;
	context.mode = AP_CONTENT_MODE_RELIC;
	context.physicalPad = 3;
	context.levSha256[0] = package.lev_sha256[0] = 'a';
	context.vrmSha256[0] = package.vrm_sha256[0] = 'v';
	package.fill_crates = crates;
	package.fill_crate_count = 3;

	assert(!AP_TrackFillPrepare());
	assert(!AP_TrackFillPrepare() && asset_reads == 2);
	asset_reads = 0;
	asset_dir = getenv("CTR_TEST_FILL_ASSETS");
	if (asset_dir) {
		assert(!AP_TrackFillPrepare()); // transient atlas upload failure
		assert(AP_TrackFillPrepare());
		assert(apFillCrateModels[0]->id == STATIC_TIME_CRATE_01);
		assert(apFillCrateModels[1]->id == STATIC_TIME_CRATE_02);
		assert(apFillCrateModels[2]->id == STATIC_TIME_CRATE_03);
		for (int model = 0; model < 3; ++model)
			for (int header = 0; header < 2; ++header)
				for (int i = 0; i < (header ? 2 : 20); ++i)
					assert((apFillCrateModels[model]->headers[header].ptrTexLayout[i]->tpage &
					        AP_TPAGE_SIDELOAD_MASK) == AP_TPAGE_TIME_CRATE_BITS);
		assert(apFillWeaponModel->id == PU_RANDOM_CRATE && !strcmp(apFillWeaponModel->name, "crate_question"));
		for (int header = 0; header < 2; ++header)
			for (int i = 0; i < (header ? 2 : 20); ++i) {
				const struct TextureLayout *layout = apFillWeaponModel->headers[header].ptrTexLayout[i];
				assert((layout->tpage & AP_TPAGE_SIDELOAD_MASK) == AP_TPAGE_TIME_CRATE_BITS && layout->v0 >= 128);
			}
		printf("retail weapon crate hitbox: flag %04x radius %d squared %d lift %d\n", apFillWeaponHitboxTemplate.flag,
		       apFillWeaponHitboxTemplate.data.hitbox.radius, apFillWeaponHitboxTemplate.data.hitbox.unk18,
		       apFillWeaponHitboxLift);
		assert(apFillWeaponHitboxReady && apFillWeaponHitboxLift == 48 &&
		       !memcmp(&apFillWeaponHitboxTemplate, &retail_weapon_hitbox, sizeof retail_weapon_hitbox));
		puts("PASS: pinned retail texture and model import, time crates and the weapon crate");
		puts("PASS: the weapon crate hitbox comes from Dingo Canyon's own crates, all of one shape");
	} else {
		models[0].id = STATIC_TIME_CRATE_01;
		models[1].id = STATIC_TIME_CRATE_02;
		models[2].id = STATIC_TIME_CRATE_03;
		for (int i = 0; i < 3; ++i)
			apFillCrateModels[i] = &models[i];
		weapon_model.id = PU_RANDOM_CRATE;
		apFillWeaponModel = &weapon_model;
		apFillWeaponHitboxTemplate = retail_weapon_hitbox;
		apFillWeaponHitboxLift = 48;
		apFillWeaponHitboxReady = 1;
		apFillAssetState = 1;
	}

	tracker.JitPools.instance.free.count = 3;
	AP_TrackFillSpawn(&tracker);
	assert(born == 3 && tracker.timeCratesInLEV == 3);
	for (int i = 0; i < 3; ++i) {
		assert(instances[i].matrix.t[0] == crates[i].pos[0]);
		for (int row = 0; row < 3; ++row)
			for (int column = 0; column < 3; ++column)
				assert(instances[i].matrix.m[row][column] == (row == column ? 0x1000 : 0));
	}
	struct Instance before_hit = instances[0];
	driver.posPrev.x = driver.posCurr.x = 1000 * 256;
	tracker.gameMode1 |= START_OF_RACE;
	AP_TrackFillCollide(&tracker);
	assert(!driver.numTimeCrates && !tracker.frozenTimeRemaining);
	tracker.gameMode1 &= ~START_OF_RACE;
	AP_TrackFillCollide(&tracker);
	assert(driver.numTimeCrates == 1 && tracker.frozenTimeRemaining == 960 && instances[0].scale.x == 0);
	AP_TrackFillCollide(&tracker);
	assert(driver.numTimeCrates == 1 && tracker.frozenTimeRemaining == 960);
	struct Instance after_hit = instances[0];
	// Model a quick restore of the engine-owned instance and clock memory.
	instances[0] = before_hit;
	tracker.frozenTimeRemaining = driver.numTimeCrates = 0;
	AP_TrackFillCollide(&tracker);
	assert(driver.numTimeCrates == 1 && tracker.frozenTimeRemaining == 960);
	instances[0] = after_hit;
	AP_TrackFillCollide(&tracker);
	assert(driver.numTimeCrates == 1 && tracker.frozenTimeRemaining == 960);
	driver.posPrev.x = driver.posCurr.x = 3000 * 256;
	AP_TrackFillCollide(&tracker);
	assert(driver.numTimeCrates == 2 && tracker.frozenTimeRemaining == 960 * 4);
	tracker.elapsedEventTime = 320;
	tracker.elapsedTimeMS = 32;
	for (int frame = 0; frame < 120; ++frame) {
		tracker.timer++;
		MainFrame_AdvanceRaceClock(&tracker);
		assert(tracker.elapsedEventTime == 320);
	}
	assert(tracker.frozenTimeRemaining == 0);
	MainFrame_AdvanceRaceClock(&tracker);
	assert(tracker.elapsedEventTime == 352);
	puts("PASS: elapsed race time stops during freeze credit and resumes afterward");
	puts("PASS: three spawned; retail timer freeze and broken state follow the instance pool");

	born = 0;
	tracker.timeCratesInLEV = 0;
	tracker.JitPools.instance.free.count = 2;
	tracker.JitPools.instance.taken.first = NULL;
	AP_TrackFillSpawn(&tracker);
	assert(AP_TrackFillFailed(&tracker) && born == 0);

	// WF1: two filled weapon crates in a Trophy Race. A hit gives a weapon
	// roll through the retail handler, the crate regrows on its own thread,
	// and a crate whose AP box stands is out of reach until the box frees it.
	{
		const ctr_content_fill_weapon weapons[] = {{{5000, 10, 0}, {0, -2048, 0}, 0}, {{7000, 10, 0}, {1, 1019, -35}, 2}};
		born = 0;
		thread_count = 0;
		tracker.JitPools.instance.taken.first = NULL;
		tracker.timeCratesInLEV = 0;
		package.fill_weapons = weapons;
		package.fill_weapon_count = 2;
		tracker.gameMode1 = RELIC_RACE;
		AP_TrackFillWeaponSpawn(&tracker);
		assert(born == 0 && !AP_TrackFillWeaponCount(&tracker));
		tracker.gameMode1 = 0;
		context.mode = AP_CONTENT_MODE_TROPHY;
		tracker.JitPools.instance.free.count = 1;
		AP_TrackFillWeaponSpawn(&tracker);
		assert(born == 0 && !tracker.JitPools.instance.taken.first); // all or nothing
		born = 0;
		tracker.JitPools.instance.free.count = 2;
		AP_TrackFillWeaponSpawn(&tracker);
		assert(born == 2 && AP_TrackFillWeaponCount(&tracker) == 2);
		assert(AP_TrackFillWeaponOrdinal(&tracker, 0) == &instances[0] && instances[0].model == apFillWeaponModel);
		assert(AP_TrackFillWeaponAt(&tracker, 7000, 10, 0, 1019) == &instances[1]);
		assert(!AP_TrackFillWeaponAt(&tracker, 7000, 10, 0, 1018));
		assert(instances[1].matrix.t[0] == 7000 && instances[1].matrix.t[1] == 10);
		driver.heldItemID = 0xf;
		driver.noItemTimer = 0;
		driver.numHeldItems = 0;
		// Every search of the level's BSP that tests instances, and the
		// thread-bucket search explosions use, sees both crates with the
		// retail weapon crate hitbox around each.
		assert(!weapon_hitbox(&instances[0], COLL_FIXED_BSPLEAF_TestQuadblocks, 0, 1)); // terrain only
		struct BSP *hit = weapon_hitbox(&instances[1], PROC_PerBspLeaf_CheckInstances, 0, 2);
		assert(hit && hit->flag == apFillWeaponHitboxTemplate.flag &&
		       hit->data.hitbox.radius == apFillWeaponHitboxTemplate.data.hitbox.radius);
		// Upright crate (identity rotation): centre 48 above, box of radius 76.
		for (int axis = 0; axis < 3; ++axis) {
			const int center = instances[1].matrix.t[axis] + (axis == 1 ? 48 : 0);
			assert(hit->data.hitbox.center.v[axis] == center);
			assert(hit->box.min.v[axis] == center - 76 && hit->box.max.v[axis] == center + 76);
		}
		assert(hit->data.hitbox.unk18 == 76 * 76 && hit->data.hitbox.instDef->pos.x == 7000);
		{
			struct ScratchpadStruct other;
			memset(&other, 0, sizeof other);
			captured_leaf = NULL;
			AP_TrackFillWeaponSearch(&level_bsp[1], COLL_FIXED_BSPLEAF_TestInstance, &other);
			assert(!captured_leaf); // another BSP (a cutscene, a menu) is not touched
		}
		{
			struct ScratchpadStruct probe;
			memset(&probe, 0, sizeof probe);
			// A search away from every crate gets no hitbox.
			probe.bbox = (struct BoundingBox){.min = {{-9000, -100, -100}}, .max = {{-8000, 100, 100}}};
			captured_leaf = NULL;
			AP_TrackFillWeaponSearch(level_bsp, COLL_FIXED_BSPLEAF_TestInstance, &probe);
			assert(!captured_leaf);
			// One that reaches crate 0 only gets crate 0.
			probe.bbox = (struct BoundingBox){.min = {{4950, 0, -10}}, .max = {{5050, 20, 10}}};
			AP_TrackFillWeaponSearch(level_bsp, COLL_FIXED_BSPLEAF_TestInstance, &probe);
			assert(captured_leaf && captured_leaf->data.leaf.bspHitboxArray[0].data.hitbox.instDef->ptrInstance == &instances[0] &&
			       !captured_leaf->data.leaf.bspHitboxArray[1].flag);
			// A level hitbox the search already touched keeps its claim.
			probe.boolDidTouchHitbox = 1;
			captured_leaf = NULL;
			AP_TrackFillWeaponSearch(level_bsp, COLL_FIXED_BSPLEAF_TestInstance, &probe);
			assert(!captured_leaf);
			// TNT and Nitro blasts hand the bucket search a thread callback.
			memset(&probe, 0, sizeof probe);
			probe.Union.ThBuckColl.funcCallback = RB_Burst_CollThBucket;
			probe.Union.ThBuckColl.bbox = everywhere;
			AP_TrackFillWeaponSearch(level_bsp, PROC_PerBspLeaf_CheckInstances, &probe);
			assert(!captured_leaf);
		}
		puts("PASS: filled crates join every instance search of the level BSP with the retail hitbox");
		// A rocket breaks a crate and its shooter gets the weapon roll.
		struct TrackerWeapon rocket_object = {0}, bomb_object = {0};
		struct Thread rocket = {0}, bomb = {0};
		rocket_object.driverParent = &driver;
		rocket.object = &rocket_object;
		bomb_object.driverParent = &driver;
		bomb.object = &bomb_object;
		assert(break_with(&instances[0], DYNAMIC_ROCKET, &rocket) == 1);
		assert(driver.heldItemID == 0x10 && instances[0].scale.x == 0 && instances[0].thread);
		struct Thread *grow = instances[0].thread;
		assert(grow->funcThTick == (void *)RB_CrateAny_ThTick_Grow);
		driver.heldItemID = 0xf;
		break_with(&instances[0], DYNAMIC_PLAYER, &kart_thread); // cooldown: no second reward
		assert(driver.heldItemID == 0xf);
		for (int frame = 0; frame < 60 && instances[0].thread; ++frame)
			RB_CrateAny_ThTick_Grow(grow);
		assert(instances[0].scale.x == 0x1000 && !instances[0].thread);
		puts("PASS: a rocket breaks a filled weapon crate, its shooter rolls, the crate grows back");
		assert(break_with(&instances[0], DYNAMIC_BOMB, &bomb) == 1);
		assert(driver.heldItemID == 0x10 && instances[0].scale.x == 0);
		grow = instances[0].thread;
		for (int frame = 0; frame < 60 && instances[0].thread; ++frame)
			RB_CrateAny_ThTick_Grow(grow);
		assert(instances[0].scale.x == 0x1000 && !instances[0].thread);
		puts("PASS: a bomb breaks a filled weapon crate and it grows back");
		driver.heldItemID = 0xf;
		assert(break_with(&instances[0], DYNAMIC_PLAYER, &kart_thread) == 1);
		assert(driver.heldItemID == 0x10 && instances[0].scale.x == 0);
		grow = instances[0].thread;
		for (int frame = 0; frame < 60 && instances[0].thread; ++frame)
			RB_CrateAny_ThTick_Grow(grow);
		puts("PASS: a kart breaks a filled weapon crate through the retail hitbox");
		// The AP box seat clears the draw and collision bits.
		instances[1].flags &= ~DRAW_COLLISION_MASK;
		driver.heldItemID = 0xf;
		assert(!break_with(&instances[1], DYNAMIC_ROCKET, &rocket));
		assert(driver.heldItemID == 0xf && instances[1].scale.x == 0x1000);
		instances[1].flags |= DRAW_COLLISION_MASK;
		break_with(&instances[1], DYNAMIC_PLAYER, &kart_thread);
		assert(driver.heldItemID == 0x10 && instances[1].scale.x == 0);
		// Outside a weapon fill race, or while loading, no hitbox is added.
		sdata_static.Loading.stage = LOAD_IDLE + 1;
		assert(!weapon_hitbox(&instances[0], COLL_FIXED_BSPLEAF_TestInstance, 0, 1));
		sdata_static.Loading.stage = LOAD_IDLE;
		puts("PASS: a seated weapon crate is out of reach until its AP box frees it");
		// A boss race on a custom venue serving the same LEV stands the crates
		// too; another venue does not.
		tracker.gameMode1 = ADVENTURE_BOSS;
		context.seedEpoch = 0; // no pad or cup context: the garage venue decides
		boss_venue_lev = "b";
		assert(!AP_TrackFillWeaponCount(&tracker));
		boss_venue_lev = package.lev_sha256;
		assert(AP_TrackFillWeaponCount(&tracker) == 2);
		// A boss cup leg keeps ADVENTURE_BOSS and serves its own context.
		context.seedEpoch = 1;
		context.cup = 1;
		boss_venue_lev = NULL;
		assert(AP_TrackFillWeaponCount(&tracker) == 2);
		context.cup = 0;
		tracker.gameMode1 = 0;
		puts("PASS: the served LEV decides on pad, cup and boss venue routes");
		package.fill_weapon_count = 0;
		tracker.gameMode1 = RELIC_RACE;
		context.mode = AP_CONTENT_MODE_RELIC;
	}

	const char *positions = getenv("CTR_TEST_FILL_POSITIONS");
	if (positions) {
		ctr_content_fill_crate parsed[AP_CONTENT_MAX_FILL_CRATES];
		int count;
		FILE *input = fopen(positions, "r");
		assert(input && fscanf(input, "%d", &count) == 1 && count > 0 && count <= AP_CONTENT_MAX_FILL_CRATES);
		for (int i = 0; i < count; ++i) {
			unsigned type;
			assert(fscanf(input, "%hd %hd %hd %u", &parsed[i].pos[0], &parsed[i].pos[1],
			              &parsed[i].pos[2], &type) == 4 && type >= 1 && type <= 3);
			parsed[i].type = (uint8_t)type;
		}
		fclose(input);
		package.fill_crates = parsed;
		package.fill_crate_count = count;
		tracker.timeCratesInLEV = 0;
		tracker.JitPools.instance.free.count = count;
		AP_TrackFillSpawn(&tracker);
		assert(tracker.timeCratesInLEV == count && born == count);
		for (int i = 0; i < count; ++i) {
			assert(instances[i].model == apFillCrateModels[parsed[i].type - 1]);
			for (int axis = 0; axis < 3; ++axis)
				assert(instances[i].matrix.t[axis] == parsed[i].pos[axis]);
		}
		printf("PASS: %d apworld crates parsed and spawned at the pinned coordinates\n", count);
	}
	return 0;
}
