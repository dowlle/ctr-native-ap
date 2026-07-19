#ifndef AP_DEATHLINK_H
#define AP_DEATHLINK_H

// Archipelago DeathLink for CTR Native (issue #6).
//
// Compiled ONLY under CTR_AP, in the C unity build (game_unity.h includes
// ap_deathlink.c after the game translation units, same as ap_traps.c), so it can
// read the engine structs/enums directly. The pure networking half (the tagged
// Bounce send/receive + the DeathLink connection tag) lives in ap_net.cpp behind
// the ap_net_deathlink_* C API; this module owns the GAME-side semantics:
//
//   SEND (adventure mode only, honouring amnesty):
//     * mask_reset tier -> a rising edge into KS_MASK_GRABBED on the local player
//       (fell off the track, or eaten by Papu's plant -- both land in that state).
//     * any_hit tier    -> the above PLUS every landed hit routed through
//       VehPickState_NewState with damageType 1/2/3/4 on the local player
//       (spin / blast / squish / burn); mask-grab (damageType 5) is left to the
//       edge detector so it is never counted twice.
//
//   RECEIVE:
//     * force the full mask-grab reset via DRIVER_COLL_FLAG_MASK_GRAB_REQUEST (the
//       proven Shortcutless precedent, COLL.c:1488). The request bit MUST be OR'd
//       from INSIDE the physics pipeline (AP_DeathLinkForceReset, hooked into
//       COLL_FIXED_PlayerSearch), not from AP_OnFrame: VehPhysForce_OnApplyForces
//       zeroes collisionFlags every frame before the mask-grab gate reads it, so a
//       pre-pipeline OR would be wiped. A death that arrives outside a race window
//       is queued ONE deep (extras dropped) and fires at the next race window; a
//       death that arrives mid-race fires as soon as the window is active. Receive,
//       like send, is gated to adventure mode.
//
//   NO-LOOP GUARD (hard requirement): the forced mask-grab is itself a send
//   trigger, so the resulting rising edge is swallowed exactly once and never
//   produces an outgoing death -- two CTR players can never ping-pong.

#ifdef CTR_AP

struct GameTracker;
struct Driver;

// death_link option values, mirrored from the apworld DeathLink choice + slot_data
// (ctr_cfg.death_link). Kept in lockstep with worlds/ctr/Options.py::DeathLink.
enum
{
	CTR_DL_OFF        = 0,
	CTR_DL_MASK_RESET = 1,
	CTR_DL_ANY_HIT    = 2
};

// Per-connect reset: clears the edge/queue/amnesty state and, when this seed opted
// in (ctr_cfg.death_link != off), enables the DeathLink connection tag. Called from
// AP_NetTick's fresh-connect block, after slot_data has been parsed.
void AP_DeathLinkConnectReset(void);

// 1 when DeathLink is effectively on (seed option + runtime override combined).
int AP_DeathLinkActive(void);

// Per-frame driver, called from AP_OnFrame (all modes). Drains the network inbound
// latch into the depth-1 queue and runs the mask-grab send edge (consuming the
// no-loop guard). The queued death is APPLIED separately by AP_DeathLinkForceReset
// from inside the physics pipeline; this runs before the pipeline each frame.
void AP_DeathLinkTick(struct GameTracker *gGT);

// Receive-apply hook, called from INSIDE COLL_FIXED_PlayerSearch (game/COLL.c,
// right before the mask-grab gate) so the request bit survives to the gate. Returns
// 1 when a queued received death should force this driver's mask reset THIS frame;
// the caller then OR's DRIVER_COLL_FLAG_MASK_GRAB_REQUEST. It returns 1 only once
// every gate precondition is already met, so the grab is guaranteed to fire, which
// is why it may clear the queue and arm the no-loop guard here. Adventure mode +
// race window + local player only.
int AP_DeathLinkForceReset(struct Driver *d);

// any_hit send hook, called from VehPickState_NewState once a hit is confirmed to
// land on its victim. Sends only when death_link == any_hit, the victim is the
// local player, and damageType is 1/2/3/4 (mask-grab is handled by the edge
// detector). reason is the VehPickState reason code (VehPickState.c:141-198),
// used to build a short, name-free cause string.
void AP_DeathLinkOnHit(struct Driver *victim, int damageType, int reason);

#endif // CTR_AP

#endif // AP_DEATHLINK_H
