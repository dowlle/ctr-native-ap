// cc -m32 -Wall -Wextra -DCTR_NATIVE -DCTR_AP -DBUILD=926 -I include -I . tools/test-hub-backdrop-pieces.c -o /tmp/test-hub-backdrop-pieces -lm
/* Execute the production scenery helper with engine structures. Only the
 * instance allocator and trig are stubbed, no gameplay functions link. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <common.h>
struct sData sdata_static;

static struct Instance born;
static int calls, failAllocation;
struct Instance *INSTANCE_Birth3D(struct Model *model, const char *name, struct Thread *thread)
{
	assert(thread == NULL && name != NULL);
	calls++;
	if (failAllocation) return NULL;
	memset(&born, 0, sizeof born);
	born.model = model;
	born.scale.x = born.scale.y = born.scale.z = 0x1000;
	return &born;
}
int MATH_Sin(u32 angle) { return (int)lround(sin((angle & 4095) * 6.283185307179586 / 4096) * 4096); }
int MATH_Cos(u32 angle) { return MATH_Sin(angle + 1024); }
#include "../ap/ap_hub_backdrop_pieces.h"

int main(void)
{
	static struct GameTracker gt;
	static struct Model models[256];
	struct Instance inst, before;
	struct InstDef def;
	int i, hub, yaw, ids[] = {STATIC_PINGARAGE, STATIC_PAPUGARAGE, STATIC_ROOGARAGE, STATIC_JOEGARAGE};
	for (i = 0; i < 256; i++) { models[i].id = i; gt.modelPtr[i] = &models[i]; }
	memset(&def, 0, sizeof def);
	memset(&inst, 0, sizeof inst);
	inst.instDef = &def;
	inst.matrix.t[0] = -500;
	inst.matrix.t[1] = def.pos.y = 120;
	inst.matrix.t[2] = 800;
	inst.matrix.m[0][0] = inst.matrix.m[1][1] = inst.matrix.m[2][2] = 4096;
	for (hub = GEM_STONE_VALLEY; hub <= CITADEL_CITY; hub++)
	for (yaw = 0; yaw < 4096; yaw += 1024)
	for (i = 4; i <= 5; i++)
	{
		int expected = hub == GLACIER_PARK ? STATIC_DOOR3 : hub == N_SANITY_BEACH && i == 5 ? STATIC_DOOR2 : STATIC_DOOR;
		gt.levelID = hub;
		inst.model = &models[STATIC_DOOR];
		strcpy(inst.name, i == 5 ? "door#5" : "door#4");
		def.rot.y = yaw;
		assert(AP_HNE_AddBackdropPiece(&gt, &inst) == 1);
		assert(born.model == &models[expected] && born.thread == NULL && inst.thread == NULL);
		assert((born.flags & (SPLIT_SPECIAL | REVERSE_CULL_DIRECTION)) == (SPLIT_SPECIAL | REVERSE_CULL_DIRECTION));
		assert(inst.flags & SPLIT_SPECIAL);
		assert(born.scale.x == -4096 && born.scale.y == 4096 && born.scale.z == 4096);
		assert(memcmp(born.matrix.m, inst.matrix.m, sizeof born.matrix.m) == 0);
		assert(born.matrix.t[0] == -500 + (MATH_Cos(yaw) * 0x600 >> 12));
		assert(born.matrix.t[1] == 120);
		assert(born.matrix.t[2] == 800 + (MATH_Sin(yaw) * 0x600 >> 12));
	}
	for (i = 0; i < 4; i++)
	for (yaw = 0; yaw < 4096; yaw += 1024)
	{
		inst.model = &models[ids[i]];
		def.rot.y = yaw;
		assert(AP_HNE_AddBackdropPiece(&gt, &inst) == 1);
		assert(born.model == &models[STATIC_GARAGETOP] && born.thread == NULL);
		assert(born.matrix.t[0] == -500 + (MATH_Sin(yaw) * 0x4c >> 12));
		assert(born.matrix.t[1] == 120 + 0x300);
		assert(born.matrix.t[2] == 800 + (MATH_Cos(yaw) * 0x4c >> 12));
		assert(memcmp(born.matrix.m, inst.matrix.m, sizeof born.matrix.m) == 0);
		assert(born.depthBiasNormal == 0xfe && inst.depthBiasNormal == 1 && inst.depthBiasSecondary == 1);
		assert(inst.unk53 == 0 && inst.vertSplit == 120 + 0x300);
	}
	/* Everything except wooden doors and the four roll-up garages is inert. */
	for (i = 0; i < 256; i++)
	{
		if (i == STATIC_DOOR || (i >= STATIC_PINGARAGE && i <= STATIC_JOEGARAGE)) continue;
		int n = calls;
		inst.model = &models[i]; before = inst;
		assert(AP_HNE_AddBackdropPiece(&gt, &inst) == 0 && calls == n);
		assert(memcmp(&inst, &before, sizeof inst) == 0);
	}
	inst.model = &models[STATIC_DOOR];
	gt.levelID = GEM_STONE_VALLEY;
	before = inst;
	failAllocation = 1;
	assert(AP_HNE_AddBackdropPiece(&gt, &inst) == 0 && memcmp(&inst, &before, sizeof inst) == 0);
	failAllocation = 0;
	gt.modelPtr[STATIC_DOOR] = NULL;
	i = calls;
	assert(AP_HNE_AddBackdropPiece(&gt, &inst) == 0 && calls == i);
	gt.modelPtr[STATIC_DOOR] = &models[STATIC_DOOR];
	inst.thread = (struct Thread *)&gt;
	assert(AP_HNE_AddBackdropPiece(&gt, &inst) == 0 && calls == i);
	inst.thread = NULL;
	inst.instDef = NULL;
	assert(AP_HNE_AddBackdropPiece(&gt, &inst) == 0 && calls == i);
	inst.instDef = &def; inst.model = NULL;
	assert(AP_HNE_AddBackdropPiece(&gt, &inst) == 0 && calls == i);
	assert(AP_HNE_AddBackdropPiece(&gt, NULL) == 0 && calls == i);
	puts("PASS: production backdrop pieces, five hubs, four orientations, model selection and failure guards");
	return 0;
}
