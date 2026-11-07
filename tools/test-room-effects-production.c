// cc -m32 -std=c99 -Wall -Wextra -DCTR_AP -DCTR_NATIVE -DBUILD=926 -ffunction-sections -fdata-sections -Wl,--gc-sections -I ap -I . -I include tools/test-room-effects-production.c -o /tmp/test-room-effects-production
/* Run the actual boost and surface consumers across the race-mode boundary. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <common.h>
#include "../ap/ap_capability.c"
#include "../ap/ap_surface.c"

struct sData sdata_static;
struct Data data;
ctr_seed_config ctr_cfg;
int ctr_cfg_active(void) { return ctr_cfg.schema_version > 0; }
void AP_LogLine(const char *line) { (void)line; }
int AP_TrapWeakenBoostTier(int tier) { return tier; }
int AP_TrapMudSeason(struct Driver *driver) { (void)driver; return 0; }

int main(void)
{
    struct GameTracker gt = {0};
    struct Driver local = {0}, ai = {0};
    const unsigned outside[] = {ARCADE_MODE, TIME_TRIAL, BATTLE_MODE,
        ARCADE_MODE | ADVENTURE_MODE, TIME_TRIAL | RELIC_RACE, 0};
    const unsigned adventure[] = {ADVENTURE_MODE, ADVENTURE_ARENA, RELIC_RACE,
        ADVENTURE_MODE | ADVENTURE_CUP, ADVENTURE_MODE | ADVENTURE_BOSS, CRYSTAL_CHALLENGE};
    sdata->gGT = &gt;
    gt.drivers[0] = &local;
    ctr_cfg.schema_version = 18;
    ctr_cfg.boost_mode = AP_CAP_MODE_SHARED_GLOBAL;
    AP_CapabilityReset();
    AP_CapabilitySelectConnectReset();
    AP_SurfaceReceive(AP_SURFACE_WATER);
    for (unsigned i = 0; i < sizeof outside / sizeof outside[0]; ++i)
    {
        int reserve = 50000, fire = 0x800;
        gt.gameMode1 = outside[i];
        gt.numPlyrCurrGame = outside[i] ? 1 : 2; /* Versus has no dedicated bit. */
        assert(AP_CapabilityBoostTier() == 0); /* Inventory is retained. */
        assert(AP_CapabilityAppliedBoostTier() == -1);
        assert(AP_CapabilityFireGrant(&local, &reserve, 0, &fire));
        assert(reserve == 50000 && fire == 0x800);
        assert(!AP_CapabilityRetroFueled(&local));
        assert(AP_SurfaceTerrain(&local, TERRAIN_WATER, 1) == TERRAIN_WATER);
    }
    for (unsigned i = 0; i < sizeof adventure / sizeof adventure[0]; ++i)
    {
        int reserve = 100, fire = 0x100;
        gt.gameMode1 = adventure[i];
        gt.numPlyrCurrGame = 1;
        assert(AP_CapabilityAppliedBoostTier() == 0);
        assert(!AP_CapabilityFireGrant(&local, &reserve, 0, &fire));
        assert(AP_SurfaceTerrain(&local, TERRAIN_WATER, 1) == TERRAIN_ASPHALT);
        assert(AP_SurfaceTerrain(&ai, TERRAIN_WATER, 1) == TERRAIN_WATER);
        assert(AP_SurfaceTerrain(&local, TERRAIN_WATER, 0) == TERRAIN_WATER);
    }
    /* Leaving Arcade must not lose an upgrade received before it. */
    AP_CapabilityReceive(AP_CAP_CHAIN_BOOST);
    gt.gameMode1 = ARCADE_MODE;
    assert(AP_CapabilityBoostTier() == 1 && AP_CapabilityAppliedBoostTier() == -1);
    gt.gameMode1 = ADVENTURE_MODE;
    assert(AP_CapabilityAppliedBoostTier() == 1);
    puts("PASS: production boost and surface effects stay in Adventure; inventory survives");
    return 0;
}
