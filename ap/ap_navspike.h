#ifndef AP_NAVSPIKE_H
#define AP_NAVSPIKE_H

// ============================================================================
// NAV-PATH INJECTION SPIKE -- THROWAWAY, NOT FOR MERGE.
//
// Question being answered: can the client supply bot nav paths at runtime,
// instead of them coming from the LEV via level1->LevNavTable?
//
// Why it looks possible: BOTS.c reads LevNavTable in exactly ONE place
// (BOTS_InitNavPath, game/BOTS.c:93-98). Every other consumer -- including
// BOTS_GotoStartingLine's initial frame assignment (game/BOTS.c:3026), the
// per-frame follow, killplane goBackCount rewind and the boss lane-change --
// reads sdata->NavPath_ptrHeader[] / sdata->NavPath_ptrNavFrameArray[].
// So re-pointing those two arrays after init should fully retarget the AI.
//
// Controlled by env vars so the build is inert unless deliberately switched on.
// getenv has no precedent elsewhere in this tree; that is acceptable for a
// spike branch and would need replacing with a real option before any merge.
//
//   CTR_AP_NAV_SPIKE=1  dump path stats only, no injection (safe, read-only)
//   CTR_AP_NAV_SPIKE=2  copy paths into our own storage and inject VERBATIM
//                       (the control: bots must behave exactly as before)
//   CTR_AP_NAV_SPIKE=3  inject with every node shifted laterally in X by
//                       CTR_AP_NAV_SPIKE_OFFSET (default 0x200)
//   CTR_AP_NAV_SPIKE=4  inject with path 0 truncated to half its node count
//
// Mode 2 proves the plumbing. Modes 3 and 4 prove the data is really being
// consumed from our buffer and not still from the LEV.
// ============================================================================

void AP_NavSpike_AfterInit(void);

#endif // AP_NAVSPIKE_H
