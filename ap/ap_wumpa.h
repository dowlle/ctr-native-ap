#ifndef AP_WUMPA_H
#define AP_WUMPA_H

// Archipelago Wumpa Fruit filler grant for CTR Native. Issue #11.
//
// Compiled ONLY when CTR_AP is defined, same as the rest of the AP layer. The
// apworld pool includes a "Wumpa Fruit" filler item (item index 15). On the rando
// ROM it did nothing native-side; this module makes a received Wumpa Fruit actually
// hand the local player fruit in a race, so even filler feels like getting
// something. Purely cosmetic/QoL: the fruit is the vanilla speed pickup, capped at
// the vanilla max of 10, wiped at race end like any race's fruit. It touches no
// AdvProgress bit, no gate, no location -- zero randomizer logic impact.
//
// ── Design: bank-then-grant ── (mirrors the trap framework's receive/tick split)
//   AP_WumpaReceive(n) -> add n to a pending bank (does NOT touch the driver).
//   AP_WumpaTick()     -> per frame: once we are in an ACTIVE race (post-countdown)
//                         with a valid local driver and pending > 0, hand the fruit
//                         to drivers[0] via the engine's own RB_Player_ModifyWumpa
//                         (which caps at 10, plays the "juiced up" jingle at 10, and
//                         respects the unlimited-wumpa cheat), then clear the bank.
//
// TIMING SEMANTICS (documented for the PR):
//   - Received while NOT racing (hub/menus): banked, then granted at the start of
//     the next race (the first frame the race is active, after the countdown and
//     after VehBirth has zeroed the kart's fruit).
//   - Received DURING a race: granted live on the next frame.
// A single bank drains in one RB_Player_ModifyWumpa call, so N banked fruit is one
// grant of +N (still capped at 10). The bank itself is capped at 10 since a kart
// can never hold more, and it resets each race with the fruit, so there is no
// cross-race hoard.

#ifdef CTR_AP

struct GameTracker;

// A Wumpa Fruit filler item was received (count >= 1). Adds to the pending bank;
// the actual grant happens in AP_WumpaTick during an active race. Called from the
// received-item drain loop in ap_hooks.c (AP_NetTick).
void AP_WumpaReceive(int count);

// Per-frame driver. Called once per frame from AP_OnFrame (ap_hooks.c), alongside
// AP_TrapTick. Drains the pending bank into drivers[0] when a race is active. gGT
// is the live GameTracker; no-op outside a race or with nothing pending.
void AP_WumpaTick(struct GameTracker *gGT);

#endif // CTR_AP

#endif // AP_WUMPA_H
