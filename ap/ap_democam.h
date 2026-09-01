#ifndef AP_DEMOCAM_H
#define AP_DEMOCAM_H

// ============================================================================
// DEMO CAMERA.
//
// This module drives the shipped trap and retains the direct debug toggle for
// live acceptance: can CTR's retail
// end-of-race cinematic camera be driven against the local human racer, without
// its normal bot / end-of-race owner, so that it changes no driver control field
// and leaves no camera flag behind when it is released?
//
// The pure state machine and the full retail constraints live in
// ap_democam_logic.h. This header is the engine seam only.
//
// LIVE TRIGGER (no item required). Both gates must be set in ap-config.txt:
//     dev_keys=1
//     debug_democam=1
// then Numpad + (SDL scancode 87, unclaimed: 89-99 belong to traps,
// Shortcutless and the character picker) toggles the engagement on and off.
// Every transition writes one [AP DEMOCAM] line to the AP log.
//
// The direct debug trigger is completely inert without both config lines.
// ============================================================================

struct GameTracker;

// Per-frame lifecycle: polls the debug toggle, holds the engagement, and runs
// the force-clear rule. Safe in every game mode; gates its own race-only work.
void AP_DemoCamTick(struct GameTracker *gGT);

// Scheduler seam. A suspended or expired trap passes 0, which releases the
// camera snapshot immediately; a resumed trap passes 1 and re-engages only
// after the live camera gate is safe again.
void AP_DemoCamSetTrapActive(int active);

// Conditional-predicate seam: true only when the retail cinematic camera has a
// safe local-human owner on this frame. An armed item waits while false.
int AP_DemoCamCanEngage(struct GameTracker *gGT);

// Drop any engagement on a fresh connect, alongside AP_Trap_ConnectReset. A
// snapshot cannot outlive the session that took it.
void AP_DemoCam_ConnectReset(void);

// ap-config.txt parser hook. Consumes "debug_democam=". Returns 1 if the line
// was ours.
int AP_DemoCamConfigLine(const char *line);

#endif // AP_DEMOCAM_H
