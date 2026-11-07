#ifndef AP_NAVREC_PLAYBACK_LOGIC_H
#define AP_NAVREC_PLAYBACK_LOGIC_H

// ============================================================================
// What a recorded lap goes through between the file and the engine lane.
//
// FREESTANDING, like ap_navrec_format.h: no engine headers, no file IO, no
// logging. ap/ap_navrec.c runs these on the playback copy of a lap, never on the
// file; tools/test-navrec.c and tools/navrec-inspect.c compile the same code.
//
//   Seam smoothing   the finish-line wrap of a closed lap is blended over a
//                    short window on each side, or the lap is refused.
//   Crate bit        recorded nodes inherit the level-instance collision bit
//                    (0x40) from the nearest node of the level's own lanes.
//   Opcode guard     a level lane kept beside recorded lanes never jumps onto
//                    a recorded lane by frame index.
//   Label owner      one name per recorded lane, on the first bot to drive it.
// ============================================================================

#include "ap_navrec_format.h"

// ---------------------------------------------------------------------------
// Finish seam smoothing
// ---------------------------------------------------------------------------

// Nodes blended on EACH side of the wrap. Laps decimate to 230 nodes, so eight
// nodes is about 3.5 percent of the line per side; a short lap uses an eighth of
// its nodes instead.
#define AP_NAVREC_SEAM_WINDOW 8

// Largest shift the blend may apply to a single node, in NavFrame units. Half
// the seam error lands on each side, so this refuses a seam whose error is over
// twice this. 500 is one retail lane separation (AP_NAVREC_FALLBACK_LANE_OFFSET),
// the same yardstick the closure rule uses. Measured over the 284 closed laps in
// the 2026-08-30 community intake: sideways wrap offset median 211, p90 468,
// max 1081; height offset p99 222. A seam past the bound is refused rather than
// bridged, because a blend that large would carry the line off the road.
#define AP_NAVREC_SEAM_MAX_SHIFT 500

// The along-track part of the wrap is only corrected when it is outside half to
// one and a half times the expected step: a wrap that runs backwards or doubles
// a node, or one that skips a node's worth of road. Inside that band the wrap
// is an ordinary step and is left alone.
#define AP_NAVREC_SEAM_ALONG_MIN_PERMILLE 500
#define AP_NAVREC_SEAM_ALONG_MAX_PERMILLE 1500

enum AP_NavRecSeamResult
{
	AP_NAVREC_SEAM_SMOOTH = 0,   // blended (or already within a unit)
	AP_NAVREC_SEAM_TOO_SHORT,    // too few nodes to own a window on each side
	AP_NAVREC_SEAM_TOO_WIDE      // the blend would move a node past the bound
};

struct AP_NavRecSeamReport
{
	int          result;
	unsigned int window;     // nodes blended per side
	double       errorLat;   // sideways offset of the wrap, signed
	double       errorAlong; // along-track correction applied to the wrap
	double       errorY;     // height offset of the wrap against the neighbour steps
	double       maxShift;   // largest single-node move (half the error)
	double       wrapBefore; // last -> first XZ distance before
	double       wrapAfter;  // and after
};

static double AP_NavRecSeam_WrapXZ(const struct AP_NavRecNode *nodes, unsigned int count)
{
	double dx = (double)nodes[0].posX - (double)nodes[count - 1u].posX;
	double dz = (double)nodes[0].posZ - (double)nodes[count - 1u].posZ;
	return sqrt((dx * dx) + (dz * dz));
}

static short AP_NavRecSeam_ClampShort(double v)
{
	if (v > 32767.0)
		return 32767;
	if (v < -32768.0)
		return -32768;
	return (short)((v < 0.0) ? (v - 0.5) : (v + 0.5));
}

// Signed difference b - a of two yaw bytes (256 to the turn), in -128..127.
static int AP_NavRecSeam_YawDelta(unsigned char a, unsigned char b)
{
	int d = (int)b - (int)a;
	while (d > 127)
		d -= 256;
	while (d < -128)
		d += 256;
	return d;
}

