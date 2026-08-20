#ifndef AP_BOX_TEXTURE_H
#define AP_BOX_TEXTURE_H

#ifdef CTR_AP

// Supply the AP box's face texture from art compiled into this build.
//
// The AP build ships its own box art rather than harvesting the retail weapon
// crate out of the player's game data. The harvest existed because the relic
// variant of every track's level file omits `crate_question` (PU_RANDOM_CRATE)
// and its texels, so AP boxes could not be drawn in a relic race at all; a
// static atlas answers that for every race mode without reading a single byte
// of level data, and without a PNG decoder in the engine.
//
// Returns non-zero when outFace holds an AP-owned layout addressing the face
// rect of the sideload atlas and the atlas is uploaded. Returns zero while
// unavailable or after failure. A failure here must never mean "no box": the
// caller keeps the untextured fallback cube.
//
// Safe to call every frame: the upload runs at most once, and afterwards this
// only copies twelve bytes into the caller's AP-owned layout.
int AP_BoxTexture_EnsureFace(struct TextureLayout *outFace);

#endif // CTR_AP
#endif // AP_BOX_TEXTURE_H
