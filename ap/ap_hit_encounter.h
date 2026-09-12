#ifndef AP_HIT_ENCOUNTER_H
#define AP_HIT_ENCOUNTER_H

// Hit Character encounters (schema 14, ticket 06): the gather half.
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

#include "ap_seedcfg.h" // ctr_hit_candidates

#ifdef __cplusplus
extern "C" {
#endif

// Is the feature active this seed? False when slot_data is inactive, the block
// was absent/disabled/refused, or the feature scalar was off.
int AP_HitEncounterEnabled(void);

// The parsed candidate lists for a destination: ordinary tracks 0..17 and cups
// 100..104. NULL when the feature is off or the destination is unsupported.
const ctr_hit_candidates *AP_HitEncounterCandidates(int destLevelID);

// Should the ordinary roster be replaced by the encounter field for this load?
// Single-player ordinary Adventure Trophy races only: the sixteen retail tracks
// (0..15) and the two trial Trophy tracks (16/17). Boss/cup/arcade/relic/token/
// crystal and multiplayer loads return 0. `isAdventure` is ADVENTURE_MODE.
// Custom-served loads are excluded at the call site.
int AP_HitEncounterShouldApply(int isAdventure, int destLevelID, int isCup,
                               int isBoss, int isArcade, int isRelic,
                               int isToken, int isCrystal, int numPlayers);

// Is one guest's authoritative trigger satisfied? Defaults (0..7) always are;
// guests (8..15) need one of their parsed any_of win codes checked on the
// server. Reconstructed live from checked state, so a race load after a win (or
// after a reconnect/reload) sees the new eligibility.
int AP_HitEncounterGuestEligible(int guest);

// The eligible unchecked guest opportunity for a destination, or -1. `player`
// is the EFFECTIVE player engine id (after any racer lock).
int AP_HitEncounterOpportunity(int destLevelID, int player);

// Select the AI field for an ordinary race at `destLevelID`. `player` is the
// effective player; `aiSeats` is 7 (ordinary) or 4 (retail Purple cup). Writes
// engine ids into `outIDs` and returns the count. Never seats the player.
int AP_HitEncounterBuildField(int destLevelID, int player, int aiSeats, int *outIDs);

// Plan BI_RACERMODELHI extras for a selected field. Returns the number needed
// (may exceed `cap`, so the caller can refuse a required load it cannot fit).
int AP_HitEncounterExtras(const int *selected, int selectedCount, int player,
                          int *outExtras, int cap);

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
