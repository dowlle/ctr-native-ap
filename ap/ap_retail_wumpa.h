#ifndef AP_RETAIL_WUMPA_H
#define AP_RETAIL_WUMPA_H

#ifdef CTR_AP

struct GameTracker;

// Supply the retail Wumpa Fruit model (#222) on surfaces the engine never loads
// it on.
//
// The 2026-09-20 ruling gives every Wumpa package the retail fruit model, but
// PU_WUMPA_FRUIT (0x02) ships only inside loose-fruit level files, never in any
// MPK pack, so gGT->modelPtr[0x02] is null in the hub and on every surface that
// does not itself carry a fruit-bearing track. A naive model swap there is the
// Lessons Learned §24 failure: the pad silently keeps the placeholder it was
// born with because the model pointer it was handed was null.
//
// This harvests the fruit out of the player's own game data through the #256
// reader and parks it wherever the level left the slot empty. It is a compact
// harvest: the 12,760-byte self-contained span (model struct, four headers,
// command lists, colour tables, pointer arrays, four 32-frame spin animations)
// is copied into an AP-owned static buffer and its 21 internal pointers are
// relocated, so residency is a CONSTRUCTION rather than a premise. The fruit is
// untextured, so no VRAM page, CLUT or atlas work is involved.
//
// Safe to call every frame: the harvest runs at most once and never retries
// after a failure, and afterwards this only reasserts the model slot, which
// LibraryOfModels_Clear wipes on every level transition. It never displaces a
// fruit model the level itself loaded.
void AP_RetailWumpa_Register(struct GameTracker *gGT);

// 1 once the harvested fruit model is built and stored. Mirrors
// AP_RetailCrystal_IsRegistered.
int AP_RetailWumpa_IsReady(void);

// 1 when a Wumpa package may resolve to model 0x02 on this frame: either the
// client's own harvest is ready, or the current level already carries a retail
// fruit model at slot 0x02. The display resolver reads this exactly as it reads
// the crystal's registration flag, so an unavailable fruit resolves to the AP
// marker rather than leaving a stale placeholder in the slot.
int AP_RetailWumpa_IsDrawable(struct GameTracker *gGT);

#endif // CTR_AP
#endif // AP_RETAIL_WUMPA_H