// Measure the seam and, when `apply` is set and it is within bound, blend it.
//
// The expected wrap is the mean of the two steps either side of it (last but one
// to last, first to second). The error is the wrap minus that expectation, split
// into a sideways part, an along-track part (only outside the band above) and a
// height part. Half of it is removed on each side of the seam, ramping linearly
// to nothing over the window, so the wrap becomes an ordinary step and the line
// outside the window is untouched. Yaw is blended the same way. Distances are
// recomputed over the whole closed loop; timestamps and goBackCount are not
// touched, so timing stays monotonic and the checkpoint walk is the recorded one.
static int AP_NavRecSeam_Process(struct AP_NavRecNode *nodes, unsigned int count, int apply, struct AP_NavRecSeamReport *rep)
{
	struct AP_NavRecSeamReport local;
	struct AP_NavRecSeamReport *r = (rep != NULL) ? rep : &local;
	unsigned int window;
	unsigned int j;
	double sx, sy, sz, sl;
	double tx, tz;
	double wx, wy, wz;
	double along, lat;
	double alongTarget;
	double ex, ey, ez;
	int yawErr;

	memset(r, 0, sizeof *r);

	if ((nodes == NULL) || (count < 16u))
	{
		r->result = AP_NAVREC_SEAM_TOO_SHORT;
		return r->result;
	}

	window = AP_NAVREC_SEAM_WINDOW;
	if (window > (count / 8u))
		window = count / 8u;
	r->window = window;

	sx = (((double)nodes[count - 1u].posX - (double)nodes[count - 2u].posX) + ((double)nodes[1].posX - (double)nodes[0].posX)) / 2.0;
	sy = (((double)nodes[count - 1u].posY - (double)nodes[count - 2u].posY) + ((double)nodes[1].posY - (double)nodes[0].posY)) / 2.0;
	sz = (((double)nodes[count - 1u].posZ - (double)nodes[count - 2u].posZ) + ((double)nodes[1].posZ - (double)nodes[0].posZ)) / 2.0;
	sl = sqrt((sx * sx) + (sz * sz));
	if (sl < 0.0001)
	{
		r->result = AP_NAVREC_SEAM_TOO_SHORT;
		return r->result;
	}
	tx = sx / sl;
	tz = sz / sl;

	wx = (double)nodes[0].posX - (double)nodes[count - 1u].posX;
	wy = (double)nodes[0].posY - (double)nodes[count - 1u].posY;
	wz = (double)nodes[0].posZ - (double)nodes[count - 1u].posZ;

	along = (wx * tx) + (wz * tz);
	lat = (-wx * tz) + (wz * tx);

	alongTarget = along;
	if (along < (sl * (double)AP_NAVREC_SEAM_ALONG_MIN_PERMILLE / 1000.0))
		alongTarget = sl * (double)AP_NAVREC_SEAM_ALONG_MIN_PERMILLE / 1000.0;
	if (along > (sl * (double)AP_NAVREC_SEAM_ALONG_MAX_PERMILLE / 1000.0))
		alongTarget = sl * (double)AP_NAVREC_SEAM_ALONG_MAX_PERMILLE / 1000.0;

	r->errorLat = lat;
	r->errorAlong = along - alongTarget;
	r->errorY = wy - sy;
	r->wrapBefore = AP_NavRecSeam_WrapXZ(nodes, count);

	// The error vector in world axes: sideways along the normal, plus the
	// along-track excess along the tangent, plus height.
	ex = (r->errorAlong * tx) + (lat * -tz);
	ez = (r->errorAlong * tz) + (lat * tx);
	ey = r->errorY;

	r->maxShift = sqrt((ex * ex) + (ey * ey) + (ez * ez)) / 2.0;
	if (r->maxShift > (double)AP_NAVREC_SEAM_MAX_SHIFT)
	{
		r->result = AP_NAVREC_SEAM_TOO_WIDE;
		r->wrapAfter = r->wrapBefore;
		return r->result;
	}

	r->result = AP_NAVREC_SEAM_SMOOTH;
	if (!apply)
	{
		r->wrapAfter = r->wrapBefore;
		return r->result;
	}

	yawErr = AP_NavRecSeam_YawDelta(nodes[count - 1u].rot[1], nodes[0].rot[1]) -
	         ((AP_NavRecSeam_YawDelta(nodes[count - 2u].rot[1], nodes[count - 1u].rot[1]) +
	           AP_NavRecSeam_YawDelta(nodes[0].rot[1], nodes[1].rot[1])) / 2);

	// j = 0 is the node next to the seam on each side, weight one half; the
	// weight falls linearly to zero at the window's inner edge.
	for (j = 0; j < window; j++)
	{
		double w = 0.5 * (double)(window - j) / (double)window;
		struct AP_NavRecNode *tail = &nodes[count - 1u - j];
		struct AP_NavRecNode *head = &nodes[j];
		int yawShift = (int)((double)yawErr * w + ((yawErr < 0) ? -0.5 : 0.5));

		tail->posX = AP_NavRecSeam_ClampShort((double)tail->posX + (ex * w));
		tail->posY = AP_NavRecSeam_ClampShort((double)tail->posY + (ey * w));
		tail->posZ = AP_NavRecSeam_ClampShort((double)tail->posZ + (ez * w));
		tail->rot[1] = (unsigned char)(((int)tail->rot[1] + yawShift) & 0xFF);

		head->posX = AP_NavRecSeam_ClampShort((double)head->posX - (ex * w));
		head->posY = AP_NavRecSeam_ClampShort((double)head->posY - (ey * w));
		head->posZ = AP_NavRecSeam_ClampShort((double)head->posZ - (ez * w));
		head->rot[1] = (unsigned char)(((int)head->rot[1] - yawShift) & 0xFF);
	}

	AP_NavRecFormat_FillDistances(nodes, count);
	r->wrapAfter = AP_NavRecSeam_WrapXZ(nodes, count);
	return r->result;
}

