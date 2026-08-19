#ifndef AP_RETAIL_CRATE_H
#define AP_RETAIL_CRATE_H

#ifdef CTR_AP

// Supply the retail weapon crate's face texture where the engine never loads
// it. Geometry is deliberately NOT exported: a Model fixed up for another LEV
// has proved unsafe and can register while remaining invisible.
//
// The relic variant of every track's level file omits `crate_question`
// (PU_RANDOM_CRATE), so AP boxes cannot be drawn in any relic race. This
// harvests the texture out of a 1p level file in the player's own game data and
// publishes it through the AP sideload texture slot rather than emulated VRAM.
//
// Returns non-zero when outFace contains an AP-owned value copy of the retail
// crate's largest face layout and the sideload atlas is ready. Returns zero
// while unavailable or after failure. A failure here must never mean "no box".
//
// Safe to call every frame: the harvest runs at most once, and afterwards this
// only copies twelve bytes into the caller's AP-owned layout.
int AP_RetailCrate_EnsureTexture(struct TextureLayout *outFace);

#endif // CTR_AP
#endif // AP_RETAIL_CRATE_H
