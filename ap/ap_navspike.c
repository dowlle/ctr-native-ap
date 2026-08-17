#ifdef CTR_AP

#include <common.h> // sdata, struct NavHeader / NavFrame (namespace_Bots.h)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ap_navspike.h"
#include "ap_hooks.h" // AP_LogLine
#include "ap_navrec.h" // mode 5: recorded-path loader
#include "platform/native_config.h" // g_config.navUseRecorded

// Storage for our own copy of the three paths. Static rather than malloc'd so
// the pointers stay valid for the process lifetime and cannot be freed out from
// under the AI. 3 * 2048 * 20 bytes = 120 KB.
#define AP_NAVSPIKE_MAX_FRAMES 2048

static struct NavHeader g_spikeHeader[3];
static struct NavFrame  g_spikeFrames[3][AP_NAVSPIKE_MAX_FRAMES];
static int              g_spikeActive;

static int AP_NavSpike_EnvInt(const char *name, int fallback)
{
	const char *v = getenv(name);
	if ((v == NULL) || (v[0] == '\0'))
		return fallback;
	return (int)strtol(v, NULL, 0);
}

// Log what the LEV actually gave us. This alone is data we have never
// collected: how many nav paths a retail track ships, how long they are, and
// what the per-node flags look like.
static void AP_NavSpike_DumpPath(int i, const struct NavHeader *nh, const struct NavFrame *frames)
{
	char msg[256];

	if (nh == NULL)
	{
		snprintf(msg, sizeof msg, "[AP NAVSPIKE] path %d: header NULL\n", i);
		AP_LogLine(msg);
		return;
	}

	snprintf(msg, sizeof msg,
	         "[AP NAVSPIKE] path %d: magic=0x%04x numPoints=%d header=%p frames=%p\n",
	         i, (unsigned)(u16)nh->magicNumber, (int)nh->numPoints, (void *)nh, (void *)frames);
	AP_LogLine(msg);

	if ((frames == NULL) || (nh->numPoints <= 0))
		return;

	// First, middle and last node, plus an OR of every flag word seen. The OR
	// tells us which behaviours (drift, hop, turbo, terrain bits) this track's
	// line actually uses without dumping hundreds of lines.
	u16 flagsOr = 0;
	u16 specialOr = 0;
	int laneChanges = 0;
	for (int f = 0; f < nh->numPoints; f++)
	{
		flagsOr |= (u16)frames[f].flags;
		specialOr |= (u16)frames[f].specialBits;
		if (frames[f].pathChangeOpcode != 0)
			laneChanges++;
	}

	snprintf(msg, sizeof msg,
	         "[AP NAVSPIKE] path %d: flagsOR=0x%04x specialOR=0x%02x laneChangeNodes=%d\n",
	         i, (unsigned)flagsOr, (unsigned)specialOr, laneChanges);
	AP_LogLine(msg);

	// Ground truth for the recorder: what magnitude does retail actually store in
	// distToNextNav? A generated path has to land in the same range, and this is
	// how we caught v1 saturating every node at 32767.
	int dMin = 0x7FFFFFFF, dMax = -1, dSum = 0;
	for (int f = 0; f < nh->numPoints; f++)
	{
		int v = (int)frames[f].distToNextNavXZ;
		if (v < dMin)
			dMin = v;
		if (v > dMax)
			dMax = v;
		dSum += v;
	}
	snprintf(msg, sizeof msg, "[AP NAVSPIKE] path %d: distToNextNavXZ min=%d max=%d mean=%d\n", i, dMin, dMax, dSum / nh->numPoints);
	AP_LogLine(msg);

	int mid = nh->numPoints / 2;
	int end = nh->numPoints - 1;
	snprintf(msg, sizeof msg,
	         "[AP NAVSPIKE] path %d: n0=(%d,%d,%d) n%d=(%d,%d,%d) n%d=(%d,%d,%d)\n",
	         i,
	         (int)frames[0].pos.x, (int)frames[0].pos.y, (int)frames[0].pos.z,
	         mid, (int)frames[mid].pos.x, (int)frames[mid].pos.y, (int)frames[mid].pos.z,
	         end, (int)frames[end].pos.x, (int)frames[end].pos.y, (int)frames[end].pos.z);
	AP_LogLine(msg);
}

