#ifndef AP_NAVREC_FORMAT_H
#define AP_NAVREC_FORMAT_H

// ============================================================================
// AI lap recording container, format version 2.
//
// Everything in this header is FREESTANDING: no engine headers, no globals, no
// file IO, no logging. It encodes, decodes, decimates and classifies, and it
// does all of it in caller-supplied memory. That is what lets tools/test-navrec.c
// compile the real code rather than a copy of it.
//
// The full byte-level specification, including why each threshold has the value
// it has, is tools/navrec/FORMAT.md. This header is the implementation of that
// document and the two are meant to be read together.
//
// The engine side lives in ap/ap_navrec.c: sampling, the options, the file IO
// and the injection into sdata->NavPath_ptr*.
// ============================================================================

#include <math.h>
#include <stddef.h>
#include <string.h>

#define AP_NAVREC_FORMAT_VERSION_V2 2
#define AP_NAVREC_FORMAT_VERSION_V3 3
#define AP_NAVREC_FORMAT_VERSION    AP_NAVREC_FORMAT_VERSION_V2
#define AP_NAVREC_HEADER_BYTES      96
#define AP_NAVREC_HEADER_BYTES_V3   128
#define AP_NAVREC_LAPDIR_BYTES   32
#define AP_NAVREC_TRAILER_BYTES  8
#define AP_NAVREC_NODE_BYTES     20
#define AP_NAVREC_STAMP_BYTES    4

// The engine has three nav lanes (NavPath_ptrHeader[3]) and a submission is up
// to three laps by one driver, one per lane. The two caps are the same number
// for that reason, not by coincidence, so they are kept as separate names.
#define AP_NAVREC_LANES    3
#define AP_NAVREC_MAX_LAPS 3

#define AP_NAVREC_MIN_NODES 8
#define AP_NAVREC_MAX_NODES 1024

// Target nodes per lap. Retail Crash Cove uses 227 to 239 for a lap, so a
// generated line that lands in the same range consumes distToNextNav at the same
// magnitude the engine's own follower expects.
#define AP_NAVREC_TARGET_NODES 230

#define AP_NAVREC_NAME_FIELD      32
#define AP_NAVREC_NAME_CHARS      24
#define AP_NAVREC_CLIENTVER_FIELD 32

// Fourteen times the largest legitimate file (three laps of 1024 nodes is about
// 74 KiB). A reader refuses anything larger before it allocates.
#define AP_NAVREC_MAX_FILE_BYTES (1024u * 1024u)

// pathChangeOpcode sentinel. MUST be positive and at or above the engine's
// path-change cap (3 << 10 == 0xC00) so `changeOpcode < cap` fails and no lane
// change fires. Zero decodes as lane 0 / node 0 and would teleport an overtaking
// bot to the start line, and BOTS.c runs that path for ordinary bots, not just
// bosses.
#define AP_NAVREC_NO_LANE_CHANGE 0x7FFF

// NavFrame flag bit 0x4000 is one of the two terms in BOTS_Killplane's rewind
// loop: `while (backCount == currNav || (frame->flags & 0x4000))`. A node that
// sets it is never accepted as a rewind target, so a lane on which EVERY node
// sets it gives that loop no exit. A recorder must never emit it and a reader
// must never pass one through from a file it did not write.
#define AP_NAVREC_FLAG_KILLPLANE_SKIP 0x4000

// Shortcut corridor, in NavFrame units (world units shifted right by 8). Retail
// lanes sit about 500 units apart and retail node spacing reaches 499, so a
// driver exactly on a retail line but halfway between two of its nodes already
// measures about 250 from the nearest node. 1200 is roughly two lane widths
// outside the outermost lane plus that sampling slack: a wide line through a
// corner does not reach it, leaving the road does.
#define AP_NAVREC_SHORTCUT_CORRIDOR_UNITS 1200

// Consecutive out-of-corridor nodes needed to call a lap a shortcut. Nodes are
// spaced by arc length, so 8 of about 230 is roughly 3.5 percent of the lap's
// driven distance, one to two seconds. One node outside is a wall bump; eight in
// a row is a route the retail line does not contain.
#define AP_NAVREC_SHORTCUT_MIN_RUN_NODES 8

// Lateral offset used to synthesise the lanes a file did not supply, in
// NavFrame units. Matches the measured retail lane separation.
#define AP_NAVREC_FALLBACK_LANE_OFFSET 500

// Uniform grid over the retail corridor, so the shortcut test costs a bounded
// amount per node instead of scanning every retail node. See GridBuild.
#define AP_NAVREC_GRID_DIM    64
#define AP_NAVREC_GRID_CELLS  (AP_NAVREC_GRID_DIM * AP_NAVREC_GRID_DIM)
#define AP_NAVREC_GRID_POINTS (AP_NAVREC_LANES * AP_NAVREC_MAX_NODES)

// shortcutFlag values. UNKNOWN is a distinct value on purpose: "no shortcut
// detected" and "there was nothing to detect against" are different facts and a
// custom track produces the second one.
#define AP_NAVREC_SHORTCUT_NO      0
#define AP_NAVREC_SHORTCUT_YES     1
#define AP_NAVREC_SHORTCUT_UNKNOWN 2

// trackKind values.
#define AP_NAVREC_TRACK_RETAIL 0
#define AP_NAVREC_TRACK_NONAV  1

// A zero identity is the legacy retail interpretation: levelId names the
// actual retail track. Custom packages use a permanent author-controlled UUID
// plus a navigation compatibility revision. The physical level slot is still
// retained separately in levelId, but never decides custom-track compatibility.
#define AP_NAVREC_IDENTITY_RETAIL 0
#define AP_NAVREC_IDENTITY_CUSTOM 1
#define AP_NAVREC_TRACK_UUID_BYTES 16

// Reader results. Named rather than a bare 0/1 so a rejection produces a log
// line that says which rule failed.
enum
{
	AP_NAVREC_OK = 0,
	AP_NAVREC_ERR_TOO_SMALL,
	AP_NAVREC_ERR_MAGIC,
	AP_NAVREC_ERR_VERSION,
	AP_NAVREC_ERR_HEADER_SHAPE,
	AP_NAVREC_ERR_SIZE,
	AP_NAVREC_ERR_LAPCOUNT,
	AP_NAVREC_ERR_HASH,
	AP_NAVREC_ERR_NODECOUNT,
	AP_NAVREC_ERR_OFFSETS,
	AP_NAVREC_ERR_OVERLAP,
	AP_NAVREC_ERR_FLAT_GOBACK,
	AP_NAVREC_ERR_TIMESTAMPS,
	AP_NAVREC_ERR_CAPACITY
};

#define AP_NAVREC_STATIC_ASSERT(cond, name) typedef char ap_navrec_assert_##name[(cond) ? 1 : -1]

