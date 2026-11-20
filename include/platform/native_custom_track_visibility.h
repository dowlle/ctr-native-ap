#ifndef NATIVE_CUSTOM_TRACK_VISIBILITY_H
#define NATIVE_CUSTOM_TRACK_VISIBILITY_H

#include <string.h>

/* Unused levels left in the Lockbox and 30th Anniversary patches.
   This is a discovery rule. Room lookup still uses its own file pins. */
static const char *const kCustomTrackHiddenLevs[] = {
    "268064f31236405f384778077e544c69079fb39587ebea60368102fa99ce945a",
    "e9c3b0bb1793809a0bf7e396897ddb54058c94e8dc796b3f587d0921d543857c",
    "b1754b2e860f94938de583be6be6e0b66976f843f1aa8805d8dc6c67fcad270d",
    "20576dcd3f777402c3c39e7532ccbd71b2a2f3368d813bfcbb479cd1300810f3",
    "87f416ec29a93f16013f7497de35c0832c13712aaf94695b6597d309e37edf80",
};

static inline int CustomTrack_IsHiddenLev(const char *hash)
{
    for (size_t i = 0; i < sizeof kCustomTrackHiddenLevs / sizeof kCustomTrackHiddenLevs[0]; ++i)
        if (strcmp(hash, kCustomTrackHiddenLevs[i]) == 0) return 1;
    return 0;
}

#endif
