#ifndef AP_BOXES_H
#define AP_BOXES_H

// AP item boxes (#109): the runtime half. Authored placements become real,
// breakable crates that send a location check.
//
// RUNTIME SEMANTICS, ruled by Stef 2026-08-10 13:46/13:49 CEST:
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

// How close the local player's kart has to get, in LEV world units. The engine's
// own driver hit radius is 0x40 (THREAD_DRIVER_HIT_RADIUS, namespace_Proc.h:60),
// so 0x60 is the kart plus roughly half a crate. LinkedCollide_Radius takes the
// SQUARED value despite its parameter name, which is why the square is derived
// here rather than written out (Lessons Learned §5).
#define AP_BOX_HIT_RADIUS         0x60
#define AP_BOX_HIT_RADIUS_SQUARED (AP_BOX_HIT_RADIUS * AP_BOX_HIT_RADIUS)

// Per-frame. Called from AP_OnFrame (ap_hooks.c) AFTER AP_Author_OnFrame and
// BEFORE AP_Spawn_OnFrame, so a box added this frame is born in the same frame
// and author mode has already decided whether it owns the markers.
void AP_Boxes_OnFrame(struct GameTracker *gGT);

// How many AP boxes are standing on the current level. Diagnostics only.
int AP_Boxes_LiveCount(void);

#endif // CTR_AP
#endif // AP_BOXES_H
