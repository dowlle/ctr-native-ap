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
#ifdef CTR_CUSTOM_PACKAGES
#include <platform/native_assets.h>
#include <platform/native_custom_offline.h>

static ctr_content_fill_crate apFillOfflineCrates[AP_CONTENT_MAX_FILL_CRATES];
static ctr_content_package apFillOffline;
static struct SpawnPosRot apFillOfflineSpawns[8];

int AP_TrackFillStageOffline(const struct CustomOfflineRequest *request)
{
	struct CustomPackageManifest manifest;
	unsigned int laps;
	if (!CustomOffline_GetManifest(request, &manifest) || !CustomOffline_RequestLaps(request, &laps))
		return 0;
	memset(&apFillOffline, 0, sizeof apFillOffline);
	for (unsigned i = 0; i < manifest.count; ++i)
	{
		const struct CustomPackageFile *file = &manifest.files[i];
		if (strcmp(file->role, "lev"))
			continue;
		apFillOffline.fill_crate_count = ap_content_track_fill_bundled(
		    NativeAssets_GetAssetDir(), file->sha256, laps, apFillOfflineCrates, apFillOffline.relic_targets);
		if (!apFillOffline.fill_crate_count || !AP_TrackFillPrepare())
			return 0;
		const size_t spawn_offset = sizeof(u32) + offsetof(struct Level, DriverSpawn);
		size_t copied = 0;
		u8 *lev = malloc(file->bytes);
		if (!lev)
			return 0;
		const int ok = CustomOffline_CopyFile(request, "lev", lev, file->bytes, &copied) &&
		               copied >= spawn_offset + sizeof apFillOfflineSpawns;
		if (ok)
			memcpy(apFillOfflineSpawns, lev + spawn_offset, sizeof apFillOfflineSpawns);
		free(lev);
		if (!ok)
			return 0;
		apFillOffline.fill_crates = apFillOfflineCrates;
		memcpy(apFillOffline.content_id, "offline", sizeof "offline");
		return 1;
	}
	return 0;
}

int AP_TrackFillOfflineSpawn(u8 preferred, struct SpawnPosRot *out)
{
	const struct SpawnPosRot empty = {0};
	if (!out || preferred >= 8 || AP_TrackFillOfflineTarget(0) <= 0)
		return 0;
	*out = apFillOfflineSpawns[preferred];
	if (memcmp(out, &empty, sizeof empty))
		return 1;
	for (int i = 0; i < 8; ++i)
		if (memcmp(&apFillOfflineSpawns[i], &empty, sizeof empty))
		{
			*out = apFillOfflineSpawns[i];
			break;
		}
	return 1;
}

int AP_TrackFillOfflineTarget(int tier)
{
	return tier >= 0 && tier <= 2 && sdata->gGT && (sdata->gGT->gameMode1 & RELIC_RACE) &&
	    CustomOffline_RuntimeServing(sdata->gGT->levelID, CTR_OFFLINE_MODE_RELIC)
	    ? apFillOffline.relic_targets[tier] : -1;
}

int AP_TrackFillOfflineTier(int race_time)
{
	for (int tier = 2; tier >= 0; --tier)
	{
		int target = AP_TrackFillOfflineTarget(tier);
		if (target >= 0 && race_time <= target)
			return tier;
	}
	return -1;
}
#endif

#define AP_FILL_LEV_BYTES 576392
#define AP_FILL_VRM_BYTES 458808
#define AP_FILL_WEAPON_LEV_BYTES 551532
#define AP_FILL_TEXTURE_W 256
#define AP_FILL_TEXTURE_H 256

static u32 apFillRetailLev[577536 / sizeof(u32)];
static struct Model *apFillCrateModels[3];
static int apFillAssetState;
// WF1: Dingo Canyon's 1P pair (BIGFILE 1 and 0) holds the retail weapon
// crate, model 8 "crate_question", at the audited offset below.
static u32 apFillWeaponLev[552960 / sizeof(u32)];
static struct Model *apFillWeaponModel;
static const unsigned apFillWeaponModelOffset = 406760;

