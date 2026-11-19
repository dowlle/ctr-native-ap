#ifndef AP_BOX_TEXTURE_H
#define AP_BOX_TEXTURE_H

#if defined(CTR_AP) || defined(CTR_RECORDED_RACE)

// The AP box's texture atlas: wood border and crate face built from the
// player's own disc, recoloured, with the Archipelago logo on the face, in the
// default pink and the four Archipelago item colours. Falls back to
// JurnthReinal's compiled art when the disc read fails. See ap_box_texture.c.

// Upload the atlas as the AP sideload texture. Returns non-zero once it is
// uploaded; zero after a failed upload, in which case the caller keeps the
// untextured fallback cube. Safe to call every frame: the upload runs once.
int AP_BoxTexture_Ensure(void);

// Read the wood tile and crate face from the player's disc. Runs once, on the
// first frame with no load in flight; call it every frame. Sticky on failure.
void AP_BoxTexture_Prepare(void);

#endif
#ifdef CTR_RECORDED_RACE
/* Unused sixth slot of the existing 3-by-2 box atlas. */
#define CTR_WEAPON_CRATE_ATLAS_X 160
#define CTR_WEAPON_CRATE_ATLAS_Y 64
#endif
#endif // AP_BOX_TEXTURE_H
