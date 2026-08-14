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
// ── Design: the priming rule ──
// A trap received OUTSIDE a race does NOT fire immediately. It PRIMES silently and
// only FIRES during an active race on LAP 2 OR LAP 3, at a random moment after
// priming. This is shared framework behaviour for the whole trap family (not
// per-trap), so every effect below inherits the same lifecycle:
//
//   AP_TrapReceive(id)  -> a registry slot goes PRIMED (armed, hidden from player)
//   AP_TrapTick()       -> per frame: once we are racing on lap 2/3, roll a random
//                          delay; when it elapses the slot goes FIRING (effect on,
//                          timed); when the duration elapses the slot CLEARS.
//
// ── The mid-race rule (RULED 2026-08-13, superseding the 08-11 instant fire) ──
// A trap that ARRIVES while a race is already running fires after a SHORT FIXED
// DELAY (AP_TRAP_MIDRACE_DELAY_MS, ap_trap_sched.h), measured from the receipt and
// with no lap gate. Itemsanity boxes (#109) deliver traps mid-race, and a trap
// earned by breaking a box on lap 1 that then sits silent until lap 2 of some LATER
// race reads as a bug, not as suspense -- so the lap deferral is not the answer.
// Neither is the same-frame instant fire that replaced it in ed4fe2b8b: it landed
// the effect inside the item-drain frame section and was root-caused as the
// regression behind two live freeze/crash reports on 2026-08-12. Outside a race the
// primed-then-next-race behaviour above is unchanged, which is still every trap
// that arrives from a menu, a hub or a ceremony.
//
// ── The apply rule ──
// Every effect is applied ONLY on a frame that is safe to mutate: on a live track,
// level loaded, countdown observed and finished, the local driver born, and no
// menu / cutscene / end-of-race / pause in progress. An apply that cannot proceed
// safely WAITS for the next tick -- it is never forced through and never dropped
// silently. Boss races are deliberately inside this window, not excluded from it.
// ap_trap_sched.h owns the schedule; ap_traps.c owns what "safe" means here.
//
// The effects themselves are applied at engine call-sites: the environmental ones
// (icy road, low gravity) scale physics scalars in VehPhysForce; the control ones
// (USF-no-brake, boost) pin driver speed/suppress braking in VehPhysGeneral /
// VehPhysProc; the first-person trap drives the camera mode from AP_TrapTick.
//
// SCOPE NOTE: all five effects target the LOCAL PLAYER (drivers[0]) only -- as a
// TRAP it should afflict the receiver. "Global low-friction / low-gravity" is
// global across the whole TRACK SURFACE for that player (every quad turns icy /
// floaty), matching CTR Unlimited's Icy Tracks / Moon Gravity feel, but it does
// not touch the AI racers. Making icy/low-grav affect every racer instead is a
// one-line change (drop the AP_TrapIsLocal() guard in the two VehPhysForce hooks).

#ifdef CTR_AP

struct GameTracker;
struct Driver;
struct GamepadBuffer;

// ── Effect ids ──
// The v1 trap set. These are the
// EFFECT ids, internal to native. The AP ITEM ids that map to them are owned by
// the apworld and wired later -- see AP_TrapReceive's TODO seam.
enum AP_TrapEffect
{
	AP_TRAP_ICY = 0,       // global low road friction (Unlimited "Icy Tracks")
	AP_TRAP_LOWGRAV,       // low gravity            (Unlimited "Moon Gravity")
	AP_TRAP_USF_NOBRAKE,   // forced Ultra Sacred Fire top speed, braking disabled
	AP_TRAP_BOOST,         // forced (milder) boost/fire, braking still works
	AP_TRAP_FIRSTPERSON,   // forced first-person / hood camera
	AP_TRAP_COUNT
};

// ── AP item pipeline seam ──
// Prime a trap by EFFECT id (0..AP_TRAP_COUNT-1). Idempotent-ish: adds one primed
// instance to the registry (capacity AP_TRAP_REGISTRY_CAP; over capacity is
// dropped with a log line). Called by whatever surfaces AP items.
//
// Wired: the apworld emits 5 trap items directly after Wumpa Fruit, one per
// AP_TrapEffect in enum order, and ap_hooks.c's received-item loop maps
// item index (AP_TRAP_ITEM_FIRST_INDEX + effect) to AP_TrapReceive(effect).
// DO NOT invent AP item ids here -- the apworld owns that space (slot_data
// contract). This function stays the single native entry point the pipeline
// calls.
void AP_TrapReceive(int effect);

