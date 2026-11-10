#ifndef AP_SANITY_COLOUR_H
#define AP_SANITY_COLOUR_H
#include "ap_pad_colour.h"

// 0 subtle (70%), 1 medium (85%, default), 2 strong (100%).
#define AP_SANITY_VISIBILITY 1

static inline int AP_SanityLook(int checked, int logicState)
{
    return checked ? AP_PAD_LOOK_DONE : logicState == 0 ? AP_PAD_LOOK_CLOSED : AP_PAD_LOOK_OPEN;
}

// Native-only primitive metadata in unused TPAGE bits 10..13. Bits 14..15
// belong to sideloaded art. Zero means ordinary rendering. Per-primitive
// metadata survives ordering-table depth sorting without leaking state.
static inline unsigned AP_SanityTag(int look, int variant)
{
    unsigned state = look == AP_PAD_LOOK_DONE ? 2 : look == AP_PAD_LOOK_CLOSED ? 1 : 0;
    return (1 + state * 3 + (unsigned)variant) << 10;
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
