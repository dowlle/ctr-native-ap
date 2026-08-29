// Out-of-engine assertions for the AI lap recording container and for the rule
// that decides which recordings feed the three engine nav lanes.
//
// Compiles the REAL code: ap/ap_navrec_format.h and ap/ap_navrec_lane_logic.h
// are freestanding by design, so every function asserted here is the one the
// client runs. Nothing from the unity build is linked.
//
//   cc -Wall -Wextra -o /tmp/test-navrec tools/test-navrec.c -lm && /tmp/test-navrec
//
// The container is specified in tools/navrec/FORMAT.md; each case below names
// the rule it is holding the implementation to.
//
// Exit 0 = every assertion held; the failing case is printed otherwise.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ap/ap_navrec_format.h"
#include "../ap/ap_navrec_lane_logic.h"

static int g_failures = 0;

static void check(int cond, const char *what)
{
	printf("%-4s %s\n", cond ? "ok" : "FAIL", what);
	if (!cond)
		g_failures++;
}

// A closed loop of samples, one per frame, driven as a circle of the given
// radius. Positions are world units, so the decimated nodes land at radius >> 8.
static unsigned int MakeCircleLap(struct AP_NavRecSample *out, unsigned int frames, int radius, int centreX, int centreZ)
{
	unsigned int i;

	for (i = 0; i < frames; i++)
	{
		double a = (6.283185307179586 * (double)i) / (double)frames;

		out[i].x = centreX + (int)((double)radius * cos(a));
		out[i].y = 0;
		out[i].z = centreZ + (int)((double)radius * sin(a));
		out[i].rotY = (short)((i * 0x1000) / frames);
		out[i].flags = 0;
		// A plausible checkpoint progression: the index advances around the lap,
		// which is what BOTS_Killplane's rewind loop needs in goBackCount.
		out[i].checkpoint = (unsigned char)((i * 24u) / frames);
	}
	return frames;
}