// WF1b: see AP_TrackFillWeaponSearch.
static struct BSP apFillWeaponHitboxTemplate; // flag, radius and radius squared
static int apFillWeaponHitboxLift;              // centre height along the crate's up axis
static int apFillWeaponHitboxReady;
static struct BSP apFillWeaponHitboxes[AP_CONTENT_MAX_FILL_WEAPONS + 1];
static struct InstDef apFillWeaponDefs[AP_CONTENT_MAX_FILL_WEAPONS];
static struct BSP apFillWeaponLeaf;

// Retail weapon crate hitboxes (Dingo Canyon, 16 crates, 32 leaf entries): flag 0x04c0 (touch,
// Y axis, collidable), a sphere of radius 76 whose centre sits 48 units along
// the crate's up axis, and a box of the centre plus and minus the radius.
// Every crate on the donor must agree, or the fill refuses.
static int AP_TrackFillFindWeaponHitbox(u8 *body, unsigned map)
{
	struct Level *level = (struct Level *)body;
	struct mesh_info *mesh = level->ptr_mesh_info;
	struct InstDef *defs = level->ptrInstDefs;
	uintptr_t lo = (uintptr_t)body, hi = lo + map;
	int found = 0, upright = 0;
	if (!mesh || !defs || (uintptr_t)mesh < lo || (uintptr_t)(mesh + 1) > hi ||
	    (uintptr_t)defs < lo || level->numInstances > 65536 ||
	    (uintptr_t)(defs + level->numInstances) > hi || !mesh->bspRoot ||
	    (uintptr_t)mesh->bspRoot < lo || mesh->numBspNodes <= 0 || mesh->numBspNodes > 65536 ||
	    (uintptr_t)(mesh->bspRoot + mesh->numBspNodes) > hi)
		return 0;
	for (int i = 0; i < mesh->numBspNodes; ++i)
	{
		struct BSP *node = &mesh->bspRoot[i];
		if (!(node->flag & BSP_NODE_FLAG_LEAF) || !node->data.leaf.bspHitboxArray)
			continue;
		struct BSP *hit = node->data.leaf.bspHitboxArray;
		if ((uintptr_t)hit < lo)
			return 0;
		for (int j = 0; j < 2048 && (uintptr_t)(hit + j + 1) <= hi && hit[j].flag; ++j)
		{
			struct InstDef *def = hit[j].data.hitbox.instDef;
			if (!def || (uintptr_t)def < (uintptr_t)defs || (uintptr_t)def >= (uintptr_t)(defs + level->numInstances) ||
			    def->modelID != PU_RANDOM_CRATE)
				continue;
			const int radius = hit[j].data.hitbox.radius;
			if (radius <= 0 || hit[j].data.hitbox.unk18 != radius * radius)
				return 0;
			// The box is the broad phase only; the donor rounds one of its 32
			// boxes by a unit (a crate near y 0 has a box from 0, not -1).
			for (int axis = 0; axis < 3; ++axis)
				if (abs(hit[j].box.min.v[axis] - (hit[j].data.hitbox.center.v[axis] - radius)) > 1 ||
				    abs(hit[j].box.max.v[axis] - (hit[j].data.hitbox.center.v[axis] + radius)) > 1)
					return 0;
			if (found && (hit[j].flag != apFillWeaponHitboxTemplate.flag ||
			              radius != apFillWeaponHitboxTemplate.data.hitbox.radius ||
			              hit[j].data.hitbox.unk1A != apFillWeaponHitboxTemplate.data.hitbox.unk1A))
				return 0;
			const int dx = hit[j].data.hitbox.center.x - def->pos.x;
			const int dy = hit[j].data.hitbox.center.y - def->pos.y;
			const int dz = hit[j].data.hitbox.center.z - def->pos.z;
			if (def->rot.x == 0 && def->rot.z == 0)
			{
				// An upright crate gives the lift exactly.
				if (dx || dz || dy <= 0 || (upright && dy != apFillWeaponHitboxLift))
					return 0;
				apFillWeaponHitboxLift = dy;
				++upright;
			}
			if (!found)
			{
				memset(&apFillWeaponHitboxTemplate, 0, sizeof apFillWeaponHitboxTemplate);
				apFillWeaponHitboxTemplate.flag = hit[j].flag;
				apFillWeaponHitboxTemplate.data.hitbox.radius = (s16)radius;
				apFillWeaponHitboxTemplate.data.hitbox.unk18 = hit[j].data.hitbox.unk18;
				apFillWeaponHitboxTemplate.data.hitbox.unk1A = hit[j].data.hitbox.unk1A;
			}
			++found;
		}
	}
	return found > 0 && upright > 0;
}

