#ifndef AP_CRYSTAL_CHECKS_H
#define AP_CRYSTAL_CHECKS_H

#define AP_CRYSTAL_ARENAS 4
#define AP_CRYSTALS_PER_ARENA 20
#define AP_CRYSTAL_PSEUDO_BASE 52000

typedef struct AP_CrystalChecks
{
    int state; /* 0 absent/off, 1 valid, -1 refused */
    int present[AP_CRYSTAL_ARENAS];
    long locations[AP_CRYSTAL_ARENAS][AP_CRYSTALS_PER_ARENA];
} AP_CrystalChecks;

static inline int AP_CrystalArenaIndex(int level)
{
    static const int levels[4] = {21, 19, 23, 18};
    int a;
    for (a=0; a<4; ++a) if (levels[a] == level) return a;
    return -1;
}

static inline int AP_CrystalInstanceBase(int arena, int players)
{
    static const int bases[4][4] = {{8,8,8,0},{28,28,28,19},
                                  {25,25,25,25},{45,45,45,41}};
    return arena >= 0 && arena < 4 && players >= 1 && players <= 4
               ? bases[arena][players-1] : -1;
}

static inline long AP_CrystalPickupCode(const AP_CrystalChecks *checks,
                                      int level, int players, int instanceIndex)
{
    int arena = AP_CrystalArenaIndex(level);
    int base = AP_CrystalInstanceBase(arena, players);
    if (!checks || checks->state != 1 || arena < 0 || base < 0 ||
        !checks->present[arena] || instanceIndex < base || instanceIndex >= base+20) return -1;
    return checks->locations[arena][instanceIndex-base];
}

static inline long AP_CrystalPseudoCode(const AP_CrystalChecks *checks, int bit)
{
    int off;
    if (bit < AP_CRYSTAL_PSEUDO_BASE || bit >= AP_CRYSTAL_PSEUDO_BASE+80) return -1;
    off = bit - AP_CRYSTAL_PSEUDO_BASE;
    if (!checks || checks->state != 1 ||
        !checks->present[off/20]) return -1;
    return checks->locations[off/20][off%20];
}
#endif
