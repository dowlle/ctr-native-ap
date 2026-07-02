#ifndef AP_SEEDCFG_H
#define AP_SEEDCFG_H
#ifdef CTR_AP

// Per-seed slot_data configuration for CTR-Native (Phase 2-MVP).
//
// apclientpp surfaces slot_data as nlohmann::json on the network thread inside
// poll(); ap_net.cpp's Connected handler calls ap_seedcfg_parse_json() to fill
// the C-accessible ctr_cfg struct below. The C gate sites (game/232/AH_*.c)
// read ctr_cfg + the helpers through this header (pulled in via ap_hooks.h ->
// game_includes.h), gated on schema_version. When schema_version stays 0 (no
// slot_data, or schema absent / < 1) every gate falls back to its Phase-1
// static rule -- the parser never partially-activates because schema_version is
// written LAST, after all fields are populated.
//
// Thread-safety: ctr_cfg is written on the network thread inside poll() and read
// on the game thread in the SAME ap_net_poll() call (apclientpp is
// single-threaded -- the same guarantee the received-item queue relies on). No
// lock needed.

#ifdef __cplusplus
extern "C" {
#endif

// 28 physical warp-pad LevelIDs (0..27 covers the §0 track + cup IDs we index;
// CupRed..CupPurple are 100..104 and are handled by the fixed type:0 rule, so
// they are not addressed through these 28-wide arrays).
#define CTR_CFG_PAD_COUNT 28
#define CTR_CFG_BOSS_COUNT 5 // 0 roo, 1 papu, 2 komodo, 3 pinstripe, 4 oxide

// One resolved requirement. type per the §0 shared table:
//   0 none/use native vanilla fixed rule
//   1 trophies   2 keys   3 tokens   4 sapphire   5 gems
//   6 AnyToken   7 AnyRelic   8 AnyGem   (any-of aggregates, colour = -1)
// colour: -1 (n/a) or 0..4 = R,G,B,Y,P (only meaningful for tokens / gems).
//
// any-of semantics (requirement_specificity = any_of, the apworld default):
//   type 3 token / 5 gem with colour 0..4 = a SPECIFIC colour; colour -1 = the
//     legacy single-colour defensive fallback (apworld never emits it under any_of).
//   type 6 AnyToken  (colour -1) = owned >= count summed over ALL 5 token colours.
//   type 7 AnyRelic  (colour -1) = owned >= count summed over Sapphire+Gold+Platinum.
//   type 8 AnyGem    (colour -1) = owned >= count summed over ALL 5 gem colours.
// See AP_BossReqMet (ap_hooks.c) + AP_GateCount*Sum (ap_hooks.{h,c}).
typedef struct
{
	int type;
	int count;
	int colour;
} ctr_req;

// Two-stage warp-pad unlock (open-rando). stage1 opens the trophy race; stage2
// opens the relic Time Trials + CTR Token Challenge menu. Each stage is an
// independent resolved requirement that can be ANY CTR item type (relic/trophy/
// gem/token/key). A pad's stage2 MAY equal stage1 when a seed collapses it.
// schema_version 1 (flat, pre two-stage) is parsed into stage1 with a type:0
// (always-open) stage2 for back-compat -- see ap_seedcfg_parse_json.
typedef struct
{
	ctr_req stage1; // open the trophy race
	ctr_req stage2; // open the relic Time Trials + CTR Token Challenge
} ctr_warp_unlock;

typedef struct
{
	int schema_version; // 0 = not parsed -> Phase-1 fallback everywhere

	int goal;
	int relic_min_time;
	int relics_require_perfect;
	int oxide_final_unlock; // 0 = 18 sapphire, 1 = 18 gold+plat
	int shuffle_warp_pads;
	int warppad_unlock_mode; // 0 vanilla / 1 random / 2 random_without_4_keys
	int bossgarage_mode;     // 0 original4 / 1 same_hub / 2 trophies
	// item #5 placement toggles (forward-looking; MVP native ignores them because
	// locked gems/keys never enter the multiworld pool -> native never receives an
	// item it must place). A future native build can branch on these to tell
	// "reward not shuffled" apart from "reward shuffled elsewhere".
	int shuffle_gems;        // 0 gems pinned to gem cups / 1 gems in the pool
	int shuffle_keys;        // 0 keys pinned to boss races / 1 keys in the pool

	int             warp_pad_map[CTR_CFG_PAD_COUNT];    // physical pad LevelID -> target trackID (identity default)
	ctr_warp_unlock warp_pad_unlock[CTR_CFG_PAD_COUNT]; // per-pad two-stage resolved reqs (stage1.type 0 = native vanilla rule)
	// Gem cups (CupRed..CupPurple = LevelID 100..104) live OUTSIDE the 28-wide
	// warp_pad_unlock array, so their per-seed randomized unlock requirement is
	// stored here, keyed by cup colour 0..4 = R,G,B,Y,P (= LevelID - 100). The
	// apworld emits these under the same warp_pad_unlock object using string keys
	// "100".."104"; the parser routes those keys into this array. stage1.type 0 =
	// fall back to the Phase-1 "4 tokens of this colour" rule. stage2 unused (gem
	// cups have no tier-2 menu); kept as ctr_warp_unlock only for parse symmetry.
	ctr_warp_unlock gem_cup_unlock[5];                  // gem cups by colour (LevelID 100..104); stage1.type 0 = native vanilla rule
	ctr_req         boss_req[CTR_CFG_BOSS_COUNT];       // 0 roo,1 papu,2 komodo,3 pinstripe,4 oxide
	// Per-boss required race-track LevelIDs for the track-based garage modes
	// (bossgarage_mode 0 Original4Tracks / 1 SameHubTracks). boss_tracks[b][0..
	// boss_n_tracks[b]-1] are the LevelIDs the player must have WON for boss b to
	// open; n==0 / entry -1 = no track list (mode 2 Trophies, or Oxide) -> use
	// boss_req. Emitted by the apworld under boss_garage_req[<boss>].tracks.
	int             boss_tracks[CTR_CFG_BOSS_COUNT][4];
	int             boss_n_tracks[CTR_CFG_BOSS_COUNT];
} ctr_seed_config;

// Global config, zero-init; schema_version == 0 until ap_seedcfg_parse_json runs.
extern ctr_seed_config ctr_cfg;

// schema_version >= 1 (slot_data parsed and active).
int ctr_cfg_active(void);

// Remapped destination trackID for a physical pad LevelID. Identity (returns the
// input unchanged) when inactive or out of range -- safe to call unconditionally.
int ctr_cfg_warp_dest(int physPadLevelID);

// Inverse of ctr_cfg_warp_dest: given the destination trackID a pad currently
// LOADS (warppadObj->levelID after the LInB remap), return the PHYSICAL pad
// LevelID. Identity (returns the input unchanged) when inactive, out of range,
// or when no pad maps to that destination. Used by AH_WarpPad_ThTick's load gate
// so the per-pad unlock requirement (keyed by PHYSICAL pad) is checked against
// the physical pad and not the remapped destination. warp_pad_map is a
// permutation within each shuffle group, so the inverse is unique.
int ctr_cfg_warp_phys(int destTrackLevelID);

// Is a trophy-track warp pad's LOAD gate satisfied? When active and the pad has a
// per-seed requirement (type != 0), compares owned >= count for that requirement
// (colour-aware for tokens/gems). Otherwise falls back to the Phase-1 trophy rule
// (received trophies >= numTrophiesToOpen). Used by the load-time gate in
// AH_WarpPad_ThTick so the spawn visual and the load gate read one source.
//
// IMPLEMENTED C-SIDE in ap_hooks.c (not ap_seedcfg.cpp): it needs AP_GateCount*
// (received-item counters live in the C unity build) and the per-track
// numTrophiesToOpen threshold from data.metaDataLEV[] for the Phase-1 fallback,
// neither of which is reachable from the isolated C++ static lib.
int ctr_cfg_warp_unlocked(int levelID);

// Is a trophy-track warp pad's STAGE-2 satisfied (open the relic Time Trials +
// CTR Token Challenge menu)? When active and the pad has a stage2 requirement
// (type != 0), compares owned >= count for stage2 (colour-aware for tokens/gems
// via AP_BossReqMet). type 0 (no stage2 / collapsed / flat-v1 slot_data) returns
// 1 (always open) so the tier-2 menu opens as soon as stage1 is met -- exactly
// the pre-two-stage behaviour. physPadLevelID is the PHYSICAL pad LevelID, the
// same key ctr_cfg_warp_unlocked uses. IMPLEMENTED C-SIDE in ap_hooks.c.
int ctr_cfg_warp_stage2_unlocked(int physPadLevelID);

// Typed comparator for a resolved requirement (boss garages + warp-pad load
// gate). Returns owned >= count for r->type using AP_GateCount* (colour-aware).
// type 0 / unknown -> 1 (no requirement). IMPLEMENTED C-SIDE in ap_hooks.c.
int AP_BossReqMet(const ctr_req *r);

// Per-mode boss-garage gate for the four boss hubs (bossIdx 0..3 = Roo, Papu,
// Komodo, Pinstripe). Honours bossgarage_mode: modes 0/1 (Original4Tracks /
// SameHubTracks) open the garage once every required race track for that boss
// has been WON (boss_tracks[bossIdx]); mode 2 (Trophies) and any boss with no
// track list fall back to the flat trophy-count requirement via AP_BossReqMet.
// IMPLEMENTED C-SIDE in ap_hooks.c (needs AP_LocationCheckedByBit).
int AP_BossGarageOpen(int bossIdx);

#ifdef __cplusplus
} // extern "C"

#include <nlohmann/json.hpp>
// C++-only: parse the slot_data JSON into ctr_cfg. Called from ap_net.cpp.
void ap_seedcfg_parse_json(const nlohmann::json &j);
#endif

#endif // CTR_AP
#endif // AP_SEEDCFG_H
