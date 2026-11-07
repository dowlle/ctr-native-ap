#ifndef AP_SURFACE_LOGIC_H
#define AP_SURFACE_LOGIC_H

#include "ap_surface.h"

// Shared terrain-family mapping for the runtime hook and focused acceptance
// harness. Sand deliberately has no row because the engine exposes no distinct
// sand terrain type.
static inline int AP_SurfaceItemForTerrain(int terrain)
{
	switch (terrain)
	{
	case TERRAIN_GRASS:
	case TERRAIN_SLOWGRASS:
		return AP_SURFACE_GRASS;
	case TERRAIN_DIRT:
	case TERRAIN_SLOWDIRT:
	case TERRAIN_MUD:
		return AP_SURFACE_DIRT;
	case TERRAIN_SNOW:
		return AP_SURFACE_SNOW;
	case TERRAIN_WATER:
		return AP_SURFACE_WATER;
	case TERRAIN_ICE:
	case TERRAIN_ICY_ROAD:
		return AP_SURFACE_ICE;
	default:
		return -1;
	}
}

static inline int AP_SurfaceResolveTerrain(int terrain, int naturalTerrain,
	                                        int localDriver,
	                                        const int held[AP_SURFACE_ITEM_COUNT])
{
	int item;

	// Weapon, cheat and Icy Road trap effects pass naturalTerrain=0. They remain
	// effective even while Ignore Ice is held, as ruled for #15.
	if (!naturalTerrain || !localDriver || held == NULL)
		return terrain;

	item = AP_SurfaceItemForTerrain(terrain);
	if (item >= 0 && held[item])
		return TERRAIN_ASPHALT;
	return terrain;
}

// Mud Season (ruled 2026-11-06): every natural surface is dirt for the local
// kart, before the held items are applied, so a held Ignore Dirt turns it into
// asphalt and the trap changes nothing (decision D6 A). Weapon, cheat and Icy
// Road ice (naturalTerrain 0) stay as they are.
static inline int AP_SurfaceResolveTerrainMud(int terrain, int naturalTerrain,
	                                           int localDriver,
	                                           const int held[AP_SURFACE_ITEM_COUNT],
	                                           int mudSeason)
{
	if (mudSeason && naturalTerrain && localDriver)
		terrain = TERRAIN_DIRT;
	return AP_SurfaceResolveTerrain(terrain, naturalTerrain, localDriver, held);
}

#endif
