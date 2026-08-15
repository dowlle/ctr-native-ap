#ifndef AP_BOX_MODEL_H
#define AP_BOX_MODEL_H

#ifdef CTR_AP

struct GameTracker;

// Install the AP-owned crate model only when this level has no retail weapon
// crate. Returns PU_RANDOM_CRATE when either the retail or fallback model is
// available, and -1 when registration could not produce a usable model.
int AP_BoxModel_Ensure(struct GameTracker *gGT);

#endif
#endif
