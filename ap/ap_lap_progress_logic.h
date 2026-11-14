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
// points that exist. On a quad without a checkpoint the update is skipped and
// the racer keeps its last progress and checkpoint, as a human already does.
// A4-78 narrows the skip to that case: see AP_LapProgress_MeasureIndex.
// ============================================================================

// Is a restart point index inside the level's restart point array? The
// caller asks for the racer's checkpoint first and then, one at a time, for
// the two forward neighbours the measurement reads.
static inline int AP_LapProgress_IndexUsable(int index, int numRestartPoints)
{
	return (index >= 0) && (index < numRestartPoints);
}

// A quad without a checkpoint stores this index.
#define AP_LAP_PROGRESS_NO_CHECKPOINT 0xff

// Which restart point to measure from (A4-78), or -1 to skip the update.
//
// Papu's Pyramid has 102 restart points (0 to 101), but each of its three AI
// nav lines names restart point 102 on the frames that cross the finish line
// (path 0 frames 278 to 282, path 1 frames 271 to 273, path 2 frames 286 and
// 287). Retail reads point 102 from past the array; those bytes link forward
// to point 0, as point 101 does, so retail measures exactly as from 101 and
// the bot's progress falls below 1200 before it crosses. Skipping those
// frames, as A3-26 first did, froze a bot on nav line 0 at about 1568, so the
// crossing rule never fired and the bot lost a lap every lap: a CPU ahead on
// track was ranked behind the player.
//
// Rule: a usable index is measured. 0xff (a quad without a checkpoint, the
// A3-26 N. Gin Labs case) still skips. Any other index past the array is
// measured from the racer's last checkpoint only when that checkpoint links
// forward to restart point 0, the last point before the finish line, as 101
// does on Papu's Pyramid, and the kart is near restart point 0
// (AP_LapProgress_NearFinish); the progress is then retail's. Anywhere else,
// or without a usable last checkpoint, it skips as A3-26 did, so a stale
// checkpoint is never measured toward the line from elsewhere on the track.
// lastForward is the last checkpoint's nextIndex_forward, or -1 when the
// caller could not read it.
static inline int AP_LapProgress_MeasureIndex(int index, int lastIndex, int lastForward, int nearFinish, int numRestartPoints)
{
	if (AP_LapProgress_IndexUsable(index, numRestartPoints))
	{
		return index;
	}
	if (index == AP_LAP_PROGRESS_NO_CHECKPOINT)
	{
		return -1;
	}
	if (AP_LapProgress_IndexUsable(lastIndex, numRestartPoints) && (lastForward == 0) && nearFinish)
	{
		return lastIndex;
	}
	return -1;
}

// How far from restart point 0, on every axis, in world units, the kart may
// be for the fallback above. Papu's Pyramid's finish-line nav frames reach
// 1314 on one axis; this is about three times that.
#define AP_LAP_PROGRESS_FINISH_REACH 0x1000

// Is the kart (world units) within the reach of restart point 0 on every axis?
static inline int AP_LapProgress_NearFinish(int kartX, int kartY, int kartZ, int startX, int startY, int startZ)
{
	int dx = kartX - startX, dy = kartY - startY, dz = kartZ - startZ;
	return (dx >= -AP_LAP_PROGRESS_FINISH_REACH) && (dx <= AP_LAP_PROGRESS_FINISH_REACH) &&
	       (dy >= -AP_LAP_PROGRESS_FINISH_REACH) && (dy <= AP_LAP_PROGRESS_FINISH_REACH) &&
	       (dz >= -AP_LAP_PROGRESS_FINISH_REACH) && (dz <= AP_LAP_PROGRESS_FINISH_REACH);
}

#endif // AP_LAP_PROGRESS_LOGIC_H
