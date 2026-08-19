#ifndef AP_BOX_MODEL_H
#define AP_BOX_MODEL_H

#ifdef CTR_AP

struct GameTracker;

// Install the AP-owned crate model only when this level has no retail weapon
// crate. Returns PU_RANDOM_CRATE when either the retail or fallback model is
// available, and -1 when registration could not produce a usable model.
int AP_BoxModel_Ensure(struct GameTracker *gGT);

// Relic levels contain no resident weapon crate. Install the validated,
// engine-independent AP cube directly instead of depending on a harvested
// retail pointer graph becoming drawable in a different level file.
int AP_BoxModel_EnsureOwned(struct GameTracker *gGT);
int AP_BoxModel_EnsureRelic(struct GameTracker *gGT);

#endif
#endif
