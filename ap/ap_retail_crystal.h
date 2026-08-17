#ifndef AP_RETAIL_CRYSTAL_H
#define AP_RETAIL_CRYSTAL_H

#ifdef CTR_AP

// Supply the retail crystal (STATIC_CRYSTAL) where the engine never loads it.
//
// #219 renders every CTR progression item as the purple crystal, but the model
// is not resident on the surface that needs it: the adventure hub loads
// BI_ADVENTUREPACK, which carries gem1/relic/key/trophylow/token and NO crystal.
// `crystal` (0x60) lives in BI_1PARCADEPACK. So a naive modelPtr[STATIC_CRYSTAL]
// read in the hub returns NULL, which is exactly how the two earlier attempts at
// this feature failed (Lessons Learned §24 and §29).
//
// This harvests the real model out of the player's own game data through the
// #256 reader and parks it wherever the level left the slot empty, so residency
// is a CONSTRUCTION rather than a premise -- including on surfaces nobody has
// enumerated.
//
// Safe to call every frame: the harvest runs at most once and never retries
// after a failure, and afterwards this only reasserts the model slot, which
// LibraryOfModels_Clear wipes on every level transition. It never displaces a
// retail crystal the level did load.
void AP_RetailCrystal_Register(struct GameTracker *gGT);

// 1 once the harvested crystal is built. The display resolver asks this before
// routing anything to STATIC_CRYSTAL: a category that cannot be drawn must fall
// back to the Archipelago-logo marker rather than leave a slot showing whatever
// placeholder it happened to be born with (Lessons Learned §24). Mirrors
// AP_MarkerModel_IsRegistered, which the same resolver already consults.
int AP_RetailCrystal_IsRegistered(void);

#endif // CTR_AP
#endif // AP_RETAIL_CRYSTAL_H
