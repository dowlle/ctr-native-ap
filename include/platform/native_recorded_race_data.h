#ifndef NATIVE_RECORDED_RACE_DATA_H
#define NATIVE_RECORDED_RACE_DATA_H

#include <platform/native_recorded_race.h>
#include "../../ap/ap_navrec_format.h"

struct CustomRecordedRace
{
    struct AP_NavSetInfo set;
    struct AP_NavRecNode nodes[AP_NAVSET_MAX_LAPS][AP_NAVREC_MAX_NODES];
    unsigned int stamps[AP_NAVSET_MAX_LAPS][AP_NAVREC_MAX_NODES];
    struct CustomRacePlacement grid[CTR_RECORDED_GRID_COUNT];
    struct CustomRacePlacement crates[CTR_RECORDED_CRATE_MAX];
    unsigned int crateCount;
};

#endif
