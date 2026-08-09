#ifdef CTR_AP

#include <common.h>

#include "ap_surface.h"

static int g_surface_held[AP_SURFACE_ITEM_COUNT];

void AP_SurfaceReset(void)
{
	int i;
	for (i = 0; i < AP_SURFACE_ITEM_COUNT; i++)
		g_surface_held[i] = 0;
}

void AP_SurfaceReceive(int item)
{
	if (item >= 0 && item < AP_SURFACE_ITEM_COUNT)
		g_surface_held[item] = 1;
}

static int AP_SurfaceItemForTerrain(int terrain)
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

int AP_SurfaceTerrain(struct Driver *driver, int terrain, int naturalTerrain)
{
	int item;

	if (!naturalTerrain || driver == 0 || sdata == 0 || sdata->gGT == 0 ||
	    driver != sdata->gGT->drivers[0])
		return terrain;

	item = AP_SurfaceItemForTerrain(terrain);
	if (item >= 0 && g_surface_held[item])
		return TERRAIN_ASPHALT;
	return terrain;
}

#endif // CTR_AP
