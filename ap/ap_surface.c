#ifdef CTR_AP

#include <common.h>

#include "ap_surface.h"
#include "ap_surface_logic.h"

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

int AP_SurfaceTerrain(struct Driver *driver, int terrain, int naturalTerrain)
{
	int localDriver = driver != 0 && sdata != 0 && sdata->gGT != 0 &&
	                  driver == sdata->gGT->drivers[0];
	return AP_SurfaceResolveTerrain(terrain, naturalTerrain, localDriver,
	                                g_surface_held);
}

#endif // CTR_AP
