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

// Called when the player beats Oxide. oxideSecond != 0 = final win. Records the
// event; whether it COMPLETES the seed depends on ctr_cfg.goal (see AP_EvaluateGoal).
void AP_NotifyGoal(int oxideSecond);

// Evaluate the per-seed goal (ctr_cfg.goal) against received items + game events
// and send StatusUpdate(GOAL) once when met. Call every adventure frame (item-
// based goals: all-bosses / all-gem-cups / 101%) and on the Oxide beat.
void AP_EvaluateGoal(void);

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

// ── any-of aggregate counters (requirement_specificity = any_of) ──
// Sum the received counts across a whole item TYPE, for the type 6/7/8 gates.
int AP_GateCountTokenSum(void); // all 5 token colours (idx 4..8)
int AP_GateCountRelicSum(void); // all 3 relic tiers  (idx 1..3 = Sapphire+Gold+Platinum)
int AP_GateCountRelicTier(int colour); // type-4 tier: 0=Sapphire,1=Gold,2=Platinum; -1=Sapphire
int AP_GateCountGemSum(void);   // all 5 gem colours  (idx 9..13)

// Append a line to the AP debug log (forwards to the module's AP_AppendLog).
// Exposed so the game-side gate files (game/232/AH_*.c) can emit confirmation
// lines -- e.g. AH_WarpPad_LInB logs each pad whose destination was remapped.
void AP_LogLine(const char *msg);

// 1 if Aku Aku mask hints should be SKIPPED. Read once from ap-config.txt
// (line "skip_hints=1") at connect. Honoured at the single choke point
// MainFrame_RequestMaskHint (#ifdef CTR_AP) by early-returning, so no hint is
// ever armed. Default 0 (hints behave normally). QoL / testing convenience;
// later the apworld can write this value into ap-config.txt from a YAML option.
int AP_SkipHints(void);

// 1 if the hub-map "Raceable" two-tone flicker (state 2 GREEN) is enabled.
// Default 1; set to 0 by ap-config.txt "map_flash=0" for a static GREEN. Read by
// AH_Map_Warppads (#ifdef CTR_AP). Part of the Warp-Pad State Model v2.
int AP_MapFlashOn(void);

// ── Reward glow ──
// Model id to DISPLAY in a warp-pad prize slot, for the location identified by
// its AdvProgress global bit (= word*32 + bit) on the pad's DESTINATION track.
// Returns the model of the AP item actually placed at that location -- an own
// CTR reward shows its category model (trophy/relic/token/gem/key); a foreign
// multiworld item shows STATIC_GEM (tinted white) as a generic "AP" marker.
// Returns -1 when not connected, not yet scouted, or the bit is not a checkable
// location, so the caller keeps the vanilla model. Used by AH_WarpPad_LInB
// (#ifdef CTR_AP).
int AP_WarpPadRewardModel(int globalBit);

// Reward-glow TINT. Vanilla renders every relic tier the same blue, so a glow
// advertising a Sapphire / Gold / Platinum relic looked identical. Returns a
// tier-specific packed colorRGBA for an OWN relic scouted at this location; pure
// white for a FOREIGN multiworld item (so the generic STATIC_GEM marker renders
// as a distinct white gem, not mistaken for an own coloured gem or boss Key); or
// 0 to keep the caller's default colour (own non-relic / unscouted -> 0). Caller
// applies it to relic + gem marker models.
int AP_WarpPadRewardTint(int globalBit);

// ── Requirement-hologram relic tint (closed pad) ──
// The relic icon shown on a LOCKED pad advertises that pad's relic REQUIREMENT.
// Vanilla renders it in one blue; the reward-glow rework cycles it through the 3
// tiers so an AnyRelic req reads as "any tier". A specific-tier req (schema-4
// type 4) must instead pin THAT tier's tint. LInB owns the physical-pad/stage
// selection that decides which requirement a pad shows, so it records the shown
// req's tint tier here; AH_WarpPad_ThTick (destination-keyed, must not re-run
// that selection) reads it back keyed by the physical pad.
//
// AP_ReqRelicTintTier maps a resolved requirement to that tier: 0/1/2 =
// Sapphire/Gold/Platinum for a type-4 req (legacy colour -1 = Sapphire, matching
// AP_GateCountRelicTier); any other type -- including AnyRelic (type 7) -- returns
// a negative value meaning "keep the sapphire->gold->platinum cycle".
int AP_ReqRelicTintTier(const ctr_req *r);

