#ifndef AP_SPAWN_H
#define AP_SPAWN_H

// Additive model loader: put an extra model into a level at runtime, at
// arbitrary world coordinates, without touching the baked level assets (#109
// groundwork, shared with the #124 item display).
//
// Why this is possible without LEV surgery: the engine draws instances from two
// separate lists every frame (MainFrame_RenderFrame.c:695-698). LEV instances
// come from the level's InstDef array; everything else is walked out of the
// instance JitPool's TAKEN list by RenderBucket_QueueNonLevInstances
// (RenderBucket_QueueExecute.c:2198). INSTANCE_LevInitAll pulls its instances
// off the FREE list directly (INSTANCE.c:253) and never links them into TAKEN,
// so the TAKEN list holds exactly the runtime-born instances -- the hub's
// warp-pad prizes, pause gems and garage tops (AH_WarpPad.c, AH_Pause.c:599,
// AH_Garage.c:489) and, now, ours. Birthing through INSTANCE_Birth3D and
// writing the instance matrix is the whole mechanism.
//
// What this module deliberately does NOT do: collision. A spawn is a visual
// only. Making a spawned model a functional pickup is the unsolved half of #109
// (BSP hitbox injection vs the per-frame proximity-thread pattern the Crystal
// Challenge nitros already use, RB_GenericMine.c) and is out of scope here.
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

#include <ctr_math.h>

struct GameTracker;
struct Instance;
struct Model;

// Opaque handle. >= 0 is a live entry; AP_SPAWN_INVALID is the failure value
// every add path returns when the table is full.
typedef int AP_SpawnHandle;
#define AP_SPAWN_INVALID (-1)

// Table size. The instance JitPool is the real budget (it is sized off the
// render-bucket size, MainInit.c:280-286, and AP already claims +48 on hub
// levels for pads and pause gems); 64 is a table cap that cannot on its own
// exhaust a race level's pool, and a full table degrades to "no spawn" plus a
// log line rather than to a crash.
#define AP_SPAWN_MAX 64

// Lifetime. Every level load (and every race restart) re-inits the instance
// pool, so a spawned instance does not survive one -- the memory is handed back
// to the free list underneath us.
//
//   AP_SPAWN_LIFE_LEVEL      the entry is dropped when the pool resets. Use for
//                            anything tied to the level currently loaded.
//   AP_SPAWN_LIFE_PERSISTENT the entry survives the reset as a request and is
//                            re-born on the next frame that has its model.
//                            Callers that only want it in one level must still
//                            do their own level filtering.
enum
{
	AP_SPAWN_LIFE_LEVEL = 0,
	AP_SPAWN_LIFE_PERSISTENT = 1
};

// Add a spawn request. pos is in world units -- the same space as
// Instance.matrix.t and Driver.posCurr (INSTANCE.c:315 copies a LEV InstDef's
// s16 position straight into the instance matrix, so LEV coordinates, driver
// coordinates and these are one space). rot may be NULL for no rotation; its
// components are engine angles (0x1000 = a full turn, RB_Crate.c:124).
//
// The model does not have to be loaded yet: the request is held and the
// instance is born on the first AP_Spawn_OnFrame that finds modelID in
// gGT->modelPtr[]. That is what makes this safe to call before a level's models
// are up.
//
// name must be at least 16 BYTES readable, not merely NUL-terminated:
// INSTANCE_Birth copies a fixed 15 characters out of it (INSTANCE.c:24-27), so a
// short string literal would be read past its end. Pass a char[16]; the engine's
// own call sites pass fields of a resident struct for the same reason.
AP_SpawnHandle AP_Spawn_Add(int modelID, const Vec3 *pos, const SVec3 *rot, int lifetime, const char *name);

// Add a spawn backed by an AP-owned model pointer instead of a modelPtr[] id.
// The model must have static lifetime. This keeps an AP visual distinct from a
// resident retail model without replacing the retail model's global id slot.
AP_SpawnHandle AP_Spawn_AddModel(struct Model *model, const Vec3 *pos, const SVec3 *rot,
                                int lifetime, const char *name);

// Move / re-orient / resize / recolour a live entry. All are cheap: they mark
// the entry dirty and the next AP_Spawn_OnFrame rewrites the instance.
void AP_Spawn_SetPos(AP_SpawnHandle h, const Vec3 *pos);
void AP_Spawn_SetRot(AP_SpawnHandle h, const SVec3 *rot);
void AP_Spawn_SetScale(AP_SpawnHandle h, s16 scale);
void AP_Spawn_SetColour(AP_SpawnHandle h, u32 colorRGBA);
void AP_Spawn_SetVisible(AP_SpawnHandle h, int visible);

// Drop one entry / every entry. Safe on a stale handle and safe after a pool
// reset (the instance pointer is dropped, never freed twice).
void AP_Spawn_Remove(AP_SpawnHandle h);
void AP_Spawn_RemoveAll(void);

// Live instance for a handle, or NULL while the entry is waiting for its model
// (or between a pool reset and the next frame). For callers that need to reach
// past this module; nothing here requires it.
struct Instance *AP_Spawn_Instance(AP_SpawnHandle h);

// How many entries the table currently holds (born or waiting).
int AP_Spawn_Count(void);

// Bumped by every pool reset. A caller that holds handles across frames compares
// this to the value it saw when it took them: if it moved, every handle it holds
// is void and must be dropped WITHOUT calling AP_Spawn_Remove, because a slot
// freed by the reset may already belong to someone else. The alternative --
// inferring a reset from a levelID change -- misses a race restart, which
// re-inits the pool on the same level.
int AP_Spawn_Generation(void);

// Called from MainInit_JitPools (MAIN/MainInit.c) at the point the instance
// pool has just been re-inited and every previously handed-out instance pointer
// is dangling. Drops the pointers, drops AP_SPAWN_LIFE_LEVEL entries, and
// leaves persistent entries queued for re-birth.
void AP_Spawn_OnPoolReset(void);

// Per-frame: births whatever is waiting and pushes pending transform edits into
// the live instances. Called from AP_OnFrame (ap_hooks.c).
void AP_Spawn_OnFrame(struct GameTracker *gGT);

#endif // CTR_AP
#endif // AP_SPAWN_H