// Mirror of the engine's struct NavFrame (include/namespace_Bots.h), field for
// field and byte for byte. It exists so this header stays freestanding; the
// engine translation unit asserts the two agree, so a decomp change to NavFrame
// breaks the build here rather than silently writing a malformed file.
struct AP_NavRecNode
{
	short         posX;             // 0x00
	short         posY;             // 0x02
	short         posZ;             // 0x04
	unsigned char rot[4];           // 0x06, only rot[1] (yaw >> 4) is meaningful
	short         distToNextNavXYZ; // 0x0A
	short         distToNextNavXZ;  // 0x0C
	short         flags;            // 0x0E
	short         pathChangeOpcode; // 0x10
	unsigned char goBackCount;      // 0x12
	unsigned char specialBits;      // 0x13
};

AP_NAVREC_STATIC_ASSERT(sizeof(struct AP_NavRecNode) == AP_NAVREC_NODE_BYTES, node_size);

// One raw per-frame sample, in world units. This is what the recorder banks; it
// is never written to disk.
//
// `checkpoint` is the driver's own checkpoint.currentIndex at that frame, and it
// is the reason this field exists at all. goBackCount is NOT a spare byte: the
// engine reads it as a checkpoint index (BOTS.c assigns it to
// ai_quadblock_checkpointIndex) and BOTS_Killplane rewinds with
// `while (backCount == currNav || (frame->flags & 0x4000))`. That loop's only
// exit is goBackCount VARYING along the lane, so a lane built with a constant
// goBackCount hangs the game the moment a bot falls off while its own checkpoint
// index happens to equal that constant. Recording the driver's index per sample
// and carrying it into each node is what makes the loop terminate.
struct AP_NavRecSample
{
	int            x;
	int            y;
	int            z;
	short          rotY;
	unsigned short flags;
	unsigned char  checkpoint;
};

struct AP_NavRecLapInfo
{
	unsigned int   nodeCount;
	unsigned int   lapFrames;
	unsigned int   sampleCount;
	unsigned char  clean;
	unsigned char  shortcut;
	unsigned char  laneHint;
	unsigned short maskFires;  // Mask held item fired during this lap
	unsigned short turboFires; // Turbo held item fired during this lap
};

// Everything the writer needs that is not per lap.
struct AP_NavRecMeta
{
	int           levelId;
	const char   *clientVersion;    // may be NULL
	const char   *driverName;       // already sanitized, may be NULL or empty
	short         characterId;      // -1 unknown
	short         difficultyPreset; // -1 unknown
	unsigned char shortcutTier;     // 0 unknown, 1 easy, 2 medium, 3 hard
	unsigned int  trackKind;
	unsigned char identityKind;
	unsigned char trackUuid[AP_NAVREC_TRACK_UUID_BYTES];
	unsigned int  navRevision;
};

// What the reader recovers. Text fields are NUL terminated here even though the
// on-disk fields need not be.
struct AP_NavRecFileInfo
{
	unsigned int            formatVersion;
	unsigned int            totalSize;
	unsigned int            trackKind;
	int                     levelId;
	char                    clientVersion[AP_NAVREC_CLIENTVER_FIELD + 1];
	char                    driverName[AP_NAVREC_NAME_FIELD + 1];
	short                   characterId;
	short                   difficultyPreset;
	unsigned char           shortcutTier;
	unsigned int            lapCount;
	unsigned char           identityKind;
	unsigned char           trackUuid[AP_NAVREC_TRACK_UUID_BYTES];
	unsigned int            navRevision;
	struct AP_NavRecLapInfo laps[AP_NAVREC_MAX_LAPS];
};

static int AP_NavRecFormat_IdentityMatches(const struct AP_NavRecFileInfo *info, int levelId,
                                           unsigned char identityKind, const unsigned char trackUuid[AP_NAVREC_TRACK_UUID_BYTES],
                                           unsigned int navRevision)
{
	if ((info == NULL) || (info->levelId != levelId))
		return 0;
	if (identityKind == AP_NAVREC_IDENTITY_RETAIL)
		return info->identityKind == AP_NAVREC_IDENTITY_RETAIL;
	if ((identityKind != AP_NAVREC_IDENTITY_CUSTOM) || (trackUuid == NULL))
		return 0;
	return (info->identityKind == AP_NAVREC_IDENTITY_CUSTOM) &&
	       (memcmp(info->trackUuid, trackUuid, AP_NAVREC_TRACK_UUID_BYTES) == 0) &&
	       (info->navRevision == navRevision);
}

// Bounded-cost lookup over the retail corridor. Built once per level, queried
// once per decimated node.
struct AP_NavRecGrid
{
	int          minX;
	int          minZ;
	int          cell; // cell size in NavFrame units, never below the threshold
	int          nx;
	int          nz;
	unsigned int points;
	unsigned int cellStart[AP_NAVREC_GRID_CELLS + 1];
	unsigned short order[AP_NAVREC_GRID_POINTS];
};

// ---------------------------------------------------------------------------
// Little-endian primitives. Explicit byte work rather than a struct overlay, so
// the file layout does not depend on the compiler's padding decisions and a
// misaligned offset can never fault.
// ---------------------------------------------------------------------------

