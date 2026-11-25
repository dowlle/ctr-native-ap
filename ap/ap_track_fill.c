#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
#include <common.h>
#include <stdlib.h>
#include "ap_content_plan.h"
#include "ap_hooks.h"
#include "ap_retail_asset.h"
#include "ap_trial_letter_logic.h"
#include <platform/native_custom_tracks.h>
#include <platform/native_gpu.h>
#include <platform/native_renderer.h>

#define AP_FILL_LEV_BYTES 576392
#define AP_FILL_VRM_BYTES 458808
#define AP_FILL_TEXTURE_W 256
#define AP_FILL_TEXTURE_H 128

static u32 apFillRetailLev[577536 / sizeof(u32)];
static struct Model *apFillCrateModels[3];
static int apFillAssetState;

// Crash Cove's fourth LEV/VRM pair contains all three models. Check both
// pins before relocation or reading the audited model and texture tables.
static const unsigned apFillModelOffsets[] = {389876, 388628, 391124};
static const struct {
	u16 tpage, clut;
	u8 u, v, size;
} apFillTiles[] = {
	{105, 16294,   0,   0, 64},
	{105, 16229,  64,   0, 64},
	{105, 16166, 128,   0, 64},
	{105, 16230, 192,   0, 64},
	{104, 16294, 128, 128, 64},
	{104, 16165, 192, 128, 64},
	{123, 31293, 196, 160, 16},
};

static int AP_TrackFillDecodeTextures(const u8 *vrm, size_t size, u8 *pixels)
{
	for (unsigned i = 0; i < sizeof apFillTiles / sizeof apFillTiles[0]; ++i)
	{
		unsigned tile_x = (i % 4) * 64, tile_y = (i / 4) * 64;
		unsigned vram_x = (apFillTiles[i].tpage & 15) * 64;
		unsigned vram_y = (apFillTiles[i].tpage & 16) ? 256 : 0;
		unsigned clut_x = (apFillTiles[i].clut & 63) * 16;
		unsigned clut_y = apFillTiles[i].clut >> 6;
		unsigned colors[16];
		for (unsigned color = 0; color < 16; ++color)
			if (!AP_TrialVramWord(vrm, size, clut_x + color, clut_y, &colors[color]))
				return 0;
		for (unsigned y = 0; y < apFillTiles[i].size; ++y)
			for (unsigned x = 0; x < apFillTiles[i].size; ++x)
			{
				unsigned u = apFillTiles[i].u + x, word;
				if (!AP_TrialVramWord(vrm, size, vram_x + u / 4, vram_y + apFillTiles[i].v + y, &word))
					return 0;
				unsigned color = colors[(word >> (4 * (u % 4))) & 15];
				u8 *pixel = pixels + 4 * ((tile_y + y) * AP_FILL_TEXTURE_W + tile_x + x);
				pixel[0] = (color & 31) * 255 / 31;
				pixel[1] = ((color >> 5) & 31) * 255 / 31;
				pixel[2] = ((color >> 10) & 31) * 255 / 31;
				pixel[3] = color ? 255 : 0;
			}
	}
	return 1;
}

static void AP_TrackFillRemapTexture(struct TextureLayout *layout)
{
	for (unsigned i = 0; i < sizeof apFillTiles / sizeof apFillTiles[0]; ++i)
		if (layout->tpage == apFillTiles[i].tpage && layout->clut == apFillTiles[i].clut)
		{
			int du = (int)((i % 4) * 64) - apFillTiles[i].u;
			int dv = (int)((i / 4) * 64) - apFillTiles[i].v;
			layout->u0 += du; layout->u1 += du; layout->u2 += du; layout->u3 += du;
			layout->v0 += dv; layout->v1 += dv; layout->v2 += dv; layout->v3 += dv;
			layout->tpage = (layout->tpage & 0x60) | AP_TPAGE_TIME_CRATE_BITS;
			return;
		}
}

int AP_TrackFillPrepare(void)
{
	if (apFillAssetState)
		return apFillAssetState > 0;
	if (!sdata->ptrBigfile1)
		return 0;
	apFillAssetState = -1;
	u8 *raw = (u8 *)apFillRetailLev;
	int size = 0;
	if (!AP_RetailAsset_ReadSubfile(7, 0, raw, sizeof apFillRetailLev, &size) ||
	    size != AP_FILL_LEV_BYTES ||
	    !AP_TrialDigest(raw, size, "80c8c89f0e6e630df59759dc40e3f99fa1fe0c5df11613bee41398884d8f87ac") ||
	    !AP_TrialPointerMapValid(raw, size))
		goto refused;

	u8 *vrm = malloc(460800);
	u8 *pixels = calloc(AP_FILL_TEXTURE_W * AP_FILL_TEXTURE_H, 4);
	unsigned texture = 0;
	if (vrm && pixels && AP_RetailAsset_ReadSubfile(6, 0, vrm, 460800, &size) &&
	    size == AP_FILL_VRM_BYTES &&
	    AP_TrialDigest(vrm, size, "a9509de1b16083da24a0e9b90a28511ba668a1d83677bb1e2868508e0678f84a") &&
	    AP_TrackFillDecodeTextures(vrm, size, pixels))
		texture = NativeRenderer_CreateRGBATexture(AP_FILL_TEXTURE_W, AP_FILL_TEXTURE_H, pixels);
	free(vrm);
	free(pixels);
	if (!texture)
		goto refused;

	u8 *body = raw + 4;
	unsigned map = AP_TrialU32(raw);
	LOAD_RunPtrMap((char *)body, (int *)(body + map + 4), AP_TrialU32(body + map) / 4);
	for (unsigned type = 0; type < 3; ++type)
	{
		struct Model *model = (struct Model *)(body + apFillModelOffsets[type]);
		apFillCrateModels[type] = model;
		for (int i = 0; i < 20; ++i)
			AP_TrackFillRemapTexture(model->headers[0].ptrTexLayout[i]);
		for (int i = 0; i < 2; ++i)
			AP_TrackFillRemapTexture(model->headers[1].ptrTexLayout[i]);
	}
	NativeGpu_SetTimeCrateTexture(texture, AP_FILL_TEXTURE_W, AP_FILL_TEXTURE_H);
	apFillAssetState = 1;
	AP_LogLine("[TrackFill] three retail time crate models and textures loaded\n");
	return 1;

refused:
	AP_LogLine("[TrackFill] Relic Race unavailable: retail time crate models or textures could not be verified and loaded\n");
	return 0;
}

