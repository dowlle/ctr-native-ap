#ifndef AP_CAPABILITY_H
#define AP_CAPABILITY_H

#ifdef CTR_AP

// Progressive Boost (#12) + Progressive Speed / Acceleration / Turning (#13):
// the NATIVE CONSUMER of the item packs the apworld ships as of spine 1
// (dowlle/ctr-archipelago-apworld PR #42). This module turns received-item
// counts into actual kart capability.
//
// ── What drives it ──
// Two independent inputs, and BOTH must say yes before a single engine value
// changes:
//   1. the seed's wire modes -- ctr_cfg.boost_mode / ctr_cfg.stats_mode, from
//      the additive ctr_options keys (schema 7, no bump: the Q28 ruling already
//      made 7 unconditional on every 0.2.0 seed);
//   2. the received-item counts rebuilt from the server's authoritative
//      ReceivedItems replay, exactly like the #14/#15 surface items.
// A seed with the packs off -- or no slot_data at all -- never reaches a write,
// so the vanilla kart is untouched by construction, not by a value that happens
// to equal vanilla.
//
// ── Boost ladder (#12, comment 2026-07-16 + the 2026-07-26 correction) ──
//   0 NONE      self-earned boost grants nothing: powerslide/mini-turbo, hang
//               time, the start-line rev boost, the Turbo pickup and the
//               10-wumpa Super Engine. Turbo PADS still work -- that is the
//               design's explicit invariant, so every track stays drivable at
//               the bottom of the chain. Field-confirmed by Stef in the Test Lab
//               build on 2026-08-09 21:31 (the boost weapon fires, is consumed,
//               and does nothing).
//   1 BOOST     everything grants again, but the boost-speed cap is clamped to
//               the linear cap's red-fire point, so no grant reaches USF speed.
//   2 USF       the full super turbo pad payload; the top of the IMPLEMENTED
//               chain.
//   3 BLUE FIRE the 2026-07-26 capstone, only in the ladder when the seed set
//               boost_blue_fire. **It carries no physics of its own yet** -- its
//               values come from CTR Unlimited's Retro Fueled mode and have
//               never been sourced, so this build tracks the tier honestly and
//               drives it at USF rather than inventing numbers. See the loud
//               note on AP_CAP_BOOST_BLUEFIRE below.
// Super turbo pads act as NORMAL pads at every tier below USF, in both the speed
// cap and the banked reserves, which is what makes a USF-dependent jump a real
// progression gate (Cortex Castle, measured USF-only in the field notes).
//
// ── Stat ladder (#13, the 2026-08-08 five-rank ruling) ──
// VERY LOW -> LOW -> MEDIUM -> HIGH -> VERY HIGH, four received copies per
// chain. Ranks 0..3 are the engine's own four per-class values for that stat,
// sorted weakest-first and read out of data.metaPhys at runtime rather than
// copied into a table here (Lessons Learned #5: derive, never restate). Rank 4
// is the ruled above-vanilla step; see AP_CapabilityStatRank for the one place
// its magnitude is decided and why that magnitude is still provisional.
//
// Three chains drive six engine stats:
//   Progressive Top Speed     -> const_Speed_ClassStat, const_AccelSpeed_ClassStat
//   Progressive Acceleration  -> const_Accel_ClassStat
//   Progressive Turning       -> const_TurnRate, const_DriftTurnBase,
//                                const_TurnInputDelay
// The same six the Test Lab exposes, and for the same reason: they are the
// class-varying stats whose four values order with kart quality on every axis.
// TURN_DECREASE_RATE, PRE_TURBO and COLLISION_WEIGHT also vary by class but do
// NOT order with quality, so "the weakest one" has no defensible meaning for
// them and they stay vanilla.
//
// ── Scope ──
// Local player only (drivers[0]) at every hook, so AI racers and ghost replays
// are never touched -- the same isolation convention ap_surface.c and ap_traps.c
// use. per_character mode (wire value 2) is a RESERVED shape the apworld refuses
// to generate today (it raises OptionError pending
// dowlle/ctr-native-ap#71, location supply); this module recognises its item
// block and declines it loudly instead of guessing a roster mapping.

#include <stdint.h>

struct Driver;