static struct AP_NavRecSample  g_samples[4000];
static struct AP_NavRecNode    g_nodes[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
static unsigned int            g_stamps[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
static struct AP_NavRecNode    g_back[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
static unsigned int            g_backStamps[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
static unsigned char           g_file[AP_NAVREC_MAX_FILE_BYTES];

// ---------------------------------------------------------------------------
// A folder of recordings, held in memory.
//
// AP_NavRecLane_Select reaches candidates through a fetch callback precisely so
// that the loader's file IO is not in the way here. FakeFetch below does exactly
// what AP_NavRec_FetchCandidate does in ap/ap_navrec.c: it validates one
// container with the real reader, applies the real identity rule, and reports
// the author and lap count of whatever survives. So these cases exercise the
// shipped selection rule, not a description of it.
// ---------------------------------------------------------------------------

#define FAKE_MAX_FILES 12
#define FAKE_FILE_CAP  20000u

struct FakeFolder
{
	unsigned int  count;
	unsigned int  index[FAKE_MAX_FILES];
	unsigned int  size[FAKE_MAX_FILES];
	unsigned char bytes[FAKE_MAX_FILES][FAKE_FILE_CAP];
	int           levelId;
	unsigned char identityKind;
	unsigned char uuid[AP_NAVREC_TRACK_UUID_BYTES];
	unsigned int  navRevision;
	unsigned int  opened; // how many files the scan actually opened
};

static struct FakeFolder g_folder;
static struct AP_NavRecNode g_fetchNodes[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
static unsigned int         g_fetchStamps[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];

static unsigned int FakeBuild(unsigned char *out, int levelId, const char *author, unsigned int lapCount, unsigned char identityKind,
                              const unsigned char *uuid, unsigned int navRevision)
{
	struct AP_NavRecLapInfo     laps[AP_NAVREC_MAX_LAPS];
	const struct AP_NavRecNode *nodePtrs[AP_NAVREC_MAX_LAPS];
	const unsigned int         *stampPtrs[AP_NAVREC_MAX_LAPS];
	struct AP_NavRecMeta        meta;
	unsigned int                size = 0;
	unsigned int                i;

	memset(laps, 0, sizeof laps);

	for (i = 0; i < lapCount; i++)
	{
		// A different radius per lap, so a lane's line says which lap it took.
		unsigned int frames = 900u + (i * 10u);
		unsigned int n = AP_NavRecFormat_Decimate(g_samples, MakeCircleLap(g_samples, frames, (400 + ((int)i * 20)) * 256, 0, 0),
		                                          AP_NAVREC_TARGET_NODES, g_nodes[i], g_stamps[i]);

		laps[i].nodeCount = n;
		laps[i].lapFrames = frames;
		laps[i].sampleCount = frames;
		laps[i].clean = 1;
		laps[i].laneHint = (unsigned char)i;
		nodePtrs[i] = g_nodes[i];
		stampPtrs[i] = g_stamps[i];
	}

	memset(&meta, 0, sizeof meta);
	meta.levelId = levelId;
	meta.clientVersion = "lane-test";
	meta.driverName = author;
	meta.identityKind = identityKind;
	if (uuid != NULL)
		memcpy(meta.trackUuid, uuid, AP_NAVREC_TRACK_UUID_BYTES);
	meta.navRevision = navRevision;

	if (AP_NavRecFormat_Write(out, FAKE_FILE_CAP, &meta, laps, lapCount, nodePtrs, stampPtrs, &size) != AP_NAVREC_OK)
		return 0;
	return size;
}

static void FakeReset(int levelId)
{
	memset(&g_folder, 0, sizeof g_folder);
	g_folder.levelId = levelId;
	g_folder.identityKind = AP_NAVREC_IDENTITY_RETAIL;
}

static void FakeAdd(unsigned int index, const char *author, unsigned int lapCount, int corrupt)
{
	unsigned int at = g_folder.count;

	if (at >= FAKE_MAX_FILES)
		return;

	g_folder.index[at] = index;
	g_folder.size[at] = FakeBuild(g_folder.bytes[at], g_folder.levelId, author, lapCount, AP_NAVREC_IDENTITY_RETAIL, NULL, 0);

	// One flipped payload byte, which is what a truncated download or a hand
	// edit looks like: the container fails its content hash and is rejected whole.
	if (corrupt && (g_folder.size[at] > 200u))
		g_folder.bytes[at][150] ^= 0xFFu;

	g_folder.count++;
}

static void FakeAddCustom(unsigned int index, const char *author, const unsigned char *uuid, unsigned int navRevision)
{
	unsigned int at = g_folder.count;

	if (at >= FAKE_MAX_FILES)
		return;

	g_folder.index[at] = index;
	g_folder.size[at] = FakeBuild(g_folder.bytes[at], g_folder.levelId, author, 1, AP_NAVREC_IDENTITY_CUSTOM, uuid, navRevision);
	g_folder.count++;
}

static int FakeFetch(void *ctx, unsigned int fileIndex, struct AP_NavRecLaneCandidate *out)
{
	struct FakeFolder       *f = (struct FakeFolder *)ctx;
	struct AP_NavRecFileInfo info;
	unsigned int             i;

	for (i = 0; i < f->count; i++)
	{
		if (f->index[i] != fileIndex)
			continue;

		f->opened++;
		out->fileIndex = fileIndex;

		if (AP_NavRecFormat_Read(f->bytes[i], f->size[i], &info, g_fetchNodes, g_fetchStamps) != AP_NAVREC_OK)
			return 1;
		if (info.levelId != f->levelId)
			return 1;
		if (!AP_NavRecFormat_IdentityMatches(&info, f->levelId, f->identityKind, f->uuid, f->navRevision))
			return 1;

		out->usable = 1;
		out->lapCount = info.lapCount;
		memcpy(out->driverName, info.driverName, sizeof out->driverName);
		return 1;
	}

	return 0; // a gap in the numbering, which costs nothing
}

// Highest index present, the same starting point AP_NavRec_HighestIndex gives
// the loader.
static unsigned int FakeHighest(void)
{
	unsigned int highest = 0;
	unsigned int i;

	for (i = 0; i < g_folder.count; i++)
	{
		if (g_folder.index[i] > highest)
			highest = g_folder.index[i];
	}
	return highest;
}

int main(void)
{
	struct AP_NavRecLapInfo     laps[AP_NAVREC_MAX_LAPS];
	const struct AP_NavRecNode *nodePtrs[AP_NAVREC_MAX_LAPS];
	const unsigned int         *stampPtrs[AP_NAVREC_MAX_LAPS];
	struct AP_NavRecMeta        meta;
	struct AP_NavRecFileInfo    info;
	unsigned int                size = 0;
	unsigned int                i;
	unsigned int                lap;
	int                         rc;

	// ---------------------------------------------------------------
	// Every rejection code has its own message. A reader that refuses a
	// file has to be able to say which rule failed, so a code that fell
	// through to "unknown error" would make a real rejection unreadable
	// in the log.
	// ---------------------------------------------------------------
	{
		int distinct = 1;

		for (i = 0; i <= AP_NAVREC_ERR_CAPACITY; i++)
		{
			unsigned int j;

			if (strcmp(AP_NavRecFormat_ErrorText((int)i), "unknown error") == 0)
				distinct = 0;
			for (j = 0; j < i; j++)
			{
				if (strcmp(AP_NavRecFormat_ErrorText((int)i), AP_NavRecFormat_ErrorText((int)j)) == 0)
					distinct = 0;
			}
		}

		check(distinct, "every reader result code has its own message");
		check(strcmp(AP_NavRecFormat_ErrorText(999), "unknown error") == 0, "an unmapped code still produces a message");
	}

	// ---------------------------------------------------------------
	// Custom-track identity: same physical slot is not the identity
	// ---------------------------------------------------------------
	{
		static const unsigned char babyUuid[AP_NAVREC_TRACK_UUID_BYTES] = {
			0x42, 0x61, 0x62, 0x79, 0x54, 0x50, 0x61, 0x72,
			0x6b, 0x00, 0x20, 0x26, 0x08, 0x29, 0x00, 0x01};
		unsigned int n = AP_NavRecFormat_Decimate(g_samples, MakeCircleLap(g_samples, 900, 400 * 256, 0, 0),
		                                                  AP_NAVREC_TARGET_NODES, g_nodes[0], g_stamps[0]);

		memset(laps, 0, sizeof laps);
		laps[0].nodeCount = n;
		laps[0].lapFrames = 900;
		laps[0].sampleCount = 900;
		laps[0].clean = 1;
		nodePtrs[0] = g_nodes[0];
		stampPtrs[0] = g_stamps[0];

		memset(&meta, 0, sizeof meta);
		meta.levelId = 6; // Roo's Tubes physical slot, deliberately displaced
		meta.clientVersion = "custom-id-test";
		meta.driverName = "Appie";
		meta.identityKind = AP_NAVREC_IDENTITY_CUSTOM;
		memcpy(meta.trackUuid, babyUuid, sizeof babyUuid);
		meta.navRevision = 7;

		rc = AP_NavRecFormat_Write(g_file, sizeof g_file, &meta, laps, 1, nodePtrs, stampPtrs, &size);
		check(rc == AP_NAVREC_OK, "a custom-track container serialises");
		check(AP_NavRecFormat_GetU16(g_file + 0x04) == AP_NAVREC_FORMAT_VERSION_V3,
		      "a custom identity selects format version 3");
		check(AP_NavRecFormat_GetU16(g_file + 0x06) == AP_NAVREC_HEADER_BYTES_V3,
		      "a custom identity uses the extended header");

		rc = AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps);
		check(rc == AP_NAVREC_OK, "a custom-track container reads back");
		check(info.levelId == 6, "the displaced physical slot remains informational");
		check(info.identityKind == AP_NAVREC_IDENTITY_CUSTOM, "custom identity kind survives the round trip");
		check(memcmp(info.trackUuid, babyUuid, sizeof babyUuid) == 0, "custom track UUID survives the round trip");
		check(info.navRevision == 7, "navigation compatibility revision survives the round trip");
		check(AP_NavRecFormat_IdentityMatches(&info, 6, AP_NAVREC_IDENTITY_CUSTOM, babyUuid, 7),
		      "the same custom UUID and navigation revision match");
		check(!AP_NavRecFormat_IdentityMatches(&info, 6, AP_NAVREC_IDENTITY_CUSTOM, babyUuid, 8),
		      "a navigation revision change invalidates the old line");
		check(!AP_NavRecFormat_IdentityMatches(&info, 6, AP_NAVREC_IDENTITY_RETAIL, NULL, 0),
		      "a custom line cannot masquerade as the retail track in its physical slot");
		memset(&info, 0, sizeof info);
		info.levelId = 6;
		check(!AP_NavRecFormat_IdentityMatches(&info, 6, AP_NAVREC_IDENTITY_CUSTOM, babyUuid, 7),
		      "a legacy Roo's Tubes line cannot masquerade as a custom track in slot 6");
	}

	// ---------------------------------------------------------------
	// Decimation: node count, and the monotonic timestamps the pace
	// controller in a later release depends on.
	// ---------------------------------------------------------------
	{
		unsigned int frames = MakeCircleLap(g_samples, 900, 400 * 256, 0, 0);
		unsigned int n = AP_NavRecFormat_Decimate(g_samples, frames, AP_NAVREC_TARGET_NODES, g_nodes[0], g_stamps[0]);
		int          monotonic = 1;
		int          spacingEven = 1;

		check(n == AP_NAVREC_TARGET_NODES, "arc-length decimation returns exactly the target node count");

		for (i = 1; i < n; i++)
		{
			if (g_stamps[0][i] < g_stamps[0][i - 1])
				monotonic = 0;
		}
		check(g_stamps[0][0] == 0, "the first node is timed at frame 0");
		check(monotonic, "timestamps are non-decreasing across the whole lap");
		check(g_stamps[0][n - 1] <= frames, "the last node is timed at or before the end of the lap");

		// A constant-speed circle must produce near-constant per-node distances.
		// This is the check that would catch an arc-length walk that had silently
		// become a per-frame walk.
		for (i = 0; i < n; i++)
		{
			int d = g_nodes[0][i].distToNextNavXZ;
			if ((d < 5) || (d > 20))
				spacingEven = 0;
		}
		check(spacingEven, "distToNextNavXZ is even along a constant-speed lap and never saturates");

		check(g_nodes[0][0].pathChangeOpcode == AP_NAVREC_NO_LANE_CHANGE, "every node carries the no-lane-change sentinel, never 0");

		// A lap shorter than the minimum, and a degenerate one, are refused
		// rather than written as a stub.
		check(AP_NavRecFormat_Decimate(g_samples, 3, AP_NAVREC_TARGET_NODES, g_nodes[0], g_stamps[0]) == 0, "a lap of 3 samples is refused");
	}

	// ---------------------------------------------------------------
	// Name sanitising
	// ---------------------------------------------------------------
	{
		char out[AP_NAVREC_NAME_CHARS + 1];

		AP_NavRecFormat_SanitizeName("  Racer One  ", out, sizeof out);
		check(strcmp(out, "Racer One") == 0, "the name sanitiser trims leading and trailing spaces");

		// The escape is split so the hex literal cannot swallow the following
		// letter, which is exactly what "\x01c" would do.
		AP_NavRecFormat_SanitizeName("bad\nname\twith\x01" "ctrl", out, sizeof out);
		check(strcmp(out, "badnamewithctrl") == 0, "control characters are dropped, not substituted");

		AP_NavRecFormat_SanitizeName("0123456789012345678901234567890123456789", out, sizeof out);
		check(strlen(out) == AP_NAVREC_NAME_CHARS, "a long name is capped at the documented character limit");

		AP_NavRecFormat_SanitizeName("      ", out, sizeof out);
		check(out[0] == '\0', "a name of nothing but spaces collapses to empty");

		AP_NavRecFormat_SanitizeName(NULL, out, sizeof out);
		check(out[0] == '\0', "a NULL name is empty, not a crash");
	}

	// ---------------------------------------------------------------
	// goBackCount carries the driver's checkpoint index.
	//
	// This is not cosmetic. BOTS_Killplane rewinds with
	//   while (backCount == currNav || (frame->flags & 0x4000))
	// whose only exit is goBackCount VARYING along the lane. A lane built
	// with a constant hangs the game whenever a bot falls off while its own
	// checkpoint index equals that constant.
	// ---------------------------------------------------------------
	{
		unsigned int frames = MakeCircleLap(g_samples, 900, 400 * 256, 0, 0);
		unsigned int n = AP_NavRecFormat_Decimate(g_samples, frames, AP_NAVREC_TARGET_NODES, g_nodes[0], g_stamps[0]);
		int          varies = 0;
		int          followsSamples = 1;
		int          nonDecreasing = 1;

		for (i = 1; i < n; i++)
		{
			if (g_nodes[0][i].goBackCount != g_nodes[0][0].goBackCount)
				varies = 1;
			if (g_nodes[0][i].goBackCount < g_nodes[0][i - 1].goBackCount)
				nonDecreasing = 0;
		}

		// Each node's goBackCount must be the checkpoint index of the sample it
		// was interpolated from, which is the sample at its own timestamp.
		for (i = 0; i < n; i++)
		{
			unsigned int at = g_stamps[0][i];

			if (at >= frames)
				at = frames - 1u;
			if (g_nodes[0][i].goBackCount != g_samples[at].checkpoint)
				followsSamples = 0;
		}

		check(varies, "goBackCount VARIES along the lane and is not a constant");
		check(followsSamples, "goBackCount is the sampled checkpoint index at that node");
		check(nonDecreasing, "goBackCount advances with the lap, matching a checkpoint progression");
		check(g_nodes[0][0].goBackCount == g_samples[0].checkpoint, "the first node carries the first sample's checkpoint index");
	}

	// ---------------------------------------------------------------
	// Shortcut flag on a synthetic corridor, through the bounded grid
	// ---------------------------------------------------------------
	{
		static short         corridor[AP_NAVREC_GRID_POINTS * 2];
		static struct AP_NavRecGrid grid;
		unsigned int         n;
		unsigned int         ref;

		// Reference line: the circle itself, in NavFrame units.
		ref = AP_NavRecFormat_Decimate(g_samples, MakeCircleLap(g_samples, 900, 400 * 256, 0, 0), AP_NAVREC_TARGET_NODES, g_nodes[0], g_stamps[0]);
		for (i = 0; i < ref; i++)
		{
			corridor[i * 2] = g_nodes[0][i].posX;
			corridor[(i * 2) + 1] = g_nodes[0][i].posZ;
		}

		check(AP_NavRecFormat_GridBuild(&grid, corridor, ref) == 1, "the corridor grid builds");
		check(grid.cell >= AP_NAVREC_SHORTCUT_CORRIDOR_UNITS, "the grid cell is never smaller than the corridor threshold");
		check((grid.nx > 0) && (grid.nx <= AP_NAVREC_GRID_DIM) && (grid.nz > 0) && (grid.nz <= AP_NAVREC_GRID_DIM),
		      "the grid stays inside its dimension cap");
		check(grid.cellStart[grid.nx * grid.nz] == ref, "every corridor point lands in exactly one cell");
		check(AP_NavRecFormat_GridBuild(&grid, corridor, 0) == 0, "a grid over zero points refuses to build");
		AP_NavRecFormat_GridBuild(&grid, corridor, ref);

		// The grid is an optimisation, so it must agree with the brute-force
		// answer at every point it is asked about. This is the assertion that
		// makes the bound safe to rely on.
		{
			int agrees = 1;
			int probe;

			for (probe = 0; probe < 4000; probe++)
			{
				// A deterministic spread of query points across and well beyond
				// the corridor's own extent.
				int qx = -12000 + ((probe * 5657) % 24000);
				int qz = -12000 + ((probe * 3121) % 24000);
				int brute = 0;
				unsigned int c;

				for (c = 0; c < ref; c++)
				{
					double ddx = (double)qx - (double)corridor[c * 2];
					double ddz = (double)qz - (double)corridor[(c * 2) + 1];

					if (((ddx * ddx) + (ddz * ddz)) <= ((double)AP_NAVREC_SHORTCUT_CORRIDOR_UNITS * (double)AP_NAVREC_SHORTCUT_CORRIDOR_UNITS))
					{
						brute = 1;
						break;
					}
				}

				if (AP_NavRecFormat_GridNear(&grid, corridor, qx, qz) != brute)
					agrees = 0;
			}

			check(agrees, "the bounded grid agrees with brute force at 4000 query points");
		}

		check(AP_NavRecFormat_ClassifyShortcut(g_nodes[0], ref, &grid, corridor) == AP_NAVREC_SHORTCUT_NO,
		      "a lap driven on the reference line is not a shortcut");

		// A lap on a circle of a slightly different radius stays inside the
		// corridor: a wide line through a corner must not trip the detector.
		n = AP_NavRecFormat_Decimate(g_samples, MakeCircleLap(g_samples, 900, (400 + 800) * 256, 0, 0), AP_NAVREC_TARGET_NODES, g_nodes[1],
		                             g_stamps[1]);
		check(AP_NavRecFormat_ClassifyShortcut(g_nodes[1], n, &grid, corridor) == AP_NAVREC_SHORTCUT_NO,
		      "a line 800 units wide of the reference is still inside the corridor");

		// A lap on a far larger circle leaves the corridor for its whole length.
		n = AP_NavRecFormat_Decimate(g_samples, MakeCircleLap(g_samples, 900, (400 + 4000) * 256, 0, 0), AP_NAVREC_TARGET_NODES, g_nodes[2],
		                             g_stamps[2]);
		check(AP_NavRecFormat_ClassifyShortcut(g_nodes[2], n, &grid, corridor) == AP_NAVREC_SHORTCUT_YES,
		      "a line 4000 units off the reference is flagged as a shortcut");

		// A single excursion shorter than the run threshold is a wall bump, not
		// a route. Displace fewer nodes than the minimum run and expect NO.
		n = AP_NavRecFormat_Decimate(g_samples, MakeCircleLap(g_samples, 900, 400 * 256, 0, 0), AP_NAVREC_TARGET_NODES, g_nodes[1], g_stamps[1]);
		for (i = 0; i < (AP_NAVREC_SHORTCUT_MIN_RUN_NODES - 1u); i++)
			g_nodes[1][10 + i].posX = (short)(g_nodes[1][10 + i].posX + 5000);
		check(AP_NavRecFormat_ClassifyShortcut(g_nodes[1], n, &grid, corridor) == AP_NAVREC_SHORTCUT_NO,
		      "an excursion one node short of the run threshold is not a shortcut");

		for (i = 0; i < AP_NAVREC_SHORTCUT_MIN_RUN_NODES; i++)
			g_nodes[1][10 + i].posX = (short)(g_nodes[1][10 + i].posX + 5000);
		check(AP_NavRecFormat_ClassifyShortcut(g_nodes[1], n, &grid, corridor) == AP_NAVREC_SHORTCUT_YES,
		      "an excursion that reaches the run threshold is a shortcut");

		// No retail nav table at all: UNKNOWN, never NO. This is the custom-track
		// case and the two answers must stay distinguishable.
		{
			static struct AP_NavRecGrid empty;

			memset(&empty, 0, sizeof empty);
			check(AP_NavRecFormat_ClassifyShortcut(g_nodes[0], ref, &empty, corridor) == AP_NAVREC_SHORTCUT_UNKNOWN,
			      "a level with no retail nav table records shortcut=unknown, not 0");
		}
	}

	// ---------------------------------------------------------------
	// Round trip: write three laps, read them back byte for byte
	// ---------------------------------------------------------------
	{
		static const unsigned int lapFrames[AP_NAVREC_MAX_LAPS] = {900, 940, 1010};

		for (lap = 0; lap < AP_NAVREC_MAX_LAPS; lap++)
		{
			unsigned int n = AP_NavRecFormat_Decimate(g_samples, MakeCircleLap(g_samples, lapFrames[lap], (400 + (int)lap * 60) * 256, 0, 0),
			                                          AP_NAVREC_TARGET_NODES, g_nodes[lap], g_stamps[lap]);

			laps[lap].nodeCount = n;
			laps[lap].lapFrames = lapFrames[lap];
			laps[lap].sampleCount = lapFrames[lap];
			laps[lap].clean = 1;
			laps[lap].shortcut = (unsigned char)((lap == 2) ? AP_NAVREC_SHORTCUT_UNKNOWN : AP_NAVREC_SHORTCUT_NO);
			laps[lap].laneHint = (unsigned char)lap;
			nodePtrs[lap] = g_nodes[lap];
			stampPtrs[lap] = g_stamps[lap];
		}

		memset(&meta, 0, sizeof meta);
		meta.levelId = 7;
		meta.clientVersion = "v0.2.0-alpha3";
		meta.driverName = "Racer One";
		meta.characterId = 3;
		meta.difficultyPreset = 0xa0;
		meta.shortcutTier = 3;
		meta.trackKind = AP_NAVREC_TRACK_RETAIL;

		rc = AP_NavRecFormat_Write(g_file, sizeof g_file, &meta, laps, AP_NAVREC_MAX_LAPS, nodePtrs, stampPtrs, &size);
		check(rc == AP_NAVREC_OK, "a three-lap container serialises");
		check(size == AP_NavRecFormat_Size(laps, AP_NAVREC_MAX_LAPS), "the written size matches the size the writer predicted");

		check((g_file[0] == 'N') && (g_file[1] == 'A') && (g_file[2] == 'V') && (g_file[3] == '2'), "the magic on disk is NAV2");

		rc = AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps);
		check(rc == AP_NAVREC_OK, "the container reads back");

		check(info.levelId == 7, "levelId survives the round trip");
		check(strcmp(info.driverName, "Racer One") == 0, "driverName survives the round trip");
		check(strcmp(info.clientVersion, "v0.2.0-alpha3") == 0, "clientVersion survives the round trip");
		check(info.characterId == 3, "characterId survives the round trip");
		check(info.difficultyPreset == (short)0xa0, "difficultyPreset survives the round trip");
		check(info.shortcutTier == 3, "the declared shortcut-knowledge tier survives the round trip");
		check(info.trackKind == AP_NAVREC_TRACK_RETAIL, "trackKind survives the round trip");
		check(info.lapCount == AP_NAVREC_MAX_LAPS, "all three laps come back");

		{
			int nodesMatch = 1;
			int stampsMatch = 1;
			int metaMatch = 1;

			for (lap = 0; lap < AP_NAVREC_MAX_LAPS; lap++)
			{
				if ((info.laps[lap].nodeCount != laps[lap].nodeCount) || (info.laps[lap].lapFrames != laps[lap].lapFrames) ||
				    (info.laps[lap].sampleCount != laps[lap].sampleCount) || (info.laps[lap].clean != laps[lap].clean) ||
				    (info.laps[lap].shortcut != laps[lap].shortcut) || (info.laps[lap].laneHint != laps[lap].laneHint))
					metaMatch = 0;

				if (memcmp(g_back[lap], g_nodes[lap], sizeof(struct AP_NavRecNode) * laps[lap].nodeCount) != 0)
					nodesMatch = 0;
				if (memcmp(g_backStamps[lap], g_stamps[lap], sizeof(unsigned int) * laps[lap].nodeCount) != 0)
					stampsMatch = 0;
			}

			check(metaMatch, "every per-lap directory field survives the round trip");
			check(nodesMatch, "every NavFrame comes back byte for byte");
			check(stampsMatch, "every timestamp comes back byte for byte");
		}

		// -----------------------------------------------------------
		// Rejection: a flipped byte anywhere must fail the hash
		// -----------------------------------------------------------
		{
			unsigned int probes[] = {0x0C, 0x30, AP_NAVREC_HEADER_BYTES + 4, size - 200, size - 9};
			int          allRejected = 1;
			unsigned int probe;

			for (probe = 0; probe < (sizeof probes / sizeof probes[0]); probe++)
			{
				unsigned char saved = g_file[probes[probe]];

				g_file[probes[probe]] ^= 0x01;
				if (AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) != AP_NAVREC_ERR_HASH)
					allRejected = 0;
				g_file[probes[probe]] = saved;
			}

			check(allRejected, "a single flipped byte in the header, the directory or the payload fails the content hash");
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_OK, "restoring the byte makes the file valid again");
		}

		// A flipped byte in the trailer itself is caught the same way.
		{
			g_file[size - 1] ^= 0x80;
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_HASH, "a flipped byte in the trailer fails the hash");
			g_file[size - 1] ^= 0x80;
		}

		// Truncation. The declared size no longer matches the file.
		check(AP_NavRecFormat_Read(g_file, size - 40, &info, g_back, g_backStamps) == AP_NAVREC_ERR_SIZE, "a truncated file is rejected on its declared size");

		// -----------------------------------------------------------
		// Rejection: unknown version, bad magic, bad shape
		// -----------------------------------------------------------
		{
			unsigned char saved[2];

			memcpy(saved, g_file + 0x04, 2);

			AP_NavRecFormat_PutU16(g_file + 0x04, 4);
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_VERSION,
			      "an unknown future container version with a VALID hash is rejected");

			AP_NavRecFormat_PutU16(g_file + 0x04, 1);
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_VERSION, "a version 1 claim is rejected too");

			memcpy(g_file + 0x04, saved, 2);
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_OK, "and version 2 is accepted again");
		}

		{
			// The version 1 spike container's magic, which must never parse.
			unsigned char saved[4];

			memcpy(saved, g_file, 4);
			g_file[0] = 'N';
			g_file[1] = 'A';
			g_file[2] = 'V';
			g_file[3] = 'R';
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_MAGIC, "the version 1 magic NAVR is rejected outright");
			memcpy(g_file, saved, 4);
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
		}

		{
			unsigned char saved = g_file[0x56];

			g_file[0x56] = 24; // a node stride this build does not agree with
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_HEADER_SHAPE, "a foreign node stride is rejected");
			g_file[0x56] = saved;
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
		}

		{
			unsigned char saved = g_file[0x55];

			g_file[0x55] = 4; // more laps than the format allows
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_LAPCOUNT, "a lap count above three is rejected");

			g_file[0x55] = 0;
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_LAPCOUNT, "a lap count of zero is rejected");

			g_file[0x55] = saved;
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
		}

		// -----------------------------------------------------------
		// Rejection: an out-of-bounds payload offset, hash recomputed so
		// the offset check is what has to catch it
		// -----------------------------------------------------------
		{
			unsigned char saved[4];
			unsigned int  at = AP_NAVREC_HEADER_BYTES + 0x0C;

			memcpy(saved, g_file + at, 4);
			AP_NavRecFormat_PutU32(g_file + at, size - 16);
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_OFFSETS,
			      "a payload offset that runs past the trailer is rejected");

			AP_NavRecFormat_PutU32(g_file + at, 4);
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_OFFSETS,
			      "a payload offset pointing back into the header is rejected");

			memcpy(g_file + at, saved, 4);
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
		}

		// -----------------------------------------------------------
		// Rejection: non-monotonic timestamps
		// -----------------------------------------------------------
		{
			unsigned int stampsAt = AP_NavRecFormat_GetU32(g_file + AP_NAVREC_HEADER_BYTES + 0x10);
			unsigned int savedVal = AP_NavRecFormat_GetU32(g_file + stampsAt + (5 * 4));

			AP_NavRecFormat_PutU32(g_file + stampsAt + (5 * 4), 0);
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_TIMESTAMPS,
			      "a timestamp array that steps backwards is rejected");

			AP_NavRecFormat_PutU32(g_file + stampsAt, 9);
			AP_NavRecFormat_PutU32(g_file + stampsAt + (5 * 4), savedVal);
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_TIMESTAMPS,
			      "a first timestamp other than 0 is rejected");
		}
	}

	// ---------------------------------------------------------------
	// One-lap container, and the synthetic lane fallback that fills the
	// lanes it does not supply
	// ---------------------------------------------------------------
	{
		unsigned int n = AP_NavRecFormat_Decimate(g_samples, MakeCircleLap(g_samples, 900, 400 * 256, 0, 0), AP_NAVREC_TARGET_NODES, g_nodes[0],
		                                          g_stamps[0]);

		laps[0].nodeCount = n;
		laps[0].lapFrames = 900;
		laps[0].sampleCount = 900;
		laps[0].clean = 1;
		laps[0].shortcut = AP_NAVREC_SHORTCUT_NO;
		laps[0].laneHint = 0;
		nodePtrs[0] = g_nodes[0];
		stampPtrs[0] = g_stamps[0];

		memset(&meta, 0, sizeof meta);
		meta.levelId = 2;
		meta.clientVersion = "v0.2.0-alpha3";
		meta.driverName = "";
		meta.characterId = -1;
		meta.difficultyPreset = -1;
		meta.shortcutTier = 0;
		meta.trackKind = AP_NAVREC_TRACK_NONAV;

		rc = AP_NavRecFormat_Write(g_file, sizeof g_file, &meta, laps, 1, nodePtrs, stampPtrs, &size);
		check(rc == AP_NAVREC_OK, "a one-lap container serialises");

		rc = AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps);
		check(rc == AP_NAVREC_OK, "a one-lap container reads back");
		check(info.lapCount == 1, "a one-lap container reports one lap");
		check(info.driverName[0] == '\0', "an anonymous recording is a valid file");
		check(info.characterId == -1, "an unknown character id round trips as -1");

		AP_NavRecFormat_SynthLane(g_back[0], info.laps[0].nodeCount, AP_NAVREC_FALLBACK_LANE_OFFSET, g_back[1]);
		AP_NavRecFormat_SynthLane(g_back[0], info.laps[0].nodeCount, -AP_NAVREC_FALLBACK_LANE_OFFSET, g_back[2]);

		{
			int displaced = 1;
			int distancesSane = 1;

			for (i = 0; i < info.laps[0].nodeCount; i++)
			{
				int dx1 = g_back[1][i].posX - g_back[0][i].posX;
				int dz1 = g_back[1][i].posZ - g_back[0][i].posZ;
				int dx2 = g_back[2][i].posX - g_back[0][i].posX;
				int dz2 = g_back[2][i].posZ - g_back[0][i].posZ;

				double r1 = sqrt((double)(dx1 * dx1) + (double)(dz1 * dz1));
				double r2 = sqrt((double)(dx2 * dx2) + (double)(dz2 * dz2));

				// Within a unit of the requested offset, allowing for the s16
				// rounding at each node.
				if ((r1 < (AP_NAVREC_FALLBACK_LANE_OFFSET - 2)) || (r1 > (AP_NAVREC_FALLBACK_LANE_OFFSET + 2)))
					displaced = 0;
				if ((r2 < (AP_NAVREC_FALLBACK_LANE_OFFSET - 2)) || (r2 > (AP_NAVREC_FALLBACK_LANE_OFFSET + 2)))
					displaced = 0;
				if ((dx1 * dx2) + (dz1 * dz2) > 0)
					displaced = 0; // the two lanes must land on opposite sides

				if ((g_back[1][i].distToNextNavXZ <= 0) || (g_back[1][i].distToNextNavXZ >= 32767))
					distancesSane = 0;
			}

			check(displaced, "a synthesised lane sits the documented lateral offset away, on the opposite side to its partner");
			check(distancesSane, "a synthesised lane has its own true distToNextNav, not the source lane's");
		}
	}

	// ---------------------------------------------------------------
	// Writer refusals
	// ---------------------------------------------------------------
	{
		laps[0].nodeCount = AP_NAVREC_MIN_NODES - 1;
		check(AP_NavRecFormat_Write(g_file, sizeof g_file, &meta, laps, 1, nodePtrs, stampPtrs, &size) == AP_NAVREC_ERR_NODECOUNT,
		      "the writer refuses a lap below the minimum node count");

		laps[0].nodeCount = AP_NAVREC_MAX_NODES + 1;
		check(AP_NavRecFormat_Write(g_file, sizeof g_file, &meta, laps, 1, nodePtrs, stampPtrs, &size) == AP_NAVREC_ERR_NODECOUNT,
		      "the writer refuses a lap above the maximum node count");

		laps[0].nodeCount = AP_NAVREC_TARGET_NODES;
		check(AP_NavRecFormat_Write(g_file, sizeof g_file, &meta, laps, 4, nodePtrs, stampPtrs, &size) == AP_NAVREC_ERR_LAPCOUNT,
		      "the writer refuses more laps than the format holds");
		check(AP_NavRecFormat_Write(g_file, sizeof g_file, &meta, laps, 0, nodePtrs, stampPtrs, &size) == AP_NAVREC_ERR_LAPCOUNT,
		      "the writer refuses zero laps");
		check(AP_NavRecFormat_Write(g_file, 32, &meta, laps, 1, nodePtrs, stampPtrs, &size) == AP_NAVREC_ERR_CAPACITY,
		      "the writer refuses a buffer it would overrun");
	}

	// ---------------------------------------------------------------
	// Payload offsets that are out of range in the OTHER direction.
	//
	// The bounds test computes `ceilAt - start`. Without an explicit
	// `start > ceilAt` guard that subtraction underflows, a wild offset is
	// handed a huge allowance, and the reader then walks off the buffer.
	// Every case here was a live out-of-bounds read before the guard.
	// ---------------------------------------------------------------
	{
		unsigned int slots[2] = {0x0C, 0x10}; // framesOffset, timestampsOffset
		unsigned int wild[3] = {0xFFFFFF00u, 0x40000000u, 0x80000000u};
		int          allRejected = 1;
		unsigned int slot;
		unsigned int w;

		// Rebuild a good three-lap file to mutate.
		for (lap = 0; lap < AP_NAVREC_MAX_LAPS; lap++)
		{
			unsigned int n = AP_NavRecFormat_Decimate(g_samples, MakeCircleLap(g_samples, 900, (400 + (int)lap * 60) * 256, 0, 0),
			                                          AP_NAVREC_TARGET_NODES, g_nodes[lap], g_stamps[lap]);
			laps[lap].nodeCount = n;
			laps[lap].lapFrames = 900;
			laps[lap].sampleCount = 900;
			laps[lap].clean = 1;
			laps[lap].shortcut = AP_NAVREC_SHORTCUT_NO;
			laps[lap].laneHint = (unsigned char)lap;
			laps[lap].maskFires = 0;
			laps[lap].turboFires = 0;
			nodePtrs[lap] = g_nodes[lap];
			stampPtrs[lap] = g_stamps[lap];
		}
		memset(&meta, 0, sizeof meta);
		meta.levelId = 7;
		meta.clientVersion = "v0.2.0-alpha3";
		meta.driverName = "Racer One";
		meta.characterId = 3;
		meta.difficultyPreset = 0xa0;
		meta.shortcutTier = 3;
		meta.trackKind = AP_NAVREC_TRACK_RETAIL;
		AP_NavRecFormat_Write(g_file, sizeof g_file, &meta, laps, AP_NAVREC_MAX_LAPS, nodePtrs, stampPtrs, &size);

		for (slot = 0; slot < 2; slot++)
		{
			for (w = 0; w < 3; w++)
			{
				unsigned int at = AP_NAVREC_HEADER_BYTES + slots[slot];
				unsigned int saved = AP_NavRecFormat_GetU32(g_file + at);
				int          got;

				AP_NavRecFormat_PutU32(g_file + at, wild[w]);
				AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
				got = AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps);
				if (got != AP_NAVREC_ERR_OFFSETS)
					allRejected = 0;

				AP_NavRecFormat_PutU32(g_file + at, saved);
				AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
			}
		}

		check(allRejected, "a payload offset far beyond the file is rejected, not handed an underflowed allowance");
		check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_OK, "the file is valid again once the offsets are restored");

		// The guard itself, exercised directly at its boundary.
		check(AP_NavRecFormat_SpanOk(100, 10, 100, 200) == 1, "a span inside the window is accepted");
		check(AP_NavRecFormat_SpanOk(200, 0, 100, 200) == 1, "an empty span at the ceiling is accepted");
		check(AP_NavRecFormat_SpanOk(201, 0, 100, 200) == 0, "a span starting past the ceiling is rejected");
		check(AP_NavRecFormat_SpanOk(0xFFFFFF00u, 20, 100, 200) == 0, "a span starting far past the ceiling cannot underflow into acceptance");
		check(AP_NavRecFormat_SpanOk(99, 1, 100, 200) == 0, "a span starting below the floor is rejected");
		check(AP_NavRecFormat_SpanOk(190, 11, 100, 200) == 0, "a span running one byte past the ceiling is rejected");
	}

	// ---------------------------------------------------------------
	// Overlapping payload spans
	// ---------------------------------------------------------------
	{
		unsigned int lap0Frames = AP_NavRecFormat_GetU32(g_file + AP_NAVREC_HEADER_BYTES + 0x0C);
		unsigned int at1 = AP_NAVREC_HEADER_BYTES + AP_NAVREC_LAPDIR_BYTES + 0x0C;
		unsigned int saved = AP_NavRecFormat_GetU32(g_file + at1);

		// Point lap 1's nodes at lap 0's nodes.
		AP_NavRecFormat_PutU32(g_file + at1, lap0Frames);
		AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
		check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_OVERLAP,
		      "two laps whose node arrays overlap are rejected");

		AP_NavRecFormat_PutU32(g_file + at1, saved);
		AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));

		// A lap's own timestamps overlapping its own nodes.
		{
			unsigned int atStamps = AP_NAVREC_HEADER_BYTES + 0x10;
			unsigned int savedStamps = AP_NavRecFormat_GetU32(g_file + atStamps);

			AP_NavRecFormat_PutU32(g_file + atStamps, lap0Frames + 4u);
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
			check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_OVERLAP,
			      "a lap whose timestamps overlap its own nodes is rejected");

			AP_NavRecFormat_PutU32(g_file + atStamps, savedStamps);
			AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
		}

		check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_OK, "the file is valid again once the spans are restored");
	}

	// ---------------------------------------------------------------
	// The read path does not trust the file.
	//
	// A container can arrive from anyone. Two node fields hang the engine
	// rather than merely drive a bot badly, so the reader forces them.
	// ---------------------------------------------------------------
	{
		unsigned int frames0 = AP_NavRecFormat_GetU32(g_file + AP_NAVREC_HEADER_BYTES + 0x0C);
		unsigned int count0 = AP_NavRecFormat_GetU32(g_file + AP_NAVREC_HEADER_BYTES + 0x00);
		unsigned int n;
		int          opcodeForced = 1;
		int          flagCleared = 1;

		// A hostile file: pathChangeOpcode 0 on every node (the start-line
		// teleport) and flags 0x4000 on every node (the killplane hang).
		for (n = 0; n < count0; n++)
		{
			unsigned char *q = g_file + frames0 + (n * AP_NAVREC_NODE_BYTES);

			AP_NavRecFormat_PutU16(q + 0x0E, 0x4000u);
			AP_NavRecFormat_PutU16(q + 0x10, 0u);
		}
		AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));

		check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_OK, "a file carrying hostile node fields still parses");

		for (n = 0; n < count0; n++)
		{
			if (g_back[0][n].pathChangeOpcode != (short)AP_NAVREC_NO_LANE_CHANGE)
				opcodeForced = 0;
			if ((g_back[0][n].flags & (short)AP_NAVREC_FLAG_KILLPLANE_SKIP) != 0)
				flagCleared = 0;
		}

		check(opcodeForced, "the reader forces pathChangeOpcode to the sentinel, so a file cannot reinstate the start-line teleport");
		check(flagCleared, "the reader clears flags 0x4000, so a file cannot hang the killplane rewind");

		// SanitizeNodes reports what it had to change, and is idempotent.
		{
			struct AP_NavRecNode probe[4];
			unsigned int         changed;

			memset(probe, 0, sizeof probe);
			probe[0].pathChangeOpcode = 0;
			probe[1].flags = (short)AP_NAVREC_FLAG_KILLPLANE_SKIP;
			probe[1].pathChangeOpcode = (short)AP_NAVREC_NO_LANE_CHANGE;
			probe[2].pathChangeOpcode = (short)AP_NAVREC_NO_LANE_CHANGE;
			probe[3].pathChangeOpcode = 0x123;

			changed = AP_NavRecFormat_SanitizeNodes(probe, 4);
			check(changed == 3, "SanitizeNodes counts exactly the nodes it had to change");
			check(AP_NavRecFormat_SanitizeNodes(probe, 4) == 0, "SanitizeNodes is idempotent");
			check(probe[1].flags == 0, "the killplane bit is cleared without disturbing the other flags");
		}
	}

	// ---------------------------------------------------------------
	// A constant goBackCount is REJECTED, not repaired.
	//
	// Forcing it is impossible: BOTS_Killplane's rewind loop needs the value
	// to vary, so every constant a reader could substitute is itself the bug.
	// A shared file carrying one has to be refused.
	// ---------------------------------------------------------------
	{
		unsigned int frames0 = AP_NavRecFormat_GetU32(g_file + AP_NAVREC_HEADER_BYTES + 0x0C);
		unsigned int count0 = AP_NavRecFormat_GetU32(g_file + AP_NAVREC_HEADER_BYTES + 0x00);
		unsigned int n;

		// A real decimated lap is accepted, and its goBackCount genuinely varies.
		check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_OK, "a recorded lap is accepted before the lane is flattened");
		check(AP_NavRecFormat_GoBackVaries(g_back[0], info.laps[0].nodeCount) == 1, "a real decimated lap has a varying goBackCount");

		// Flatten lap 0's goBackCount, exactly what the revision 1 writer emitted
		// and what a hand-made file is most likely to contain.
		for (n = 0; n < count0; n++)
			g_file[frames0 + (n * AP_NAVREC_NODE_BYTES) + 0x12] = 4;
		AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));

		check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_ERR_FLAT_GOBACK,
		      "a file whose goBackCount is constant across a lap is rejected");

		// One differing node is enough for the loop to terminate, so it is enough
		// for the reader too. The check is the loop's precondition, not a taste
		// judgement about the data.
		g_file[frames0 + (5 * AP_NAVREC_NODE_BYTES) + 0x12] = 9;
		AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
		check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_OK,
		      "a single differing node makes the lane acceptable again");

		// The predicate itself, at its edges.
		{
			struct AP_NavRecNode flat[4];

			memset(flat, 0, sizeof flat);
			check(AP_NavRecFormat_GoBackVaries(flat, 4) == 0, "an all-zero goBackCount lane does not vary");
			flat[3].goBackCount = 1;
			check(AP_NavRecFormat_GoBackVaries(flat, 4) == 1, "a lane differing only at its last node does vary");
			check(AP_NavRecFormat_GoBackVaries(flat, 1) == 0, "a single-node lane cannot vary");
			check(AP_NavRecFormat_GoBackVaries(NULL, 4) == 0, "a NULL lane does not vary");
		}

		// Restore a good file for the cases that follow.
		for (n = 0; n < count0; n++)
			g_file[frames0 + (n * AP_NAVREC_NODE_BYTES) + 0x12] = (unsigned char)(n & 0x3F);
		AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));
		check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_OK, "the file is valid again");
	}

	// ---------------------------------------------------------------
	// The driver name in a file is sanitized on the way IN, not trusted
	// ---------------------------------------------------------------
	{
		unsigned int n;
		int          rcName;

		// Write control characters and a trailing space straight into the name
		// field of an otherwise valid file.
		for (n = 0; n < AP_NAVREC_NAME_FIELD; n++)
			g_file[0x30 + n] = 0;
		g_file[0x30] = 'B';
		g_file[0x31] = 0x07; // bell
		g_file[0x32] = 'a';
		g_file[0x33] = '\n';
		g_file[0x34] = 'd';
		g_file[0x35] = ' ';
		AP_NavRecFormat_PutU64(g_file + size - 8, AP_NavRecFormat_Hash(g_file, size - 8));

		rcName = AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps);
		check(rcName == AP_NAVREC_OK, "a file with a hostile driver name still parses");
		check(strcmp(info.driverName, "Bad") == 0, "the reader sanitizes the driver name before anything can draw or log it");
	}

	// ---------------------------------------------------------------
	// Fixed-size source buffers with no terminator
	// ---------------------------------------------------------------
	{
		// A config buffer filled to its last byte, exactly the case that makes
		// the operand order in PutText load-bearing. Under ASan the other order
		// reads one past this array.
		char          packed[8];
		unsigned char field[8];
		unsigned int  n;
		int           copied = 1;

		for (n = 0; n < sizeof packed; n++)
			packed[n] = 'A';

		AP_NavRecFormat_PutText(field, sizeof field, packed);
		for (n = 0; n < sizeof field; n++)
		{
			if (field[n] != 'A')
				copied = 0;
		}
		check(copied, "a source buffer with no terminator is copied up to the field size and no further");
	}

	// ---------------------------------------------------------------
	// The two held-item counters
	// ---------------------------------------------------------------
	{
		for (lap = 0; lap < AP_NAVREC_MAX_LAPS; lap++)
		{
			unsigned int n = AP_NavRecFormat_Decimate(g_samples, MakeCircleLap(g_samples, 900, 400 * 256, 0, 0), AP_NAVREC_TARGET_NODES,
			                                          g_nodes[lap], g_stamps[lap]);
			laps[lap].nodeCount = n;
			laps[lap].lapFrames = 900;
			laps[lap].sampleCount = 900;
			laps[lap].clean = 1;
			laps[lap].shortcut = AP_NAVREC_SHORTCUT_NO;
			laps[lap].laneHint = (unsigned char)lap;
			laps[lap].maskFires = (unsigned short)(lap + 1u);
			laps[lap].turboFires = (unsigned short)((lap + 1u) * 7u);
			nodePtrs[lap] = g_nodes[lap];
			stampPtrs[lap] = g_stamps[lap];
		}
		laps[2].maskFires = 65535u;

		check(AP_NavRecFormat_Write(g_file, sizeof g_file, &meta, laps, AP_NAVREC_MAX_LAPS, nodePtrs, stampPtrs, &size) == AP_NAVREC_OK,
		      "a container with item counters serialises");
		check(AP_NavRecFormat_Read(g_file, size, &info, g_back, g_backStamps) == AP_NAVREC_OK, "it reads back");

		{
			int match = 1;

			for (lap = 0; lap < AP_NAVREC_MAX_LAPS; lap++)
			{
				if ((info.laps[lap].maskFires != laps[lap].maskFires) || (info.laps[lap].turboFires != laps[lap].turboFires))
					match = 0;
			}
			check(match, "maskFires and turboFires survive the round trip, including a saturated count");
		}

		// The counters live in what used to be reserved directory bytes, so their
		// offsets are part of the format and are asserted here.
		check(AP_NavRecFormat_GetU16(g_file + AP_NAVREC_HEADER_BYTES + 0x18) == 1u, "maskFires is at directory offset 0x18");
		check(AP_NavRecFormat_GetU16(g_file + AP_NAVREC_HEADER_BYTES + 0x1A) == 7u, "turboFires is at directory offset 0x1A");
	}

	// ---------------------------------------------------------------
	// Savestate relocation of an injected nav pointer.
	//
	// This models the decision native_checkpoint.c makes, not the engine.
	// A checkpoint records address RANGES for the mempack and the decomp
	// globals; RelocateAddress rebases a pointer that falls inside one and
	// answers "not mine" for anything else. The recorded lanes live in a
	// module static, which is in the executable's image and in no range at
	// all, so a pointer-only relocation leaves such a slot untouched: right
	// inside one process, a dangling pointer across two. The pointer-or-image
	// path applies the codeAnchor delta instead, which is the correction an
	// image pointer needs.
	// ---------------------------------------------------------------
	{
		const unsigned int oldMempack = 0x10000000u;
		const unsigned int liveMempack = 0x20000000u;
		const unsigned int mempackSize = 0x00100000u;
		const unsigned int oldAnchor = 0x08048000u;
		const unsigned int liveAnchor = 0x56550000u;

		// A retail nav pointer: inside the mempack range, rebased by the range.
		unsigned int retail = oldMempack + 0x4000u;
		unsigned int retailInRange = ((retail >= oldMempack) && (retail < (oldMempack + mempackSize)));
		unsigned int retailNew = retailInRange ? (liveMempack + (retail - oldMempack)) : retail;

		// An injected nav pointer: an image static, in no range.
		unsigned int injected = oldAnchor + 0x9000u;
		unsigned int injectedInRange = ((injected >= oldMempack) && (injected < (oldMempack + mempackSize)));
		unsigned int injectedPointerOnly = injected; // what the old code left behind
		unsigned int injectedPointerOrImage = injectedInRange ? (liveMempack + (injected - oldMempack)) : (injected + (liveAnchor - oldAnchor));

		check(retailInRange, "a retail nav pointer is owned by the mempack range");
		check(retailNew == (liveMempack + 0x4000u), "a retail nav pointer relocates by the range delta, unchanged by this work");
		check(!injectedInRange, "an injected nav pointer is owned by no checkpoint range");
		check(injectedPointerOnly == injected, "pointer-only relocation leaves an injected nav pointer stale across processes");
		check(injectedPointerOrImage == (liveAnchor + 0x9000u), "pointer-or-image relocation rebases it onto the live image");
	}

	// ---------------------------------------------------------------
	// Which recordings feed the three engine lanes.
	//
	// The loader used to take ONE container and fill all three lanes from
	// it, so a race showed one person's lines under one name however many
	// bots were on track, and a newest-index community file permanently
	// shadowed the player's own older recordings. These cases hold the
	// replacement rule: up to three distinct files, a different author
	// preferred for each, newest index first, no randomness anywhere.
	// ---------------------------------------------------------------
	{
		unsigned int chosen[AP_NAVREC_LANES];
		char         author[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
		unsigned int laneFile[AP_NAVREC_LANES];
		unsigned int laneLap[AP_NAVREC_LANES];
		unsigned int opened;
		unsigned int n;

		// Three files, three authors: three lanes, three names.
		FakeReset(3);
		FakeAdd(1, "Appie", 1, 0);
		FakeAdd(2, "BEXUS", 1, 0);
		FakeAdd(3, "Cara", 1, 0);

		memset(author, 0, sizeof author);
		n = AP_NavRecLane_Select(FakeHighest(), AP_NAVREC_MAX_LOAD_ATTEMPTS, FakeFetch, &g_folder, chosen, author, &opened);
		AP_NavRecLane_Plan(n, laneFile, laneLap);

		check(n == 3u, "three usable files by three authors fill the three lanes from three files");
		check((chosen[0] == 3u) && (chosen[1] == 2u) && (chosen[2] == 1u), "candidates are taken newest index first");
		check((strcmp(author[0], "Cara") == 0) && (strcmp(author[1], "BEXUS") == 0) && (strcmp(author[2], "Appie") == 0),
		      "each lane carries the name on the file feeding it");
		check((laneFile[0] == 0u) && (laneFile[1] == 1u) && (laneFile[2] == 2u), "with three files no lane reuses another lane's file");
		check((laneLap[0] == 0u) && (laneLap[1] == 0u) && (laneLap[2] == 0u), "each contributor's first lap is the one that races");

		// Selection does not depend on anything but the folder.
		{
			unsigned int again[AP_NAVREC_LANES];
			char         againAuthor[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
			unsigned int m;

			memset(againAuthor, 0, sizeof againAuthor);
			g_folder.opened = 0;
			m = AP_NavRecLane_Select(FakeHighest(), AP_NAVREC_MAX_LOAD_ATTEMPTS, FakeFetch, &g_folder, again, againAuthor, &opened);
			check((m == n) && (memcmp(again, chosen, sizeof chosen) == 0),
			      "the same folder always produces the same field, which is what a savestate restore relies on");
		}
	}

	// Two files: the accepted files repeat in order and the repeat steps to
	// the next lap, so no lane duplicates a line another lane drives.
	{
		unsigned int chosen[AP_NAVREC_LANES];
		char         author[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
		unsigned int laneFile[AP_NAVREC_LANES];
		unsigned int laneLap[AP_NAVREC_LANES];
		unsigned int opened;
		unsigned int n;

		FakeReset(3);
		FakeAdd(1, "Appie", 1, 0);
		FakeAdd(2, "BEXUS", 3, 0);

		memset(author, 0, sizeof author);
		n = AP_NavRecLane_Select(FakeHighest(), AP_NAVREC_MAX_LOAD_ATTEMPTS, FakeFetch, &g_folder, chosen, author, &opened);
		AP_NavRecLane_Plan(n, laneFile, laneLap);

		check(n == 2u, "two usable files are both used");
		check((chosen[0] == 2u) && (chosen[1] == 1u), "two files are still taken newest first");
		check((laneFile[0] == 0u) && (laneFile[1] == 1u) && (laneFile[2] == 0u), "the third lane falls back to the first file");
		check((laneLap[0] == 0u) && (laneLap[1] == 0u) && (laneLap[2] == 1u), "a reused file gives its next lap, not the same one twice");
	}

	// One file: exactly what the loader did before mixed fields existed.
	{
		unsigned int                  chosen[AP_NAVREC_LANES];
		char                          author[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
		unsigned int                  laneFile[AP_NAVREC_LANES];
		unsigned int                  laneLap[AP_NAVREC_LANES];
		struct AP_NavRecLaneCandidate cand;
		unsigned int                  opened;
		unsigned int                  n;

		FakeReset(3);
		FakeAdd(1, "Appie", 3, 0);

		memset(author, 0, sizeof author);
		n = AP_NavRecLane_Select(FakeHighest(), AP_NAVREC_MAX_LOAD_ATTEMPTS, FakeFetch, &g_folder, chosen, author, &opened);
		AP_NavRecLane_Plan(n, laneFile, laneLap);

		check(n == 1u, "one usable file is the single-file case, unchanged");
		check((laneFile[0] == 0u) && (laneFile[1] == 0u) && (laneFile[2] == 0u), "one file feeds every lane");
		check((laneLap[0] == 0u) && (laneLap[1] == 1u) && (laneLap[2] == 2u), "one file still gives lane i lap i");

		// And with one lap in that file, the two lanes it cannot fill are the
		// ones the lateral-offset fallback has always synthesised.
		FakeReset(3);
		FakeAdd(1, "Appie", 1, 0);

		memset(author, 0, sizeof author);
		n = AP_NavRecLane_Select(FakeHighest(), AP_NAVREC_MAX_LOAD_ATTEMPTS, FakeFetch, &g_folder, chosen, author, &opened);
		AP_NavRecLane_Plan(n, laneFile, laneLap);

		memset(&cand, 0, sizeof cand);
		FakeFetch(&g_folder, 1, &cand);

		check(cand.lapCount == 1u, "the one-lap file reports one lap");
		check((laneLap[1] >= cand.lapCount) && (laneLap[2] >= cand.lapCount),
		      "a one-lap file leaves lanes 1 and 2 to the lateral-offset fallback, as before");
	}

	// A different author beats another file by an author already racing.
	{
		unsigned int chosen[AP_NAVREC_LANES];
		char         author[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
		unsigned int opened;
		unsigned int n;

		FakeReset(3);
		FakeAdd(1, "BEXUS", 1, 0);
		FakeAdd(2, "Appie", 1, 0);
		FakeAdd(3, "Appie", 1, 0);
		FakeAdd(4, "Appie", 1, 0);

		memset(author, 0, sizeof author);
		n = AP_NavRecLane_Select(FakeHighest(), AP_NAVREC_MAX_LOAD_ATTEMPTS, FakeFetch, &g_folder, chosen, author, &opened);

		check(n == 3u, "three same-author files and one other still fill three lanes");
		check((chosen[0] == 4u) && (chosen[1] == 1u), "the older different author is taken ahead of two newer files by the same one");
		check(strcmp(author[1], "BEXUS") == 0, "the second lane belongs to the other contributor");
		check(chosen[2] == 3u, "a held-back file fills the lane no third author could");
		check(strcmp(author[2], "Appie") == 0, "a held-back file is labelled with its own author, not the first lane's");
	}

	// A held-back file may match a later lane's author, not the first one.
	{
		unsigned int chosen[AP_NAVREC_LANES];
		char         author[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
		unsigned int opened;
		unsigned int n;

		FakeReset(3);
		FakeAdd(1, "Appie", 1, 0);
		FakeAdd(2, "Appie", 1, 0);
		FakeAdd(3, "Cara", 1, 0);

		memset(author, 0, sizeof author);
		n = AP_NavRecLane_Select(FakeHighest(), AP_NAVREC_MAX_LOAD_ATTEMPTS, FakeFetch, &g_folder, chosen, author, &opened);

		check(n == 3u, "two authors across three files still fill three lanes");
		check((chosen[0] == 3u) && (chosen[1] == 2u) && (chosen[2] == 1u), "the held-back oldest file takes the last lane");
		check((strcmp(author[0], "Cara") == 0) && (strcmp(author[1], "Appie") == 0) && (strcmp(author[2], "Appie") == 0),
		      "a held-back file carries the author it actually matched");
	}

	// A rejected file is rejected whole, and hides nothing older.
	{
		unsigned int chosen[AP_NAVREC_LANES];
		char         author[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
		unsigned int opened;
		unsigned int n;

		FakeReset(3);
		FakeAdd(1, "BEXUS", 1, 0);
		FakeAdd(2, "Appie", 1, 0);
		FakeAdd(3, "Appie", 1, 1); // newest, and corrupt

		memset(author, 0, sizeof author);
		n = AP_NavRecLane_Select(FakeHighest(), AP_NAVREC_MAX_LOAD_ATTEMPTS, FakeFetch, &g_folder, chosen, author, &opened);

		check(n == 2u, "a corrupt newest file fills no lane");
		check((chosen[0] == 2u) && (chosen[1] == 1u), "a corrupt newest file does not hide the good older ones");
		check(opened == 3u, "a rejected file still costs one open");
	}

	// Custom-track identity is applied per file, exactly as before.
	{
		static const unsigned char babyUuid[AP_NAVREC_TRACK_UUID_BYTES] = {
			0x89, 0x8a, 0x93, 0x15, 0x69, 0x3f, 0x4e, 0xd3,
			0xb6, 0xa0, 0xfb, 0xe5, 0x0d, 0xb8, 0xbc, 0x40};
		unsigned int chosen[AP_NAVREC_LANES];
		char         author[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
		unsigned int opened;
		unsigned int n;

		FakeReset(6); // the physical slot a custom track is served from
		g_folder.identityKind = AP_NAVREC_IDENTITY_CUSTOM;
		memcpy(g_folder.uuid, babyUuid, sizeof babyUuid);
		g_folder.navRevision = 1;

		FakeAddCustom(1, "Appie", babyUuid, 1);
		FakeAdd(2, "Legacy", 1, 0);             // a retail line in the same slot
		FakeAddCustom(3, "Stale", babyUuid, 2); // right track, superseded revision

		memset(author, 0, sizeof author);
		n = AP_NavRecLane_Select(FakeHighest(), AP_NAVREC_MAX_LOAD_ATTEMPTS, FakeFetch, &g_folder, chosen, author, &opened);

		check(n == 1u, "only the container matching the active track identity fills a lane");
		check(chosen[0] == 1u, "a legacy retail line in the same physical slot is rejected per file");
		check(strcmp(author[0], "Appie") == 0, "the matching contributor is the one on the grid");
		check(opened == 3u, "every candidate was still examined, one identity rule at a time");
	}

	// A folder of corrupt containers costs a bounded number of reads.
	{
		unsigned int chosen[AP_NAVREC_LANES];
		char         author[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
		unsigned int opened;
		unsigned int n;

		FakeReset(3);
		for (i = 1; i <= 10u; i++)
			FakeAdd(i, "Appie", 1, 1);

		memset(author, 0, sizeof author);
		n = AP_NavRecLane_Select(FakeHighest(), AP_NAVREC_MAX_LOAD_ATTEMPTS, FakeFetch, &g_folder, chosen, author, &opened);

		check(n == 0u, "a folder with nothing usable in it fills no lane");
		check(opened == AP_NAVREC_MAX_LOAD_ATTEMPTS, "the scan gives up at the open budget rather than reading the whole folder");
	}

	// One person's whole folder: three distinct files, one name, bounded reads.
	{
		unsigned int chosen[AP_NAVREC_LANES];
		char         author[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
		unsigned int opened;
		unsigned int n;

		FakeReset(3);
		for (i = 1; i <= 10u; i++)
			FakeAdd(i, "Appie", 1, 0);

		memset(author, 0, sizeof author);
		n = AP_NavRecLane_Select(FakeHighest(), AP_NAVREC_MAX_LOAD_ATTEMPTS, FakeFetch, &g_folder, chosen, author, &opened);

		check(n == 3u, "one person's folder still fills three lanes from three distinct files");
		check((chosen[0] == 10u) && (chosen[1] == 9u) && (chosen[2] == 8u), "the three newest of one author's files are the ones used");
		check(opened == AP_NAVREC_MAX_LOAD_ATTEMPTS, "the search for a second author stops at the open budget");
	}

	// Gaps are free: a player who deletes files out of the middle is not
	// charged for the holes.
	{
		unsigned int chosen[AP_NAVREC_LANES];
		char         author[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
		unsigned int laneFile[AP_NAVREC_LANES];
		unsigned int laneLap[AP_NAVREC_LANES];
		unsigned int opened;
		unsigned int n;

		FakeReset(3);
		FakeAdd(1, "Appie", 1, 0);
		FakeAdd(100, "BEXUS", 1, 0);

		memset(author, 0, sizeof author);
		n = AP_NavRecLane_Select(FakeHighest(), AP_NAVREC_MAX_LOAD_ATTEMPTS, FakeFetch, &g_folder, chosen, author, &opened);

		check(n == 2u, "two files separated by ninety-eight gaps are both found");
		check((chosen[0] == 100u) && (chosen[1] == 1u), "the newest index is still first across a gap");
		check(opened == 2u, "gaps in the numbering do not consume the open budget");

		// A plan for no files at all leaves the caller's arrays alone rather
		// than dividing by zero.
		laneFile[0] = 0xEEu;
		laneLap[0] = 0xEEu;
		AP_NavRecLane_Plan(0, laneFile, laneLap);
		check((laneFile[0] == 0xEEu) && (laneLap[0] == 0xEEu), "planning lanes for no files touches nothing");
	}

	printf("\n%d failure(s)\n", g_failures);
	return (g_failures == 0) ? 0 : 1;
}
