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
static struct Thread threads[3], kart_thread;
static struct Crate crate_objects[3];
static struct Model models[3];
static struct BigHeader bigfile;
static struct CustomTrackRaceContext context;
static ctr_content_pad pad;
static ctr_content_package package;
static int born, thread_count;
static int asset_reads;
static const char *asset_dir;

uint64_t ap_content_plan_epoch(void) { return 1; }
const ctr_content_pad *ap_content_plan_pad(int index) { assert(index == 3); return &pad; }
const ctr_content_package *ap_content_plan_package(int index) { assert(index == 0); return &package; }
const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int index)
{
	assert(index == 3);
	return &context;
}
void AP_LogLine(const char *line) { fputs(line, stdout); }
void ConvertRotToMatrix(MATRIX *m, const SVec3 *r) { (void)m; (void)r; }
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
	assert(thread_count < 3);
	struct Thread *t = &threads[thread_count];
	memset(t, 0, sizeof *t);
	t->funcThTick = tick;
	t->object = &crate_objects[thread_count++];
	return t;
}
struct Instance *INSTANCE_Birth3D(struct Model *model, const char *name, struct Thread *t)
{
	assert(born < AP_CONTENT_MAX_FILL_CRATES && strcmp(name, "ap-fill-crate") == 0 && t == NULL);
	struct Instance *inst = &instances[born++];
	memset(inst, 0, sizeof *inst);
	memset(inst->matrix.m, 0x7a, sizeof inst->matrix.m);
	inst->model = model;
	inst->scale = (SVec3){{0x1000, 0x1000, 0x1000}};
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
	assert(!dram && (entry == 6 || entry == 7));
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
TextureID NativeRenderer_CreateRGBATexture(int w, int h, u8 *rgba)
{
	assert(w == 256 && h == 128 && rgba[3] == 255);
	static int attempts;
	return ++attempts == 1 ? 0 : 42;
}
void NativeGpu_SetTimeCrateTexture(unsigned id, int w, int h) { assert(id == 42 && w == 256 && h == 128); }

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
	driver.instSelf = &kart;
	kart.thread = &kart_thread;
	kart_thread.object = &driver;
	context.seedEpoch = 1;
	context.mode = AP_CONTENT_MODE_RELIC;
	context.physicalPad = 3;
	context.levSha256[0] = package.lev_sha256[0] = 'a';
	package.fill_crates = crates;
	package.fill_crate_count = 3;

	assert(!AP_TrackFillPrepare());
	assert(!AP_TrackFillPrepare() && asset_reads == 2);
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
		const u32 delta = 0x800000;
		const u32 model = (u32)(uintptr_t)apFillCrateModels[0];
		u32 rebased;
		assert(AP_TrackFillRebaseAssetPointer(model - delta, delta, &rebased) && rebased == model);
		assert(!AP_TrackFillRebaseAssetPointer(model, delta, &rebased));
		puts("PASS: pinned retail texture and model import");
	} else {
		models[0].id = STATIC_TIME_CRATE_01;
		models[1].id = STATIC_TIME_CRATE_02;
		models[2].id = STATIC_TIME_CRATE_03;
		for (int i = 0; i < 3; ++i)
			apFillCrateModels[i] = &models[i];
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
	AP_TrackFillOnFrame(&tracker);
	assert(!driver.numTimeCrates && !tracker.frozenTimeRemaining);
	tracker.gameMode1 &= ~START_OF_RACE;
	AP_TrackFillOnFrame(&tracker);
	assert(driver.numTimeCrates == 1 && tracker.frozenTimeRemaining == 960 && instances[0].scale.x == 0);
	AP_TrackFillOnFrame(&tracker);
	assert(driver.numTimeCrates == 1 && tracker.frozenTimeRemaining == 960);
	struct Instance after_hit = instances[0];
	// Model a quick restore of the engine-owned instance and clock memory.
	instances[0] = before_hit;
	tracker.frozenTimeRemaining = driver.numTimeCrates = 0;
	AP_TrackFillOnFrame(&tracker);
	assert(driver.numTimeCrates == 1 && tracker.frozenTimeRemaining == 960);
	instances[0] = after_hit;
	AP_TrackFillOnFrame(&tracker);
	assert(driver.numTimeCrates == 1 && tracker.frozenTimeRemaining == 960);
	driver.posPrev.x = driver.posCurr.x = 3000 * 256;
	AP_TrackFillOnFrame(&tracker);
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
