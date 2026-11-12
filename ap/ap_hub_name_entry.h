#ifndef AP_HUB_NAME_ENTRY_H
#define AP_HUB_NAME_ENTRY_H

// A random hub behind adventure name entry (A4-60c). Engine glue in
// ap/ap_hub_name_entry.c, decisions in ap/ap_hub_name_entry_logic.h.

#include "ap_hub_name_entry_logic.h" // AP_HNE_PRIM_BYTES and AP_HNE_OT_BYTES for MainInit

struct GameTracker;

// MM_Title New Adventure: the level to request instead of ADVENTURE_GARAGE.
// A hub when the seed owns the racer and a hub is usable, else the garage.
int AP_HubNameEntry_NewAdventureLevel(void);

// The backdrop is requested or up. The AP hooks that key on a hub level id
// alone (no ADVENTURE_ARENA, no hub overlay test) stay quiet while it is.
int AP_HubNameEntry_Active(void);

// This level load is the backdrop: load it the way the garage loads.
int AP_HubNameEntry_IsBackdropLevel(int levelID);

// CS_Garage_Init on the backdrop: set up the screen instead of the garage
// picker. Returns 1 when it did (the garage init must not run).
int AP_HubNameEntry_GarageInit(void);

// LOAD_LevelFile: the prevLEV to record when leaving `leaving` for `next`.
int AP_HubNameEntry_OnLevelFile(int leaving, int next);

// Name entry Cancel on the backdrop: leave for the title. Returns 1 when it
// did.
int AP_HubNameEntry_CancelToTitle(void);

// Per frame, from AP_OnFrame: the name entry handoff, the camera and the
// fallback to the garage.
void AP_HubNameEntry_OnFrame(struct GameTracker *gGT);

#endif
