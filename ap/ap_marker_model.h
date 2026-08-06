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
// all. Just short of full keeps the tint unambiguous while letting a little of
// the logo's internal shading survive. One of the values Stef tunes in-game.
#define AP_MARKER_TINT_STRENGTH 0x0e00

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
