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
//       proven Shortcutless precedent, COLL.c:1484). A death that arrives outside
//       a race window is queued ONE deep (extras dropped) and fires at the next
//       race start; a death that arrives mid-race fires as soon as the window is
//       active.
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

// Per-frame driver, called from AP_OnFrame (all modes). Drains inbound deaths,
// fires a queued death at the race window, runs the mask-grab send edge, and
// counts down the no-loop guard. gGT is the live GameTracker.
void AP_DeathLinkTick(struct GameTracker *gGT);

// any_hit send hook, called from VehPickState_NewState once a hit is confirmed to
// land on its victim. Sends only when death_link == any_hit, the victim is the
// local player, and damageType is 1/2/3/4 (mask-grab is handled by the edge
// detector). reason is the VehPickState reason code (VehPickState.c:141-198),
// used to build a short, name-free cause string.
void AP_DeathLinkOnHit(struct Driver *victim, int damageType, int reason);

#endif // CTR_AP

#endif // AP_DEATHLINK_H
