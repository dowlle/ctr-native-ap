#ifndef AP_WUMPA_H
#define AP_WUMPA_H

// Archipelago Wumpa Fruit, starting fruit and the Wumpa Shop wallet.
//
// With a shop or Wumpa goal active, Adventure pickups and in-race AP receipts
// fill the kart to ten, then bank overflow immediately. Hits remove held fruit
// only. Outside-race AP receipts bank in full after their effect marker write.
// Without banking, the fallback received-fruit grant waits for a race and caps
// at ten. Arcade, Time Trial, Battle and the unlimited-fruit cheat cannot farm
// race fruit into the wallet.
//
// AP_WumpaReceive queues in-race fruit without touching the driver. The tick
// grants it on an active, unpaused post-countdown frame through the engine's
// RB_Player_ModifyWumpa, preserving its Juiced jingle and mask protection.
// Pending paused/countdown receipts bank in full if that attempt is left.
//
// A finish banks the remaining held fruit once. Exit, restart and a received
// DeathLink loss bank held fruit minus the attempt's starting fruit, never
// below zero. Overflow is already paid and never enters those payouts.
// Progressive Starting Wumpa fills free kart space only and cannot create
// overflow. Each restart observes a fresh countdown and reapplies that ladder.
// Load stages are boundaries even when the race overlay stays loaded.

#ifdef CTR_AP

struct GameTracker;

// A Wumpa Fruit filler item was received (count >= 1). Queues an in-race grant
// or an outside-race wallet credit; called by the received-item effect pipeline.
void AP_WumpaReceive(int count);

// Rebuild the persistent starting-fruit ladder from ReceivedItems. Called once
// per Progressive Starting Wumpa receipt after AP_WumpaConnectReset zeroes it.
void AP_WumpaReceiveStarting(void);

// Clear slot-scoped state before the server's authoritative item replay. A
// reconnect during a live race does not reapply starting fruit to that race;
// the next off-track/race-start transition arms it normally.
void AP_WumpaConnectReset(void);

// Per-frame driver. Called once per frame from AP_OnFrame (ap_hooks.c), alongside
// AP_TrapTick. Grants pending race fruit to drivers[0] in the active window and
// handles countdown, finish and leave edges.
void AP_WumpaTick(struct GameTracker *gGT);

// #249 banking. AP_WumpaRaceModify is called from RB_Player_ModifyWumpa for
// gains and losses; it updates held fruit and immediately banks overflow above
// ten. The finish edge banks only remaining held fruit. Exit, restart and a
// forced DeathLink loss take held fruit minus starting fruit through the same
// once-per-attempt latch. See the contract above.
// Declared in ap_hooks.h, where RB_Player.c and the race listener see them.

#endif // CTR_AP

#endif // AP_WUMPA_H
