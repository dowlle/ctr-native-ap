#ifndef AP_BOX_MODEL_H
#define AP_BOX_MODEL_H

#ifdef CTR_AP

#include <ctr_math.h> // Vec3, the spawn transform's output type

struct GameTracker;
struct Model;

// Install the AP-owned crate model only when this level has no retail weapon
// crate. Returns PU_RANDOM_CRATE when either the retail or fallback model is
// available, and -1 when registration could not produce a usable model.
int AP_BoxModel_Ensure(struct GameTracker *gGT);

// Relic levels contain no resident weapon crate. Install the validated,
// engine-independent AP cube directly instead of depending on a harvested
// retail pointer graph becoming drawable in a different level file.
int AP_BoxModel_EnsureOwned(struct GameTracker *gGT);
int AP_BoxModel_EnsureRelic(struct GameTracker *gGT);

// Return the AP-owned textured box model without occupying PU_RANDOM_CRATE.
// Runtime AP boxes use this on every race type so resident retail weapon crates
// keep their own model while AP locations remain visually distinct.
struct Model *AP_BoxModel_GetOwned(struct GameTracker *gGT);

// How far this model's own origin sits above its lowest face, in world units, at
// the header scale it is currently carrying. 0 when the model cannot be measured
// (which reproduces the pre-correction behaviour: the spawn lands on the authored
// anchor untouched) and 0 for a model whose origin is already at its base.
int AP_BoxModel_BaseOffsetY(struct Model *model);

// THE SHARED AP CRATE SPAWN TRANSFORM. Turns an authored placement anchor into
// the world position the crate instance is actually born at, by lifting the
// model until its lowest face rests on that anchor (see ap_box_offset_logic.h
// for why the anchor is a ground point and the crate origin is a centre).
//
// Runtime boxes (ap_boxes.c) and author-mode preview markers (ap_author.c) both
// go through this one function, which is what makes a box dropped in author mode
// preview the height the runtime box will stand at. X and Z are carried through
// untouched: the authored table stays the semantic anchor and no row moves.
void AP_BoxModel_SpawnPos(struct Model *model, int x, int y, int z, Vec3 *out);

#endif
#endif
