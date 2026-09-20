#ifndef AP_HIT_ENCOUNTER_H
#define AP_HIT_ENCOUNTER_H

// Hit Character encounters (global schema 16, block schema 2): the gather half.
//
// ap/ap_hit_policy.h holds the freestanding decisions; this module reads the
// parsed slot_data block, the server's checked-location state and the engine's
// dispatch facts, then calls those decisions. It is deliberately free of engine
// structs so a production host harness can link it with stubs and drive the REAL
// eligibility reconstruction, roster selection, extras planning and damage
// attribution.
//
// Compiled only under CTR_AP, like the rest of ap/.

#ifdef CTR_AP

#include "ap_seedcfg.h" // ctr_hit_order

#ifdef __cplusplus
extern "C" {
#endif

// Is the feature active this seed? False when slot_data is inactive, the block
// was absent/disabled/refused, or the feature scalar was off.
int AP_HitEncounterEnabled(void);

// The parsed draw order (16 engine ids) for a destination: ordinary tracks
// 0..17 and cups 100..104. NULL when the feature is off or the destination is
// unsupported.
const int *AP_HitEncounterOrder(int destLevelID);

// Should the ordinary roster be replaced by the encounter field for this load?
// Single-player ordinary Adventure races on the sixteen retail tracks (0..15)
// and the two trial Trophy tracks (16/17): the Trophy Race AND that track's CTR
// Challenge (TOKEN_RACE), which is the same driver load with one extra mode bit.
// Boss/cup/arcade/relic/crystal and multiplayer loads return 0. `isAdventure` is
// ADVENTURE_MODE. Custom-served loads are excluded at the call site.
int AP_HitEncounterShouldApply(int isAdventure, int destLevelID, int isCup,
                               int isBoss, int isArcade, int isRelic,
                               int isToken, int isCrystal, int numPlayers);

// Is one guest's authoritative trigger satisfied? Defaults (0..7) always are;
// guests (8..15) need one of their parsed any_of win codes checked on the
// server. Reconstructed live from checked state, so a race load after a win (or
// after a reconnect/reload) sees the new eligibility.
int AP_HitEncounterGuestEligible(int guest);

// Fill eligible[16] (defaults always, guests once an unlock win is checked) and
// unchecked[16] (Hit location in the seed and not checked on the server) from
// the parsed block and the server's checked set. Returns 0 (all zero) when the
// feature is off.
int AP_HitEncounterGather(unsigned char *eligible, unsigned char *unchecked);

// The Hit opportunity for a destination, or -1: the lowest eligible engine id
// other than `player` with an unchecked Hit (AP_HitOpportunityPure). `player`
// is the EFFECTIVE player engine id (after any racer lock). -1 for a
// destination without an order.
int AP_HitEncounterOpportunity(int destLevelID, int player);

// Draw one FRESH field at `destLevelID` with that destination's cursors and
// advance them (AP_HitDrawFieldPure). `player` is the effective player;
// `aiSeats` is 7 (ordinary) or 4 (retail Purple cup). Writes engine ids into
// `outIDs` and returns the count. Never seats the player.
int AP_HitEncounterDrawFresh(int destLevelID, int player, int aiSeats, int *outIDs);

// Diagnostics: the destination's three cursors and its fresh-draw count.
int AP_HitEncounterDrawState(int destLevelID, int *outCursors3, unsigned *outDraws);

// Clear every cursor, draw count and snapshot (new seed identity; harnesses).
void AP_HitEncounterResetDrawState(void);

// Called at the top of LOAD_DriverMPK for EVERY driver load. Latches whether
// the previous load was an ordinary apply load, which is what lets a restart
// or retry reuse its field.
void AP_HitLoadBegin(void);

// The field for an ordinary apply load at `destLevelID`: reuses the stored
// field when the immediately previous driver load was an ordinary apply for
// the same level, player and seat count (a restart/retry), else draws fresh.
// *outFresh (may be NULL) reports which. Returns the count.
int AP_HitRaceField(int destLevelID, int player, int aiSeats, int *outIDs, int *outFresh);

// Plan BI_RACERMODELHI extras for a selected field. Returns the number needed
// (may exceed `cap`, so the caller can refuse a required load it cannot fit).
int AP_HitEncounterExtras(const int *selected, int selectedCount, int player,
                          int *outExtras, int cap);

// ── Gem Cup roster snapshot (ticket 11) ─────────────────────────────────────
// The AI seat count for a cup: four in the retail Purple cup (cupID 4), seven
// otherwise. Matches MainInit's field count.
int AP_HitEncounterCupFieldSize(int cupID);

// Mark a NEW cup start (the adventure cup pad entry). The next cup load draws a
// fresh roster from current eligibility with the cup's cursors.
void AP_HitCupSnapshotBegin(int cupID);

// Clear the snapshot (fresh seed/connect).
void AP_HitCupSnapshotReset(void);

// Resolve/reuse the frozen cup roster for this load. A NEW cup resolves fresh;
// every continuing leg and same-session retry copies the stored roster
// unchanged, so a mid-cup unlock cannot alter the active cup. Writes the AI ids
// into outIDs and returns the count.
int AP_HitCupSnapshotField(int cupID, int trackIndex, int player, int aiSeats,
                           int *outIDs);

// Dispatch decision for one accepted BOTS_ChangeState event. `flags`:
//   bit0 victimIsAI, bit1 victimLive, bit2 victimIsGhost, bit3 victimIsPlayer,
//   bit4 attackerPresent, bit5 attackerIsLocalP1, bit6 attackerIsAI,
//   bit7 raceSupported, bit8 wasDamageActive.
// Returns 1 when a Hit check was emitted (or already settled), 0 otherwise.
int AP_HitEncounterOnDamage(int victimEngineID, int damageType, unsigned flags);

// Clear the per-session "already emitted" mask on a fresh slot-connect, so the
// held-check/reconnect path can re-send anything the server has not confirmed.
void AP_HitEncounterConnectReset(void);

// BOTS-side gather (implemented in ap/ap_hit_bots.c). BOTS_ChangeState calls
// this after its accepted-damage switch. Extracted from BOTS.c so a host harness
// can link the REAL function with stubbed engine globals.
struct Driver;
void AP_HitBotsVictim(struct Driver *victim, int damageType, struct Driver *attacker,
                      int wasDamageActive);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CTR_AP
#endif // AP_HIT_ENCOUNTER_H