// The relic LEV/VRM pair of track 0 (BIGFILE 7 and 6) contains all three time
// crate models. Check both pins before relocation or reading the audited
// model and texture tables. Texture sheet slots 8 to 10 hold the weapon crate tiles.
static const unsigned apFillModelOffsets[] = {389876, 388628, 391124};
static const struct {
	u16 tpage, clut;
	u8 u, v, size, weapon;
} apFillTiles[] = {
	{105, 16294,   0,   0, 64, 0},
	{105, 16229,  64,   0, 64, 0},
	{105, 16166, 128,   0, 64, 0},
	{105, 16230, 192,   0, 64, 0},
	{104, 16294, 128, 128, 64, 0},
	{104, 16165, 192, 128, 64, 0},
	{123, 31293, 196, 160, 16, 0},
	{  0,     0,   0,   0,  0, 2}, // unused slot
	{105, 16234, 128,   0, 64, 1},
	{120, 18425, 192, 240, 16, 1},
	{122, 29560,  96, 224, 32, 1},
};

static int AP_TrackFillDecodeTextures(const u8 *vrm, size_t size, u8 *pixels, int weapon)
{
	for (unsigned i = 0; i < sizeof apFillTiles / sizeof apFillTiles[0]; ++i)
	{
		if (apFillTiles[i].weapon != weapon)
			continue;
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

static void AP_TrackFillRemapTexture(struct TextureLayout *layout, int weapon)
{
	for (unsigned i = 0; i < sizeof apFillTiles / sizeof apFillTiles[0]; ++i)
		if (apFillTiles[i].weapon == weapon &&
		    layout->tpage == apFillTiles[i].tpage && layout->clut == apFillTiles[i].clut)
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
		return 1;
	if (!sdata->ptrBigfile1)
		goto refused;
	u8 *raw = (u8 *)apFillRetailLev;
	int size = 0;
	if (!AP_RetailAsset_ReadSubfile(7, 0, raw, sizeof apFillRetailLev, &size) ||
	    size != AP_FILL_LEV_BYTES ||
	    !AP_TrialDigest(raw, size, "80c8c89f0e6e630df59759dc40e3f99fa1fe0c5df11613bee41398884d8f87ac") ||
	    !AP_TrialPointerMapValid(raw, size))
		goto refused;

	u8 *weaponRaw = (u8 *)apFillWeaponLev;
	int weaponSize = 0;
	if (!AP_RetailAsset_ReadSubfile(1, 0, weaponRaw, sizeof apFillWeaponLev, &weaponSize) ||
	    weaponSize != AP_FILL_WEAPON_LEV_BYTES ||
	    !AP_TrialDigest(weaponRaw, weaponSize, "e88732cbc1c776e9d4e5f20e3d4aecbfb797b5c9652eaa3fdc78cfdf1f52c9c5") ||
	    !AP_TrialPointerMapValid(weaponRaw, weaponSize))
		goto refused;

	// Relocate the donor before any texture exists, so a refusal leaks nothing.
	u8 *weaponBody = weaponRaw + 4;
	unsigned weaponMap = AP_TrialU32(weaponRaw);
	LOAD_RunPtrMap((char *)weaponBody, (int *)(weaponBody + weaponMap + 4), AP_TrialU32(weaponBody + weaponMap) / 4);
	apFillWeaponModel = (struct Model *)(weaponBody + apFillWeaponModelOffset);
	apFillWeaponHitboxReady = apFillWeaponModel->id == PU_RANDOM_CRATE &&
	    AP_TrackFillFindWeaponHitbox(weaponBody, weaponMap);
	if (!apFillWeaponHitboxReady)
		goto refused;

	u8 *vrm = malloc(460800);
	u8 *pixels = calloc(AP_FILL_TEXTURE_W * AP_FILL_TEXTURE_H, 4);
	unsigned texture = 0;
	if (vrm && pixels && AP_RetailAsset_ReadSubfile(6, 0, vrm, 460800, &size) &&
	    size == AP_FILL_VRM_BYTES &&
	    AP_TrialDigest(vrm, size, "a9509de1b16083da24a0e9b90a28511ba668a1d83677bb1e2868508e0678f84a") &&
	    AP_TrackFillDecodeTextures(vrm, size, pixels, 0) &&
	    AP_RetailAsset_ReadSubfile(0, 0, vrm, 460800, &size) && size == AP_FILL_VRM_BYTES &&
	    AP_TrialDigest(vrm, size, "17e85a7b561fb2d5574c82152c49e0e79e7a57da09a19eb0cd94439e2382df46") &&
	    AP_TrackFillDecodeTextures(vrm, size, pixels, 1))
		texture = NativeRenderer_CreateRGBATextureChecked(AP_FILL_TEXTURE_W, AP_FILL_TEXTURE_H, pixels);
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
			AP_TrackFillRemapTexture(model->headers[0].ptrTexLayout[i], 0);
		for (int i = 0; i < 2; ++i)
			AP_TrackFillRemapTexture(model->headers[1].ptrTexLayout[i], 0);
	}
	for (int i = 0; i < 20; ++i)
		AP_TrackFillRemapTexture(apFillWeaponModel->headers[0].ptrTexLayout[i], 1);
	for (int i = 0; i < 2; ++i)
		AP_TrackFillRemapTexture(apFillWeaponModel->headers[1].ptrTexLayout[i], 1);
	NativeGpu_SetTimeCrateTexture(texture, AP_FILL_TEXTURE_W, AP_FILL_TEXTURE_H);
	apFillAssetState = 1;
	AP_LogLine("[TrackFill] retail time crate and weapon crate models and textures loaded\n");
	return 1;

refused:
	AP_LogLine("[TrackFill] track fill unavailable: retail crate models or textures could not be verified and loaded\n");
	return 0;
}

static const ctr_content_package *AP_TrackFillRace(const struct GameTracker *g)
{
	if (g->numPlyrCurrGame != 1 || !(g->gameMode1 & RELIC_RACE))
		return NULL;
#ifdef CTR_CUSTOM_PACKAGES
	if (CustomOffline_RuntimeServing(g->levelID, CTR_OFFLINE_MODE_RELIC))
		return apFillOffline.fill_crate_count ? &apFillOffline : NULL;
#endif
	const struct CustomTrackRaceContext *context = CustomTrack_StandaloneContext(g->levelID);
	if (!context || context->retail || context->cortex || context->mode != AP_CONTENT_MODE_RELIC ||
	    !context->seedEpoch || context->seedEpoch != ap_content_plan_epoch())
		return NULL;
	const ctr_content_pad *pad = ap_content_plan_pad(context->physicalPad);
	const ctr_content_package *pkg = pad ? ap_content_plan_package(pad->package_index) : NULL;
	return pkg && pkg->fill_crate_count && !strcmp(context->levSha256, pkg->lev_sha256) ? pkg : NULL;
}

int AP_TrackFillRaceActive(const struct GameTracker *g)
{
	return g && AP_TrackFillRace(g) != NULL;
}

void AP_TrackFillRegister(struct GameTracker *g)
{
	if (!AP_TrackFillRace(g) || !AP_TrackFillPrepare())
		return;
	for (int i = 0; i < 3; ++i)
		g->modelPtr[apFillCrateModels[i]->id] = apFillCrateModels[i];
}

void AP_TrackFillSpawn(struct GameTracker *g)
{
	const ctr_content_package *pkg = AP_TrackFillRace(g);
	if (!pkg)
		return;
	struct Instance *spawned[AP_CONTENT_MAX_FILL_CRATES];
	int count = 0;
	if (!AP_TrackFillPrepare() || g->timeCratesInLEV ||
	    g->JitPools.instance.free.count < pkg->fill_crate_count)
		goto refused;
	for (; count < pkg->fill_crate_count; ++count)
	{
		const ctr_content_fill_crate *crate = &pkg->fill_crates[count];
		struct Instance *inst = INSTANCE_Birth3D(apFillCrateModels[crate->type - 1], "ap-fill-crate", NULL);
		if (!inst)
			goto refused;
		spawned[count] = inst;
		memset(inst->matrix.m, 0, sizeof inst->matrix.m);
		for (int axis = 0; axis < 3; ++axis)
		{
			inst->matrix.m[axis][axis] = 0x1000;
			inst->matrix.t[axis] = crate->pos[axis];
		}
	}
	g->timeCratesInLEV = count;
	char line[128];
	snprintf(line, sizeof line, "[TrackFill] spawned %d time crates for %s\n", count, pkg->content_id);
	AP_LogLine(line);
	return;

refused:
	while (count > 0)
		INSTANCE_Death(spawned[--count]);
	g->timeCratesInLEV = -1;
	AP_LogLine("[TrackFill] could not spawn the complete time crate set; no relic awards for this attempt\n");
}

int AP_TrackFillFailed(const struct GameTracker *g)
{
	return g->timeCratesInLEV < 0 && AP_TrackFillRace(g) != NULL;
}

void AP_TrackFillAfterCheckpointRestore(struct GameTracker *g)
{
	if (AP_TrackFillRaceActive(g) && !((g->gameMode1 & END_OF_RACE) && (g->gameModeEnd & NEW_RELIC)))
		AP_RelicTargetRestore(g);
}

static s16 AP_TrackFillBound(int value)
{
	return value < -32768 ? -32768 : value > 32767 ? 32767 : (s16)value;
}

void AP_TrackFillCollide(struct GameTracker *g)
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
			.min = {{AP_TrackFillBound(x - 92), AP_TrackFillBound(y - 44), AP_TrackFillBound(z - 92)}},
			.max = {{AP_TrackFillBound(x + 92), AP_TrackFillBound(y + 140), AP_TrackFillBound(z + 92)}},
		};
		if (AP_TrialLetterHit(&prev, &curr, &box))
		{
			struct ScratchpadStruct collision = {0};
			collision.Input1.modelID = DYNAMIC_PLAYER;
			RB_CrateTime_LInC(inst, d->instSelf->thread, &collision);
		}
	}
}

