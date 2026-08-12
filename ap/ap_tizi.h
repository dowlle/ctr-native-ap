#ifndef AP_TIZI_H
#define AP_TIZI_H
#ifdef CTR_AP

// ---------------------------------------------------------------------------
// Tizi Helper (#223): the Papu's Pyramid mask helper, as a received AP item.
//
// THE RULE, verbatim from the issue and the 2026-08-10 ruling. Once the slot has
// received `Tizi Helper`, the first row of four weapon boxes immediately after
// the Papu's Pyramid start line yields a Mask instead of a rolled weapon. With
// itemsanity enabled it also needs the separate `Mask` weapon item, and stays
// inert until both are in. Everywhere else -- other boxes on that track, every
// other track, every mode -- behaviour is untouched.
//
// WHAT THIS MODULE OWNS, and why each piece is here rather than in ap_hooks.c:
//
//   * The two receipts. `Tizi Helper` is apworld item index 188, `Mask` is the
//     itemsanity weapon at index 101. Both are rebuilt from zero on every fresh
//     connect and slot switch, exactly like the capability chains and the
//     surface items, because the server resends the full ReceivedItems list and
//     a stale flag would leak one slot's helper into another's session.
//   * "Is itemsanity on this seed" -- read off SERVER LOCATION MEMBERSHIP
//     (the 35016000 block), not off slot_data. Same answer the #145 native half
//     uses, and it needs no new wire key and no parser path, so an absent or
//     pre-0.2.0 slot_data is inert rather than a special case. When #145 lands,
//     the two readers can collapse into one; they agree by construction today.
//   * WHICH FOUR BOXES. Resolved once per level load from the level's own
//     checkpoint chain, and REFUSED (leaving every box vanilla) whenever the
//     layout does not match the shape the rule describes. The selection rule and
//     its refusal codes are in ap_tizi_logic.h, which the host harness drives
//     directly. See that header for why this is derived rather than tabulated.
//
// NOT VERIFIED IN GAME. Atlas has no disc, so the four crates this picks on the
// real Papu's Pyramid LEV have never been observed. The rule fails closed and
// logs its full census either way, so an Artemis pass reads one log line to
// confirm or refute the mapping. Until that pass runs, treat the four-box
// identity as unverified -- the rolling testing list carries it.
// ---------------------------------------------------------------------------

struct Driver;
struct GameTracker;
struct Instance;
struct InstDef;

// Apworld item indices (id = AP_ITEM_BASE + index, data/items.json).
// 188 is the #223 amendment, appended one past the #177 freeze's last name.
#define AP_TIZI_ITEM_INDEX 188
// The itemsanity `Mask` weapon, index 95 + 6. Spelled out rather than derived
// from the #145 constants because those live on an unlanded branch; the
// itemsanity item-order test on the apworld side pins the same number.
#define AP_TIZI_MASK_ITEM_INDEX 101
// Engine held-item id for Aku Aku / the mask (VehPhysGeneral_SetHeldItem's boss
// substitution block reads 0x7 and 0x8 as Mask and Clock).
#define AP_TIZI_HELD_ITEM_MASK 0x7

// Clear both receipts. Called from the same fresh-connect / slot-reset point
// that zeroes the capability chains, before the authoritative ReceivedItems
// replay rebuilds them.
void AP_TiziReset(void);

// One receipt each. Duplicates are harmless.
void AP_TiziReceiveHelper(void);
void AP_TiziReceiveMask(void);

// Level load handed us this LEV's instance definition array. Stored, not walked:
// the crate census is resolved lazily on the first weapon-box hit, by which time
// the level's checkpoint chain is certainly up.
void AP_TiziLevelInstances(struct InstDef *defs, int count);

// Per-frame. Drops a pending forced Mask whose roll ended without resolving
// (a race restart or a level change mid-roll), so a helper box can never hand
// its Mask to a later, unrelated box.
void AP_TiziTick(struct GameTracker *gGT);

// The local player just broke `crateInst` and started an item roll. Arms the
// forced Mask when the helper is active and this crate is one of the four.
void AP_TiziOnWeaponBoxHit(struct Driver *driver, struct Instance *crateInst);

// Called with the weapon the vanilla roll produced. Returns the Mask instead
// when a helper box armed this roll, and `rolled` unchanged in every other case.
int AP_TiziFilterRoll(struct Driver *driver, int rolled);

#endif // CTR_AP
#endif // AP_TIZI_H
