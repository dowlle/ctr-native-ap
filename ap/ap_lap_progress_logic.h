#ifndef AP_LAP_PROGRESS_LOGIC_H
#define AP_LAP_PROGRESS_LOGIC_H

// ============================================================================
// Which checkpoint a racer's lap progress may be measured from (A3-26).
//
// FREESTANDING, like ap_boss_lane_logic.h: no engine headers, no config, no
// logging. game/Vehicle/VehLap.c calls it from VehLap_UpdateProgress;
// tools/test-lap-progress.c drives it over synthetic checkpoint lists and,
// when the file is named in the environment, over a retail track.
//
// VehLap_UpdateProgress measures a racer against the restart point it is on,
// that point's forward neighbour and the one after. A human takes the index
// from the quad under the kart and skips the update on a quad without one
// (0xff). A bot on its nav line takes the nav frame's index. A bot off its
// line (after a lane change or a bump, BOT_FLAG_ESTIMATE_NAV or
// BOT_FLAG_FREE_PHYSICS) takes the quad's index too, and retail does not skip
// 0xff for it: it reads restart point 255, past the end of the array.
//
// On N. Gin Labs the low lane of the barrel ramp (quads 1248 to 1251, 1106,
// 843 and 776, between restart points 75 and 86) has no checkpoint. Restart
// point 255 there reads as restart point 0 with no distance, so the bot's
// progress falls from about 7400 at the top of the ramp to about 770 at the
// bottom: it looks as if it is about to cross the finish line. When it steps
// back onto a quad with a checkpoint (86, progress 41432) the crossing rule
// in PlayLevel_UpdateLapStats (from below 1200 to above 32000) counts a lap.
// A boss that changes lane on the ramp gains a lap and wins a lap early.
//
// Ruling (A3-26, conservative): every racer is measured only from restart
// points that exist. Otherwise the update is skipped and the racer keeps its
// last progress and checkpoint, as a human already does on such a quad.
// ============================================================================

// Is a restart point index inside the level's restart point array? The
// caller asks for the racer's checkpoint first and then, one at a time, for
// the two forward neighbours the measurement reads.
static inline int AP_LapProgress_IndexUsable(int index, int numRestartPoints)
{
	return (index >= 0) && (index < numRestartPoints);
}

#endif // AP_LAP_PROGRESS_LOGIC_H