// ── WF1: weapon crates ─────────────────────────────────────────────────────
// A seed's frozen fill adds real weapon crates to a LEV measured without any:
// the retail model from the player's disc, the retail weapon crate collision
// handler (reward, break and regrowth), at the fill's exact position and
// angle. They stand in every race on that LEV except Time Trial and Relic
// Race, where retail tracks carry no weapon crates either.

// The plan package pinned to exactly this LEV and VRM pair (unique per room).
static const ctr_content_package *AP_TrackFillWeaponPackage(const char *lev, const char *vrm)
{
	if (!lev || !vrm || !lev[0] || !vrm[0])
		return NULL;
	for (int i = 0; i < AP_CONTENT_MAX_PACKAGES; ++i)
	{
		const ctr_content_package *pkg = ap_content_plan_package(i);
		if (!pkg)
			break;
		if (pkg->fill_weapon_count && !strcmp(lev, pkg->lev_sha256) && !strcmp(vrm, pkg->vrm_sha256))
			return pkg;
	}
	return NULL;
}

static const ctr_content_package *AP_TrackFillWeaponRace(const struct GameTracker *g)
{
	if (!g || g->numPlyrCurrGame != 1 || (g->gameMode1 & (RELIC_RACE | TIME_TRIAL | BATTLE_MODE | MAIN_MENU)) ||
	    !ap_content_plan_active())
		return NULL;
	// The served pair decides: a pad race or cup leg (boss cups included)
	// serves its context's pair, another boss race its custom venue's pair.
	// Every route that races the package stands its filled crates, so the
	// sanity counts agree with the apworld on each of them.
	const struct CustomTrackRaceContext *context = CustomTrack_StandaloneContext(g->levelID);
	if (context && !context->retail && !context->cortex && context->seedEpoch &&
	    context->seedEpoch == ap_content_plan_epoch() && (context->cup || !IS_BOSS_RACE(g->gameMode1)))
		return AP_TrackFillWeaponPackage(context->levSha256, context->vrmSha256);
	const char *lev = NULL, *vrm = NULL;
	if (IS_BOSS_RACE(g->gameMode1) && CustomTrack_BossVenuePins(g->levelID, g->bossID, 1, &lev, &vrm))
		return AP_TrackFillWeaponPackage(lev, vrm);
	return NULL;
}

