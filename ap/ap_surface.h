#ifndef AP_SURFACE_H
#define AP_SURFACE_H

#ifdef CTR_AP

struct Driver;

// Stable apworld item indices. Ignore Sand (26) is a reserved datapackage name
// and deliberately has no runtime item until track capture proves it distinct.
#define AP_SURFACE_ITEM_FIRST_INDEX 21
#define AP_SURFACE_ITEM_COUNT 5

enum AP_SurfaceItem
{
	AP_SURFACE_GRASS = 0,
	AP_SURFACE_DIRT,
	AP_SURFACE_SNOW,
	AP_SURFACE_WATER,
	AP_SURFACE_ICE
};

// Rebuilt from the server's authoritative ReceivedItems replay every connect.
void AP_SurfaceReset(void);
void AP_SurfaceReceive(int item);

// Return the terrain whose metadata should drive this local player. Natural
// covered surfaces become asphalt while held. Forced ice from a weapon or cheat
// must pass naturalTerrain=0 and remains untouched.
int AP_SurfaceTerrain(struct Driver *driver, int terrain, int naturalTerrain);

#endif // CTR_AP
#endif // AP_SURFACE_H
