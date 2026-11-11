#ifndef AP_SANITY_COLOUR_H
#define AP_SANITY_COLOUR_H
#include "ap_pad_colour.h"
#include "../include/platform/native_sanity_recolour.h"

struct Instance;
unsigned AP_SanityRecolourTag(struct Instance *inst);

// 0 subtle (70%), 1 medium (85%, default), 2 strong (100%).
#define AP_SANITY_VISIBILITY 1

static inline int AP_SanityLook(int checked, int logicState)
{
    return checked ? AP_PAD_LOOK_DONE : logicState == 0 ? AP_PAD_LOOK_CLOSED : AP_PAD_LOOK_OPEN;
}

// Marker-gated native metadata survives depth sorting without treating
// ordinary TPAGE flags as check state. Zero means ordinary rendering.
static inline unsigned AP_SanityTag(int look, int variant)
{
    unsigned state = look == AP_PAD_LOOK_DONE ? 2 : look == AP_PAD_LOOK_CLOSED ? 1 : 0;
    return NativeSanityEncodeTag(1 + state * 3 + (unsigned)variant);
}

// Untextured pickups (notably loose Wumpa) have no TPAGE word. Apply the
// same luminance recolour directly to their lit Gouraud vertex colours.
static inline unsigned AP_SanityRecolourRGB(unsigned colour, unsigned tag)
{
    unsigned result = colour & 0xff000000u;
    unsigned palette, light, strength, state, r, g, b;
    if (tag < 1 || tag > 9) return colour;
    state = (tag - 1) / 3;
    palette = state == 2 ? 0xa4a4a4u : AP_PadLookRGB(state == 1 ? AP_PAD_LOOK_CLOSED : AP_PAD_LOOK_OPEN);
    strength = 70 + 15 * ((tag - 1) % 3);
    r = colour & 255; g = (colour >> 8) & 255; b = (colour >> 16) & 255;
    light = (299 * r + 587 * g + 114 * b) / 1000;
    for (unsigned channel = 0; channel < 3; ++channel) {
        unsigned original = (colour >> (channel * 8)) & 255;
        unsigned target = (palette >> ((2 - channel) * 8)) & 255;
        unsigned mixed = (original * (100 - strength) + light * target * strength / 164) / 100;
        result |= (mixed > 255 ? 255 : mixed) << (channel * 8);
    }
    return result;
}

// Replace chroma after texture sampling and vertex lighting, before dither.
// Preserve luminance detail, including the question mark and wooden edges.
// Texture/CLUT bytes and shared model data are never changed.
#define AP_SANITY_RECOLOUR_GLSL \
    "vec3 sanityRecolour(vec3 rgb, float tag) {\n" \
    "  if (tag < 0.5 || tag > 9.5) return rgb;\n" \
    "  float state = floor((tag - 1.0) / 3.0);\n" \
    "  float strength = 0.70 + 0.15 * mod(tag - 1.0, 3.0);\n" \
    "  vec3 palette = state < 0.5 ? vec3(112.0,232.0,96.0) : state < 1.5 ? vec3(184.0,86.0,72.0) : vec3(164.0);\n" \
    "  float light = dot(rgb, vec3(0.299,0.587,0.114));\n" \
    "  return mix(rgb, light * palette / 164.0, strength);\n" \
    "}\n"
#endif
