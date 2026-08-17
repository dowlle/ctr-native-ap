#ifndef AP_NAVREC_H
#define AP_NAVREC_H

// ============================================================================
// NAV-PATH RECORDER PROTOTYPE -- THROWAWAY, NOT FOR MERGE.
//
// Drive a lap, get AI nav paths out. Companion to ap_navspike.c, which already
// proved (2026-08-17, in game) that the engine follows client-supplied paths.
//
// Pipeline:
//   1. record   -- sample the player's driver state every race frame
//   2. resample -- arc-length decimate to ~230 nodes (retail Crash Cove uses
//                  227-239 for a lap; sampling by time would crowd the corners)
//   3. lanes    -- lane 0 = the driven line; lanes 1 and 2 = perpendicular XZ
//                  offsets. Retail lanes sit ~500 units apart (measured), and
//                  three human laps would be near-coincident, so synthesising
//                  the offsets beats driving three lines.
//   4. emit     -- NavHeader + NavFrame arrays to a .navpath file
//
// pathChangeOpcode is written as a SENTINEL, not zero. Zero decodes to
// "lane 0, node 0" and BOTS.c:1143 would happily teleport an overtaking bot to
// the start line. Any positive value >= BOTS_PathChangeCap() (0xC00) fails the
// `changeOpcode < cap` test and disables lane changes safely. Real cross-lane
// correspondence is a v2 problem.
//
// Env-gated, inert unless switched on:
//   CTR_AP_NAV_REC=1          arm the recorder; records automatically in-race
//   CTR_AP_NAV_REC_NODES=N    target node count per lane (default 230)
//   CTR_AP_NAV_REC_OFFSET=N   lane 1/2 lateral offset in world units (default 400)
//
// Numpad 9 writes the current recording to navpath-<levelID>.bin + a .txt dump.
// Load it back with ap_navspike mode 5.
// ============================================================================

void AP_NavRec_Tick(struct GameTracker *gGT);
int  AP_NavRec_LoadForLevel(int levelID, struct NavHeader **outHeaders, struct NavFrame **outFrames, int *outCounts);

#endif // AP_NAVREC_H