// ── Connect / slot-swap reset ──
// Drop every registry instance and every firing flag. Called from the fresh-connect
// reset block in ap_hooks.c, alongside AP_FeedConnectReset and the received-item
// tally reset, and for exactly the same reason: session state must not cross a
// connection.
//
// FOUND LIVE 2026-08-11 (Bandi-slot session): a PRIMED-but-unfired trap survived a
// reconnect onto a DIFFERENT slot and fired there. An Icy Road Trap sent to
// Appie's slot from a Blizzard Bluff Finish-Any went off during Bandi's Slide
// Coliseum relic race, on a slot the server log proves was never sent a trap. The
// registry is per-session state keyed to nothing, so a reconnect has to clear it.
//
// A FIRING trap is cleared too, not left to run down: its effect belongs to the
// previous connection just as much as a primed one does. The first-person camera
// is restored on the next AP_TrapTick -- the restore latch is deliberately left
// standing here, because this runs off the network path with no GameTracker in
// hand and writing the camera from there is not a thing this module does.
void AP_Trap_ConnectReset(void);

// ── Per-frame driver ──
// Called once per frame from AP_OnFrame (ap_hooks.c), BEFORE gamepad processing
// and the vehicle/camera update (MainMain.c:323). Advances every registry slot
// through prime -> fire -> clear, and applies the first-person camera each frame
// while that effect is FIRING. gGT is the live GameTracker.
void AP_TrapTick(struct GameTracker *gGT);

// Poll debug keybinds (Numpad 1..6) to DELIVER traps without an AP server, through
// the same AP_TrapReceive the item pipeline uses -- press one mid-race and it takes
// the mid-race delay, press one in the hub and it primes. No key bypasses the
// schedule any more. Called from AP_OnFrame. See ap_traps.c for the key map. No-op
// in a normal session (the keys are not in the gameplay input map, and the whole
// poll is dead unless ap-config.txt sets dev_keys=1).
void AP_TrapDebugKeys(void);

// Parse one ap-config.txt line for a trap test trigger (prefix "debug_trap=").
// Called from AP_ReadConfig (ap_hooks.c) for each config line. Recognised values:
//   icy | lowgrav | usf | boost | fp | all  -> PRIMES that trap at connect, so it
// fires on lap 2/3 of the next race (exercises the real priming path). Unknown
// values are ignored. Returns 1 if the line was consumed, 0 otherwise.
int AP_TrapConfigLine(const char *line);

// ── Engine physics/input call-sites (all invoked under #ifdef CTR_AP) ──

// VehPhysForce_OnGravity (VehPhysForce.c, after the per-quad low-gravity block):
//   gravityY = AP_TrapGravity(driver, gravityY);
// Returns gravityY scaled down while the low-gravity trap is FIRING on the local
// player; returns it unchanged otherwise. Pure scalar transform, no side effects.
int AP_TrapGravity(struct Driver *driver, int gravityY);

// VehPhysForce_OnGravity (VehPhysForce.c, right after the two friction scalars are
// time-scaled): AP_TrapFriction(driver, &perpendicularFriction, &forwardFriction);
// Scales both grip coefficients down IN PLACE while the icy-road trap is FIRING on
// the local player; leaves them untouched otherwise.
void AP_TrapFriction(struct Driver *driver, int *perpendicularFriction, int *forwardFriction);

// VehPhysGeneral_GetBaseSpeed (VehPhysGeneral.c, immediately before the
// `if (driver->reserves != 0)` boost-speed add): AP_TrapForceBoost(driver);
// While the boost or USF-no-brake trap is FIRING on the local player, FLOORS
// driver->reserves (only raised to the floor when below it, so banked reserves
// survive) and FLOORS driver->fireSpeedCap (USF -> a true USF-tier cap computed
// via the VehFire.c fire-level formula at super-turbo-pad fireLevel, boost ->
// const_SingleTurboSpeed; never downgrades a higher cap). No-op otherwise.
void AP_TrapForceBoost(struct Driver *driver);

// VehPhysProc_Driving_PhysLinear (VehPhysProc.c, right after `square`/`cross` are
// read from the pad): AP_TrapDriveInput(driver, ptrgamepad, &buttonsHeld, &cross, &square);
// While the USF-no-brake trap is FIRING on the local player, suppresses braking:
// clears the brake button from the local button vars AND neutralises the pad's
// analog sticks (so the stick-pull-back reverse/brake path also goes dead), and
// forces the throttle button on. No-op otherwise. Mutates only this frame's input.
void AP_TrapDriveInput(struct Driver *driver, struct GamepadBuffer *pad,
                       int *buttonsHeld, int *cross, int *square);

#endif // CTR_AP

#endif // AP_TRAPS_H
