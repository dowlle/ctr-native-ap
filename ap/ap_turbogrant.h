#ifndef AP_TURBOGRANT_H
#define AP_TURBOGRANT_H
#ifdef CTR_AP

// ---------------------------------------------------------------------------
// Turbo Grant (#224): an in-race Turbo hand-out, as a received AP item.
//
// THE RULE SINCE #487 (ruling of 2026-10-23). Once `Turbo Grant` has been received,
// a normal Turbo is put straight into the player's existing weapon slot at the
// start of every race, with the usual item-pickup ping. It is fired on demand
// through the ordinary held-item path, so it adds no inventory interface and no
// second firing path. With itemsanity enabled the race's Turbo waits until the
// separate progression `Turbo` weapon item has been received; with itemsanity
// disabled no weapon-item gate applies. One Turbo per race attempt (a restart
// is a new attempt); more received copies do not add more Turbos. Before #487
// each copy was a single one-shot grant with a fired count kept in the room.
//
// The sibling Mask filler is the already-frozen `Invincibility Mask` item and is
// deliberately not touched here; the ruling forbids minting a duplicate.
//
// WHAT THIS MODULE OWNS:
//
//   * THE PER-RACE LATCH. ap_turbogrant_logic.h holds it; the host harness
//     drives that header directly.
//   * THE DELIVERY WINDOW. The same race window the Wumpa filler uses
//     (ap_wumpa.c documents why the raw flag/timer test is not enough), plus
//     the weapon-slot preconditions RB_Crate_Collide itself applies before it
//     lets a weapon box start a roll.
//   * "IS ITEMSANITY ON THIS SEED" -- read off SERVER LOCATION MEMBERSHIP (the
//     35016000 block), not off slot_data, exactly as #223 does. It needs no new
//     wire key and no parser path, so an absent or pre-0.2.0 slot_data is inert
//     rather than a special case, and it cannot drift from what the server
//     actually sent. The apworld emits a `turbo_grant` scalar for trackers only;
//     nothing here reads it.
//
// WHAT IT DELIBERATELY DOES NOT OWN: the boost itself. A delivered Turbo is a
// perfectly ordinary held Turbo, so firing it runs VehPickupItem_ShootNow's
// Turbo arm into VehFire_Increment and through AP_CapabilityFireGrant like every
// other boost in the game. Progressive Boost therefore governs it for free: at
// tier off the held Turbo is consumed and produces no boost, and at higher tiers
// it produces only what that tier allows.
//
// NOT VERIFIED IN GAME. The build host has no disc. Delivery into a live weapon
// slot, the pickup ping, the delivery at each race start and restart and the
// behaviour at each boost tier have never been observed running; the rolling
// testing list carries the in-game pass.
// ---------------------------------------------------------------------------

struct Driver;
struct GameTracker;

// Apworld item indices (id = AP_ITEM_BASE + index, data/items.json).
// 189 is the #224 amendment, appended one past #223's `Tizi Helper` at 188 and
// at the code the slot_data Contract reserved for it.
#define AP_TURBOGRANT_ITEM_INDEX 189
// The itemsanity `Turbo` weapon: the FIRST of the eleven, index 95 + 0. Spelled
// out here the way #223 spells out its Mask index; the apworld's itemsanity
// item-order test pins the same number.
#define AP_TURBOGRANT_TURBO_ITEM_INDEX 95
// Engine held-item id for the Turbo weapon, and the engine's "slot is empty"
// sentinel (VehBirth writes 0xf for "no item").
#define AP_TURBOGRANT_HELD_ITEM_TURBO 0x0
#define AP_TURBOGRANT_HELD_ITEM_NONE  0xf

// Fresh connect / slot reset. Zeroes the receipts; the per-race latch is kept.
// Called from the same point that zeroes the capability chains, BEFORE the
// authoritative ReceivedItems replay rebuilds the receipts.
void AP_TurboGrantReset(void);

// One `Turbo Grant` receipt. Called for every occurrence in the drain, replays
// included: the receipts are rebuilt from zero each connect.
void AP_TurboGrantReceive(void);

// The itemsanity `Turbo` weapon item was received. Boolean; duplicates are
// harmless, and it is cleared by the reset above like every other receipt.
void AP_TurboGrantReceiveTurboWeapon(void);

// Per-frame. Maintains the per-race latch and delivers this race's Turbo when
// the race window, the ruled itemsanity gate and the empty-slot preconditions
// all hold. No-op outside a race or once this race's Turbo was delivered.
void AP_TurboGrantTick(struct GameTracker *gGT);


#endif // CTR_AP
#endif // AP_TURBOGRANT_H
