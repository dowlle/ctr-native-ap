#ifdef CTR_AP

#include <common.h> // struct Instance / GameTracker / Model, sdata, InstanceFlags, functions.h
#include <stdio.h>

#include "ap_spawn.h"
#include "ap_hooks.h" // AP_LogLine

// ============================================================================
// ADDITIVE MODEL LOADER -- implementation. See ap_spawn.h for the mechanism and
// for what this deliberately leaves out (collision). Everything here is one
// translation-unit member of the unity build.
// ============================================================================

struct ApSpawnEntry
{
	int              used;
	int              modelID;
	int              lifetime;
	Vec3             pos;
	SVec3            rot;
	s16              scale;
	u32              colorRGBA; // 0 = the INSTANCE_Birth default (no tint)
	int              visible;
	int              dirty; // transform / colour edited since the last push
	const char      *name;
	struct Instance *inst; // NULL while waiting for a model or a pool
};

static struct ApSpawnEntry s_spawns[AP_SPAWN_MAX];
static int                 s_spawnCount;
static int                 s_generation;

// Default instance flags for a spawned model.
//
// DRAW_COLLISION_MASK is what INSTANCE_Birth3D itself hands every runtime
// instance (INSTANCE.c:70) and is what the LOD test in RenderBucket_QueueDraw
// (RenderBucket_QueueExecute.c:2062) is looking for -- an instance without it is
// silently skipped.
//
// Do not add VISIBLE_DURING_GAMEPLAY/RB_INSTANCE_SKIP_OT_RANGE. Fresh runtime
// instances have no ordering-table range to reuse, so skipping its allocation
// makes QueueDraw succeed while the primitive writer drops every triangle.
// (One of the two 2026-08-07 authoring-session runtime repairs, landed from
// the checkpoint's dirty tree on 2026-08-11 -- it answered #182's draw-culling
// open question the hard way, in game.)
#define AP_SPAWN_FLAGS DRAW_COLLISION_MASK

static struct ApSpawnEntry *AP_SpawnEntry(AP_SpawnHandle h)
{
	if (h < 0 || h >= AP_SPAWN_MAX)
		return 0;
	if (!s_spawns[h].used)
		return 0;
	return &s_spawns[h];
}

AP_SpawnHandle AP_Spawn_Add(int modelID, const Vec3 *pos, const SVec3 *rot, int lifetime, const char *name)
{
	int i;

	for (i = 0; i < AP_SPAWN_MAX; i++)
	{
		if (s_spawns[i].used)
			continue;

		s_spawns[i].used = 1;
		s_spawns[i].modelID = modelID;
		s_spawns[i].lifetime = lifetime;
		s_spawns[i].pos = (pos != 0) ? *pos : (Vec3){{0, 0, 0}};
		s_spawns[i].rot = (rot != 0) ? *rot : (SVec3){{0, 0, 0}};
		s_spawns[i].scale = 0x1000; // 1.0, the INSTANCE_Birth default
		s_spawns[i].colorRGBA = 0;
		s_spawns[i].visible = 1;
		s_spawns[i].dirty = 1;
		s_spawns[i].name = name;
		s_spawns[i].inst = 0;
		s_spawnCount++;
		return (AP_SpawnHandle)i;
	}

	{
		char msg[80];
		snprintf(msg, sizeof msg, "[AP SPAWN] table full (%d), model %d not spawned\n",
		         AP_SPAWN_MAX, modelID);
		AP_LogLine(msg);
	}
	return AP_SPAWN_INVALID;
}

void AP_Spawn_SetPos(AP_SpawnHandle h, const Vec3 *pos)
{
	struct ApSpawnEntry *e = AP_SpawnEntry(h);
	if (e == 0 || pos == 0)
		return;
	e->pos = *pos;
	e->dirty = 1;
}

void AP_Spawn_SetRot(AP_SpawnHandle h, const SVec3 *rot)
{
	struct ApSpawnEntry *e = AP_SpawnEntry(h);
	if (e == 0 || rot == 0)
		return;
	e->rot = *rot;
	e->dirty = 1;
}

void AP_Spawn_SetScale(AP_SpawnHandle h, s16 scale)
{
	struct ApSpawnEntry *e = AP_SpawnEntry(h);
	if (e == 0)
		return;
	e->scale = scale;
	e->dirty = 1;
}

void AP_Spawn_SetColour(AP_SpawnHandle h, u32 colorRGBA)
{
	struct ApSpawnEntry *e = AP_SpawnEntry(h);
	if (e == 0)
		return;
	e->colorRGBA = colorRGBA;
	e->dirty = 1;
}