// Record / read the tint tier for a physical pad's closed-req relic hologram.
// physLevelID is the PHYSICAL pad LevelID (0..27 warp pads, 100..104 gem cups).
// A negative stored value (the default, and what unknown pads return) means cycle.
void AP_SetWarpReqRelicTint(int physLevelID, int tier);
int  AP_WarpReqRelicTint(int physLevelID);

// 1 if the AP location at `globalBit` (= word*32+bit) has been CHECKED on the
// server for our own slot. Use this -- NOT CHECK_ADV_BIT on the AdvProgress bits
// -- to ask "has the player completed this location" in AP mode: AP_ApplyItems
// clears any location bit not backed by a received item every frame, so the raw
// bit can't reflect a local win. Returns 0 if not a checkable bit / not connected.
int AP_LocationCheckedByBit(int globalBit);

// ── Reward-glow uncollected enumeration ──
// Fill `outBits` (capacity `cap`) with the still-UNCOLLECTED (unchecked) AP reward
// locations of race track `destLevelID` (0..15), in fixed tier order: Trophy
// +0x06, Sapphire +0x16, Gold +0x28, Platinum +0x3a, CTR Token +0x4c. Returns the
// count written (0..5). "Collected" is decided ONLY by AP checked-state (the
// server's set), never by CHECK_ADV_BIT / modelIndex (which AP_ApplyItems clears
// every frame). Tiers absent from this seed's location table are skipped. Returns
// 0 for a non-race destination or when all tiers are checked. The warp-pad glow
// (AH_WarpPad_LInB / _ThTick, #ifdef CTR_AP) drives its model/tint/visibility and
// the multi-reward cycle entirely off this list -- fully decoupled from the
// vanilla pad-state machine.
int AP_WarpPadUncollectedBits(int destLevelID, int *outBits, int cap);

// ── Unified pad state (Warp-Pad State Model v2) ──
// Category-general uncollected-location enumerator: fills `outBits` (capacity
// `cap`) with the still-unchecked AP reward locations of destination
// `destLevelID` for ANY category (race 0..15 = 5 tiers; trial 16/17 = 3 relic
// tiers; arena 18/19/21/23 = 1 crystal; cup 100..104 = 1 gem) and returns the
// count. The category-general sibling of AP_WarpPadUncollectedBits (race-only).
int AP_PadUncollectedBits(int destLevelID, int *outBits, int cap);

// AP state-generation counter (foundation for live in-hub pad re-birth). Bumped
// on fresh slot-connect, on a received item that changes a gate count, and on a
// location-checked notification -- i.e. whenever a pad's AP_PadState may change.
// A future re-birth consumer tags each pad at birth and rebuilds it on mismatch.
// Not yet consumed for re-birth (AH_WarpPad_LInB is level-load-only; see the
// note in ap_hooks.c). The map + gates already recompute AP_PadState live.
unsigned AP_StateGen(void);

// The single pad-state function driving map colour, in-hub look, and the gate.
// Returns 1 Locked / 2 Raceable / 3 Re-locked / 4 Tier-2-open / 5 Done, or 0 for
// vanilla mode (no slot_data) / unrecognised destination (leave the pad
// untouched). physLevelID keys the requirement (physical pad); destLevelID keys
// the location + lifecycle category (loaded destination track). Consumed by
// AH_Map_Warppads + AH_WarpPad_LInB/_ThTick (#ifdef CTR_AP).
int AP_PadState(int physLevelID, int destLevelID);

#endif // CTR_AP

#endif // AP_HOOKS_H