static const char *AP_NavRecSeam_ResultText(int result)
{
	switch (result)
	{
	case AP_NAVREC_SEAM_SMOOTH:
		return "smoothed";
	case AP_NAVREC_SEAM_TOO_SHORT:
		return "too few nodes to blend the finish seam";
	case AP_NAVREC_SEAM_TOO_WIDE:
		return "finish seam is wider than the blend bound";
	default:
		return "unknown";
	}
}

// Which of a decoded file's laps may drive a lane: closed loops
// (AP_NavRecFormat_ClosedLaps) whose seam is within the blend bound. Same shape
// and order as ClosedLaps, so the pick and the placement agree on lap numbers.
static unsigned int AP_NavRecPlayback_EligibleLaps(const struct AP_NavRecFileInfo *info,
                                                   struct AP_NavRecNode nodes[][AP_NAVREC_MAX_NODES],
                                                   unsigned int outLapIndex[AP_NAVREC_MAX_LAPS])
{
	unsigned int closed[AP_NAVREC_MAX_LAPS];
	unsigned int closedCount = AP_NavRecFormat_ClosedLaps(info, nodes, closed);
	unsigned int found = 0;
	unsigned int i;

	for (i = 0; i < closedCount; i++)
	{
		unsigned int lap = closed[i];

		if (AP_NavRecSeam_Process(nodes[lap], info->laps[lap].nodeCount, 0, NULL) == AP_NAVREC_SEAM_SMOOTH)
			outLapIndex[found++] = lap;
	}

	return found;
}

// ---------------------------------------------------------------------------
// Crate collision bit
// ---------------------------------------------------------------------------

// BOTS_ThTick_Drive runs BOTS_LevInstColl, the bot's contact test against level
// instances such as crates, only on a nav node carrying 0x40 (or while the bot
// is off its line). The recorder writes specialBits 0, so a bot on a recorded
// lane drove through every crate untouched. The bit is a property of the road,
// not of the driver, so it is borrowed from the level's own lanes: a recorded
// node takes 0x40 when the nearest node of any level lane carries it and lies
// within one corridor width. Only that bit is set; nothing else in specialBits
// is borrowed, because ramp physics indexes the level header's own tables.
#define AP_NAVREC_SPECIAL_LEVEL_INST_COLL 0x40
#define AP_NAVREC_CRATE_BIT_REACH         AP_NAVREC_SHORTCUT_CORRIDOR_UNITS

