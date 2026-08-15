#ifndef AP_MARKER_MODEL_H
#define AP_MARKER_MODEL_H

// The Archipelago-logo marker model: a foreign multiworld item's stand-in on a
// warp pad, replacing the white gem that carried no information (#124).
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

struct GameTracker;

// Model id for the marker. 0xE2 is the one free slot in gGT->modelPtr[]:
//   - the array is sized 0xe3 on UsaRetail (namespace_Main.h:1003),
//   - the vanilla MODEL_ID enum stops at STATIC_GNORMALZ = 0xE1, with
//     NUM_TYPES = 0xE2 as the one-past-the-end marker
//     (namespace_Instance.h:285-286),
//   - and LibraryOfModels_Clear only zeroes i < 0xe2 (LibraryOfModels.c:26),
//     so a pointer parked at 0xE2 survives every hub/level reload.
// Defined here rather than in the vanilla enum so the retail id space is
// untouched in a non-AP build.
#define STATIC_AP 0xE2

// How hard the per-class tint is pushed onto the marker, as the GTE's IR0
// interpolation factor: 0x1000 is a full lerp to the class colour (flat, no
// shading left), 0 leaves the model's own neutral greys and no classification at
// all. The final vertex colour is grey + (tint - grey) * IR0/0x1000, so the
// model's shading only survives in the (1 - IR0) share.
//
// 0x0e00 shipped in the first prototype and read FLAT in game (2026-08-06 rc3
// playtest): it left the greys just 1/8 of the result, about 20 levels of
// variation. 0x0c00 gives them a quarter, so the shaded LUT's 20..208 spread
// lands as ~47 levels of visible gradient while the class hue still supplies
// three quarters of the colour. Lower it further for more modelling and less
// classification; raise it back toward 0x1000 for flat, maximally legible class
// colours. One of the values tuned in-game.
#define AP_MARKER_TINT_STRENGTH 0x0c00

// Park the marker model at gGT->modelPtr[STATIC_AP]. Idempotent and cheap (one
// compare + one store), so it is safe to call every frame; that also re-asserts
// the slot after a savestate load, whose pointer relocation does not rewrite
// image addresses.
void AP_MarkerModel_Register(struct GameTracker *gGT);

// 1 once the marker model has been built and parked. The classified display is
// gated on this as well as on the network state, so the model choice and the
// class tint can never disagree about which presentation is in play.
int AP_MarkerModel_IsRegistered(void);

#endif // CTR_AP
#endif // AP_MARKER_MODEL_H
