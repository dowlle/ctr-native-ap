#ifndef NATIVE_SANITY_RECOLOUR_H
#define NATIVE_SANITY_RECOLOUR_H

// getTPage and DecalMP legitimately set bit 11 for source Y=0x200.
// Bits 12..13 are the native marker field; bits 14..15 belong to sideloaded
// art. Store the nine logical tags as codes 4..12 so every tagged polygon
// has a marker, while ordinary TPAGE values (including bit 11) decode to 0.
#define NATIVE_SANITY_TPAGE_MASK 0x3c00u
#define NATIVE_SANITY_TPAGE_MARKER 0x3000u

static inline unsigned NativeSanityEncodeTag(unsigned tag)
{
    return tag >= 1 && tag <= 9 ? (tag + 3) << 10 : 0;
}

static inline unsigned NativeSanityDecodeTag(unsigned tpage)
{
    unsigned code = (tpage & NATIVE_SANITY_TPAGE_MASK) >> 10;
    return (tpage & NATIVE_SANITY_TPAGE_MARKER) && code >= 4 && code <= 12 ? code - 3 : 0;
}

static inline unsigned NativeSanityClearTag(unsigned tpage)
{
    return NativeSanityDecodeTag(tpage) ? tpage & ~NATIVE_SANITY_TPAGE_MASK : tpage;
}
#endif