// Before a custom garage venue is entered: the crates its race would stand
// need their model and texture (pads and cup legs check at race entry).
int AP_TrackFillBossVenueReady(int levelID, int bossID)
{
	const char *lev = NULL, *vrm = NULL;
	if (!ap_content_plan_active() || !CustomTrack_BossVenuePins(levelID, bossID, 1, &lev, &vrm) ||
	    !AP_TrackFillWeaponPackage(lev, vrm))
		return 1;
	return AP_TrackFillPrepare();
}

int AP_TrackFillWeaponRaceActive(const struct GameTracker *g)
{
	return AP_TrackFillWeaponRace(g) != NULL;
}

static int AP_TrackFillIsWeapon(const struct Instance *inst)
{
	return inst && apFillWeaponModel && inst->model == apFillWeaponModel;
}

// The spawned crate of fill ordinal `index` (0-based), found by its exact
// position in the instance pool, which owns it across quick loads.
static struct Instance *AP_TrackFillWeaponInstance(struct GameTracker *g, const ctr_content_fill_weapon *crate)
{
	for (struct Instance *inst = (struct Instance *)g->JitPools.instance.taken.first; inst; inst = inst->next)
		if (AP_TrackFillIsWeapon(inst) && inst->matrix.t[0] == crate->pos[0] &&
		    inst->matrix.t[1] == crate->pos[1] && inst->matrix.t[2] == crate->pos[2])
			return inst;
	return NULL;
}

