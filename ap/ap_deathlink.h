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
//   SEND (adventure mode only, honouring amnesty and the send cooldown). What sends
//   is a bitmask, separate from the receive effect: slot_data death_link_send, 1 =
//   mask_grab, 2 = weapon_hit, 4 = race_loss. An absent key follows the legacy
//   coupling from death_link (1 -> 1, 2 -> 1|2, 3 -> 1|4). The OPTIONS rows DL SEND
//   FALL / HIT / LOSS override each trigger; a forced DeathLink row brings its own
//   legacy coupling; the row OFF is fully off. Anything that sends also receives
//   (AP_DeathLinkResolve in ap_race_attempt_logic.h).
//     * mask_grab   -> a rising edge into KS_MASK_GRABBED on the local player
//       (fell off the track, or eaten by Papu's plant -- both land in that state).
//     * weapon_hit  -> every landed hit routed through VehPickState_NewState with
//       damageType 1/2/3/4 on the local player (spin / blast / squish / burn);
//       mask-grab (damageType 5) is left to the edge detector so it is never
//       counted twice.
//     * race_loss   -> the game's own loss result: a trophy / boss / CTR Challenge
//       race finished outside 1st or a Crystal Challenge with crystals missing
//       (AP_DeathLinkOnRaceEnd), the whole Gem Cup lost (AP_DeathLinkOnCupLost,
//       never a single leg), and pause-menu RESTART or EXIT TO MAP mid-race
//       (AP_DeathLinkOnPauseLeave).
//     No trigger sends while the #286 forced-loss latch is set: a received death's
//     own aftermath (results, retry, quit, restart, hits) never sends.
//
//   RECEIVE (effect driven by death_link; never queued):
//     * A received death applies only inside a live adventure race (adventure mode,
//       on a track, lights out, not paused / menu / cutscene / end of race /
//       loading). Anywhere else it is DROPPED, in every mode, and not held for the
//       next race. The one hold: a death pending while the same race is paused
//       waits for that pause and applies when the race runs again; leaving the race
//       first (quit, restart, exit, level unload) drops it. A death inside the
//       window that cannot land yet (mask-grab gate closed, rank order invalid) is
//       retried for AP_DL_GRACE_FRAMES live frames, then dropped.
//     * Every received death shows a feed popup naming the source and cause, or
//       saying it was ignored (AP_FeedDeathLinkLine).
//     * modes 1/2: force the full mask-grab reset via
//       DRIVER_COLL_FLAG_MASK_GRAB_REQUEST (the proven Shortcutless precedent,
//       COLL.c:1488). The request bit MUST be OR'd from INSIDE the physics
//       pipeline (AP_DeathLinkForceReset, hooked into COLL_FIXED_PlayerSearch),
//       not from AP_OnFrame: VehPhysForce_OnApplyForces zeroes collisionFlags
//       every frame before the mask-grab gate reads it, so a pre-pipeline OR
//       would be wiped. Receive, like send, is gated to adventure mode.
//     * mode 3 (race_loss): end the current attempt as a retail loss instead of
//       resetting the mask. The local driver is moved to the last place rank
//       (bijectively), marked finished and handed to the AI, then the common
//       MainGameEnd_Initialize runs. The attempt latch (AP_RaceAttemptIsForcedLoss)
//       is armed before that call and suppresses every result-derived check until
//       the next eligible racing level starts. A Crystal Challenge fails like a
//       retail time-out; a Relic Race ends as a failed run (no relic, no high
//       score, no ghost) and keeps the retail RETRY / EXIT TO MAP menu.
//
//   NO-LOOP GUARD (hard requirement): the forced mask-grab is itself a send
//   trigger, so the resulting rising edge is swallowed exactly once and never
//   produces an outgoing death -- two CTR players can never ping-pong.

#ifdef CTR_AP

#include "ap_race_attempt_logic.h" // #286 freestanding producer classes + decision

struct GameTracker;
struct Driver;

// death_link option values, mirrored from the apworld DeathLink choice + slot_data
// (ctr_cfg.death_link). Kept in lockstep with worlds/ctr/Options.py::DeathLink.
enum
{
	CTR_DL_OFF        = 0,
	CTR_DL_MASK_RESET = 1,
	CTR_DL_ANY_HIT    = 2,
	CTR_DL_RACE_LOSS  = 3
};

// Per-connect reset: clears the edge/receive/amnesty state and, when this seed opted
// in (anything receives or sends), enables the DeathLink connection tag. Called from
// AP_NetTick's fresh-connect block, after slot_data has been parsed.
void AP_DeathLinkConnectReset(void);

