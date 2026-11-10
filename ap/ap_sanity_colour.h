#ifndef AP_SANITY_COLOUR_H
#define AP_SANITY_COLOUR_H

#include "ap_pad_colour.h"

// Picture candidates: 0 subtle, 1 medium (default), 2 strong. One choice
// controls both the original model's blend and its soft additive halo.
#define AP_SANITY_VISIBILITY 1

static inline int AP_SanityLook(int checked, int logicState)
{
    // Unknown exported logic keeps the same fallback as the pads.
    return checked ? AP_PAD_LOOK_DONE : logicState == 0 ? AP_PAD_LOOK_CLOSED : AP_PAD_LOOK_OPEN;
}

static inline unsigned AP_SanityTint(int look)
{
    // Instance far colour is packed 0x0RRGGBB0, not GPU 0x00BBGGRR.
    return AP_PadLookRGB(look) << 4;
}

static inline int AP_SanityBlend(int variant)
{
    return variant == 0 ? 0x600 : variant == 2 ? 0xB00 : 0x880;
}

static inline int AP_SanityHaloBudget(int freeBytes)
{
    // Keep at least three quarters of the remaining arena for the world.
    int count = freeBytes > 0 ? freeBytes / (4 * 512) : 0;
    return count < 32 ? count : 32;
}

static inline unsigned AP_SanityHaloStrength(int variant)
{
    return variant == 0 ? 48 : variant == 2 ? 112 : 80;
}

static inline unsigned AP_SanityHaloColour(int look, int variant)
{
    unsigned rgb = AP_PadLookRGB(look);
    unsigned strength = AP_SanityHaloStrength(variant);
    if (look == AP_PAD_LOOK_DONE) strength /= 2;
    return ((AP_PadLookB(rgb) * strength / 255) << 16) |
           ((AP_PadLookG(rgb) * strength / 255) << 8) |
           (AP_PadLookR(rgb) * strength / 255);
}

static inline unsigned AP_SanityHaloFilterColour(int look, int variant)
{
    // The same shaped subtraction as the pad beam keeps green from reading
    // cyan over blue road/sky. Done has no filter, like the pad's grey beam.
    unsigned rgb = AP_PadLookBeamFilter(look);
    unsigned strength = AP_SanityHaloStrength(variant);
    return ((AP_PadLookB(rgb) * strength / 255) << 16) |
           ((AP_PadLookG(rgb) * strength / 255) << 8) |
           (AP_PadLookR(rgb) * strength / 255);
}
#endif
