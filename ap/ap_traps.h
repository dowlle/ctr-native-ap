#ifndef AP_TRAPS_H
#define AP_TRAPS_H

// Archipelago TRAP framework for CTR Native.
//
// Compiled ONLY when CTR_AP is defined (CMake: -DCTR_AP=ON), same as the rest of
// the AP layer. All trap logic lives in this module so the upstream ctr-native
// diff stays minimal: the engine calls a handful of AP_Trap* hooks at existing
// physics/input/camera sites (each guarded by #ifdef CTR_AP) and everything else
// stays here. Mirrors the ap_hooks.c convention.
//
// ── Where the rules live (#280) ──
// The shipped one-size lifecycle is gone. It primed every out-of-race receipt
// silently, waited for a racing lap, rolled a 0.5 to 8 second delay and ran every
// effect for a flat 20 seconds. The trap rework design notebook (2026-08-19) rules
// that timing belongs to the effect, so scheduling now comes from a per-effect
// descriptor table in ap/ap_trap_sched_logic.h, which is pure and is driven
// directly by tools/test-trap-scheduler.c on the host.
//
// This module is the ENGINE HALF of that split. It observes the world once per
// frame into an AP_TrapWorld, hands it to the scheduler, turns the scheduler's
// events into log lines and on-screen presentation, and applies whichever effects
// the scheduler reports active at the physics, input and camera call sites. It
// decides nothing about timing, eligibility, duplicates or families.
//
// SCOPE NOTE: every implemented effect targets the LOCAL PLAYER (drivers[0]) only.
// Icy road and low gravity are global across the whole track surface for that
// player, matching CTR Unlimited's Icy Tracks / Moon Gravity feel, but they do not
// touch the AI racers.

#ifdef CTR_AP

struct GameTracker;
struct Driver;
struct GamepadBuffer;
struct InstDef;
struct PushBuffer;

// Effect ids, the descriptor table and the scheduler itself.
#include "ap_trap_sched_logic.h"
// Pure observation predicates: held-item slot classification, engine-natural
// completion for the two inventory traps, and the Warpball Ambush lead timer.
#include "ap_trap_observe_logic.h"

// ── AP item pipeline seam ──
// Arm one trap by EFFECT id. The apworld owns the item id space; ap_hooks.c
// resolves a received item to an effect through the explicit table in
// ap_trap_items.h (the trap ids are three separate runs, so no arithmetic can
// derive an effect from an index). DO NOT invent AP item ids here.
//
// An effect id whose descriptor is still a wave 2 scaffold is accepted, armed and
// reported once. It is never fired and never consumed, so a seed built against a
// newer apworld cannot lose a trap or crash this client.
void AP_TrapReceive(int effect);

// ── Connect / slot-swap reset ──
// Drop every armed and every active trap. Called from the fresh-connect reset
// block in ap_hooks.c, alongside AP_FeedConnectReset and the received-item tally
// reset, and for the same reason: session state must not cross a connection.
//
// FOUND LIVE 2026-08-11 (Bandi-slot session): an armed-but-unfired trap survived a
// reconnect onto a DIFFERENT slot and fired there. An Icy Road sent to Appie's slot
// from a Blizzard Bluff Finish-Any went off during Bandi's Slide Coliseum relic
// race, on a slot the server log proves was never sent a trap.
//
// An ACTIVE trap is dropped too, not left to run down: its effect belongs to the
// previous connection just as much as an armed one does. The camera restore latch
// is deliberately left standing, because this runs off the network path with no
// GameTracker in hand; the next AP_TrapTick hands the camera back.
void AP_Trap_ConnectReset(void);

// ── Per-frame driver ──
// Called once per frame from AP_OnFrame (ap_hooks.c), BEFORE gamepad processing
// and the vehicle/camera update (MainMain.c:323). Observes the world, steps the
// scheduler, drains its events and owns the forced camera. gGT is the live
// GameTracker.
void AP_TrapTick(struct GameTracker *gGT);

// Poll debug keybinds (Numpad 1..6) to test-fire traps without an AP server. Called
// from AP_OnFrame. See ap_traps.c for the key map. Dead unless ap-config.txt sets
// dev_keys=1.
void AP_TrapDebugKeys(void);

// Parse one ap-config.txt line for a trap test trigger (prefix "debug_trap=").
// Called from AP_ReadConfig (ap_hooks.c) for each config line. Recognised values:
//   icy | lowgrav | usf | boost | fp | empty | weakened | blocker | wireframe
//   nitro | potion | upside | mirror | warpball | all
// arms that trap at connect,
// so it runs its real schedule on the next eligible map. Unknown values are ignored.
// Returns 1 if the line was consumed, 0 otherwise.
int AP_TrapConfigLine(const char *line);

// ── Engine physics/input call-sites (all invoked under #ifdef CTR_AP) ──
// Signatures are unchanged from the pre-rework framework, so the engine-side diff
// stays exactly where it already is.