// 1 when DeathLink is effectively on: it receives or any send trigger is on (seed
// options and the runtime OPTIONS rows combined).
int AP_DeathLinkActive(void);

// Per-frame driver, called from AP_OnFrame (all modes). Drains the network inbound
// latch, decides each received death (apply, hold for a pause, or drop) and runs
// the mask-grab send edge (consuming the no-loop guard). A mask-reset death is
// APPLIED by AP_DeathLinkForceReset from inside the physics pipeline; this runs
// before the pipeline each frame.
void AP_DeathLinkTick(struct GameTracker *gGT);

// Receive-apply hook, called from INSIDE COLL_FIXED_PlayerSearch (game/COLL.c,
// right before the mask-grab gate) so the request bit survives to the gate. Returns
// 1 when a received death in its grace window should force this driver's mask reset THIS frame;
// the caller then OR's DRIVER_COLL_FLAG_MASK_GRAB_REQUEST. It returns 1 only once
// every gate precondition is already met, so the grab is guaranteed to fire, which
// is why it may consume the death and arm the no-loop guard here. Adventure mode +
// live race + local player only.
int AP_DeathLinkForceReset(struct Driver *d);

// weapon_hit send hook, called from VehPickState_NewState once a hit is confirmed to
// land on its victim. Sends only when the weapon_hit trigger is on, the victim is the
// local player, and damageType is 1/2/3/4 (mask-grab is handled by the edge
// detector). reason is the VehPickState reason code (VehPickState.c:141-198),
// used to build a short, name-free cause string.
void AP_DeathLinkOnHit(struct Driver *victim, int damageType, int reason);

// race_loss sends (the AP_DL_TRIG_LOSS trigger). All three go through the same
// cooldown, amnesty and forced-loss latch checks as every other send.
//   AP_DeathLinkOnRaceEnd    MainGameEnd_Initialize: the game's own result for the
//                            local race (trophy / boss / CTR Challenge outside 1st,
//                            Crystal Challenge with crystals missing).
//   AP_DeathLinkOnCupLost    UI_CupStandings: the whole Gem Cup was lost.
//   AP_DeathLinkOnPauseLeave pause menu RESTART (quit 0) or EXIT TO MAP (quit 1).
void AP_DeathLinkOnRaceEnd(struct GameTracker *gGT, struct Driver *player);
void AP_DeathLinkOnCupLost(void);
void AP_DeathLinkOnPauseLeave(int quit);

// #286 authoritative attempt predicate. True from the instant a race_loss
// receive ends an attempt until the next eligible racing level starts. Every
// result-derived check producer (podium finish rungs, advance reward, goal,
// trial, Cortex, custom, relic unlock and the #49 relic-perfect follow-up) must
// observe this one predicate. Network state never resets it.
int AP_RaceAttemptIsForcedLoss(void);

// #286 single production guard for every result-derived producer. Wraps the
// freestanding AP_RaceAttempt_SuppressResultProducer with the live attempt latch,
// so each call site names its producer class and the host harness exercises the
// exact decision production runs. Returns 1 when the producer must be blocked.
// Every finish-class producer is blocked while latched EXCEPT the cup-aggregate
// class (AP_RESULT_PRODUCER_CUP_AGGREGATE), which the final Gem Cup standings use
// so a cup won on points still grants its reward. See ap_race_attempt_logic.h.
int AP_RaceAttempt_ProducerBlocked(int producerClass);

// #286 attempt boundary, called from game/MAIN/MainInit.c immediately after
// MainGameStart_Initialize(gGT, 1) under CTR_AP. Clears the forced-loss latch
// only when the newly loaded level is an eligible racing level: hub, menu,
// cutscene and battle loads leave the latch set.
void AP_RaceAttempt_OnLevelStart(struct GameTracker *gGT);

// Bracket damage the AP layer inflicts on the local player on purpose, so the
// any_hit hook above does not read it as a death worth broadcasting. The Flatten
// trap (#280) wraps its damage-dispatch call in this: the ruling attributes the
// squish as self-inflicted and explicitly forbids sending DeathLink for it. Call
// with 1 immediately before the dispatch and 0 immediately after. It is a plain
// flag rather than a counter, so brackets must never be nested.
void AP_DeathLinkSuppressSelfInflicted(int on);

#endif // CTR_AP

#endif // AP_DEATHLINK_H
