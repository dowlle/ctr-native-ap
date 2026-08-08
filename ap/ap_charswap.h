#ifndef AP_CHARSWAP_H
#define AP_CHARSWAP_H

// ---------------------------------------------------------------------------
// Hub character picker + hub-swap FEASIBILITY PROTOTYPE (spike, not a feature)
//
// Answers the R7 / issue #54 research ticket "can the adventure hub swap the
// player's character safely, and can a thin reusable 16-portrait picker drive
// it". Everything here is gated behind ap-config.txt dev_keys=1 and the
// adventure hub, so a normal seed never sees it.
//
// SEAMS THAT ARE DELIBERATELY STUBBED (see ap_charswap.c for the marker
// comments): AP unlock state, progressive-stat item storage, the
// progressive_stats / editable_stats YAML options, and persistence of the
// current character. None of those have landed yet and this spike does not
// invent their contracts; it exposes local test toggles in their place.
// ---------------------------------------------------------------------------

struct GameTracker;
struct Driver;

// Per-frame logic + input. Called from AP_OnFrame (every frame, all modes);
// self-gates on dev keys + "adventure hub is open".
void AP_CharSwap_Tick(struct GameTracker *gGT);

// Draws the picker. Called from BOTH hub UI passes (AH_Map.c RenderAllHUD and
// UI_RenderFrame_AdvHub) next to AP_FeedDrawHub, which are mutually exclusive
// per frame, so this runs exactly once per frame while the hub is up.
void AP_CharPicker_Draw(void);

// True while the picker owns input (used to keep the driver frozen).
int AP_CharSwap_PickerOpen(void);

// Post-pass over VehBirth_SetConsts: applies the AP-owned stat package for the
// LOCAL player only, over the same struct Driver offsets the vanilla loop
// wrote. This is the shape recommended by the 2026-07-25 Tier-1 source read
// (no vanilla data layout is touched, so retail behaviour is unchanged when
// the package is inactive). No-op unless a test package is active.
void AP_CharSwap_ApplyStatPackage(struct Driver *driver);

#endif // AP_CHARSWAP_H