// VehPhysForce_OnGravity (VehPhysForce.c, after the per-quad low-gravity block):
//   gravityY = AP_TrapGravity(driver, gravityY);
// Returns gravityY scaled down while Low Gravity is active on the local player;
// returns it unchanged otherwise. The reduced gravity is allowed to produce its
// natural hang-time boost, which the ruling explicitly keeps.
// Nonzero while the First Person trap owns cameraMode (see ap_democam.c).
int AP_TrapOwnsCamera(void);

// Diagnostics only (ap-state.json `transition.diag`): how many scheduler slots
// sit in ARMED, WARNING and ACTIVE, and how many ACTIVE slots are suspended by a
// scripted sequence. Each out-pointer may be NULL.
void AP_TrapDiagCounts(int *armed, int *warning, int *active, int *suspended);

int AP_TrapGravity(struct Driver *driver, int gravityY);

// VehPhysForce_OnGravity (VehPhysForce.c, right after the two friction scalars are
// time-scaled): AP_TrapFriction(driver, &perpendicularFriction, &forwardFriction);
// Scales both grip coefficients down IN PLACE while Icy Road is active on the
// local player, across every driving surface.
void AP_TrapFriction(struct Driver *driver, int *perpendicularFriction, int *forwardFriction);

// VehPhysGeneral_GetBaseSpeed (VehPhysGeneral.c, immediately before the
// `if (driver->reserves != 0)` boost-speed add): AP_TrapForceBoost(driver);
// While Forced USF or Forced Boost is active on the local player, FLOORS
// driver->reserves and driver->fireSpeedCap. Floors, never pins: a player already
// above the trap's tier keeps what they had. No-op otherwise.
void AP_TrapForceBoost(struct Driver *driver);

// VehPhysProc_Driving_PhysLinear (VehPhysProc.c, right after `square`/`cross` are
// read from the pad): AP_TrapDriveInput(driver, ptrgamepad, &buttonsHeld, &cross, &square);
// Applies the control half of the two boost-control traps to this frame's input:
// Forced USF suppresses braking and analog reverse AND forces throttle; Forced
// Boost suppresses braking only and leaves the throttle to the player. Both
// restore reverse once the kart has been nearly stationary for about a second, so
// the trap cannot leave a kart permanently stuck. No-op otherwise.
// Applies the control half of the two boost-control traps, Reverse Steering's
// mirrored steering axis and Forced Use's injected weapon press to this frame's
// input. `buttonsTapped` joined the signature for Forced Use: the weapon path
// fires on a tapped circle, not a held one.
void AP_TrapDriveInput(struct Driver *driver, struct GamepadBuffer *pad,
                       u32 *buttonsHeld, u32 *buttonsTapped, u32 *cross, u32 *square);

// INSTANCE.c hands over the finished LEV instance table once per load, after
// every entry's ptrInstance is filled. Empty Crates' eligibility predicate needs
// it: crates have no thread until a driver first touches one, so the instance
// table is the only place a map's crates can be counted before the race starts.
void AP_TrapLevelInstances(struct InstDef *defs, int count);

// Drop that cached table. Called from the dev savestate restore path
// (platform/native_checkpoint.c), which rewrites the mempack arena the table
// points into without relocating this cache. The next census then reports no
// eligible crates until a real level load republishes the table, which keeps
// Empty Crates armed rather than reading relocated memory.
void AP_TrapForgetLevelInstances(void);

// Called after an ordinary weapon or Wumpa crate has broken. Nonzero suppresses
// only its reward for the local trap receiver. Other crate/object classes never
// enter this hook.
int AP_TrapSuppressCrateReward(struct Driver *driver);

// VehFire_Increment's first gate. Returns zero while Boost Blocker owns the local
// kart, suppressing pads, powerslides, hang time, rev boosts and item boosts at
// their shared choke point. AI and non-local drivers always pass through.
int AP_TrapAllowBoostGrant(struct Driver *driver);

// Capability-layer seams for Weakened Kart. The tier helper returns an effective
// Progressive Boost tier with one temporary downgrade while active; -1 means the
// pack is off and is treated as vanilla USF capability for the downgrade only.
int AP_TrapWeakenedActive(struct Driver *driver);
int AP_TrapWeakenBoostTier(int permanentTier);

// PushBuffer_SetMatrixVP calls this after building the gameplay view-projection
// matrix. It transforms 3D projection rows only, leaving screen-space HUD draws
// on their normal path.
void AP_TrapRenderTransform(struct PushBuffer *pb);

// Item Reroll's exclusion, read by the roll filter in ap_hooks.c at the moment
// VehPhysGeneral_SetHeldItem settles a roll. Returns the held id the trap threw
// away, or -1 when no reroll is in flight and the filter is inert.
int AP_TrapRerollExcludedItem(void);

#endif // CTR_AP

#endif // AP_TRAPS_H
