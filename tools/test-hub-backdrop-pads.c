// cc -m32 -Wall -Wextra -DCTR_NATIVE -DCTR_AP -DBUILD=926 -I include -I . tools/test-hub-backdrop-pads.c -o /tmp/test-hub-backdrop-pads -lm
/* Run the production animation using engine structures. Nothing from the
 * gameplay warp-pad subsystem is linked. */
#include <common.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
struct sData sdata_static;
static struct Instance born[128];
static int calls, failAt = -1;
struct Instance *INSTANCE_Birth3D(struct Model *model, const char *name, struct Thread *thread)
{
	struct Instance *inst;
	assert(thread == NULL && name != NULL);
	if (calls++ == failAt) return NULL;
	assert(calls <= 128);
	inst = &born[calls - 1];
	memset(inst, 0, sizeof *inst);
	inst->model = model;
	return inst;
}
int MATH_Sin(u32 a) { return (int)lround(sin((a & 4095) * 6.283185307179586 / 4096) * 4096); }
int MATH_Cos(u32 a) { return MATH_Sin(a + 1024); }
/* Record rotations in the matrix while preserving its translation, as the
 * engine converter does. The fixture checks animation, not trig conversion. */
void ConvertRotToMatrix(MATRIX *m, const SVec3 *r)
{
	m->m[0][0] = r->x; m->m[1][1] = r->y; m->m[2][2] = r->z;
}
#include "../ap/ap_hub_backdrop_pads.h"
int main(void)
{
	static struct GameTracker gt;
	static struct Model models[256];
	struct Instance inst = {0}, before;
	struct Instance snapshot[4];
	int i, f;
	for (i = 0; i < 256; i++) { models[i].id = i; gt.modelPtr[i] = &models[i]; }
	inst.matrix.t[0] = -500; inst.matrix.t[1] = 120; inst.matrix.t[2] = 800;
	for (i = 0; i < 256; i++)
	{
		if (i == STATIC_WARPPAD) continue;
		inst.model = &models[i]; before = inst;
		AP_HNE_AddBackdropPad(&gt, &inst);
		assert(calls == 0 && memcmp(&inst, &before, sizeof inst) == 0);
	}
	inst.model = &models[STATIC_WARPPAD]; before = inst;
	AP_HNE_AddBackdropPad(&gt, &inst);
	assert(calls == 4 && s_apHnePadCount == 1);
	assert(born[0].model->id == STATIC_BEAM && born[0].alphaScale == 0xc00);
	assert(born[1].model->id == STATIC_TROPHY && born[1].scale.y == 0x2800);
	assert(born[1].colorRGBA == 0 && born[1].flags == 0);
	assert(born[2].model->id == STATIC_BOTTOMRING && born[3].model->id == STATIC_BOTTOMRING);
	AP_HNE_AnimateBackdropPads();
	assert(born[1].matrix.m[1][1] == 0x80 && born[1].matrix.m[2][2] == 0x155);
	assert(born[1].matrix.t[0] == -500 && born[1].matrix.t[1] == 376 && born[1].matrix.t[2] == 960);
	for (i = 0; i < 1024; i++)
	{
		AP_HNE_AnimateBackdropPads();
		assert(abs(born[1].matrix.t[0] + 500) <= 160);
		assert(abs(born[1].matrix.t[1] - 376) <= 64);
		assert(abs(born[1].matrix.t[2] - 800) <= 160);
		assert(born[1].model->id == STATIC_TROPHY && born[1].colorRGBA == 0);
		assert(born[0].alphaScale == 0xc00);
	}
	memcpy(snapshot, born, sizeof snapshot);
	AP_HNE_ResetBackdropPads();
	AP_HNE_AnimateBackdropPads();
	assert(memcmp(snapshot, born, sizeof snapshot) == 0);
	assert(memcmp(&inst, &before, sizeof inst) == 0);
	/* Every individual allocation can fail without suppressing other parts. */
	for (f = 0; f < 4; f++)
	{
		calls = 0; failAt = f;
		AP_HNE_AddBackdropPad(&gt, &inst);
		assert(calls == 4);
		AP_HNE_AnimateBackdropPads(); AP_HNE_ResetBackdropPads();
	}
	failAt = -1; calls = 0;
	gt.modelPtr[STATIC_BEAM] = gt.modelPtr[STATIC_TROPHY] = gt.modelPtr[STATIC_BOTTOMRING] = NULL;
	for (i = 0; i < 40; i++) AP_HNE_AddBackdropPad(&gt, &inst);
	assert(calls == 0 && s_apHnePadCount == 32);
	AP_HNE_AnimateBackdropPads(); AP_HNE_ResetBackdropPads();
	inst.thread = (struct Thread *)&gt;
	AP_HNE_AddBackdropPad(&gt, &inst);
	assert(s_apHnePadCount == 0);
	AP_HNE_AddBackdropPad(&gt, NULL);
	inst.thread = NULL; inst.model = NULL;
	AP_HNE_AddBackdropPad(&gt, &inst);
	assert(s_apHnePadCount == 0);
	puts("PASS: backdrop production beam/trophy animation, isolation, lifetime, allocation failures and capacity");
}
