#ifndef AP_BOXES_H
#define AP_BOXES_H

// AP item boxes (#109): the runtime half. Authored placements become real,
// breakable crates that send a location check.
//
// RUNTIME SEMANTICS, ruled 2026-08-10 13:46/13:49 CEST:
//   1. Visibility follows the seed. A box location that is in the seed has a
//      crate standing on the track.
//   2. Box-author mode (#182) always shows the FULL authored set regardless of
//      seed state. That is exactly what ap_author.c already does, so this
//      module stands down entirely while author mode is on rather than
//      spawning a second crate inside every authored one.
//   3. PLAYER-break only. The local player breaking a box sends its check.
//   4. AI pass-through: bots drive straight through with no reaction at all.
//   5. No local pickup: the check and the item feed line are the whole effect.
//      No weapon roll, no wumpa, no relic time.
//   6. A broken box is gone for the rest of the seed, across reconnects: the
//      spawn set is rebuilt from AP checked-location state (server truth), not
//      from anything this client remembers.
//   7. The vanilla crate break animation plays on the break.
//   8. Boxes spawn in every race type INCLUDING relic races.
//   9. GEM CUP LEGS FOLLOW PHYSICAL-PAD ACCESS (ruled 2026-08-24 10:51 CEST,
//      WO-A3). Access to a Gem Cup grants NO AP-box logic. A leg shows,
//      collides with and dispatches its authored boxes only while the
//      corresponding INDIVIDUAL race is accessible now through its randomized
//      physical pad, so one cup may mix legs with boxes and legs without them.
//      Rule 8 is unchanged for every non-cup race. The decision is
//      ap_cup_box_policy.h; ap_boxes.c does the gather and applies it BEFORE
//      the live set is built, so visuals, collision and dispatch cannot
//      diverge.
//
// HOW COLLISION IS DONE, and why it is not the BSP.
//
// The blocker recorded against #109 was BSP hitbox injection. Vanilla pickups
// are collided through the level's baked BSP: CollMoved_PlayerSearch_RunHitboxLInC
// (COLL.c:2239) reaches a hitbox BSP node's InstDef, and only then finds the
// model's LInC callback. A runtime-born instance has no InstDef and no BSP node,
// so that path can never see it, and injecting nodes into a baked BSP tree is
// the expensive, risky half nobody wanted to build.
//
// It is also not needed. The engine already collides runtime objects that are
// not in the BSP, with a per-frame proximity walk over a thread bucket:
// LinkedCollide_Radius (LinkedCollide.c:6) is how the seal (RB_Seal.c:20), the
// spider (RB_Spider.c:281) and the minecart (RB_Minecart.c:15) find drivers.
// This module uses that same call, and the bucket split does the #14/#15
// player-only isolation for free: human drivers are born into threadBuckets
// [PLAYER] (VehBirth.c:724, bucket 0) and AI drivers into [ROBOT] (BOTS.c:3097).
// Walking PLAYER only means an AI can never break a box and never reacts to one.
//
// What this deliberately does NOT give a box: physical blockage. You drive
// through an AP box, you do not bounce off it. Vanilla crates get that from the
// BSP hitbox, and a box is a check, not an obstacle.
//
// RELIC-RACE COEXISTENCE with the vanilla crate strip is by construction, with
// no engine edit. The weapon crate strip (INSTANCE.c:380-427) walks the LEV's
// InstDef array and clears DRAW_COLLISION_MASK on PU_FRUIT_CRATE /
// PU_RANDOM_CRATE in TIME_TRIAL and RELIC_RACE. Our boxes are born through
// INSTANCE_Birth3D and never appear in that array, so the strip cannot reach
// them. The same loop is the only place gGT->timeCratesInLEV is incremented,
// and nothing here touches it, so the relic clock economy is untouched.
//
// That reasoning covers only ONE of the two gates a box has to clear. The
// OTHER is AP_BoxesRaceCarriesBoxes (ap_boxes.c), which decides whether the
// whole set builder runs at all, and it originally tested ADVENTURE_MODE
// alone on the unverified assumption that relic races fold into it. They do
// not: trial tracks (whose primary mode IS relic races) spawned zero boxes in
// a live session, confirmed root cause 2026-08-12 22:36. RELIC_RACE is now
// explicit in that gate; see the comment there.
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

#include "ap_box_map.h" // the code block, the LevelID -> track derivation, the set builder

struct GameTracker;
struct Instance;
struct Driver;

// How close the local player's kart has to get, in LEV world units. The engine's
// own driver hit radius is 0x40 (THREAD_DRIVER_HIT_RADIUS, namespace_Proc.h:60),
// so 0x60 is the kart plus roughly half a crate -- and the AP box renders at
// EXACTLY retail crate size (AP_BoxModel_DeriveScale, ap_box_model.c; ruled
// 2026-08-21), so the crate number is the box number. LinkedCollide_Radius
// takes the SQUARED value despite its parameter name, which is why the square
// is derived here rather than written out (Lessons Learned §5).
#define AP_BOX_HIT_RADIUS         0x60
#define AP_BOX_HIT_RADIUS_SQUARED (AP_BOX_HIT_RADIUS * AP_BOX_HIT_RADIUS)

// Per-frame. Called from AP_OnFrame (ap_hooks.c) AFTER AP_Author_OnFrame and
// BEFORE AP_Spawn_OnFrame, so a box added this frame is born in the same frame
// and author mode has already decided whether it owns the markers.
void AP_Boxes_OnFrame(struct GameTracker *gGT);

// How many AP boxes are standing on the current level. Diagnostics only.
int AP_Boxes_LiveCount(void);

// A moving weapon (bomb, missile, thrown shield) has just detonated (issue
// #234, dispatcher task #18). Called from RB_Burst_Init (game/231/RB_Burst.c),
// the single choke point for every moving-explosive detonation, right after
// it derives its own blast radius -- so `radius` here is that same value
// (sps->Input1.hitRadius, LINEAR, not squared), not a restated one (Lessons
// Learned §5). `weaponInst` gives the blast's position; `attacker` is the
// TrackerWeapon's driverParent, "who shot me". Breaks every live AP box
// within radius, but ONLY when `attacker` is the local human player: an
// opponent-caused explosion (or a bot's) does not break AP boxes, mirroring
// the existing kart-contact rule.
//
// `radius` is linear, not squared, so the proximity test (AP_BoxMap_WithinRadius,
// ap_box_map.h) can early-out per axis before any squaring -- a plain
// dx*dx+dy*dy+dz*dz in a 32-bit int overflows for real LEV-scale positions on
// a large track (2026-08-13 REJECT, see the review note).
void AP_Boxes_OnWeaponExplode(struct GameTracker *gGT, struct Instance *weaponInst,
                               struct Driver *attacker, int radius);

#endif // CTR_AP
#endif // AP_BOXES_H
