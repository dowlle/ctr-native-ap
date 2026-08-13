#ifndef AP_CRYSTAL_MODEL_H
#define AP_CRYSTAL_MODEL_H

// The stand-in crystal: what a CTR progression reward shows on a surface where
// the retail battle-arena crystal is not loaded (#219, and the 2026-08-12
// in-game gate that this fixes).
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

struct GameTracker;
struct Model;

// How hard the crystal purple is pushed onto the stand-in's neutral greys, as
// the GTE's IR0 interpolation factor -- the same knob and the same units as
// AP_MARKER_TINT_STRENGTH, which see for what the number means.
//
// Lower than the marker's 0x0c00 on purpose. The marker's job is to read as a
// CLASS COLOUR, so it wants the hue to dominate; a crystal's job is to read as a
// faceted gem, so it wants its facet contrast to survive. At 0x0a00 the shaded
// LUT keeps three eighths of the result, which is what makes a spinning gem
// catch light instead of turning into a flat purple blob. One of the values Stef
// tunes in game.
#define AP_CRYSTAL_TINT_STRENGTH 0x0a00

// Park the stand-in at gGT->modelPtr[STATIC_CRYSTAL] IF AND ONLY IF that slot is
// empty. Idempotent and cheap, and meant to be called every frame: the slot is
// inside LibraryOfModels_Clear's range (i < 0xe2, LibraryOfModels.c:26), so every
// level load wipes it and the next frame re-parks it.
//
// The emptiness test is the whole design. Where the level really does load the
// retail crystal, that model keeps the slot and the pad draws the genuine arena
// crystal, specular and all; where it does not, the stand-in fills in. Neither
// case asks which model pack the surface runs on, which is the question the
// previous attempt answered wrongly.
void AP_CrystalModel_Register(struct GameTracker *gGT);

// 1 once the stand-in has been built. Note this says nothing about whether it is
// currently PARKED -- a level that loads the real crystal keeps its own.
int AP_CrystalModel_IsBuilt(void);

// 1 when `model` is this project's stand-in rather than a retail crystal.
//
// The render site needs this and cannot infer it from the model id, because both
// models answer STATIC_CRYSTAL. The stand-in is untextured, so it must stay on
// the plain prim path: the writers selected by DRAW_TRANSPARENT and
// USE_SPECULAR_LIGHT both bail at tex == 0 and draw NOTHING
// (RenderBucket_QueueExecute.c:2924, :3063). Giving the stand-in the arena
// crystal's specular treatment would make it invisible.
int AP_CrystalModel_IsStandIn(const struct Model *model);

#endif // CTR_AP
#endif // AP_CRYSTAL_MODEL_H
