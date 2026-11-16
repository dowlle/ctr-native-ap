/* Presentation-only retail beam and trophy. No warp-pad birth or tick runs.
 * The ordinary level instance pool owns every child. Forget the pointers
 * before that pool is reset on a level change. */
#ifndef AP_HUB_BACKDROP_PADS_H
#define AP_HUB_BACKDROP_PADS_H

#define AP_HNE_BACKDROP_PADS 32
static struct AP_HNE_BackdropPad
{
	struct Instance *beam, *trophy, *ring[2];
	int pos[3];
	unsigned tick, rng, beamYaw;
} s_apHnePads[AP_HNE_BACKDROP_PADS];
static int s_apHnePadCount;

static void AP_HNE_ResetBackdropPads(void)
{
	/* Do not dereference old instances, the level pool may already be clear. */
	s_apHnePadCount = 0;
}

static struct Instance *AP_HNE_PadInstance(struct GameTracker *gGT, int id,
                                          const char *name, struct Instance *pad)
{
	struct Instance *inst;
	if (gGT->modelPtr[id] == NULL)
		return NULL;
	inst = INSTANCE_Birth3D(gGT->modelPtr[id], name, NULL);
	if (inst != NULL)
		inst->matrix = pad->matrix;
	return inst;
}

static void AP_HNE_AddBackdropPad(struct GameTracker *gGT, struct Instance *inst)
{
	struct AP_HNE_BackdropPad *pad;
	int i;
	if (inst == NULL || inst->model == NULL || inst->model->id != STATIC_WARPPAD ||
	    inst->thread != NULL || s_apHnePadCount >= AP_HNE_BACKDROP_PADS)
		return;
	pad = &s_apHnePads[s_apHnePadCount++];
	memset(pad, 0, sizeof *pad);
	for (i = 0; i < 3; i++) pad->pos[i] = inst->matrix.t[i];
	pad->rng = 0x2545f491u ^ (unsigned)s_apHnePadCount;
	pad->beam = AP_HNE_PadInstance(gGT, STATIC_BEAM, "backdrop-beam", inst);
	if (pad->beam != NULL) pad->beam->alphaScale = 0xc00;
	pad->trophy = AP_HNE_PadInstance(gGT, STATIC_TROPHY, "backdrop-trophy", inst);
	if (pad->trophy != NULL)
	{
		pad->trophy->matrix.t[1] += 0x100;
		pad->trophy->scale.x = pad->trophy->scale.y = pad->trophy->scale.z = 0x2800;
	}
	for (i = 0; i < 2; i++)
	{
		pad->ring[i] = AP_HNE_PadInstance(gGT, STATIC_BOTTOMRING, "backdrop-ring", inst);
		if (pad->ring[i] != NULL)
		{
			pad->ring[i]->matrix.t[1] += i * 0x400;
			pad->ring[i]->alphaScale = 0x400;
		}
	}
}

static void AP_HNE_AnimateBackdropPads(void)
{
	int p, i;
	for (p = 0; p < s_apHnePadCount; p++)
	{
		struct AP_HNE_BackdropPad *pad = &s_apHnePads[p];
		SVec3 rot = {0};
		unsigned tick = ++pad->tick;
		/* Same retail beam step range, with private deterministic randomness.
		 * Never consume the gameplay RNG while name entry is open. */
		if (pad->beam != NULL && (tick & 1))
		{
			pad->rng ^= pad->rng << 13;
			pad->rng ^= pad->rng >> 17;
			pad->rng ^= pad->rng << 5;
			pad->beamYaw = (pad->beamYaw + (pad->rng % 6 + 1) * 0x200) & 0xfff;
			rot.y = (s16)pad->beamYaw;
			ConvertRotToMatrix(&pad->beam->matrix, &rot);
		}
		for (i = 0; i < 2; i++)
		{
			struct Instance *ring = pad->ring[i];
			int rise = 0x20 + i * 0x10;
			if (ring == NULL) continue;
			rot.y = (s16)((tick * 0x100) & 0xfff);
			ConvertRotToMatrix(&ring->matrix, &rot);
			if (ring->matrix.t[1] < pad->pos[1] + 0x600)
			{
				ring->matrix.t[1] += rise;
				ring->alphaScale += ring->matrix.t[1] < pad->pos[1] + rise * 4 ?
				                    -0x380 : 0xc00 / (0x600 / rise);
			}
			else
			{
				ring->matrix.t[1] = pad->pos[1];
				ring->alphaScale = 0x1000;
			}
		}
		if (pad->trophy != NULL)
		{
			/* AH_WarpPad_SpinRewards slot zero: yaw, tilt, bob and orbit.
			 * Three retail slots advance the shared orbit by 3 * 4 per tick. */
			rot.y = (s16)((tick * 0x80) & 0xfff);
			rot.z = 0x155;
			ConvertRotToMatrix(&pad->trophy->matrix, &rot);
			pad->trophy->matrix.t[0] = pad->pos[0] + (MATH_Sin(((tick - 1) * 12) & 0xfff) * 0xa0 >> 12);
			pad->trophy->matrix.t[1] = pad->pos[1] + 0x100 + (MATH_Sin(((tick - 1) * 0x20) & 0xfff) * 0x40 >> 12);
			pad->trophy->matrix.t[2] = pad->pos[2] + (MATH_Cos(((tick - 1) * 12) & 0xfff) * 0xa0 >> 12);
		}
	}
}
#endif
