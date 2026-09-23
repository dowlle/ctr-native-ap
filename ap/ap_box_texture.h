#ifndef AP_BOX_TEXTURE_H
#define AP_BOX_TEXTURE_H

#ifdef CTR_AP

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

#endif // CTR_AP
#endif // AP_BOX_TEXTURE_H
