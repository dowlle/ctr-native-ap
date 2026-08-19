#include <stdio.h>
#include <string.h>

#include <common.h>

#include "../ap/ap_surface_logic.h"

struct sData sdata_static;

struct TerrainCase
{
	int terrain;
	int item;
	const char *name;
};

static const struct TerrainCase cases[] = {
	{TERRAIN_GRASS, AP_SURFACE_GRASS, "grass"},
	{TERRAIN_SLOWGRASS, AP_SURFACE_GRASS, "slow grass"},
	{TERRAIN_DIRT, AP_SURFACE_DIRT, "dirt"},
	{TERRAIN_SLOWDIRT, AP_SURFACE_DIRT, "slow dirt"},
	{TERRAIN_MUD, AP_SURFACE_DIRT, "mud"},
	{TERRAIN_SNOW, AP_SURFACE_SNOW, "snow"},
	{TERRAIN_WATER, AP_SURFACE_WATER, "water"},
	{TERRAIN_ICE, AP_SURFACE_ICE, "ice"},
	{TERRAIN_ICY_ROAD, AP_SURFACE_ICE, "icy road"},
};

static int checks;
static int failures;

static void expect(int condition, const char *name)
{
	checks++;
	if (!condition)
	{
		failures++;
		printf("FAIL %s\n", name);
	}
}

int main(void)
{
	int held[AP_SURFACE_ITEM_COUNT];
	int i;
	int item;
	char label[96];

	for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++)
	{
		snprintf(label, sizeof label, "%s maps to its ruled item", cases[i].name);
		expect(AP_SurfaceItemForTerrain(cases[i].terrain) == cases[i].item,
		       label);

		for (item = 0; item < AP_SURFACE_ITEM_COUNT; item++)
		{
			memset(held, 0, sizeof held);
			held[item] = 1;
			snprintf(label, sizeof label, "%s only responds to item %d",
			         cases[i].name, item);
			expect(AP_SurfaceResolveTerrain(cases[i].terrain, 1, 1, held) ==
			           (item == cases[i].item ? TERRAIN_ASPHALT : cases[i].terrain),
			       label);
		}

		memset(held, 1, sizeof held);
		snprintf(label, sizeof label, "%s forced effect survives held items",
		         cases[i].name);
		expect(AP_SurfaceResolveTerrain(cases[i].terrain, 0, 1, held) ==
		           cases[i].terrain,
		       label);
		snprintf(label, sizeof label, "%s does not alter a remote driver",
		         cases[i].name);
		expect(AP_SurfaceResolveTerrain(cases[i].terrain, 1, 0, held) ==
		           cases[i].terrain,
		       label);
	}

	memset(held, 1, sizeof held);
	expect(AP_SurfaceItemForTerrain(TERRAIN_ASPHALT) == -1,
	       "asphalt has no comfort item");
	expect(AP_SurfaceResolveTerrain(TERRAIN_ASPHALT, 1, 1, held) ==
	           TERRAIN_ASPHALT,
	       "asphalt remains asphalt");
	expect(AP_SurfaceResolveTerrain(TERRAIN_ICE, 1, 1, NULL) == TERRAIN_ICE,
	       "missing held-state input is conservative");

	printf("%s surface-item matrix (%d checks, %d failures)\n",
	       failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