int AP_TrackFillWeaponCount(struct GameTracker *g)
{
	const ctr_content_package *pkg = AP_TrackFillWeaponRace(g);
	return pkg ? pkg->fill_weapon_count : 0;
}

struct Instance *AP_TrackFillWeaponOrdinal(struct GameTracker *g, int index)
{
	const ctr_content_package *pkg = AP_TrackFillWeaponRace(g);
	if (!pkg || index < 0 || index >= pkg->fill_weapon_count)
		return NULL;
	return AP_TrackFillWeaponInstance(g, &pkg->fill_weapons[index]);
}

struct Instance *AP_TrackFillWeaponAt(struct GameTracker *g, int x, int y, int z, int rotY)
{
	const ctr_content_package *pkg = AP_TrackFillWeaponRace(g);
	for (int i = 0; pkg && i < pkg->fill_weapon_count; ++i)
	{
		const ctr_content_fill_weapon *crate = &pkg->fill_weapons[i];
		if (crate->pos[0] == x && crate->pos[1] == y && crate->pos[2] == z && crate->rot[1] == rotY)
			return AP_TrackFillWeaponInstance(g, crate);
	}
	return NULL;
}

void AP_TrackFillWeaponSpawn(struct GameTracker *g)
{
	const ctr_content_package *pkg = AP_TrackFillWeaponRace(g);
	if (!pkg)
		return;
	struct Instance *spawned[AP_CONTENT_MAX_FILL_WEAPONS];
	int count = 0;
	if (!AP_TrackFillPrepare() || g->JitPools.instance.free.count < pkg->fill_weapon_count)
		goto refused;
	for (; count < pkg->fill_weapon_count; ++count)
	{
		const ctr_content_fill_weapon *crate = &pkg->fill_weapons[count];
		struct Instance *inst = INSTANCE_Birth3D(apFillWeaponModel, "ap-fill-weapon", NULL);
		if (!inst)
			goto refused;
		spawned[count] = inst;
		SVec3 rot = {{crate->rot[0], crate->rot[1], crate->rot[2]}};
		ConvertRotToMatrix(&inst->matrix, &rot);
		for (int axis = 0; axis < 3; ++axis)
			inst->matrix.t[axis] = crate->pos[axis];
	}
	char line[128];
	snprintf(line, sizeof line, "[TrackFill] spawned %d weapon crates for %s\n", count, pkg->content_id);
	AP_LogLine(line);
	return;

refused:
	while (count > 0)
		INSTANCE_Death(spawned[--count]);
	AP_LogLine("[TrackFill] could not spawn the filled weapon crates; this race has none\n");
}