void AP_NavSpike_AfterInit(void)
{
	char msg[256];

	// The player-facing option maps onto mode 5 (inject recorded paths), the
	// only mode that is a FEATURE. Modes 1-4 are reconnaissance and
	// perturbation aids for developing this and stay on the environment
	// switch -- nobody should reach "truncate lane 0 and see what breaks"
	// from an options menu. An explicit env mode still wins, so a developer
	// can override the option without editing config.ini.
	int mode = AP_NavSpike_EnvInt("CTR_AP_NAV_SPIKE", 0);
	if ((mode <= 0) && g_config.navUseRecorded)
		mode = 5;
	if (mode <= 0)
		return;

	int offset = AP_NavSpike_EnvInt("CTR_AP_NAV_SPIKE_OFFSET", 0x200);

	snprintf(msg, sizeof msg, "[AP NAVSPIKE] mode=%d offset=%d levelID=%d\n",
	         mode, offset, (int)sdata->gGT->levelID);
	AP_LogLine(msg);

	// Mode 1: read-only reconnaissance.
	for (int i = 0; i < 3; i++)
		AP_NavSpike_DumpPath(i, sdata->NavPath_ptrHeader[i], sdata->NavPath_ptrNavFrameArray[i]);

	if (mode == 1)
		return;

	int injected = 0;

	// Mode 5: inject RECORDED paths (ap_navrec) instead of a copy of the LEV's.
	// Handled before the copy loop because a recorded path must also work on a
	// level whose own nav data is absent -- which is exactly the custom-track
	// case (a CrashTeamEditor track has LevNavTable == 0).
	if (mode == 5)
	{
		struct NavHeader *hdrs[3] = {NULL, NULL, NULL};
		struct NavFrame *frames[3] = {NULL, NULL, NULL};
		int counts[3] = {0, 0, 0};

		if (!AP_NavRec_LoadForLevel((int)sdata->gGT->levelID, hdrs, frames, counts))
		{
			AP_LogLine("[AP NAVSPIKE] mode 5: no recorded path for this level, leaving vanilla nav alone\n");
			return;
		}

		for (int i = 0; i < 3; i++)
		{
			sdata->NavPath_ptrHeader[i] = hdrs[i];
			sdata->NavPath_ptrNavFrameArray[i] = frames[i];
			injected++;

			snprintf(msg, sizeof msg, "[AP NAVSPIKE] path %d: INJECTED %d RECORDED nodes at %p\n", i, counts[i], (void *)frames[i]);
			AP_LogLine(msg);
		}

		BOTS_SetGlobalNavData(0);
		g_spikeActive = 1;
		AP_LogLine("[AP NAVSPIKE] recorded paths active\n");
		return;
	}

	for (int i = 0; i < 3; i++)
	{
		const struct NavHeader *src = sdata->NavPath_ptrHeader[i];
		const struct NavFrame *srcFrames = sdata->NavPath_ptrNavFrameArray[i];

		if ((src == NULL) || (srcFrames == NULL))
			continue;

		int n = (int)src->numPoints;

		// numPoints == 0 is the engine's "no AI on this path" state
		// (BOTS_InitNavPath's blank_NavHeader branch). Leave it alone.
		if (n <= 0)
			continue;

		// last is &frames[numPoints], one past the final real node, so our
		// buffer needs room for numPoints entries and a valid address at
		// index numPoints. We never READ src[numPoints].
		if (n >= AP_NAVSPIKE_MAX_FRAMES)
		{
			snprintf(msg, sizeof msg,
			         "[AP NAVSPIKE] path %d: SKIPPED, %d nodes exceeds cap %d\n",
			         i, n, AP_NAVSPIKE_MAX_FRAMES);
			AP_LogLine(msg);
			continue;
		}

		memcpy(&g_spikeHeader[i], src, sizeof(struct NavHeader));
		memcpy(&g_spikeFrames[i][0], srcFrames, (size_t)n * sizeof(struct NavFrame));

		if (mode == 3)
		{
			for (int f = 0; f < n; f++)
				g_spikeFrames[i][f].pos.x = (s16)(g_spikeFrames[i][f].pos.x + offset);
		}

		if ((mode == 4) && (i == 0))
		{
			n = n / 2;
			if (n < 2)
				n = 2;
		}

		g_spikeHeader[i].numPoints = (s16)n;
		g_spikeHeader[i].last = &g_spikeFrames[i][n];

		sdata->NavPath_ptrHeader[i] = &g_spikeHeader[i];
		sdata->NavPath_ptrNavFrameArray[i] = &g_spikeFrames[i][0];

		injected++;

		snprintf(msg, sizeof msg,
		         "[AP NAVSPIKE] path %d: INJECTED %d nodes at %p (header %p)\n",
		         i, n, (void *)&g_spikeFrames[i][0], (void *)&g_spikeHeader[i]);
		AP_LogLine(msg);
	}

	if (injected > 0)
	{
		// Re-derive the globals (nav_ptrFirstPoint / nav_ptrLastPoint /
		// nav_NumPointsOnPath) so they point into our copy, not the LEV.
		// BOTS_UpdateGlobals already called this with 0 just before us.
		BOTS_SetGlobalNavData(0);
		g_spikeActive = 1;
	}

	snprintf(msg, sizeof msg, "[AP NAVSPIKE] injected %d/3 paths\n", injected);
	AP_LogLine(msg);
}

#endif // CTR_AP
