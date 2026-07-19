#ifndef AP_VERIFY_H
#define AP_VERIFY_H
#ifdef CTR_AP

// Client-side seed completability verification (issue: Beth's seed, 2026-07-17).
//
// The client is the ground truth for CTR gating (hub key doors, per-pad
// slot_data requirements, boss-garage ladder, goal gates), and it already
// scouts the item placed at every own location on slot-connect (reward glow).
// This module joins the two: a fixed-point reachability sweep that simulates
// collection from the CURRENT received-item state using the same requirement
// semantics the live gates enforce, and reports whether the goal (and how many
// locations) can still be reached. Generation, accessibility:full and the
// fuzzer all reason over the apworld's region graph; this sweep reasons over
// the GAME's rules, so it catches graph-vs-game divergence and client/apworld
// version skew that no generator-side check can see.
//
// Verdict strength: on a SOLO seed every own progression item is in the own
// world, so a blocked fixed point is a definitive hard lock (red banner). In a
// MULTIWORLD own progression may legitimately arrive from other worlds'
// spheres, so a blocked sweep is advisory only (log line, no banner).
//
// The sweep re-runs whenever the AP state generation changes (fresh connect,
// received item, location check), so it doubles as a live stuck-detector: the
// moment a session's remaining reachable set can no longer produce the goal,
// the log (and on solo seeds the banner) says so.

// Per-frame tick: watches connection/scout/state-generation and recomputes the
// verdict when anything relevant changed. Call from AP_OnFrame.
void AP_VerifyOnFrame(void);

// Persistent on-screen warning (adventure hub + hub map, next to the schema
// warning) when a SOLO seed's goal is unreachable from the current state.
// Self-gates; a no-op while the verdict is fine, advisory, or not computed.
void AP_DrawVerifyWarning(void);

#endif // CTR_AP
#endif // AP_VERIFY_H
