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
// colour: -1 (n/a) or 0..4 = R,G,B,Y,P (only meaningful for tokens / gems).
typedef struct
{
	int type;
	int count;
	int colour;
} ctr_req;

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

	int     warp_pad_map[CTR_CFG_PAD_COUNT];    // physical pad LevelID -> target trackID (identity default)
	ctr_req warp_pad_unlock[CTR_CFG_PAD_COUNT]; // per-pad resolved req (type 0 = native vanilla rule)
	ctr_req boss_req[CTR_CFG_BOSS_COUNT];       // 0 roo,1 papu,2 komodo,3 pinstripe,4 oxide
} ctr_seed_config;

// Global config, zero-init; schema_version == 0 until ap_seedcfg_parse_json runs.
extern ctr_seed_config ctr_cfg;

// schema_version >= 1 (slot_data parsed and active).
int ctr_cfg_active(void);

// Remapped destination trackID for a physical pad LevelID. Identity (returns the
// input unchanged) when inactive or out of range -- safe to call unconditionally.
int ctr_cfg_warp_dest(int physPadLevelID);

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

// Typed comparator for a resolved requirement (boss garages + warp-pad load
// gate). Returns owned >= count for r->type using AP_GateCount* (colour-aware).
// type 0 / unknown -> 1 (no requirement). IMPLEMENTED C-SIDE in ap_hooks.c.
int AP_BossReqMet(const ctr_req *r);

#ifdef __cplusplus
} // extern "C"

#include <nlohmann/json.hpp>
// C++-only: parse the slot_data JSON into ctr_cfg. Called from ap_net.cpp.
void ap_seedcfg_parse_json(const nlohmann::json &j);
#endif

#endif // CTR_AP
#endif // AP_SEEDCFG_H
