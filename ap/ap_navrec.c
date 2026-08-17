#ifdef CTR_AP

#include <common.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ap_navrec.h"
#include "ap_hooks.h" // AP_LogLine
#include "platform/native_config.h" // g_config.navRecord
#include <errno.h>
#if defined(_WIN32)
#include "platform/native_win32.h"
#else
#include <sys/stat.h>
#endif

int Platform_InputRawKeyDown(int scancode); // native_input.c

#define AP_NAVREC_KEY_WRITE   97 // Numpad 9: generate lanes from the corpus and write
#define AP_NAVREC_MAX_SAMPLES 4000 // per lap
#define AP_NAVREC_MAX_LAPS    12
#define AP_NAVREC_MAX_NODES   1024
#define AP_NAVREC_LANES       3

// pathChangeOpcode sentinel. MUST be positive and >= BOTS_PathChangeCap()
// (3 << 10 == 0xC00) so `changeOpcode < cap` fails and no lane change fires.
// Zero decodes as lane 0 / node 0 and would teleport an overtaking bot to the
// start line -- BOTS.c:1143 runs that path for ordinary bots, not just bosses.
#define AP_NAVREC_NO_LANE_CHANGE 0x7FFF

struct AP_NavRecSample
{
	int x, y, z;
	s16 rotY;
	u16 flags;
};

// One completed lap of raw per-frame samples.
struct AP_NavRecLap
{
	struct AP_NavRecSample samples[AP_NAVREC_MAX_SAMPLES];
	int count;
	int frames; // lap length in frames, a proxy for lap time
	int dirty;  // saw a respawn / blast / spin: do not trust this line
};

static struct AP_NavRecLap g_laps[AP_NAVREC_MAX_LAPS];
static int g_lapCount;    // completed laps banked
static int g_curCount;    // samples in the lap being driven
static int g_curFrames;
static int g_curDirty;
static struct AP_NavRecSample g_cur[AP_NAVREC_MAX_SAMPLES];

static int g_armed = -1;   // -1 = never evaluated; else mirrors g_config.navRecord
static int g_shapeRead;    // env shaping numbers read once
static int g_targetNodes;
static int g_laneOffset;
static int g_keyLatch;
static int g_lastLevelID = -1;
static int g_lastLapIndex = -1;

// Emitted paths, static so injected pointers stay valid for the process lifetime.
static struct NavHeader g_outHeader[AP_NAVREC_LANES];
static struct NavFrame  g_outFrames[AP_NAVREC_LANES][AP_NAVREC_MAX_NODES];
static int              g_outCount[AP_NAVREC_LANES];

