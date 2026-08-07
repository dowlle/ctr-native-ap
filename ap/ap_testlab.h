#ifndef AP_TESTLAB_H
#define AP_TESTLAB_H

// Capability Test Lab: a LOCAL test harness for the completability matrix.
//
// The matrix asks, per track and per check tier (trophy win / token challenge /
// sapphire / gold / platinum), which capability configuration can complete it.
// Its cells are meant to be filled empirically, by driving, so this module lets
// the player race any track under a controlled capability configuration from the
// in-game options menu ("Test Lab" section, game/230/MM_ConfigMenu.c).
//
// ── What this is NOT ──
// This is not the item system. The toggles are read straight out of g_config
// (config.ini), never from slot_data or the received-item counters, so they work
// offline in arcade / time trial with no server in sight. When the AP item packs
// for progressive boost and progressive stats are actually built they will own
// their own state; this module stays a bench instrument and is expected to be
// left at its vanilla defaults during real play.
//
// ── Semantics ──
// The toggle semantics are taken from the settled item designs rather than
// invented for the harness, so a matrix cell measured here stays meaningful once
// the real items exist:
//
//   Boost tier (issue #12, comments 2026-07-16 and the 2026-07-26 correction)
//     VANILLA  system off, no hook runs at all
//     NONE     self-earned boost is gone: powerslide/mini-turbo, hang time, the
//              start-line rev boost, the Turbo pickup and the 10-wumpa Super
//              Engine all grant nothing. Turbo pads still work -- that is the
//              design's explicit invariant, so tracks stay drivable at the
//              bottom of the chain.
//     BOOST    everything grants again, but the boost-speed cap is clamped to
//              the linear cap's red-fire point, so no grant can reach USF speed.
//     USF      the full super-pad payload; the top of the implemented chain.
//   Super turbo pads act as NORMAL pads at every tier below USF (both the speed
//   cap and the banked reserves), which is what makes a USF-dependent jump a real
//   progression gate.
//
//   NO BLUE FIRE TIER. The 2026-07-26 correction adds an optional blue-fire
//   capstone above USF whose values come from CTR Unlimited's Retro Fueled mode.
//   Those values have not been sourced, so the tier is deliberately ABSENT from
//   the ladder rather than guessed at. When the numbers exist it appends after
//   USF as index 4 -- the ladder is index-keyed, so nothing else has to move.
//
//   Gas pedal
//     Zeroes the throttle button for the local player only. Reverse is a separate
//     path keyed off the left analog stick, and the reserves auto-throttle
//     overrides cross on its own whenever reserves are banked; neither is touched,
//     so both survive the gate exactly as they do in the shape this was verified
//     against.
//
//   Kart stats (issue #13)
//     VANILLA  the character's own stats, restored from the birth snapshot
//     FLOOR    the engine's minimum preset: per stat, the lowest value any engine
//              class holds. This is the settled bottom tier of the stat chain.
//     CUSTOM   six per-stat values set from the menu, applied as absolute
//              overrides. Matches the chain design, where an active system
//              overrides the character stat table outright and character choice
//              becomes cosmetic.
//     See AP_TestLab_Stats for which fields are covered and why.
//
// Every hook is scoped to the local player, so AI racers are never touched, and
// every hook is a no-op while the toggles sit at their vanilla defaults.
//
// ── Live apply ──
// All three toggles take effect the moment they change, including mid-race with
// the config menu open over a paused race. Boost tier and the gas pedal are read
// fresh out of g_config on every grant / every frame, so they need nothing extra.
// The stats need a base to return to, because the engine writes the stat
// constants exactly once per driver at birth: AP_TestLab_SnapshotStats captures
// that write, and switching back to VANILLA restores from it on the next frame.

#ifdef CTR_AP

// uint32_t is what the engine's u32 is typedef'd from (include/macros.h), so the
// prototypes below match the button/type masks exactly without pulling in common.h.
#include <stdint.h>

struct Driver;

// Boost-tier ladder. Index-keyed: the value persisted in config.ini is the index
// itself, and the menu ladder in game/230/MM_ConfigMenu.c uses the same order.
enum
{
	AP_TESTLAB_BOOST_VANILLA = 0,
	AP_TESTLAB_BOOST_NONE    = 1,
	AP_TESTLAB_BOOST_BOOST   = 2,
	AP_TESTLAB_BOOST_USF     = 3,
	// AP_TESTLAB_BOOST_BLUEFIRE = 4 -- see the header note; values unsourced.
	AP_TESTLAB_BOOST_COUNT   = 4
};

// Kart-stat ladder, same index-keyed shape as the boost tiers above.
enum
{
	AP_TESTLAB_STATS_VANILLA = 0,
	AP_TESTLAB_STATS_FLOOR   = 1,
	AP_TESTLAB_STATS_CUSTOM  = 2,
	AP_TESTLAB_STATS_COUNT   = 3
};

// Boost-grant filter, called at the top of VehFire_Increment (the single choke
// point every boost in the game goes through). Returns 0 to drop the grant
// entirely, or 1 to let it through, having possibly rewritten *reserves and
// *fireLevel to demote a super turbo pad to a normal one. type is the grant's
// TurboType mask and is never rewritten: a demoted super pad keeps a pad's own
// reserve-freeze behaviour.
int AP_TestLab_FireGrant(struct Driver *driver, int *reserves, uint32_t type, int *fireLevel);

// Gas-pedal gate, called from VehPhysProc_Driving_PhysLinear at the point where
// the throttle button has been read. Only cross / BTN_CROSS are cleared.
void AP_TestLab_DriveInput(struct Driver *driver, uint32_t *buttonsHeld, uint32_t *cross);

// Kart stats, called once per frame from the same driving-physics site, before
// anything reads the stat constants.
void AP_TestLab_Stats(struct Driver *driver);

// Birth-time stat capture, called at the end of VehBirth_SetConsts once the
// engine has written a driver's stat constants. Re-taken at every birth, so a
// character change is picked up rather than restored over. Runs unconditionally,
// independent of the menu rows, so the base is already there whenever the mode is
// switched mid-race.
void AP_TestLab_SnapshotStats(struct Driver *driver);

#endif // CTR_AP

#endif // AP_TESTLAB_H
