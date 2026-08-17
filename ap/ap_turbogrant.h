#ifndef AP_TURBOGRANT_H
#define AP_TURBOGRANT_H
#ifdef CTR_AP

// ---------------------------------------------------------------------------
// Turbo Grant (#224): an in-race Turbo hand-out, as a received AP item.
//
// THE RULE, from the issue body and the 2026-08-11 "Mask and Turbo filler
// interactions" ruling. Receiving the grant rolls a normal Turbo straight into
// the player's existing weapon slot and plays the usual item-pickup ping. It is
// fired on demand through the ordinary held-item path, so it adds no inventory
// interface and no second firing path. With itemsanity enabled a grant is
// deliverable only after the separate progression `Turbo` weapon item has been
// received; with itemsanity disabled no weapon-item gate applies. A grant that
// cannot be delivered yet -- gated, outside a race, or with the weapon slot
// occupied -- QUEUES. None is ever silently discarded.
//
// The sibling Mask filler is the already-frozen `Invincibility Mask` item and is
// deliberately not touched here; the ruling forbids minting a duplicate.
//
// WHAT THIS MODULE OWNS, and why each piece is here rather than in ap_hooks.c:
//
//   * THE ACCOUNTING. Three numbers -- received (rebuilt from the authoritative
//     ReceivedItems replay), fired (persisted per seed and slot) and one
//     in-flight bit -- with pending derived from them. ap_turbogrant_logic.h
//     holds the arithmetic and the reasoning, including why the generic
//     ap_fx_seen_max replay dedup every other one-shot effect uses would LOSE
//     grants here. The host harness drives that header directly.
//   * THE DELIVERY WINDOW. The same observed-countdown race window the Wumpa
//     filler uses (ap_wumpa.c documents why the raw flag/timer test is not
//     enough), plus the weapon-slot preconditions RB_Crate_Collide itself
//     applies before it lets a weapon box start a roll.
//   * "IS ITEMSANITY ON THIS SEED" -- read off SERVER LOCATION MEMBERSHIP (the
//     35016000 block), not off slot_data, exactly as #223 does. It needs no new
//     wire key and no parser path, so an absent or pre-0.2.0 slot_data is inert
//     rather than a special case, and it cannot drift from what the server
//     actually sent. The apworld emits a `turbo_grant` scalar for trackers only;
//     nothing here reads it.
//
// WHAT IT DELIBERATELY DOES NOT OWN: the boost itself. A delivered grant is a
// perfectly ordinary held Turbo, so firing it runs VehPickupItem_ShootNow's
// Turbo arm into VehFire_Increment and through AP_CapabilityFireGrant like every
// other boost in the game. Progressive Boost therefore governs it for free: at
// tier off the held Turbo is consumed and produces no boost, and at higher tiers
// it produces only what that tier allows. There is no code here that could
// upgrade or bypass a tier, which is the point.
//
// ONE KNOWN, DELIBERATE IMPRECISION. The in-flight bit is session state, so a
// reconnect that lands WHILE a delivered Turbo is still sitting unfired in the
// slot forgets that it is ours. The physical Turbo stays in the slot and can
// still be fired, but the fired count does not move for it, so once the slot
// empties the queue hands out one more. The player gets an extra Turbo in that
// one narrow window. Closing it would mean persisting the in-flight bit and
// then guessing, on the next connect, whether the Turbo in the slot is the same
// one -- which is not decidable from anything the client can see, and guessing
// wrong in the other direction would DISCARD a grant. The issue's rule 4 makes
// that the worse error, so the imprecision is left where it errs toward the
// player and is recorded here rather than hidden.
//
// NOT VERIFIED IN GAME. The build host has no disc. Delivery into a live weapon
// slot, the pickup ping, the queue draining at lights-out and the behaviour at
// each boost tier have never been observed running; they are argued from the
// engine sources named above and covered by the host harness only where the rule
// is pure arithmetic. The rolling testing list carries the in-game pass.
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

// Fresh connect / slot reset. Zeroes the session counters, then reloads this
// seed-and-slot's persisted fired count. Called from the same point that zeroes
// the capability chains, BEFORE the authoritative ReceivedItems replay rebuilds
// the received count.
void AP_TurboGrantReset(void);

// One `Turbo Grant` receipt. Called for every occurrence in the drain, replays
// included: the count is rebuilt from zero each connect, so replaying is how it
// stays idempotent.
void AP_TurboGrantReceive(void);

// The itemsanity `Turbo` weapon item was received. Boolean; duplicates are
// harmless, and it is cleared by the reset above like every other receipt.
void AP_TurboGrantReceiveTurboWeapon(void);

// Per-frame. Maintains the countdown latch, requeues an in-flight Turbo that
// left the slot without being fired, and delivers one pending grant when the
// race window, the ruled itemsanity gate and the empty-slot preconditions all
// hold. No-op outside a race or with nothing pending.
void AP_TurboGrantTick(struct GameTracker *gGT);

// The local player committed to firing `heldItemID`. Consumes an in-flight
// grant when that item is the Turbo this module delivered, which is the only
// point where the fired count moves and is persisted.
void AP_TurboGrantOnFire(struct Driver *driver, int heldItemID);

#endif // CTR_AP
#endif // AP_TURBOGRANT_H