// Per-lap resampled positions, in NavFrame units, used to build the envelope.
static s16 g_resX[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
static s16 g_resY[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
static s16 g_resZ[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
static s16 g_resRot[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
static u16 g_resFlags[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];

static int AP_NavRec_EnvInt(const char *name, int fallback)
{
	const char *v = getenv(name);
	if ((v == NULL) || (v[0] == '\0'))
		return fallback;
	return (int)strtol(v, NULL, 0);
}

static void AP_NavRec_ReadEnvOnce(void)
{
	// The shaping numbers are read once: they are environment switches for
	// authoring work and cannot change mid-session.
	if (g_shapeRead == 0)
	{
		g_shapeRead = 1;
		g_targetNodes = AP_NavRec_EnvInt("CTR_AP_NAV_REC_NODES", 230);
		g_laneOffset = AP_NavRec_EnvInt("CTR_AP_NAV_REC_OFFSET", 500);

		if (g_targetNodes < 8)
			g_targetNodes = 8;
		if (g_targetNodes > AP_NAVREC_MAX_NODES)
			g_targetNodes = AP_NAVREC_MAX_NODES;
	}

	// The OPTION decides whether we record, and it is re-read EVERY tick rather
	// than latched. Latching it would mean unticking "Save AI Lap Recordings"
	// mid-session left the recorder running until the player restarted, which
	// is exactly the surprise this option exists to prevent. An option about
	// writing to someone's disk has to take effect when they turn it OFF, not
	// only when they turn it on.
	const int want = g_config.navRecord ? 1 : 0;
	if (want == g_armed)
		return;

	g_armed = want;

	char msg[192];
	if (g_armed)
	{
		snprintf(msg, sizeof msg, "[AP NAVREC] recorder armed by option. nodes=%d maxLaneOffset=%d. Every lap is banked; Numpad9 writes.\n",
		         g_targetNodes, g_laneOffset);
	}
	else
	{
		// Drop what was banked. Someone who turns recording off is withdrawing
		// consent, and holding their laps in memory to write later would not
		// honour that.
		g_lapCount = 0;
		g_curCount = 0;
		g_curFrames = 0;
		g_curDirty = 0;
		snprintf(msg, sizeof msg, "[AP NAVREC] recorder disarmed by option; banked laps discarded\n");
	}
	AP_LogLine(msg);
}

// ============================================================================
// Resampling
// ============================================================================

static double AP_NavRec_Dist(const struct AP_NavRecSample *a, const struct AP_NavRecSample *b)
{
	double dx = (double)(b->x - a->x);
	double dz = (double)(b->z - a->z);
	return sqrt((dx * dx) + (dz * dz));
}

// Arc-length resample one banked lap into `count` evenly spaced nodes, written
// into the g_res* row for that lap. Arc length rather than frame index, or slow
// corners would hoard nodes and straights would get almost none.
static int AP_NavRec_ResampleLap(int lap, int count)
{
	const struct AP_NavRecLap *L = &g_laps[lap];

	if (L->count < 4)
		return 0;

	double total = 0.0;
	for (int i = 1; i < L->count; i++)
		total += AP_NavRec_Dist(&L->samples[i - 1], &L->samples[i]);

	if (total < 1.0)
		return 0;

	const double step = total / (double)count;
	double walked = 0.0;
	double nextMark = 0.0;
	int written = 0;

	for (int i = 1; (i < L->count) && (written < count); i++)
	{
		double segLen = AP_NavRec_Dist(&L->samples[i - 1], &L->samples[i]);
		if (segLen <= 0.0)
			continue;

		while ((walked + segLen >= nextMark) && (written < count))
		{
			double t = (nextMark - walked) / segLen;
			const struct AP_NavRecSample *a = &L->samples[i - 1];
			const struct AP_NavRecSample *b = &L->samples[i];

			double px = (double)a->x + ((double)(b->x - a->x) * t);
			double py = (double)a->y + ((double)(b->y - a->y) * t);
			double pz = (double)a->z + ((double)(b->z - a->z) * t);

			// NavFrame positions are posCurr >> 8, the same space BOTS.c:500-508
			// compares against (estimatePosition = posCurr >> 8).
			g_resX[lap][written] = (s16)((int)px >> 8);
			g_resY[lap][written] = (s16)((int)py >> 8);
			g_resZ[lap][written] = (s16)((int)pz >> 8);
			g_resRot[lap][written] = a->rotY;
			g_resFlags[lap][written] = a->flags;

			written++;
			nextMark += step;
		}

		walked += segLen;
	}

	return written;
}

// ============================================================================
// Lane generation from the corpus
// ============================================================================

static int AP_NavRec_CompareS16(const void *a, const void *b)
{
	s16 x = *(const s16 *)a;
	s16 y = *(const s16 *)b;
	return (x > y) - (x < y);
}

// Build three lanes out of every clean banked lap.
//
//   lane 0 = the MEDIAN line across laps (per node index)
//   lane 1 = median shifted toward the widest observed side
//   lane 2 = median shifted toward the other observed side
//
// The shift is CLAMPED to the lateral envelope humans actually drove, which is
// what makes this obstacle-safe: nobody drove through the pillar, so the
// envelope excludes the pillar. It is also edge-safe for the same reason, with
// no per-track width data and nothing hand-authored.
//
// One lap only -> zero envelope -> the three lanes collapse onto the median.
// Bots then stack, but they stay on the road. That degradation is deliberate.
static int AP_NavRec_BuildLanes(void)
{
	char msg[224];

	int used[AP_NAVREC_MAX_LAPS];
	int usedCount = 0;

	for (int i = 0; i < g_lapCount; i++)
	{
		if (g_laps[i].dirty)
			continue;
		if (AP_NavRec_ResampleLap(i, g_targetNodes) != g_targetNodes)
			continue;
		used[usedCount++] = i;
	}

	if (usedCount == 0)
	{
		AP_LogLine("[AP NAVREC] no clean laps banked, nothing to build\n");
		return 0;
	}

	snprintf(msg, sizeof msg, "[AP NAVREC] building lanes from %d clean lap(s) of %d banked\n", usedCount, g_lapCount);
	AP_LogLine(msg);

	int envMaxSeen = 0;
	int envClamped = 0;

	for (int n = 0; n < g_targetNodes; n++)
	{
		// Median position at this node across laps.
		s16 xs[AP_NAVREC_MAX_LAPS], ys[AP_NAVREC_MAX_LAPS], zs[AP_NAVREC_MAX_LAPS];
		for (int u = 0; u < usedCount; u++)
		{
			xs[u] = g_resX[used[u]][n];
			ys[u] = g_resY[used[u]][n];
			zs[u] = g_resZ[used[u]][n];
		}
		qsort(xs, (size_t)usedCount, sizeof(s16), AP_NavRec_CompareS16);
		qsort(ys, (size_t)usedCount, sizeof(s16), AP_NavRec_CompareS16);
		qsort(zs, (size_t)usedCount, sizeof(s16), AP_NavRec_CompareS16);

		const int medX = xs[usedCount / 2];
		const int medY = ys[usedCount / 2];
		const int medZ = zs[usedCount / 2];

		// Travel direction at this node, from the median line itself.
		int nNext = (n + 1) % g_targetNodes;
		s16 nxs[AP_NAVREC_MAX_LAPS], nzs[AP_NAVREC_MAX_LAPS];
		for (int u = 0; u < usedCount; u++)
		{
			nxs[u] = g_resX[used[u]][nNext];
			nzs[u] = g_resZ[used[u]][nNext];
		}
		qsort(nxs, (size_t)usedCount, sizeof(s16), AP_NavRec_CompareS16);
		qsort(nzs, (size_t)usedCount, sizeof(s16), AP_NavRec_CompareS16);

		double dirX = (double)(nxs[usedCount / 2] - medX);
		double dirZ = (double)(nzs[usedCount / 2] - medZ);
		double dirLen = sqrt((dirX * dirX) + (dirZ * dirZ));

		double perpX = 0.0, perpZ = 0.0;
		if (dirLen > 0.0001)
		{
			perpX = -dirZ / dirLen;
			perpZ = dirX / dirLen;
		}

		// Lateral spread of the corpus at this node, projected onto the normal.
		double latMin = 0.0, latMax = 0.0;
		for (int u = 0; u < usedCount; u++)
		{
			double ox = (double)(g_resX[used[u]][n] - medX);
			double oz = (double)(g_resZ[used[u]][n] - medZ);
			double lat = (ox * perpX) + (oz * perpZ);
			if (lat < latMin)
				latMin = lat;
			if (lat > latMax)
				latMax = lat;
		}

		// Stay inside what was actually driven (80% of it, so a lane never sits
		// exactly on the outermost wheel track anyone managed).
		double shiftPos = latMax * 0.8;
		double shiftNeg = latMin * 0.8;
		if (shiftPos > (double)g_laneOffset)
		{
			shiftPos = (double)g_laneOffset;
			envClamped++;
		}
		if (shiftNeg < -(double)g_laneOffset)
			shiftNeg = -(double)g_laneOffset;

		if ((int)(latMax - latMin) > envMaxSeen)
			envMaxSeen = (int)(latMax - latMin);

		const double shift[AP_NAVREC_LANES] = {0.0, shiftPos, shiftNeg};

		for (int lane = 0; lane < AP_NAVREC_LANES; lane++)
		{
			struct NavFrame *nf = &g_outFrames[lane][n];
			memset(nf, 0, sizeof *nf);

			nf->pos.x = (s16)(medX + (int)(perpX * shift[lane]));
			nf->pos.y = (s16)medY;
			nf->pos.z = (s16)(medZ + (int)(perpZ * shift[lane]));

			// rot[] holds the CTR angle >> 4 (BOTS_GotoStartingLine rebuilds it
			// with << 4). Only yaw matters for a ground path.
			nf->rot[1] = (u8)((g_resRot[used[0]][n] & 0xFFF) >> 4);

			// OR the flags across laps: if any clean lap was airborne here, the
			// node is airborne.
			u16 fl = 0;
			for (int u = 0; u < usedCount; u++)
				fl |= g_resFlags[used[u]][n];
			nf->flags = (s16)fl;

			nf->pathChangeOpcode = AP_NAVREC_NO_LANE_CHANGE;
			nf->goBackCount = 4;
			nf->specialBits = 0;
		}
	}

	// distToNextNav*, in NavFrame units. These are what the bot consumes as
	// travelled distance (navProgressRemainder, BOTS.c:656 / :1574-1700), so they
	// have to be geometrically true or the bot walks the path at the wrong rate.
	for (int lane = 0; lane < AP_NAVREC_LANES; lane++)
	{
		for (int n = 0; n < g_targetNodes; n++)
		{
			struct NavFrame *cur = &g_outFrames[lane][n];
			struct NavFrame *nxt = &g_outFrames[lane][(n + 1) % g_targetNodes];

			double dx = (double)(nxt->pos.x - cur->pos.x);
			double dy = (double)(nxt->pos.y - cur->pos.y);
			double dz = (double)(nxt->pos.z - cur->pos.z);

			double xz = sqrt((dx * dx) + (dz * dz));
			double xyz = sqrt((dx * dx) + (dy * dy) + (dz * dz));

			cur->distToNextNavXZ = (s16)((xz > 32767.0) ? 32767.0 : xz);
			cur->distToNextNavXYZ = (s16)((xyz > 32767.0) ? 32767.0 : xyz);
		}

		g_outCount[lane] = g_targetNodes;

		memset(&g_outHeader[lane], 0, sizeof g_outHeader[lane]);
		g_outHeader[lane].magicNumber = (s16)-0x1303; // 0xECFD, what BOTS_InitNavPath validates
		g_outHeader[lane].numPoints = (s16)g_targetNodes;
		g_outHeader[lane].posY_firstNode = (int)g_outFrames[lane][0].pos.y;
		g_outHeader[lane].last = &g_outFrames[lane][g_targetNodes];
	}

	snprintf(msg, sizeof msg, "[AP NAVREC] envelope: widest observed spread %d units, %d node(s) clamped to the %d cap\n", envMaxSeen,
	         envClamped, g_laneOffset);
	AP_LogLine(msg);

	if (usedCount < 2)
		AP_LogLine("[AP NAVREC] WARNING: only one clean lap, so the lanes collapse onto it. Drive more laps for overtaking room.\n");

	return 1;
}

// Everything this feature writes goes in ONE folder, so a player who changes
// their mind can delete the whole thing without hunting loose files out of the
// game directory. Created on demand, never on startup: with the option off,
// the folder does not appear at all.
#define AP_NAVREC_DIR "ap-navpaths"

// Same portable shape native_memcard.c uses for its own save directory, kept
// local rather than un-`internal`ing the assets helper: this is the only
// caller and widening a platform API for one use is not worth it.
static int AP_NavRec_MakeDir(const char *path)
{
#if defined(_WIN32)
	if (CreateDirectoryA(path, NULL) != 0)
		return 1;
	return GetLastError() == ERROR_ALREADY_EXISTS;
#else
	if (mkdir(path, 0777) == 0)
		return 1;
	return errno == EEXIST;
#endif
}

static void AP_NavRec_Write(int levelID)
{
	char msg[256];

	// Belt and braces. The caller is already gated on the option, but this is
	// the only function in the build that creates files on a player's disk, so
	// it re-checks rather than trusting its caller.
	if (!g_config.navRecord)
		return;

	if (!AP_NavRec_BuildLanes())
		return;

	AP_NavRec_MakeDir(AP_NAVREC_DIR);

	char path[128];
	snprintf(path, sizeof path, AP_NAVREC_DIR "/navpath-%d.bin", levelID);

	FILE *f = fopen(path, "wb");
	if (f == NULL)
	{
		snprintf(msg, sizeof msg, "[AP NAVREC] ERROR: cannot open \"%s\" for writing\n", path);
		AP_LogLine(msg);
		return;
	}

	// Container: magic, levelID, 3 counts, then 3 raw NavFrame arrays. This is
	// our own format, read back by our injector, never by the engine file path.
	const int magic = 0x5652414E; // 'NAVR'
	fwrite(&magic, 4, 1, f);
	fwrite(&levelID, 4, 1, f);
	fwrite(g_outCount, 4, AP_NAVREC_LANES, f);
	for (int lane = 0; lane < AP_NAVREC_LANES; lane++)
		fwrite(&g_outFrames[lane][0], sizeof(struct NavFrame), (size_t)g_outCount[lane], f);
	fclose(f);

	snprintf(msg, sizeof msg, "[AP NAVREC] wrote \"%s\": lanes %d/%d/%d nodes\n", path, g_outCount[0], g_outCount[1], g_outCount[2]);
	AP_LogLine(msg);

	// The human-readable dump is an AUTHORING aid, not something a player who
	// ticked one box should get: it is a per-node listing running to thousands
	// of lines per track. Opt in separately via the environment, which is the
	// right home for a developer switch.
	if (AP_NavRec_EnvInt("CTR_AP_NAV_REC_DUMP", 0) <= 0)
		return;

	snprintf(path, sizeof path, AP_NAVREC_DIR "/navpath-%d.txt", levelID);
	f = fopen(path, "w");
	if (f != NULL)
	{
		fprintf(f, "levelID %d, %d laps banked, target %d nodes, max lane offset %d\n", levelID, g_lapCount, g_targetNodes, g_laneOffset);
		for (int i = 0; i < g_lapCount; i++)
			fprintf(f, "  lap %d: %d samples, %d frames, %s\n", i, g_laps[i].count, g_laps[i].frames, g_laps[i].dirty ? "DIRTY" : "clean");
		for (int lane = 0; lane < AP_NAVREC_LANES; lane++)
		{
			fprintf(f, "\n--- lane %d: %d nodes ---\n", lane, g_outCount[lane]);
			for (int i = 0; i < g_outCount[lane]; i++)
			{
				struct NavFrame *nf = &g_outFrames[lane][i];
				fprintf(f, "%4d  pos=(%6d,%6d,%6d) rotY=%3u distXZ=%5d distXYZ=%5d flags=0x%04x\n", i, nf->pos.x, nf->pos.y, nf->pos.z,
				        (unsigned)nf->rot[1], nf->distToNextNavXZ, nf->distToNextNavXYZ, (unsigned)(u16)nf->flags);
			}
		}
		fclose(f);
	}

	AP_LogLine("[AP NAVREC] done. Load it back with CTR_AP_NAV_SPIKE=5 on this track.\n");
}

// ============================================================================
// Recording
// ============================================================================

static void AP_NavRec_BankLap(void)
{
	char msg[192];

	if (g_curCount < 16)
	{
		g_curCount = 0;
		g_curFrames = 0;
		g_curDirty = 0;
		return;
	}

	if (g_lapCount >= AP_NAVREC_MAX_LAPS)
	{
		AP_LogLine("[AP NAVREC] lap bank full, dropping the oldest\n");
		memmove(&g_laps[0], &g_laps[1], sizeof(struct AP_NavRecLap) * (AP_NAVREC_MAX_LAPS - 1));
		g_lapCount = AP_NAVREC_MAX_LAPS - 1;
	}

	struct AP_NavRecLap *L = &g_laps[g_lapCount];
	memcpy(L->samples, g_cur, sizeof(struct AP_NavRecSample) * (size_t)g_curCount);
	L->count = g_curCount;
	L->frames = g_curFrames;
	L->dirty = g_curDirty;
	g_lapCount++;

	snprintf(msg, sizeof msg, "[AP NAVREC] banked lap %d: %d samples, %d frames, %s\n", g_lapCount - 1, L->count, L->frames,
	         L->dirty ? "DIRTY (respawn/hit) - will be skipped" : "clean");
	AP_LogLine(msg);

	g_curCount = 0;
	g_curFrames = 0;
	g_curDirty = 0;
}

void AP_NavRec_Tick(struct GameTracker *gGT)
{
	AP_NavRec_ReadEnvOnce();

	if (!g_armed || (gGT == NULL))
		return;

	// Never sample while a load is in flight. The driver struct survives a level
	// transition but the level data it points into does not, so a driver field
	// like currBlockTouching can be non-NULL and still reference freed memory.
	// Same guard the retail-crate harvest uses, and for the same reason.
	if ((sdata == NULL) || (sdata->Loading.stage != LOAD_IDLE))
		return;

	int levelID = (int)gGT->levelID;

	// New level: drop the whole corpus, it belongs to the old track.
	if (levelID != g_lastLevelID)
	{
		g_lastLevelID = levelID;
		g_lapCount = 0;
		g_curCount = 0;
		g_curFrames = 0;
		g_curDirty = 0;
		g_lastLapIndex = -1;
		// Skip this frame outright. The driver's block pointer belongs to the
		// level we just left; the engine repopulates it on the next gameplay
		// frame. Sampling here dereferenced the old level's freed block data
		// and crashed on entering Polar Pass (0xc0000005 at ap_navrec.c:577,
		// live capture 2026-08-17 17:09). A NULL check does not help: the
		// pointer is stale, not null.
		return;
	}

	// Numpad 9, edge-triggered: bank whatever is in progress, then write.
	int keyDown = Platform_InputRawKeyDown(AP_NAVREC_KEY_WRITE);
	if (keyDown && !g_keyLatch)
	{
		AP_NavRec_BankLap();
		AP_NavRec_Write(levelID);
	}
	g_keyLatch = keyDown;

	struct Driver *d = gGT->drivers[0];
	if (d == NULL)
		return;

	if (gGT->trafficLightsTimer > 0)
		return;
	if ((gGT->gameMode1 & END_OF_RACE) != 0)
		return;

	// Lap boundary: bank the lap just finished and start the next one.
	int lapIndex = (int)d->lapIndex;
	if ((g_lastLapIndex >= 0) && (lapIndex != g_lastLapIndex))
		AP_NavRec_BankLap();
	g_lastLapIndex = lapIndex;

	// Anything that means this lap is not a line worth copying.
	u8 ks = d->kartState;
	if ((ks == KS_MASK_GRABBED) || (ks == KS_BLASTED) || (ks == KS_SPINNING))
		g_curDirty = 1;

	if (g_curCount >= AP_NAVREC_MAX_SAMPLES)
		return;

	struct AP_NavRecSample *s = &g_cur[g_curCount];
	s->x = d->posCurr.x;
	s->y = d->posCurr.y;
	s->z = d->posCurr.z;
	s->rotY = (s16)d->rotCurr.y;

	u16 flags = 0;
	if (d->currBlockTouching != NULL)
		flags |= (u16)(((u16)d->currBlockTouching->terrain_type & 0xF) << 3);

	// Airborne frames ARE recorded (retail lines carry a jump flag at 0x400, so
	// real nodes exist mid-jump). Dropping them left 2200-unit gaps across jumps
	// where retail's longest segment on Crash Cove is 499.
	if ((d->actionsFlagSet & ACTION_TOUCH_GROUND) == 0)
		flags |= 0x400;

	s->flags = flags;

	g_curCount++;
	g_curFrames++;
}

int AP_NavRec_LoadForLevel(int levelID, struct NavHeader **outHeaders, struct NavFrame **outFrames, int *outCounts)
{
	char path[128];
	char msg[192];

	// Read-only, and gated on its OWN option: someone who wants recorded lines
	// should not have to switch recording on to get them, since that is the
	// half that writes to their disk.
	if (!g_config.navUseRecorded)
		return 0;

	snprintf(path, sizeof path, AP_NAVREC_DIR "/navpath-%d.bin", levelID);

	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return 0;

	int magic = 0;
	int fileLevel = 0;
	int counts[AP_NAVREC_LANES] = {0, 0, 0};

	if ((fread(&magic, 4, 1, f) != 1) || (magic != 0x5652414E) || (fread(&fileLevel, 4, 1, f) != 1) ||
	    (fread(counts, 4, AP_NAVREC_LANES, f) != AP_NAVREC_LANES))
	{
		fclose(f);
		AP_LogLine("[AP NAVREC] load: bad header\n");
		return 0;
	}

	for (int lane = 0; lane < AP_NAVREC_LANES; lane++)
	{
		if ((counts[lane] <= 0) || (counts[lane] >= AP_NAVREC_MAX_NODES))
		{
			fclose(f);
			AP_LogLine("[AP NAVREC] load: implausible node count\n");
			return 0;
		}

		if (fread(&g_outFrames[lane][0], sizeof(struct NavFrame), (size_t)counts[lane], f) != (size_t)counts[lane])
		{
			fclose(f);
			AP_LogLine("[AP NAVREC] load: short read\n");
			return 0;
		}

		memset(&g_outHeader[lane], 0, sizeof g_outHeader[lane]);
		g_outHeader[lane].magicNumber = (s16)-0x1303;
		g_outHeader[lane].numPoints = (s16)counts[lane];
		g_outHeader[lane].posY_firstNode = (int)g_outFrames[lane][0].pos.y;
		g_outHeader[lane].last = &g_outFrames[lane][counts[lane]];

		g_outCount[lane] = counts[lane];
		outHeaders[lane] = &g_outHeader[lane];
		outFrames[lane] = &g_outFrames[lane][0];
		outCounts[lane] = counts[lane];
	}

	fclose(f);

	snprintf(msg, sizeof msg, "[AP NAVREC] loaded \"%s\" (level %d): lanes %d/%d/%d nodes\n", path, fileLevel, counts[0], counts[1],
	         counts[2]);
	AP_LogLine(msg);
	return 1;
}

#endif // CTR_AP
