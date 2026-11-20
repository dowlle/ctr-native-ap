#ifdef CTR_AP

#include <common.h>

#include "ap_surface.h"
#include "ap_surface_logic.h"
#include "ap_capability.h"
#include "ap_navrec.h"
#include "ap_traps.h" // AP_TrapMudSeason

static int g_surface_held[AP_SURFACE_ITEM_COUNT];
static unsigned int g_surface_new_for_lap;

void AP_SurfaceResetLapObservation(void)
{
	g_surface_new_for_lap = 0;
}

void AP_SurfaceReset(void)
{
	int i;
	for (i = 0; i < AP_SURFACE_ITEM_COUNT; i++)
		g_surface_held[i] = 0;
	AP_SurfaceResetLapObservation();
}

void AP_SurfaceReceive(int item)
{
	if (item < 0 || item >= AP_SURFACE_ITEM_COUNT || g_surface_held[item])
		return;
	g_surface_held[item] = 1;
	if (sdata != NULL && sdata->gGT != NULL && sdata->Loading.stage == LOAD_IDLE &&
	    sdata->gGT->trafficLightsTimer <= 0 && AP_CapabilityRoomStatsRace())
		g_surface_new_for_lap |= 1u << item;
}

int AP_SurfaceTerrain(struct Driver *driver, int terrain, int naturalTerrain)
{
	int localDriver = driver != 0 && sdata != 0 && sdata->gGT != 0 &&
	                  driver == sdata->gGT->drivers[0] && AP_CapabilityRoomStatsRace();
	int mud = localDriver && AP_TrapMudSeason(driver);
	if (g_surface_new_for_lap && localDriver && naturalTerrain)
	{
		int item = AP_SurfaceItemForTerrain(mud ? TERRAIN_DIRT : terrain);
		if (item >= 0 && (g_surface_new_for_lap & (1u << item)))
		{
			g_surface_new_for_lap &= ~(1u << item);
			AP_NavRec_NotePhysicsEffect();
		}
	}
	return AP_SurfaceResolveTerrainMud(terrain, naturalTerrain, localDriver, g_surface_held, mud);
}

int AP_SurfaceHeld(int item)
{
	return item >= 0 && item < AP_SURFACE_ITEM_COUNT && g_surface_held[item];
}

#endif // CTR_AP
