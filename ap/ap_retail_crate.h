#ifndef AP_RETAIL_CRATE_H
#define AP_RETAIL_CRATE_H

#ifdef CTR_AP

// Supply the retail weapon crate where the engine never loads it.
//
// The relic variant of every track's level file omits `crate_question`
// (PU_RANDOM_CRATE), so AP boxes cannot be drawn in any relic race. This
// harvests the model and its texture out of a 1p level file in the player's own
// game data and installs it, sampling through the AP sideload texture slot
// rather than emulated VRAM.
//
// Returns non-zero when the retail crate is installed in gGT->modelPtr. Returns
// zero when it is not available, in which case the caller should keep the
// AP-owned fallback cube -- a failure here must never mean "no box at all".
//
// Safe to call every frame: the harvest runs at most once, and afterwards this
// only reasserts the model slot, which LibraryOfModels_Clear wipes on every
// level transition. Does nothing while a load is in flight, because the read
// borrows the engine's single global load slot.
int AP_RetailCrate_Ensure(struct GameTracker *gGT);

#endif // CTR_AP
#endif // AP_RETAIL_CRATE_H