// WF1b: filled crates take part in the retail collision searches. A LEV
// measured without weapon crates has no BSP hitbox for them, so rockets,
// bombs, potions, the Warp Orb, explosions and karts never met them. Each
// live filled crate gets the hitbox retail gives a weapon crate (flag, box,
// centre and radius copied from Dingo Canyon's own crate hitboxes, relative
// to the crate) and an InstDef that points at the crate instance. The retail
// leaf tests then run over those hitboxes after every search of the level's
// BSP, so every breaker reaches RB_CrateWeapon_LInC through its own retail
// path, with the retail hitbox size.
// The hitboxes of the filled crates whose reach overlaps `search`, one slot
// per fill row so a crate keeps one address: the kart search remembers the
// hitboxes it already met by address across its sub-steps. A row that is far
// away or has no live crate holds an inert entry (not collidable, empty box)
// that no leaf test can touch. The crate rows are static, so a search far
// from every crate costs one box test per row; only a near crate is looked up
// in the instance pool, every search, so no pointer outlives a quick load, a
// restart or a crate's death.
static int AP_TrackFillWeaponHitboxes(const struct BoundingBox *search)
{
	struct GameTracker *g = sdata->gGT;
	if (apFillAssetState != 1 || !apFillWeaponHitboxReady || !g || sdata->Loading.stage != LOAD_IDLE)
		return 0;
	const ctr_content_package *pkg = AP_TrackFillWeaponRace(g);
	if (!pkg)
		return 0;
	const int radius = apFillWeaponHitboxTemplate.data.hitbox.radius;
	const int reach = radius + apFillWeaponHitboxLift;
	const int rows = pkg->fill_weapon_count < AP_CONTENT_MAX_FILL_WEAPONS ? pkg->fill_weapon_count
	                                                                     : AP_CONTENT_MAX_FILL_WEAPONS;
	int live = 0;
	for (int row = 0; row < rows; ++row)
	{
		const ctr_content_fill_weapon *crate = &pkg->fill_weapons[row];
		struct InstDef *def = &apFillWeaponDefs[row];
		struct BSP *hit = &apFillWeaponHitboxes[row];
		int near = 1;
		for (int axis = 0; axis < 3 && near; ++axis)
			near = crate->pos[axis] - reach <= search->max.v[axis] && search->min.v[axis] <= crate->pos[axis] + reach;
		struct Instance *inst = near ? AP_TrackFillWeaponInstance(g, crate) : NULL;
		memset(hit, 0, sizeof *hit);
		if (!inst)
		{
			hit->flag = BSP_HITBOX_USE_Y_AXIS; // inert: nonzero, so the list goes on
			hit->box.min.x = hit->box.min.y = hit->box.min.z = 32767;
			hit->box.max.x = hit->box.max.y = hit->box.max.z = -32768;
			continue;
		}
		memset(def, 0, sizeof *def);
		def->model = apFillWeaponModel;
		def->scale.x = def->scale.y = def->scale.z = 0x1000;
		def->ptrInstance = inst;
		def->modelID = PU_RANDOM_CRATE;
		*hit = apFillWeaponHitboxTemplate;
		for (int axis = 0; axis < 3; ++axis)
		{
			// The centre follows the crate's up axis, as on a tilted retail
			// crate (within a unit of the donor's rounding).
			const int pos = inst->matrix.t[axis];
			const int center = pos + CTR_MipsSra(inst->matrix.m[axis][1] * apFillWeaponHitboxLift, 12);
			def->pos.v[axis] = AP_TrackFillBound(pos);
			hit->data.hitbox.center.v[axis] = AP_TrackFillBound(center);
			hit->box.min.v[axis] = AP_TrackFillBound(center - radius);
			hit->box.max.v[axis] = AP_TrackFillBound(center + radius);
		}
		hit->data.hitbox.instDef = def;
		++live;
	}
	memset(&apFillWeaponHitboxes[rows], 0, sizeof apFillWeaponHitboxes[rows]);
	return live;
}