// ── Item indices (id = AP_ITEM_BASE + index, apworld data/items.json) ──
// Shared-global block, minted by spine 1 straight after the #14/#15 reserve:
//   27 Progressive Boost · 28 Progressive Top Speed
//   29 Progressive Acceleration · 30 Progressive Turning
#define AP_CAPABILITY_ITEM_FIRST_INDEX 27
#define AP_CAPABILITY_ITEM_COUNT 4

// Per-character block: 16 racers x the same 4 chains, indices 31..94. Registered
// in the datapackage now so a future dowlle/ctr-native-ap#71 landing needs no
// second naming pass, but never CREATED by generation today. Recognised here
// only so a receipt lands in the declined-path log instead of the generic
// "filler/unmapped" line -- nothing maps it to a chain, deliberately.
#define AP_CAPABILITY_PC_ITEM_FIRST_INDEX 31
#define AP_CAPABILITY_PC_ITEM_COUNT 64

// Chain index. Order matches the item indices above, so
// (idx - AP_CAPABILITY_ITEM_FIRST_INDEX) IS the chain.
enum
{
	AP_CAP_CHAIN_BOOST = 0,
	AP_CAP_CHAIN_TOP_SPEED,
	AP_CAP_CHAIN_ACCEL,
	AP_CAP_CHAIN_TURNING,
	AP_CAP_CHAIN_COUNT
};

// Boost tiers. Index-keyed and ladder-ordered: the tier IS the number of
// received Progressive Boost copies, clamped to the seed's ceiling.
enum
{
	AP_CAP_BOOST_NONE = 0,
	AP_CAP_BOOST_BOOST,
	AP_CAP_BOOST_USF,
	// The capstone from the 2026-07-26 correction. Reachable ONLY when the seed
	// set boost_blue_fire. Its speed/reserve values were to come from CTR
	// Unlimited's Retro Fueled mode and have not been sourced by anyone on this
	// project, so this build deliberately gives it USF's payload and no more --
	// an invented number here would silently become the balance everybody then
	// measures the capability matrix against.
	AP_CAP_BOOST_BLUEFIRE,
	AP_CAP_BOOST_COUNT
};

// Stat ranks: VERY LOW / LOW / MEDIUM / HIGH / VERY HIGH (2026-08-08 ruling).
// Four received copies climb rank 0 -> 4.
#define AP_CAP_STAT_RANK_COUNT 5
#define AP_CAP_STAT_COPIES (AP_CAP_STAT_RANK_COUNT - 1)

// Wire mode values for ctr_options.boost_mode / stats_mode.
#define AP_CAP_MODE_OFF 0
#define AP_CAP_MODE_SHARED_GLOBAL 1
#define AP_CAP_MODE_PER_CHARACTER 2

// Zeroed on every fresh slot-connect, then rebuilt from the server's resent
// ReceivedItems list -- so duplicates, reconnects and server switches all land on
// the same counts (the ap_recv_count / AP_SurfaceReset discipline).
void AP_CapabilityReset(void);

// One received shared-global capability item, chain 0..AP_CAP_CHAIN_COUNT-1. Out
// of range is a no-op.
void AP_CapabilityReceive(int chain);

// One received PER-CHARACTER capability item (block index 0..63). Declined, with
// a single log line per connect: no generated seed can contain these today and
// this build has no ruled roster mapping to apply them through.
void AP_CapabilityReceivePerCharacter(int blockIndex);

// This seed's effective boost tier (AP_CAP_BOOST_*), or -1 when the pack is not
// active for this seed. Read by the fire-grant filter; exposed for logging.
int AP_CapabilityBoostTier(void);

// Effective rank 0..4 for a stat chain, or -1 when the stat pack is not active.
int AP_CapabilityStatRankFor(int chain);

// Boost-grant filter, called at the top of VehFire_Increment (the single choke
// point every boost in the game passes through). Returns 0 to drop the grant
// entirely, or 1 to let it through, having possibly rewritten *reserves and
// *fireLevel to demote a super turbo pad to a normal one. type is the grant's
// TurboType mask and is never rewritten, so a demoted super pad keeps a pad's own
// reserve-freeze behaviour.
int AP_CapabilityFireGrant(struct Driver *driver, int *reserves, uint32_t type, int *fireLevel);

// Received stat ranks -> the driver's stat constants, called once per frame from
// VehPhysProc_Driving_PhysLinear before anything there reads them. No-op unless
// the stat pack is active for this seed and this is the local player.
void AP_CapabilityStats(struct Driver *driver);

#endif // CTR_AP
#endif // AP_CAPABILITY_H
