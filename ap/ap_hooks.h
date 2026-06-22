#ifndef AP_HOOKS_H
#define AP_HOOKS_H

// Archipelago integration hooks for CTR Native.
//
// Compiled ONLY when CTR_AP is defined (CMake: -DCTR_AP=ON). The clean build
// leaves this out entirely. All Archipelago logic lives in this module so the
// upstream ctr-native diff stays minimal: upstream code calls AP_* hooks, and
// everything else stays here.

#ifdef CTR_AP

#include "ap_seedcfg.h" // per-seed slot_data config (ctr_cfg + getters), Phase 2

struct GameTracker;

// Called once per frame from the main loop (CTR_Main in MainMain.c).
void AP_OnFrame(struct GameTracker *gGT);

// Location events (option A) -- called from the game's reward-grant sites the
// instant the player earns a checkable adventure reward. rewardBit is the
// AdvProgress bit index (= word*32 + bit); resolved to an AP location code.
void AP_NotifyAdvReward(int rewardBit);

// Called when the player beats Oxide (the goal). oxideSecond != 0 = final win.
void AP_NotifyGoal(int oxideSecond);

// ── AP gate counters (received-item model, Option B) ──
// Adventure gates read these received-item-TYPE counts instead of AdvProgress
// location bits, so item fill (generic, high-end) no longer collides with the
// game's own location-bit grants. itemType = raw item index 0..14.
//
// Item-TYPE indices (item id = AP_ITEM_BASE + index). Mirrored in ap_items.h;
// declared here too so the gate sites (game/232/AH_*.c) can read them by name.
// Colour order 0=Red 1=Green 2=Blue 3=Yellow 4=Purple (matches the apworld).
#ifndef AP_IDX_TROPHY
#define AP_IDX_TROPHY        0
#define AP_IDX_SAPPHIRE      1
#define AP_IDX_GOLD          2
#define AP_IDX_PLATINUM      3
#define AP_IDX_TOKEN_RED     4  // tokens: 4..8 = R,G,B,Y,P
#define AP_IDX_GEM_RED       9  // gems:   9..13 = R,G,B,Y,P
#define AP_IDX_KEY          14
#define AP_ITEM_INDEX_COUNT 15
#endif

int AP_GateCount(int itemType);          // received count for one of the 15 item indices
int AP_GateCountTokenColour(int colour); // colour 0..4 = R,G,B,Y,P -> token idx 4+colour
int AP_GateCountGemColour(int colour);   // colour 0..4 = R,G,B,Y,P -> gem   idx 9+colour

// Append a line to the AP debug log (forwards to the module's AP_AppendLog).
// Exposed so the game-side gate files (game/232/AH_*.c) can emit confirmation
// lines -- e.g. AH_WarpPad_LInB logs each pad whose destination was remapped.
void AP_LogLine(const char *msg);

#endif // CTR_AP

#endif // AP_HOOKS_H