void AP_TrackFillWeaponSearch(const struct BSP *root, void (*callback)(struct BSP *, struct ScratchpadStruct *),
                              struct ScratchpadStruct *sps)
{
	struct GameTracker *g = sdata->gGT;
	if (!g || !g->level1 || !g->level1->ptr_mesh_info || root != g->level1->ptr_mesh_info->bspRoot)
		return;
	const int instanceTest = callback == COLL_FIXED_BSPLEAF_TestInstance ||
	    ((callback == COLL_FIXED_BSPLEAF_TestQuadblocks || callback == COLL_MOVED_BSPLEAF_TestQuadblocks) &&
	     (sps->Union.QuadBlockColl.searchFlags & COLL_SEARCH_TEST_INSTANCES));
	// The thread-bucket search takes level hitboxes only through the burst
	// callback (rockets and bombs exploding); TNT and Nitro hand that search a
	// thread callback, and the potion's Teeth search ignores crates.
	const int burst = callback == PROC_PerBspLeaf_CheckInstances &&
	    sps->Union.ThBuckColl.funcCallback == RB_Burst_CollLevInst;
	if (!instanceTest && !burst)
		return;
	// A level hitbox this search already touched keeps its claim: the filled
	// crates are tested after every real leaf, so they never replace a door or
	// another object the retail order would report (they meet the next probe).
	if (instanceTest && sps->boolDidTouchHitbox)
		return;
	if (!AP_TrackFillWeaponHitboxes(instanceTest ? &sps->bbox : &sps->Union.ThBuckColl.bbox))
		return;
	memset(&apFillWeaponLeaf, 0, sizeof apFillWeaponLeaf);
	apFillWeaponLeaf.flag = BSP_NODE_FLAG_LEAF;
	apFillWeaponLeaf.data.leaf.bspHitboxArray = apFillWeaponHitboxes;
	if (instanceTest)
		COLL_FIXED_BSPLEAF_TestInstance(&apFillWeaponLeaf, sps);
	else
		PROC_PerBspLeaf_CheckInstances(&apFillWeaponLeaf, sps);
}
#endif