static const ctr_content_package *AP_TrackFillRace(const struct GameTracker *g)
{
	if (g->numPlyrCurrGame != 1 || !(g->gameMode1 & RELIC_RACE))
		return NULL;
	const struct CustomTrackRaceContext *context = CustomTrack_StandaloneContext(g->levelID);
	if (!context || context->retail || context->cortex || context->mode != AP_CONTENT_MODE_RELIC ||
	    !context->seedEpoch || context->seedEpoch != ap_content_plan_epoch())
		return NULL;
	const ctr_content_pad *pad = ap_content_plan_pad(context->physicalPad);
	const ctr_content_package *pkg = pad ? ap_content_plan_package(pad->package_index) : NULL;
	return pkg && pkg->fill_crate_count && !strcmp(context->levSha256, pkg->lev_sha256) ? pkg : NULL;
}

void AP_TrackFillSpawn(struct GameTracker *g)
{
	const ctr_content_package *pkg = AP_TrackFillRace(g);
	if (!pkg || !AP_TrackFillPrepare())
		return;
	struct Instance *spawned[AP_CONTENT_MAX_FILL_CRATES];
	int count = 0;
	if (g->timeCratesInLEV || g->JitPools.instance.free.count < pkg->fill_crate_count)
		goto refused;
	for (; count < pkg->fill_crate_count; ++count)
	{
		const ctr_content_fill_crate *crate = &pkg->fill_crates[count];
		struct Instance *inst = INSTANCE_Birth3D(apFillCrateModels[crate->type - 1], "ap-fill-crate", NULL);
		if (!inst)
			goto refused;
		spawned[count] = inst;
		for (int axis = 0; axis < 3; ++axis)
			inst->matrix.t[axis] = crate->pos[axis];
	}
	g->timeCratesInLEV = count;
	char line[96];
	snprintf(line, sizeof line, "[TrackFill] spawned %d time crates for %s\n", count, pkg->content_id);
	AP_LogLine(line);
	return;

refused:
	while (count > 0)
		INSTANCE_Death(spawned[--count]);
	AP_LogLine("[TrackFill] could not spawn the complete time crate set\n");
}

void AP_TrackFillOnFrame(struct GameTracker *g)
{
	if (apFillAssetState != 1 || !AP_TrackFillRace(g) || sdata->Loading.stage != LOAD_IDLE ||
	    (g->gameMode1 & (PAUSE_ALL | START_OF_RACE | END_OF_RACE)))
		return;
	struct Driver *d = g->drivers[0];
	if (!d || !d->instSelf || !d->instSelf->thread ||
	    (d->actionsFlagSet & (ACTION_RACE_FINISHED | ACTION_WARP)))
		return;
	SVec3 prev, curr;
	prev.x = (s16)CTR_MipsAddLo(CTR_MipsSra(d->posPrev.x, 8), d->originToCenter.x);
	prev.y = (s16)CTR_MipsAddLo(CTR_MipsSra(d->posPrev.y, 8), d->originToCenter.y);
	prev.z = (s16)CTR_MipsAddLo(CTR_MipsSra(d->posPrev.z, 8), d->originToCenter.z);
	curr.x = (s16)CTR_MipsAddLo(CTR_MipsSra(d->posCurr.x, 8), d->originToCenter.x);
	curr.y = (s16)CTR_MipsAddLo(CTR_MipsSra(d->posCurr.y, 8), d->originToCenter.y);
	curr.z = (s16)CTR_MipsAddLo(CTR_MipsSra(d->posCurr.z, 8), d->originToCenter.z);
	// The instance pool owns the live and broken crates. Re-read it after any
	// quick load so no process-side pointer or collected bit outlives the save.
	for (struct Instance *inst = (struct Instance *)g->JitPools.instance.taken.first; inst; inst = inst->next)
	{
		if (inst->scale.x != 0x1000 || (inst->model != apFillCrateModels[0] &&
		    inst->model != apFillCrateModels[1] && inst->model != apFillCrateModels[2]))
			continue;
		const int x = inst->matrix.t[0], y = inst->matrix.t[1], z = inst->matrix.t[2];
		struct BoundingBox box = {
			.min = {{x - 92, y - 44, z - 92}}, .max = {{x + 92, y + 140, z + 92}},
		};
		if (AP_TrialLetterHit(&prev, &curr, &box))
		{
			struct ScratchpadStruct collision = {0};
			collision.Input1.modelID = DYNAMIC_PLAYER;
			RB_CrateTime_LInC(inst, d->instSelf->thread, &collision);
		}
	}
}
#endif