static void AP_NavRecFormat_PutU16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xFFu);
	p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void AP_NavRecFormat_PutU32(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xFFu);
	p[1] = (unsigned char)((v >> 8) & 0xFFu);
	p[2] = (unsigned char)((v >> 16) & 0xFFu);
	p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static unsigned int AP_NavRecFormat_GetU16(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int AP_NavRecFormat_GetU32(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void AP_NavRecFormat_PutU64(unsigned char *p, unsigned long long v)
{
	int i;
	for (i = 0; i < 8; i++)
		p[i] = (unsigned char)((v >> (8 * i)) & 0xFFull);
}

static unsigned long long AP_NavRecFormat_GetU64(const unsigned char *p)
{
	unsigned long long v = 0;
	int                i;
	for (i = 7; i >= 0; i--)
		v = (v << 8) | (unsigned long long)p[i];
	return v;
}

// FNV-1a 64. Detects truncation and corruption. It is not a signature: anyone
// who edits a lap can recompute it, and a submission pipeline that needs to
// trust a lap has to replay it.
static unsigned long long AP_NavRecFormat_Hash(const void *data, unsigned int size)
{
	const unsigned char *p = (const unsigned char *)data;
	unsigned long long   h = 0xCBF29CE484222325ull;
	unsigned int         i;

	for (i = 0; i < size; i++)
	{
		h ^= (unsigned long long)p[i];
		h *= 0x100000001B3ull;
	}
	return h;
}

// ---------------------------------------------------------------------------
// Driver name
// ---------------------------------------------------------------------------

// Printable ASCII only, trimmed, capped. Other bytes are DROPPED rather than
// substituted, so a name cannot smuggle a control character, a newline or a NUL
// into a file that will later be drawn next to a kart. outCap counts the NUL.
//
// Applied on the WRITE path to what the player configured, and again on the READ
// path to whatever a file supplies, because a file may have come from anyone.
static void AP_NavRecFormat_SanitizeName(const char *in, char *out, unsigned int outCap)
{
	unsigned int n = 0;
	unsigned int limit;

	if ((out == NULL) || (outCap == 0))
		return;

	limit = outCap - 1u;
	if (limit > AP_NAVREC_NAME_CHARS)
		limit = AP_NAVREC_NAME_CHARS;

	if (in != NULL)
	{
		while ((*in != '\0') && (n < limit))
		{
			unsigned char c = (unsigned char)*in++;
			if ((c >= 0x20u) && (c <= 0x7Eu))
				out[n++] = (char)c;
		}
	}

	// Trim. Leading spaces first, then trailing, so a name that is nothing but
	// spaces collapses to empty rather than to a single space.
	{
		unsigned int lead = 0;
		while ((lead < n) && (out[lead] == ' '))
			lead++;
		if (lead > 0)
		{
			memmove(out, out + lead, n - lead);
			n -= lead;
		}
		while ((n > 0) && (out[n - 1] == ' '))
			n--;
	}

	out[n] = '\0';
}

// ---------------------------------------------------------------------------
// Node sanitising, applied to anything read from a file
// ---------------------------------------------------------------------------

// A container can arrive from anyone: handed over chat, downloaded, or edited.
// Two node fields are not merely wrong when hostile, they hang the game, so they
// are FORCED rather than validated. Everything else in a node is geometry: a bad
// value drives a bot badly, which is a quality problem, not a safety one.
//
//   pathChangeOpcode  0 decodes as "lane 0, node 0" and teleports an overtaking
//                     bot to the start line. Forced to the sentinel.
//   flags 0x4000      excludes a node from being a killplane rewind target. Set
//                     on every node, it leaves BOTS_Killplane's loop no exit.
//                     Cleared.
//   goBackCount       must VARY along the lane, for that same loop. This one is
//                     REJECTED rather than forced, because every possible
//                     replacement constant is itself the bug being guarded
//                     against. See AP_NavRecFormat_GoBackVaries.
//
// goBackCount is the third field the killplane rewind depends on, and the one
// that cannot be repaired. The loop needs the value to VARY along the lane, and
// any single value a reader might substitute is exactly the failure it is
// guarding against, so a flat lane is refused instead of rewritten.
//
// 1 when the lane is usable, 0 when its goBackCount never changes.
static int AP_NavRecFormat_GoBackVaries(const struct AP_NavRecNode *nodes, unsigned int count)
{
	unsigned int n;

	if ((nodes == NULL) || (count < 2u))
		return 0;

	for (n = 1; n < count; n++)
	{
		if (nodes[n].goBackCount != nodes[0].goBackCount)
			return 1;
	}

	return 0;
}

// Returns how many nodes had to be changed, so the caller can say so.
static unsigned int AP_NavRecFormat_SanitizeNodes(struct AP_NavRecNode *nodes, unsigned int count)
{
	unsigned int changed = 0;
	unsigned int n;

	if (nodes == NULL)
		return 0;

	for (n = 0; n < count; n++)
	{
		int touched = 0;

		if (nodes[n].pathChangeOpcode != (short)AP_NAVREC_NO_LANE_CHANGE)
		{
			nodes[n].pathChangeOpcode = (short)AP_NAVREC_NO_LANE_CHANGE;
			touched = 1;
		}
		if ((nodes[n].flags & (short)AP_NAVREC_FLAG_KILLPLANE_SKIP) != 0)
		{
			nodes[n].flags = (short)(nodes[n].flags & ~(short)AP_NAVREC_FLAG_KILLPLANE_SKIP);
			touched = 1;
		}
		if (touched)
			changed++;
	}

	return changed;
}

// ---------------------------------------------------------------------------
// Decimation
// ---------------------------------------------------------------------------

static double AP_NavRecFormat_SampleDist(const struct AP_NavRecSample *a, const struct AP_NavRecSample *b)
{
	double dx = (double)(b->x - a->x);
	double dz = (double)(b->z - a->z);
	return sqrt((dx * dx) + (dz * dz));
}

// Fill distToNextNavXYZ / distToNextNavXZ for a closed loop of nodes. These are
// what the bot consumes as travelled distance, so they have to be geometrically
// true or the bot walks the path at the wrong rate.
static void AP_NavRecFormat_FillDistances(struct AP_NavRecNode *nodes, unsigned int count)
{
	unsigned int n;

	for (n = 0; n < count; n++)
	{
		const struct AP_NavRecNode *cur = &nodes[n];
		const struct AP_NavRecNode *nxt = &nodes[(n + 1u) % count];

		double dx = (double)(nxt->posX - cur->posX);
		double dy = (double)(nxt->posY - cur->posY);
		double dz = (double)(nxt->posZ - cur->posZ);

		double xz = sqrt((dx * dx) + (dz * dz));
		double xyz = sqrt((dx * dx) + (dy * dy) + (dz * dz));

		nodes[n].distToNextNavXZ = (short)((xz > 32767.0) ? 32767.0 : xz);
		nodes[n].distToNextNavXYZ = (short)((xyz > 32767.0) ? 32767.0 : xyz);
	}
}

// Arc-length decimate one lap of raw samples into `targetNodes` evenly spaced
// nodes, and record for each node the frame at which the driver reached it.
//
// Arc length rather than frame index: sampling by time would let slow corners
// hoard nodes and leave straights with almost none. The recorder takes one
// sample per race frame, so a sample's index IS its frame offset within the lap,
// which is what makes the timestamps free here.
//
// Returns the node count written, or 0 when the lap is unusable.
static unsigned int AP_NavRecFormat_Decimate(const struct AP_NavRecSample *samples, unsigned int sampleCount, unsigned int targetNodes,
                                             struct AP_NavRecNode *outNodes, unsigned int *outStamps)
{
	double       total = 0.0;
	double       step;
	double       walked = 0.0;
	double       nextMark = 0.0;
	unsigned int written = 0;
	unsigned int i;

	if ((samples == NULL) || (outNodes == NULL) || (outStamps == NULL))
		return 0;
	if (sampleCount < 4u)
		return 0;
	if ((targetNodes < AP_NAVREC_MIN_NODES) || (targetNodes > AP_NAVREC_MAX_NODES))
		return 0;

	for (i = 1; i < sampleCount; i++)
		total += AP_NavRecFormat_SampleDist(&samples[i - 1u], &samples[i]);

	if (total < 1.0)
		return 0;

	step = total / (double)targetNodes;

	for (i = 1; (i < sampleCount) && (written < targetNodes); i++)
	{
		double segLen = AP_NavRecFormat_SampleDist(&samples[i - 1u], &samples[i]);
		if (segLen <= 0.0)
			continue;

		while ((walked + segLen >= nextMark) && (written < targetNodes))
		{
			double                        t = (nextMark - walked) / segLen;
			const struct AP_NavRecSample *a = &samples[i - 1u];
			const struct AP_NavRecSample *b = &samples[i];

			double px = (double)a->x + ((double)(b->x - a->x) * t);
			double py = (double)a->y + ((double)(b->y - a->y) * t);
			double pz = (double)a->z + ((double)(b->z - a->z) * t);

			struct AP_NavRecNode *nf = &outNodes[written];
			memset(nf, 0, sizeof *nf);

			// NavFrame positions are posCurr >> 8, the space BOTS.c compares
			// against (it derives estimatePosition the same way).
			nf->posX = (short)((int)px >> 8);
			nf->posY = (short)((int)py >> 8);
			nf->posZ = (short)((int)pz >> 8);

			// rot[] holds the CTR angle >> 4; only yaw matters for a ground path.
			nf->rot[1] = (unsigned char)((unsigned int)(a->rotY & 0xFFF) >> 4);

			// 0x4000 is never emitted: see AP_NAVREC_FLAG_KILLPLANE_SKIP.
			nf->flags = (short)(a->flags & ~(unsigned short)AP_NAVREC_FLAG_KILLPLANE_SKIP);
			nf->pathChangeOpcode = AP_NAVREC_NO_LANE_CHANGE;

			// The driver's own checkpoint index at this point on the track. NOT a
			// constant: BOTS_Killplane's rewind loop terminates only because this
			// varies along the lane.
			nf->goBackCount = a->checkpoint;
			nf->specialBits = 0;

			// The frame this node sits on. Samples are one per frame, so the
			// interpolation parameter lands the timestamp between the two frames
			// the node was interpolated from.
			{
				double frame = (double)(i - 1u) + t;
				if (frame < 0.0)
					frame = 0.0;
				outStamps[written] = (unsigned int)frame;
			}

			written++;
			nextMark += step;
		}

		walked += segLen;
	}

	if (written < AP_NAVREC_MIN_NODES)
		return 0;

	// Monotonicity is a format requirement, and floating point plus truncation
	// is not a proof of it. Clamp rather than trust: a node can never be timed
	// before the node in front of it.
	outStamps[0] = 0;
	for (i = 1; i < written; i++)
	{
		if (outStamps[i] < outStamps[i - 1u])
			outStamps[i] = outStamps[i - 1u];
	}

	AP_NavRecFormat_FillDistances(outNodes, written);
	return written;
}

// ---------------------------------------------------------------------------
// Shortcut classification
// ---------------------------------------------------------------------------

// Build a uniform grid over the retail corridor.
//
// corridorXZ is an interleaved (x, z) array in NavFrame units and corridorPoints
// is the number of PAIRS. The cell size is never smaller than the corridor
// threshold, which is what makes the 3 by 3 neighbourhood of a query cell
// sufficient: any point within the threshold of a query lies in one of those
// nine cells, by construction.
//
// Returns 1 when the grid is usable, 0 when there is nothing to build over.
static int AP_NavRecFormat_GridBuild(struct AP_NavRecGrid *g, const short *corridorXZ, unsigned int corridorPoints)
{
	int          maxX;
	int          maxZ;
	int          spanX;
	int          spanZ;
	unsigned int i;
	unsigned int cells;

	if ((g == NULL) || (corridorXZ == NULL) || (corridorPoints == 0u))
		return 0;
	if (corridorPoints > AP_NAVREC_GRID_POINTS)
		corridorPoints = AP_NAVREC_GRID_POINTS;

	memset(g, 0, sizeof *g);
	g->points = corridorPoints;

	g->minX = corridorXZ[0];
	g->minZ = corridorXZ[1];
	maxX = g->minX;
	maxZ = g->minZ;

	for (i = 1; i < corridorPoints; i++)
	{
		int x = corridorXZ[i * 2u];
		int z = corridorXZ[(i * 2u) + 1u];

		if (x < g->minX)
			g->minX = x;
		if (x > maxX)
			maxX = x;
		if (z < g->minZ)
			g->minZ = z;
		if (z > maxZ)
			maxZ = z;
	}

	spanX = maxX - g->minX;
	spanZ = maxZ - g->minZ;

	// Cell size: at least the corridor threshold, and large enough that the
	// track's own extent fits inside AP_NAVREC_GRID_DIM cells on each axis.
	g->cell = AP_NAVREC_SHORTCUT_CORRIDOR_UNITS;
	{
		int needX = (spanX / AP_NAVREC_GRID_DIM) + 1;
		int needZ = (spanZ / AP_NAVREC_GRID_DIM) + 1;

		if (needX > g->cell)
			g->cell = needX;
		if (needZ > g->cell)
			g->cell = needZ;
	}

	g->nx = (spanX / g->cell) + 1;
	g->nz = (spanZ / g->cell) + 1;
	if (g->nx > AP_NAVREC_GRID_DIM)
		g->nx = AP_NAVREC_GRID_DIM;
	if (g->nz > AP_NAVREC_GRID_DIM)
		g->nz = AP_NAVREC_GRID_DIM;

	cells = (unsigned int)(g->nx * g->nz);

	// Counting sort into cellStart / order, so a cell's points are contiguous.
	for (i = 0; i < corridorPoints; i++)
	{
		int cx = (corridorXZ[i * 2u] - g->minX) / g->cell;
		int cz = (corridorXZ[(i * 2u) + 1u] - g->minZ) / g->cell;

		if (cx >= g->nx)
			cx = g->nx - 1;
		if (cz >= g->nz)
			cz = g->nz - 1;

		g->cellStart[(unsigned int)((cz * g->nx) + cx) + 1u]++;
	}

	for (i = 0; i < cells; i++)
		g->cellStart[i + 1u] += g->cellStart[i];

	// Place in one pass, using cellStart itself as the moving cursor rather than
	// a second array. Afterwards cellStart[c] holds the END of cell c, so one
	// right shift restores it to the start of cell c with the end at c + 1.
	for (i = 0; i < corridorPoints; i++)
	{
		int cx = (corridorXZ[i * 2u] - g->minX) / g->cell;
		int cz = (corridorXZ[(i * 2u) + 1u] - g->minZ) / g->cell;

		if (cx >= g->nx)
			cx = g->nx - 1;
		if (cz >= g->nz)
			cz = g->nz - 1;

		g->order[g->cellStart[(unsigned int)((cz * g->nx) + cx)]++] = (unsigned short)i;
	}

	for (i = cells; i > 0u; i--)
		g->cellStart[i] = g->cellStart[i - 1u];
	g->cellStart[0] = 0;

	return 1;
}

// 1 when some corridor point lies within the threshold of (x, z).
static int AP_NavRecFormat_GridNear(const struct AP_NavRecGrid *g, const short *corridorXZ, int x, int z)
{
	const double threshold = (double)AP_NAVREC_SHORTCUT_CORRIDOR_UNITS * (double)AP_NAVREC_SHORTCUT_CORRIDOR_UNITS;
	int          cx;
	int          cz;
	int          dx;
	int          dz;

	// A query outside the grid's extent still has to look at the edge cells: the
	// nearest corridor point may sit just inside the boundary.
	cx = (x - g->minX) / g->cell;
	cz = (z - g->minZ) / g->cell;
	if (x < g->minX)
		cx = 0;
	if (z < g->minZ)
		cz = 0;
	if (cx >= g->nx)
		cx = g->nx - 1;
	if (cz >= g->nz)
		cz = g->nz - 1;
	if (cx < 0)
		cx = 0;
	if (cz < 0)
		cz = 0;

	for (dz = -1; dz <= 1; dz++)
	{
		int qz = cz + dz;

		if ((qz < 0) || (qz >= g->nz))
			continue;

		for (dx = -1; dx <= 1; dx++)
		{
			int          qx = cx + dx;
			unsigned int at;
			unsigned int end;

			if ((qx < 0) || (qx >= g->nx))
				continue;

			at = g->cellStart[(unsigned int)((qz * g->nx) + qx)];
			end = g->cellStart[(unsigned int)((qz * g->nx) + qx) + 1u];

			for (; at < end; at++)
			{
				unsigned int p = g->order[at];
				double       ddx = (double)x - (double)corridorXZ[p * 2u];
				double       ddz = (double)z - (double)corridorXZ[(p * 2u) + 1u];

				if (((ddx * ddx) + (ddz * ddz)) <= threshold)
					return 1;
			}
		}
	}

	return 0;
}

// A run of AP_NAVREC_SHORTCUT_MIN_RUN_NODES consecutive nodes outside the
// corridor marks the lap. A grid with no points means the level shipped no nav
// table, which is the custom-track case, and the answer is UNKNOWN rather than
// NO.
static int AP_NavRecFormat_ClassifyShortcut(const struct AP_NavRecNode *nodes, unsigned int nodeCount, const struct AP_NavRecGrid *grid,
                                            const short *corridorXZ)
{
	unsigned int run = 0;
	unsigned int n;

	if ((nodes == NULL) || (grid == NULL) || (corridorXZ == NULL) || (grid->points == 0u))
		return AP_NAVREC_SHORTCUT_UNKNOWN;

	for (n = 0; n < nodeCount; n++)
	{
		if (AP_NavRecFormat_GridNear(grid, corridorXZ, nodes[n].posX, nodes[n].posZ))
		{
			run = 0;
			continue;
		}

		run++;
		if (run >= AP_NAVREC_SHORTCUT_MIN_RUN_NODES)
			return AP_NAVREC_SHORTCUT_YES;
	}

	return AP_NAVREC_SHORTCUT_NO;
}

// ---------------------------------------------------------------------------
// Synthetic lane fallback
// ---------------------------------------------------------------------------

// Displace a recorded line perpendicular to its own direction of travel. This is
// what fills the engine lanes a file did not supply, so a one-lap file still
// yields three usable lanes. It is deliberately NOT a median or an envelope
// across laps: each lap in a version 2 file is one person's real line and
// averaging them would destroy the thing the format exists to preserve.
//
// goBackCount and flags come across untouched, so the synthesised lane keeps the
// checkpoint progression that makes the killplane rewind terminate.
static void AP_NavRecFormat_SynthLane(const struct AP_NavRecNode *src, unsigned int count, int lateral, struct AP_NavRecNode *dst)
{
	unsigned int n;

	if ((src == NULL) || (dst == NULL) || (count == 0u))
		return;

	for (n = 0; n < count; n++)
	{
		const struct AP_NavRecNode *cur = &src[n];
		const struct AP_NavRecNode *nxt = &src[(n + 1u) % count];

		double dirX = (double)(nxt->posX - cur->posX);
		double dirZ = (double)(nxt->posZ - cur->posZ);
		double len = sqrt((dirX * dirX) + (dirZ * dirZ));
		double perpX = 0.0;
		double perpZ = 0.0;

		if (len > 0.0001)
		{
			perpX = -dirZ / len;
			perpZ = dirX / len;
		}

		dst[n] = *cur;
		dst[n].posX = (short)((double)cur->posX + (perpX * (double)lateral));
		dst[n].posZ = (short)((double)cur->posZ + (perpZ * (double)lateral));
	}

	AP_NavRecFormat_FillDistances(dst, count);
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

static unsigned int AP_NavRecFormat_Size(const struct AP_NavRecLapInfo *laps, unsigned int lapCount)
{
	unsigned int size = (unsigned int)AP_NAVREC_HEADER_BYTES + ((unsigned int)AP_NAVREC_LAPDIR_BYTES * lapCount);
	unsigned int i;

	for (i = 0; i < lapCount; i++)
		size += laps[i].nodeCount * (unsigned int)(AP_NAVREC_NODE_BYTES + AP_NAVREC_STAMP_BYTES);

	return size + (unsigned int)AP_NAVREC_TRAILER_BYTES;
}

static unsigned int AP_NavRecFormat_SizeForMeta(const struct AP_NavRecMeta *meta, const struct AP_NavRecLapInfo *laps,
                                                unsigned int lapCount)
{
	unsigned int size = AP_NavRecFormat_Size(laps, lapCount);
	if ((meta != NULL) && (meta->identityKind == AP_NAVREC_IDENTITY_CUSTOM))
		size += AP_NAVREC_HEADER_BYTES_V3 - AP_NAVREC_HEADER_BYTES;
	return size;
}

static void AP_NavRecFormat_PutText(unsigned char *field, unsigned int fieldSize, const char *src)
{
	unsigned int n = 0;

	memset(field, 0, fieldSize);
	if (src == NULL)
		return;

	// The bound is tested BEFORE the dereference. src is normally a C string, but
	// it can also be a fixed-size config buffer filled to its last byte with no
	// NUL, and in that case the other order reads one past the array.
	while ((n < fieldSize) && (src[n] != '\0'))
	{
		field[n] = (unsigned char)src[n];
		n++;
	}
}

// Serialize a complete container into buf. Returns an AP_NAVREC_* code and, on
// success, the byte count in *outSize.
static int AP_NavRecFormat_Write(void *buf, unsigned int cap, const struct AP_NavRecMeta *meta, const struct AP_NavRecLapInfo *laps,
                                 unsigned int lapCount, const struct AP_NavRecNode *const *nodes, const unsigned int *const *stamps,
                                 unsigned int *outSize)
{
	unsigned char *p = (unsigned char *)buf;
	unsigned int   total;
	unsigned int   headerBytes;
	unsigned int   version;
	unsigned int   dirAt;
	unsigned int   payloadAt;
	unsigned int   i;

	if ((p == NULL) || (meta == NULL) || (laps == NULL) || (nodes == NULL) || (stamps == NULL))
		return AP_NAVREC_ERR_CAPACITY;
	if ((lapCount == 0u) || (lapCount > AP_NAVREC_MAX_LAPS))
		return AP_NAVREC_ERR_LAPCOUNT;

	for (i = 0; i < lapCount; i++)
	{
		if ((laps[i].nodeCount < AP_NAVREC_MIN_NODES) || (laps[i].nodeCount > AP_NAVREC_MAX_NODES))
			return AP_NAVREC_ERR_NODECOUNT;
	}

	if ((meta->identityKind != AP_NAVREC_IDENTITY_RETAIL) && (meta->identityKind != AP_NAVREC_IDENTITY_CUSTOM))
		return AP_NAVREC_ERR_HEADER_SHAPE;
	version = (meta->identityKind == AP_NAVREC_IDENTITY_CUSTOM) ? AP_NAVREC_FORMAT_VERSION_V3 : AP_NAVREC_FORMAT_VERSION_V2;
	headerBytes = (version == AP_NAVREC_FORMAT_VERSION_V3) ? AP_NAVREC_HEADER_BYTES_V3 : AP_NAVREC_HEADER_BYTES;
	dirAt = headerBytes;
	total = AP_NavRecFormat_SizeForMeta(meta, laps, lapCount);
	if (total > cap)
		return AP_NAVREC_ERR_CAPACITY;

	memset(p, 0, total);

	p[0] = 'N';
	p[1] = 'A';
	p[2] = 'V';
	p[3] = '2';
	AP_NavRecFormat_PutU16(p + 0x04, version);
	AP_NavRecFormat_PutU16(p + 0x06, headerBytes);
	AP_NavRecFormat_PutU32(p + 0x08, total);
	AP_NavRecFormat_PutU32(p + 0x0C, (unsigned int)meta->levelId);
	AP_NavRecFormat_PutText(p + 0x10, AP_NAVREC_CLIENTVER_FIELD, meta->clientVersion);
	AP_NavRecFormat_PutText(p + 0x30, AP_NAVREC_NAME_FIELD, meta->driverName);
	AP_NavRecFormat_PutU16(p + 0x50, (unsigned int)(unsigned short)meta->characterId);
	AP_NavRecFormat_PutU16(p + 0x52, (unsigned int)(unsigned short)meta->difficultyPreset);
	p[0x54] = meta->shortcutTier;
	p[0x55] = (unsigned char)lapCount;
	p[0x56] = AP_NAVREC_NODE_BYTES;
	p[0x57] = 0;
	AP_NavRecFormat_PutU32(p + 0x58, meta->trackKind);
	AP_NavRecFormat_PutU32(p + 0x5C, 0);
	if (version == AP_NAVREC_FORMAT_VERSION_V3)
	{
		p[0x60] = AP_NAVREC_IDENTITY_CUSTOM;
		memcpy(p + 0x64, meta->trackUuid, AP_NAVREC_TRACK_UUID_BYTES);
		AP_NavRecFormat_PutU32(p + 0x74, meta->navRevision);
	}

	payloadAt = dirAt + (AP_NAVREC_LAPDIR_BYTES * lapCount);

	for (i = 0; i < lapCount; i++)
	{
		unsigned char *d = p + dirAt + (AP_NAVREC_LAPDIR_BYTES * i);
		unsigned int   nodeBytes = laps[i].nodeCount * AP_NAVREC_NODE_BYTES;
		unsigned int   stampBytes = laps[i].nodeCount * AP_NAVREC_STAMP_BYTES;
		unsigned int   framesOffset = payloadAt;
		unsigned int   stampsOffset = payloadAt + nodeBytes;
		unsigned int   n;

		AP_NavRecFormat_PutU32(d + 0x00, laps[i].nodeCount);
		AP_NavRecFormat_PutU32(d + 0x04, laps[i].lapFrames);
		AP_NavRecFormat_PutU32(d + 0x08, laps[i].sampleCount);
		AP_NavRecFormat_PutU32(d + 0x0C, framesOffset);
		AP_NavRecFormat_PutU32(d + 0x10, stampsOffset);
		d[0x14] = laps[i].clean;
		d[0x15] = laps[i].shortcut;
		d[0x16] = laps[i].laneHint;
		d[0x17] = 0;
		AP_NavRecFormat_PutU16(d + 0x18, laps[i].maskFires);
		AP_NavRecFormat_PutU16(d + 0x1A, laps[i].turboFires);
		AP_NavRecFormat_PutU32(d + 0x1C, 0);

		// Nodes go out byte by byte rather than as a block memcpy of the mirror
		// struct. The layout is asserted identical, but writing the fields makes
		// the file's byte order this header's decision instead of the host's.
		for (n = 0; n < laps[i].nodeCount; n++)
		{
			const struct AP_NavRecNode *src = &nodes[i][n];
			unsigned char              *q = p + framesOffset + (n * AP_NAVREC_NODE_BYTES);

			AP_NavRecFormat_PutU16(q + 0x00, (unsigned int)(unsigned short)src->posX);
			AP_NavRecFormat_PutU16(q + 0x02, (unsigned int)(unsigned short)src->posY);
			AP_NavRecFormat_PutU16(q + 0x04, (unsigned int)(unsigned short)src->posZ);
			q[0x06] = src->rot[0];
			q[0x07] = src->rot[1];
			q[0x08] = src->rot[2];
			q[0x09] = src->rot[3];
			AP_NavRecFormat_PutU16(q + 0x0A, (unsigned int)(unsigned short)src->distToNextNavXYZ);
			AP_NavRecFormat_PutU16(q + 0x0C, (unsigned int)(unsigned short)src->distToNextNavXZ);
			AP_NavRecFormat_PutU16(q + 0x0E, (unsigned int)(unsigned short)src->flags);
			AP_NavRecFormat_PutU16(q + 0x10, (unsigned int)(unsigned short)src->pathChangeOpcode);
			q[0x12] = src->goBackCount;
			q[0x13] = src->specialBits;
		}

		for (n = 0; n < laps[i].nodeCount; n++)
			AP_NavRecFormat_PutU32(p + stampsOffset + (n * AP_NAVREC_STAMP_BYTES), stamps[i][n]);

		payloadAt += nodeBytes + stampBytes;
	}

	AP_NavRecFormat_PutU64(p + total - AP_NAVREC_TRAILER_BYTES, AP_NavRecFormat_Hash(p, total - AP_NAVREC_TRAILER_BYTES));

	if (outSize != NULL)
		*outSize = total;

	return AP_NAVREC_OK;
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

// [start, start + len) must lie inside [floorAt, ceilAt).
//
// The `start > ceilAt` test is NOT redundant. Without it, `ceilAt - start`
// underflows for a start beyond the file and yields a huge allowance, so a
// declared offset such as 0xFFFFFF00 passes the bound and the reader then walks
// off the end of the buffer.
static int AP_NavRecFormat_SpanOk(unsigned int start, unsigned int len, unsigned int floorAt, unsigned int ceilAt)
{
	if (start < floorAt)
		return 0;
	if (start > ceilAt)
		return 0;
	if (len > (ceilAt - start))
		return 0;
	return 1;
}

static int AP_NavRecFormat_SpansOverlap(unsigned int aAt, unsigned int aLen, unsigned int bAt, unsigned int bLen)
{
	if ((aLen == 0u) || (bLen == 0u))
		return 0;
	if ((aAt + aLen) <= bAt)
		return 0;
	if ((bAt + bLen) <= aAt)
		return 0;
	return 1;
}

// Decode a container into caller-supplied storage. outNodes and outStamps are
// [AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES]; only the first info->lapCount rows
// are written.
//
// The whole file is rejected, never a single lap, and the hash is verified
// BEFORE any offset is followed so a corrupted offset is never dereferenced.
static int AP_NavRecFormat_Read(const void *buf, unsigned int size, struct AP_NavRecFileInfo *info,
                                struct AP_NavRecNode outNodes[][AP_NAVREC_MAX_NODES], unsigned int outStamps[][AP_NAVREC_MAX_NODES])
{
	const unsigned char *p = (const unsigned char *)buf;
	unsigned int         total;
	unsigned int         lapCount;
	unsigned int         version;
	unsigned int         headerBytes;
	unsigned int         dirAt;
	unsigned int         payloadFloor;
	unsigned int         hashAt;
	unsigned int         spanAt[AP_NAVREC_MAX_LAPS * 2];
	unsigned int         spanLen[AP_NAVREC_MAX_LAPS * 2];
	unsigned int         spans = 0;
	unsigned int         i;
	unsigned int         j;

	if ((p == NULL) || (info == NULL) || (outNodes == NULL) || (outStamps == NULL))
		return AP_NAVREC_ERR_CAPACITY;

	if (size < (unsigned int)(AP_NAVREC_HEADER_BYTES + AP_NAVREC_LAPDIR_BYTES + AP_NAVREC_TRAILER_BYTES))
		return AP_NAVREC_ERR_TOO_SMALL;

	if ((p[0] != 'N') || (p[1] != 'A') || (p[2] != 'V') || (p[3] != '2'))
		return AP_NAVREC_ERR_MAGIC;

	version = AP_NavRecFormat_GetU16(p + 0x04);
	if ((version != AP_NAVREC_FORMAT_VERSION_V2) && (version != AP_NAVREC_FORMAT_VERSION_V3))
		return AP_NAVREC_ERR_VERSION;

	headerBytes = AP_NavRecFormat_GetU16(p + 0x06);
	if (((version == AP_NAVREC_FORMAT_VERSION_V2) && (headerBytes != AP_NAVREC_HEADER_BYTES)) ||
	    ((version == AP_NAVREC_FORMAT_VERSION_V3) && (headerBytes != AP_NAVREC_HEADER_BYTES_V3)))
		return AP_NAVREC_ERR_HEADER_SHAPE;
	dirAt = headerBytes;
	if (p[0x56] != AP_NAVREC_NODE_BYTES)
		return AP_NAVREC_ERR_HEADER_SHAPE;

	total = AP_NavRecFormat_GetU32(p + 0x08);
	if ((total != size) || (total > AP_NAVREC_MAX_FILE_BYTES))
		return AP_NAVREC_ERR_SIZE;

	lapCount = p[0x55];
	if ((lapCount == 0u) || (lapCount > AP_NAVREC_MAX_LAPS))
		return AP_NAVREC_ERR_LAPCOUNT;

	payloadFloor = dirAt + (AP_NAVREC_LAPDIR_BYTES * lapCount);
	if (total < (payloadFloor + AP_NAVREC_TRAILER_BYTES))
		return AP_NAVREC_ERR_SIZE;

	hashAt = total - AP_NAVREC_TRAILER_BYTES;
	if (AP_NavRecFormat_GetU64(p + hashAt) != AP_NavRecFormat_Hash(p, hashAt))
		return AP_NAVREC_ERR_HASH;

	memset(info, 0, sizeof *info);
	info->formatVersion = version;
	info->totalSize = total;
	info->levelId = (int)AP_NavRecFormat_GetU32(p + 0x0C);
	memcpy(info->clientVersion, p + 0x10, AP_NAVREC_CLIENTVER_FIELD);
	info->clientVersion[AP_NAVREC_CLIENTVER_FIELD] = '\0';
	memcpy(info->driverName, p + 0x30, AP_NAVREC_NAME_FIELD);
	info->driverName[AP_NAVREC_NAME_FIELD] = '\0';
	info->characterId = (short)(unsigned short)AP_NavRecFormat_GetU16(p + 0x50);
	info->difficultyPreset = (short)(unsigned short)AP_NavRecFormat_GetU16(p + 0x52);
	info->shortcutTier = p[0x54];
	info->trackKind = AP_NavRecFormat_GetU32(p + 0x58);
	info->lapCount = lapCount;
	if (version == AP_NAVREC_FORMAT_VERSION_V3)
	{
		if (p[0x60] != AP_NAVREC_IDENTITY_CUSTOM)
			return AP_NAVREC_ERR_HEADER_SHAPE;
		info->identityKind = AP_NAVREC_IDENTITY_CUSTOM;
		memcpy(info->trackUuid, p + 0x64, AP_NAVREC_TRACK_UUID_BYTES);
		info->navRevision = AP_NavRecFormat_GetU32(p + 0x74);
	}

	// Bounds first, for every lap, before a single byte of payload is read.
	for (i = 0; i < lapCount; i++)
	{
		const unsigned char *d = p + dirAt + (AP_NAVREC_LAPDIR_BYTES * i);
		unsigned int         nodeCount = AP_NavRecFormat_GetU32(d + 0x00);
		unsigned int         framesOffset = AP_NavRecFormat_GetU32(d + 0x0C);
		unsigned int         stampsOffset = AP_NavRecFormat_GetU32(d + 0x10);

		if ((nodeCount < AP_NAVREC_MIN_NODES) || (nodeCount > AP_NAVREC_MAX_NODES))
			return AP_NAVREC_ERR_NODECOUNT;

		if (!AP_NavRecFormat_SpanOk(framesOffset, nodeCount * AP_NAVREC_NODE_BYTES, payloadFloor, hashAt))
			return AP_NAVREC_ERR_OFFSETS;
		if (!AP_NavRecFormat_SpanOk(stampsOffset, nodeCount * AP_NAVREC_STAMP_BYTES, payloadFloor, hashAt))
			return AP_NAVREC_ERR_OFFSETS;

		spanAt[spans] = framesOffset;
		spanLen[spans] = nodeCount * AP_NAVREC_NODE_BYTES;
		spans++;
		spanAt[spans] = stampsOffset;
		spanLen[spans] = nodeCount * AP_NAVREC_STAMP_BYTES;
		spans++;
	}

	// No two payload spans may overlap, within a lap or between laps. Overlapping
	// spans are not a memory-safety problem once the bounds above hold, but they
	// are a sign the file was assembled rather than recorded, and they let one
	// lap's nodes masquerade as another's.
	for (i = 0; i < spans; i++)
	{
		for (j = i + 1u; j < spans; j++)
		{
			if (AP_NavRecFormat_SpansOverlap(spanAt[i], spanLen[i], spanAt[j], spanLen[j]))
				return AP_NAVREC_ERR_OVERLAP;
		}
	}

	for (i = 0; i < lapCount; i++)
	{
		const unsigned char *d = p + dirAt + (AP_NAVREC_LAPDIR_BYTES * i);
		unsigned int         nodeCount = AP_NavRecFormat_GetU32(d + 0x00);
		unsigned int         framesOffset = AP_NavRecFormat_GetU32(d + 0x0C);
		unsigned int         stampsOffset = AP_NavRecFormat_GetU32(d + 0x10);
		unsigned int         n;

		info->laps[i].nodeCount = nodeCount;
		info->laps[i].lapFrames = AP_NavRecFormat_GetU32(d + 0x04);
		info->laps[i].sampleCount = AP_NavRecFormat_GetU32(d + 0x08);
		info->laps[i].clean = d[0x14];
		info->laps[i].shortcut = d[0x15];
		info->laps[i].laneHint = d[0x16];
		info->laps[i].maskFires = (unsigned short)AP_NavRecFormat_GetU16(d + 0x18);
		info->laps[i].turboFires = (unsigned short)AP_NavRecFormat_GetU16(d + 0x1A);

		for (n = 0; n < nodeCount; n++)
		{
			const unsigned char  *q = p + framesOffset + (n * AP_NAVREC_NODE_BYTES);
			struct AP_NavRecNode *dst = &outNodes[i][n];

			dst->posX = (short)(unsigned short)AP_NavRecFormat_GetU16(q + 0x00);
			dst->posY = (short)(unsigned short)AP_NavRecFormat_GetU16(q + 0x02);
			dst->posZ = (short)(unsigned short)AP_NavRecFormat_GetU16(q + 0x04);
			dst->rot[0] = q[0x06];
			dst->rot[1] = q[0x07];
			dst->rot[2] = q[0x08];
			dst->rot[3] = q[0x09];
			dst->distToNextNavXYZ = (short)(unsigned short)AP_NavRecFormat_GetU16(q + 0x0A);
			dst->distToNextNavXZ = (short)(unsigned short)AP_NavRecFormat_GetU16(q + 0x0C);
			dst->flags = (short)(unsigned short)AP_NavRecFormat_GetU16(q + 0x0E);
			dst->pathChangeOpcode = (short)(unsigned short)AP_NavRecFormat_GetU16(q + 0x10);
			dst->goBackCount = q[0x12];
			dst->specialBits = q[0x13];
		}

		for (n = 0; n < nodeCount; n++)
			outStamps[i][n] = AP_NavRecFormat_GetU32(p + stampsOffset + (n * AP_NAVREC_STAMP_BYTES));

		if (outStamps[i][0] != 0u)
			return AP_NAVREC_ERR_TIMESTAMPS;
		for (n = 1; n < nodeCount; n++)
		{
			if (outStamps[i][n] < outStamps[i][n - 1u])
				return AP_NAVREC_ERR_TIMESTAMPS;
		}

		// The file may have come from anyone. Force the two node fields that hang
		// the engine rather than merely drive a bot badly, and REJECT on the
		// third, which cannot be forced into safety.
		AP_NavRecFormat_SanitizeNodes(outNodes[i], nodeCount);
		if (!AP_NavRecFormat_GoBackVaries(outNodes[i], nodeCount))
			return AP_NAVREC_ERR_FLAT_GOBACK;
	}

	// The driver name is drawn and logged, so it is sanitized here rather than at
	// each use site.
	{
		char clean[AP_NAVREC_NAME_CHARS + 1];

		AP_NavRecFormat_SanitizeName(info->driverName, clean, sizeof clean);
		memset(info->driverName, 0, sizeof info->driverName);
		memcpy(info->driverName, clean, strlen(clean));
	}

	return AP_NAVREC_OK;
}

static const char *AP_NavRecFormat_ErrorText(int code)
{
	switch (code)
	{
	case AP_NAVREC_OK:
		return "ok";
	case AP_NAVREC_ERR_TOO_SMALL:
		return "file shorter than the smallest valid container";
	case AP_NAVREC_ERR_MAGIC:
		return "bad magic (not a NAV2 container)";
	case AP_NAVREC_ERR_VERSION:
		return "unknown format version";
	case AP_NAVREC_ERR_HEADER_SHAPE:
		return "header size or node stride disagrees with this build";
	case AP_NAVREC_ERR_SIZE:
		return "declared size does not match the file";
	case AP_NAVREC_ERR_LAPCOUNT:
		return "lap count out of range";
	case AP_NAVREC_ERR_HASH:
		return "content hash mismatch (truncated or edited)";
	case AP_NAVREC_ERR_NODECOUNT:
		return "node count out of range";
	case AP_NAVREC_ERR_OFFSETS:
		return "lap payload offsets out of bounds";
	case AP_NAVREC_ERR_OVERLAP:
		return "lap payload spans overlap";
	case AP_NAVREC_ERR_FLAT_GOBACK:
		return "goBackCount is constant across a lap (would hang the killplane rewind)";
	case AP_NAVREC_ERR_TIMESTAMPS:
		return "timestamps are not monotonic";
	case AP_NAVREC_ERR_CAPACITY:
		return "buffer too small or missing";
	default:
		return "unknown error";
	}
}

#endif // AP_NAVREC_FORMAT_H