void AP_Spawn_SetVisible(AP_SpawnHandle h, int visible)
{
	struct ApSpawnEntry *e = AP_SpawnEntry(h);
	if (e == 0)
		return;
	e->visible = visible ? 1 : 0;
	e->dirty = 1;
}

static void AP_SpawnDropEntry(struct ApSpawnEntry *e)
{
	if (e->inst != 0)
	{
		INSTANCE_Death(e->inst);
		e->inst = 0;
	}
	e->used = 0;
	s_spawnCount--;
}

void AP_Spawn_Remove(AP_SpawnHandle h)
{
	struct ApSpawnEntry *e = AP_SpawnEntry(h);
	if (e == 0)
		return;
	AP_SpawnDropEntry(e);
}

void AP_Spawn_RemoveAll(void)
{
	int i;
	for (i = 0; i < AP_SPAWN_MAX; i++)
	{
		if (s_spawns[i].used)
			AP_SpawnDropEntry(&s_spawns[i]);
	}
}

struct Instance *AP_Spawn_Instance(AP_SpawnHandle h)
{
	struct ApSpawnEntry *e = AP_SpawnEntry(h);
	return (e != 0) ? e->inst : 0;
}

int AP_Spawn_Count(void)
{
	return s_spawnCount;
}

int AP_Spawn_Generation(void)
{
	return s_generation;
}

void AP_Spawn_OnPoolReset(void)
{
	int i;

	s_generation++;

	// The pool memory itself has already been re-inited by the caller, so every
	// instance pointer we hold is dangling and INSTANCE_Death on one would
	// corrupt the fresh free list. Drop the pointers without touching them.
	for (i = 0; i < AP_SPAWN_MAX; i++)
	{
		if (!s_spawns[i].used)
			continue;

		s_spawns[i].inst = 0;

		if (s_spawns[i].lifetime == AP_SPAWN_LIFE_PERSISTENT)
		{
			s_spawns[i].dirty = 1; // re-born and re-transformed next frame
		}
		else
		{
			s_spawns[i].used = 0;
			s_spawnCount--;
		}
	}
}

static void AP_SpawnPushTransform(struct ApSpawnEntry *e)
{
	struct Instance *inst = e->inst;

	ConvertRotToMatrix(&inst->matrix, &e->rot);
	inst->matrix.t[0] = e->pos.x;
	inst->matrix.t[1] = e->pos.y;
	inst->matrix.t[2] = e->pos.z;

	inst->scale.x = e->scale;
	inst->scale.y = e->scale;
	inst->scale.z = e->scale;

	inst->colorRGBA = e->colorRGBA;

	if (e->visible)
		inst->flags &= ~HIDE_MODEL;
	else
		inst->flags |= HIDE_MODEL;

	e->dirty = 0;
}

void AP_Spawn_OnFrame(struct GameTracker *gGT)
{
	int i;

	if (gGT == 0)
		return;

	for (i = 0; i < AP_SPAWN_MAX; i++)
	{
		struct ApSpawnEntry *e = &s_spawns[i];

		if (!e->used)
			continue;

		if (e->inst == 0)
		{
			struct Model *m;

			// Bound off the array itself: the id space differs per BUILD
			// (namespace_Main.h:1001-1003) and the AP marker parks at the last
			// slot, so a hard-coded ceiling would be wrong on one of them.
			if (e->modelID < 0 ||
			    e->modelID >= (int)(sizeof(gGT->modelPtr) / sizeof(gGT->modelPtr[0])))
				continue;

			// modelPtr[] is refilled per level from the LEV's model list
			// (LibraryOfModels.c:4-20), so a model that is not in this level is
			// simply NULL and the entry waits rather than failing.
			m = gGT->modelPtr[e->modelID];
			if (m == 0)
				continue;

			// A NULL thread is what the hub's own scan instance is born with
			// (AH_SaveObj.c:293): the thread only matters for models that want
			// per-frame behaviour, and a spawn's behaviour is this loop.
			e->inst = INSTANCE_Birth3D(m, e->name, 0);
			if (e->inst == 0)
				continue; // pool full; INSTANCE_Birth3D already logged it

			e->inst->flags = AP_SPAWN_FLAGS;
			e->dirty = 1;
		}

		if (e->dirty)
			AP_SpawnPushTransform(e);
	}
}

#endif // CTR_AP