// refLanes[i] points at refCounts[i] level nodes. Returns how many recorded
// nodes now carry the bit.
static unsigned int AP_NavRecCrate_Annotate(struct AP_NavRecNode *nodes, unsigned int count, const struct AP_NavRecNode *const *refLanes,
                                            const unsigned int *refCounts, unsigned int refLaneCount)
{
	const double reach = (double)AP_NAVREC_CRATE_BIT_REACH * (double)AP_NAVREC_CRATE_BIT_REACH;
	unsigned int marked = 0;
	unsigned int n;

	if ((nodes == NULL) || (refLanes == NULL) || (refCounts == NULL))
		return 0;

	for (n = 0; n < count; n++)
	{
		double       best = -1.0;
		int          bestBit = 0;
		unsigned int l;

		for (l = 0; l < refLaneCount; l++)
		{
			unsigned int k;

			if (refLanes[l] == NULL)
				continue;

			for (k = 0; k < refCounts[l]; k++)
			{
				const struct AP_NavRecNode *ref = &refLanes[l][k];
				double dx = (double)ref->posX - (double)nodes[n].posX;
				double dy = (double)ref->posY - (double)nodes[n].posY;
				double dz = (double)ref->posZ - (double)nodes[n].posZ;
				double d = (dx * dx) + (dy * dy) + (dz * dz);

				if ((best < 0.0) || (d < best))
				{
					best = d;
					bestBit = (ref->specialBits & AP_NAVREC_SPECIAL_LEVEL_INST_COLL) != 0;
				}
			}
		}

		if ((best >= 0.0) && (best <= reach) && bestBit)
			nodes[n].specialBits = (unsigned char)(nodes[n].specialBits | AP_NAVREC_SPECIAL_LEVEL_INST_COLL);

		if ((nodes[n].specialBits & AP_NAVREC_SPECIAL_LEVEL_INST_COLL) != 0)
			marked++;
	}

	return marked;
}

// ---------------------------------------------------------------------------
// Lane-change guard for level lanes kept beside recorded ones
// ---------------------------------------------------------------------------

// A level node's pathChangeOpcode names a lane (opcode >> 10) and a FRAME INDEX
// in it (opcode & 0x3ff). BOTS_ThTick_Drive follows it whenever it is below
// 3 << 10. Against a recorded lane that frame index means nothing: a different
// node count and a different line, so the bot would land somewhere arbitrary,
// possibly past the lane's end. Such opcodes become the no-change sentinel.
// Opcodes between two level lanes stay exactly as the level has them.
#define AP_NAVREC_PATH_CHANGE_SHIFT 10
#define AP_NAVREC_PATH_CHANGE_CAP   (AP_NAVREC_LANES << AP_NAVREC_PATH_CHANGE_SHIFT)

// recordedMask bit i is set when lane i is recorded. Returns how many opcodes
// were neutralised.
static unsigned int AP_NavRecLane_GuardOpcodes(struct AP_NavRecNode *nodes, unsigned int count, unsigned int recordedMask)
{
	unsigned int changed = 0;
	unsigned int n;

	if (nodes == NULL)
		return 0;

	for (n = 0; n < count; n++)
	{
		int op = (int)nodes[n].pathChangeOpcode;

		if ((op < 0) || (op >= AP_NAVREC_PATH_CHANGE_CAP))
			continue;
		if (((recordedMask >> (op >> AP_NAVREC_PATH_CHANGE_SHIFT)) & 1u) == 0u)
			continue;

		nodes[n].pathChangeOpcode = (short)AP_NAVREC_NO_LANE_CHANGE;
		changed++;
	}

	return changed;
}

// ---------------------------------------------------------------------------
// One label per recorded lane
// ---------------------------------------------------------------------------

// laneOwner[lane] is the driver index that carries the lane's name, or -1 until
// a bot first drives it. Call once per bot per frame, bots in index order. A bot
// owns at most one lane, a lane has at most one owner, and the owner keeps the
// name only while it is on that lane. Returns the lane whose name this bot
// draws, or -1.
static int AP_NavRecLabel_LaneFor(int laneOwner[AP_NAVREC_LANES], unsigned int recordedMask, int driverIndex, int botLane)
{
	int lane;

	if ((botLane < 0) || (botLane >= AP_NAVREC_LANES))
		return -1;
	if (((recordedMask >> botLane) & 1u) == 0u)
		return -1;

	if (laneOwner[botLane] < 0)
	{
		for (lane = 0; lane < AP_NAVREC_LANES; lane++)
		{
			if (laneOwner[lane] == driverIndex)
				return -1; // already carries another lane's name
		}
		laneOwner[botLane] = driverIndex;
	}

	return (laneOwner[botLane] == driverIndex) ? botLane : -1;
}

#endif // AP_NAVREC_PLAYBACK_LOGIC_H
