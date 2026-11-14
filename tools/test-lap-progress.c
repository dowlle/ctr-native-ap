// Host harness for lap progress on quads without a checkpoint (A3-26).
// The rule under test lives in ap/ap_lap_progress_logic.h, which
// game/Vehicle/VehLap.c calls from VehLap_UpdateProgress.
// tools/test-lap-progress-production.py checks that wiring.
//
// Build:
//   gcc -std=c99 -Wall -Wextra -Werror tools/test-lap-progress.c -o /tmp/test-lap-progress -lm
//   /tmp/test-lap-progress
//
// Synthetic cases always run. Retail track data is read only from files
// named in the environment, never from the repository:
//   CTR_LAP_PROGRESS_NGIN_LEV   N. Gin Labs 1P LEV (retail BIGFILE entry 89)
//   CTR_LAP_PROGRESS_PAPU_LEV   Papu's Pyramid 1P LEV (retail BIGFILE entry 41)
// With the N. Gin Labs file, a bot is driven off its nav line down each AI
// lane of the barrel ramp, its checkpoint taken from the quad under it as
// BOTS.c does. The retail measurement counts a lap on lane 0; with the rule
// no lane does.
// With the Papu's Pyramid file (A4-78), a bot is driven on each nav line
// across the finish line, its checkpoint taken from the nav frame as BOTS.c
// does. Those frames name restart point 102 of 102. Retail counts the lap on
// every line; skipping them, as A3-26 first did, loses it on line 0; with the
// rule every line counts it again.
// Papu's Pyramid standings (A4-78b): over every grid, empty slot and path draw,
// the human's HUD place on lap 3 and the place the finish pays out, with 7
// racers as well as 8. Some grids under the A3-26 skip pay 4th; the rule matches retail.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ap/ap_lap_progress_logic.h"

static int checks = 0;
static int fails = 0;

#define CHECK(cond, what)                                    \
	do                                                       \
	{                                                        \
		checks++;                                            \
		if (!(cond))                                         \
		{                                                    \
			fails++;                                         \
			printf("FAIL %s (line %d)\n", what, __LINE__);   \
		}                                                    \
	} while (0)

static void synthetic(void)
{
	CHECK(AP_LapProgress_IndexUsable(0, 138), "the first restart point is usable");
	CHECK(AP_LapProgress_IndexUsable(137, 138), "the last restart point is usable");
	CHECK(!AP_LapProgress_IndexUsable(138, 138), "one past the last is refused");
	CHECK(!AP_LapProgress_IndexUsable(0xff, 138), "a quad without a checkpoint is refused");
	CHECK(!AP_LapProgress_IndexUsable(-1, 138), "a negative index is refused");
	CHECK(!AP_LapProgress_IndexUsable(0, 0), "a level without restart points refuses everything");
	CHECK(AP_LapProgress_IndexUsable(254, 255), "a 255-point level keeps index 254");

	// A4-78: which index is measured.
	CHECK(AP_LapProgress_MeasureIndex(40, 39, 40, 0, 102) == 40, "a usable index is measured");
	CHECK(AP_LapProgress_MeasureIndex(0, 101, 0, 1, 102) == 0, "restart point 0 is measured");
	CHECK(AP_LapProgress_MeasureIndex(0xff, 80, 81, 0, 138) == -1, "a quad without a checkpoint still skips (A3-26)");
	CHECK(AP_LapProgress_MeasureIndex(0xff, 137, 0, 1, 138) == -1, "a quad without a checkpoint skips even at the finish");
	CHECK(AP_LapProgress_MeasureIndex(102, 101, 0, 1, 102) == 101, "past the array at the finish: measured from the last checkpoint");
	CHECK(AP_LapProgress_MeasureIndex(130, 101, 0, 1, 102) == 101, "any index past the array other than 0xff, at the finish");
	CHECK(AP_LapProgress_MeasureIndex(102, 101, 0, 0, 102) == -1, "away from restart point 0 it skips");
	CHECK(AP_LapProgress_MeasureIndex(102, 57, 58, 1, 102) == -1, "past the array mid-track skips (no stale measurement)");
	CHECK(AP_LapProgress_MeasureIndex(102, 57, -1, 1, 102) == -1, "an unread forward link skips");
	CHECK(AP_LapProgress_MeasureIndex(102, 102, 0, 1, 102) == -1, "no usable last checkpoint skips");
	CHECK(AP_LapProgress_MeasureIndex(3, 5, 0, 1, 0) == -1, "a level without restart points skips");
	CHECK(AP_LapProgress_NearFinish(1201 + 0x1000, -192, -5729 - 0x1000, 1201, -192, -5729), "the reach is inclusive");
	CHECK(!AP_LapProgress_NearFinish(1201 + 0x1001, -192, -5729, 1201, -192, -5729), "one past the reach on x is far");
	CHECK(!AP_LapProgress_NearFinish(1201, -192 - 0x1001, -5729, 1201, -192, -5729), "one past the reach on y is far");
	CHECK(!AP_LapProgress_NearFinish(1201, -192, -5729 + 0x1001, 1201, -192, -5729), "one past the reach on z is far");
}

// ---------------------------------------------------------------------------
// Retail N. Gin Labs.

struct Lev
{
	uint8_t *d;
	uint32_t size;
};

static uint32_t rd32(const struct Lev *l, uint32_t o) { uint32_t v; memcpy(&v, l->d + o, 4); return v; }
static int16_t rd16(const struct Lev *l, uint32_t o) { int16_t v; memcpy(&v, l->d + o, 2); return v; }
static uint16_t rdu16(const struct Lev *l, uint32_t o) { uint16_t v; memcpy(&v, l->d + o, 2); return v; }

struct Node { int x, y, z, dist, fwd; };

static struct Node node_at(const struct Lev *l, uint32_t rp, int i)
{
	struct Node n;
	uint32_t o = rp + (uint32_t)i * 12;
	n.x = rd16(l, o);
	n.y = rd16(l, o + 2);
	n.z = rd16(l, o + 4);
	n.dist = rdu16(l, o + 6);
	n.fwd = l->d[o + 8];
	return n;
}

enum Mode
{
	RETAIL = 0, // reads past the array as the disc does
	A3_26 = 1,  // the first A3-26 guard: skip every index past the array
	RULE = 2,   // the current rule: AP_LapProgress_MeasureIndex, then the guard
};

// VehLap_UpdateProgress, in floating point: progress node's distance plus the
// kart's projection on the progress node -> next node direction (reversed),
// signed remainder by the track length. *last is the racer's checkpoint
// (driver->checkpoint.currentIndex), written when the update runs.
static int measure(const struct Lev *l, uint32_t rp, int cnt, int cp, int x, int y, int z, int mode, int *last, int *out)
{
	int guarded = mode != RETAIL;
	if (mode == RULE)
	{
		int lastForward = AP_LapProgress_IndexUsable(*last, cnt) ? node_at(l, rp, *last).fwd : -1;
		struct Node start = node_at(l, rp, 0);
		int nearFinish = AP_LapProgress_NearFinish(x, y, z, start.x, start.y, start.z);
		cp = AP_LapProgress_MeasureIndex(cp, *last, lastForward, nearFinish, cnt);
	}
	if (cp < 0)
		return 0;
	if (guarded && !AP_LapProgress_IndexUsable(cp, cnt))
		return 0;
	struct Node c = node_at(l, rp, cp);
	if (guarded && !AP_LapProgress_IndexUsable(c.fwd, cnt))
		return 0;
	struct Node p = node_at(l, rp, c.fwd);
	if (guarded && !AP_LapProgress_IndexUsable(p.fwd, cnt))
		return 0;
	struct Node n = node_at(l, rp, p.fwd);
	double dx = p.x - n.x, dy = p.y - n.y, dz = p.z - n.z;
	double m = sqrt(dx * dx + dy * dy + dz * dz);
	if (m == 0)
		m = 1;
	double proj = ((x - p.x) * dx + (y - p.y) * dy + (z - p.z) * dz) / m;
	int len = node_at(l, rp, 0).dist * 8;
	*out = (int)fmod((double)(p.dist * 8) + floor(proj), (double)len);
	*last = cp;
	return 1;
}

// The PlayLevel_UpdateLapStats crossing rule on one new measurement.
static void cross(int have, int prev, int cur, int *laps, int *behind)
{
	if (!have)
		return;
	if (prev < 1200 && cur > 32000)
	{
		if (*behind)
			*behind = 0;
		else
			(*laps)++;
	}
	else if (cur < 1200 && prev > 32000)
	{
		*behind = 1;
	}
}

// The ground quad under (x, z) nearest height y, by its real corners. CTR
// stores a quad's corners as 0 1 / 2 3, so the outline is 0 1 3 2.
static int quad_checkpoint(const struct Lev *l, double x, double y, double z, int *quadOut)
{
	uint32_t mesh = rd32(l, 0);
	uint32_t nq = rd32(l, mesh), qa = rd32(l, mesh + 0xc), va = rd32(l, mesh + 0x10);
	int best = -1, bestCp = -1;
	double bestDy = 0;
	for (uint32_t i = 0; i < nq; i++)
	{
		uint32_t q = qa + i * 0x5c;
		uint16_t flags = rdu16(l, q + 0x12);
		if (!(flags & 0x1000) || (flags & 0x10))
			continue;
		static const int order[4] = {0, 1, 3, 2};
		double px[4], py[4], pz[4];
		for (int k = 0; k < 4; k++)
		{
			uint32_t v = va + (uint32_t)rdu16(l, q + (uint32_t)order[k] * 2) * 16;
			px[k] = rd16(l, v);
			py[k] = rd16(l, v + 2);
			pz[k] = rd16(l, v + 4);
		}
		int sign = 0, in = 1;
		for (int k = 0; k < 4 && in; k++)
		{
			int n = (k + 1) % 4;
			double cr = (px[n] - px[k]) * (z - pz[k]) - (pz[n] - pz[k]) * (x - px[k]);
			if (cr == 0)
				continue;
			if (sign == 0)
				sign = cr > 0 ? 1 : -1;
			else if ((cr > 0) != (sign > 0))
				in = 0;
		}
		if (!in)
			continue;
		double dy = fabs((py[0] + py[1] + py[2] + py[3]) / 4 - y);
		if (best < 0 || dy < bestDy)
		{
			best = (int)i;
			bestDy = dy;
			bestCp = l->d[q + 0x3e];
		}
	}
	*quadOut = best;
	return bestCp;
}

// One drive down the barrel ramp along an AI lane, shifted sideways, off the
// nav line the whole way (checkpoint from the quad under the kart). Returns
// the laps the PlayLevel_UpdateLapStats crossing rule counts.
static int drive(const struct Lev *l, uint32_t rp, int cnt, int lane, double shift, int mode, int *lowest)
{
	uint32_t hp = rd32(l, rd32(l, 0x188) + (uint32_t)lane * 4);
	int npts = rd16(l, hp + 2);
	int first = -1, last = -1;
	for (int k = 0; k < npts; k++)
	{
		int gb = l->d[hp + 0x4c + (uint32_t)k * 0x14 + 0x12];
		if (gb >= 70 && gb <= 88)
		{
			if (first < 0)
				first = k;
			last = k;
		}
	}
	int laps = 0, behind = 0, have = 0, prev = 0;
	int cp = l->d[hp + 0x4c + (uint32_t)first * 0x14 + 0x12];
	int lastCp = cp;
	*lowest = 1 << 30;
	for (int k = first; k < last; k++)
	{
		uint32_t fa = hp + 0x4c + (uint32_t)k * 0x14, fb = fa + 0x14;
		double ax = rd16(l, fa), ay = rd16(l, fa + 2), az = rd16(l, fa + 4);
		double ex = rd16(l, fb) - ax, ey = rd16(l, fb + 2) - ay, ez = rd16(l, fb + 4) - az;
		double seg = sqrt(ex * ex + ez * ez);
		if (seg == 0)
			continue;
		int steps = (int)(seg / 40) + 1;
		for (int s = 0; s < steps; s++)
		{
			double t = (double)s / steps;
			double x = ax + ex * t - ez / seg * shift, y = ay + ey * t, z = az + ez * t + ex / seg * shift;
			int quad;
			int quadCp = quad_checkpoint(l, x, y, z, &quad);
			if (quad >= 0)
				cp = quadCp; // BOTS.c: ai_quadblock_checkpointIndex = quad->checkpointIndex
			int cur;
			if (!measure(l, rp, cnt, cp, (int)x, (int)y, (int)z, mode, &lastCp, &cur))
				continue; // the update is skipped: progress is kept
			if (cur < *lowest)
				*lowest = cur;
			cross(have, prev, cur, &laps, &behind);
			prev = cur;
			have = 1;
		}
	}
	return laps;
}

// Reads a LEV named in the environment. Returns 0 (and says so) when unset.
static int load_lev(const char *env, const char *name, struct Lev *l)
{
	const char *path = getenv(env);
	l->d = NULL;
	l->size = 0;
	if (path == NULL || path[0] == 0)
	{
		printf("  skip retail %s: %s is not set\n", name, env);
		return 0;
	}
	FILE *f = fopen(path, "rb");
	CHECK(f != NULL, "the LEV opens");
	if (f == NULL)
		return 0;
	uint32_t size = 0;
	CHECK(fread(&size, 4, 1, f) == 1, "the LEV has a size");
	l->size = size;
	l->d = (uint8_t *)malloc(size);
	int ok = l->d != NULL && size > 0x190 && fread(l->d, 1, size, f) == size;
	CHECK(ok, "the LEV data reads");
	fclose(f);
	if (!ok)
	{
		free(l->d);
		l->d = NULL;
	}
	return ok;
}

static void retail_ngin(void)
{
	struct Lev l;
	if (!load_lev("CTR_LAP_PROGRESS_NGIN_LEV", "N. Gin Labs", &l))
		return;

	int cnt = (int)rd32(&l, 0x148);
	uint32_t rp = rd32(&l, 0x14c);
	CHECK(cnt == 138, "N. Gin Labs has 138 restart points");
	CHECK(node_at(&l, rp, 0).dist * 8 == 109648, "N. Gin Labs is 109648 long");

	int quad;
	struct Node mid = node_at(&l, rp, 80);
	CHECK(quad_checkpoint(&l, mid.x, mid.y, mid.z, &quad) == 0xff, "the barrel ramp has no checkpoint");
	CHECK(quad == 1106 || quad == 843 || quad == 776, "the ramp quad is one of the low lane's");

	// Lane 0 and lane 1's outer edge reach the corner of quad 776 that reads
	// below 1200; lane 2 does not.
	static const struct { int lane; double shift; int retailLaps; } cases[] = {
	    {0, 0, 1}, {0, 200, 1}, {1, -600, 1}, {1, 0, 0}, {2, 0, 0}, {0, -400, 0},
	};
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		int lowRetail, lowRule;
		int retailLaps = drive(&l, rp, cnt, cases[i].lane, cases[i].shift, RETAIL, &lowRetail);
		int ruleLaps = drive(&l, rp, cnt, cases[i].lane, cases[i].shift, RULE, &lowRule);
		int lowFirst;
		int firstLaps = drive(&l, rp, cnt, cases[i].lane, cases[i].shift, A3_26, &lowFirst);
		CHECK(firstLaps == ruleLaps && lowFirst == lowRule, "the A4-78 rule keeps the A3-26 result on the ramp");
		printf("  lane %d shift %+5.0f: retail laps %d (lowest progress %d), with the rule %d (lowest %d)\n", cases[i].lane,
		       cases[i].shift, retailLaps, lowRetail, ruleLaps, lowRule);
		CHECK(retailLaps == cases[i].retailLaps, "retail counts the expected laps on the ramp");
		CHECK(ruleLaps == 0, "with the rule the ramp counts no lap");
		CHECK(lowRule > 32000, "with the rule the ramp keeps mid-track progress");
	}
	free(l.d);
}

// ---------------------------------------------------------------------------
// Retail Papu's Pyramid (A4-78).

// One drive along nav line `lane` on the line, from `back` frames before its
// first frame naming a restart point past the array to `ahead` frames after
// its last, sampled every `step` units starting `phase` units in. The
// checkpoint is the nav frame's, as BOTS.c takes it for a bot on its line.
// Returns the laps the crossing rule counts; *lastGood is the last progress
// measured before the first such frame.
static int drive_line(const struct Lev *l, uint32_t rp, int cnt, int lane, double step, double phase, int mode,
                      int *firstBad, int *lastBad, int *lastGood)
{
	uint32_t hp = rd32(l, rd32(l, 0x188) + (uint32_t)lane * 4);
	int npts = rd16(l, hp + 2);
	*firstBad = -1;
	*lastBad = -1;
	for (int k = 0; k < npts; k++)
	{
		if (l->d[hp + 0x4c + (uint32_t)k * 0x14 + 0x12] >= cnt)
		{
			if (*firstBad < 0)
				*firstBad = k;
			*lastBad = k;
		}
	}
	if (*firstBad < 0)
		return -1;
	int lo = *firstBad - 8 < 0 ? 0 : *firstBad - 8;
	int hi = *lastBad + 6 > npts - 1 ? npts - 1 : *lastBad + 6;
	int laps = 0, behind = 0, have = 0, prev = 0;
	int last = l->d[hp + 0x4c + (uint32_t)lo * 0x14 + 0x12];
	double t = phase, s0 = 0;
	*lastGood = -1;
	for (int k = lo; k < hi; k++)
	{
		uint32_t fa = hp + 0x4c + (uint32_t)k * 0x14, fb = fa + 0x14;
		int cp = l->d[fa + 0x12];
		double ax = rd16(l, fa), ay = rd16(l, fa + 2), az = rd16(l, fa + 4);
		double ex = rd16(l, fb) - ax, ey = rd16(l, fb + 2) - ay, ez = rd16(l, fb + 4) - az;
		double seg = sqrt(ex * ex + ey * ey + ez * ez);
		for (; seg > 0 && t < s0 + seg; t += step)
		{
			double u = (t - s0) / seg;
			int cur;
			if (!measure(l, rp, cnt, cp, (int)(ax + ex * u), (int)(ay + ey * u), (int)(az + ez * u), mode, &last, &cur))
				continue;
			if (k < *firstBad)
				*lastGood = cur;
			cross(have, prev, cur, &laps, &behind);
			prev = cur;
			have = 1;
		}
		s0 += seg;
	}
	return laps;
}

static void retail_papu(void)
{
	struct Lev l;
	if (!load_lev("CTR_LAP_PROGRESS_PAPU_LEV", "Papu's Pyramid", &l))
		return;

	int cnt = (int)rd32(&l, 0x148);
	uint32_t rp = rd32(&l, 0x14c);
	CHECK(cnt == 102, "Papu's Pyramid has 102 restart points");
	CHECK(node_at(&l, rp, 0).dist * 8 == 77776, "Papu's Pyramid is 77776 long");
	CHECK(node_at(&l, rp, 101).fwd == 0, "restart point 101 links forward to 0");
	CHECK(node_at(&l, rp, 102).fwd == 0, "retail's read of point 102, past the array, links forward to 0 too");

	static const int firstBadWant[3] = {278, 271, 286};
	static const int lastBadWant[3] = {282, 273, 287};
	static const double steps[] = {20, 60, 100, 160, 240};
	for (int lane = 0; lane < 3; lane++)
	{
		int missRetail = 0, missFirst = 0, missRule = 0, runs = 0, firstBad, lastBad, good, goodFirst = -1;
		for (unsigned si = 0; si < sizeof(steps) / sizeof(steps[0]); si++)
		{
			for (int ph = 0; ph < 4; ph++)
			{
				double phase = steps[si] * ph / 4;
				int r = drive_line(&l, rp, cnt, lane, steps[si], phase, RETAIL, &firstBad, &lastBad, &good);
				int a = drive_line(&l, rp, cnt, lane, steps[si], phase, A3_26, &firstBad, &lastBad, &good);
				if (si == 0 && ph == 0)
					goodFirst = good;
				int n = drive_line(&l, rp, cnt, lane, steps[si], phase, RULE, &firstBad, &lastBad, &good);
				missRetail += r != 1;
				missFirst += a != 1;
				missRule += n != 1;
				runs++;
			}
		}
		printf("  line %d frames %d..%d name point 102: lap missed retail %d/%d, A3-26 skip %d/%d (frozen at %d), rule %d/%d\n", lane,
		       firstBad, lastBad, missRetail, runs, missFirst, runs, goodFirst, missRule, runs);
		CHECK(firstBad == firstBadWant[lane] && lastBad == lastBadWant[lane], "the finish-line frames are the expected ones");
		CHECK(missRetail == 0, "retail counts the lap on every line");
		CHECK(missRule == 0, "with the rule every line counts the lap");
		if (lane == 0)
		{
			CHECK(missFirst == runs, "the A3-26 skip loses the lap on line 0 (the A4-78 report)");
			CHECK(goodFirst >= 1200, "line 0 freezes above the crossing threshold under the A3-26 skip");
		}
	}
	free(l.d);
}

// ---------------------------------------------------------------------------
// Papu's Pyramid standings over a whole race (A4-78b). sturg's third case:
// seven racers (one grid slot empty, A4-77), HUD 4th on lap 3 of 3 with most
// CPUs ahead on the minimap, and the results screen paid 4th. This models the
// parts of the race that follow from which crossings a CPU counts: a CPU that
// counts every crossing finishes after three laps; one that never counts the
// finish crossing never finishes and is sorted by lap index. The grid-slot to
// nav-line map is BOTS.c's pathOrder; the sort is PlayLevel_UpdateLapStats'.

#define RACERS 8

// 1 when every crossing on `lane` is counted for all step sizes and phases,
// 0 when none is, -1 when it varies.
static int lane_counts(const struct Lev *l, uint32_t rp, int cnt, int lane, int mode)
{
	static const double steps[] = {20, 60, 100, 160, 240};
	int seen = -2, fb, lb, good;
	for (unsigned si = 0; si < sizeof(steps) / sizeof(steps[0]); si++)
	{
		for (int ph = 0; ph < 4; ph++)
		{
			int r = drive_line(l, rp, cnt, lane, steps[si], steps[si] * ph / 4, mode, &fb, &lb, &good);
			if (r != 0 && r != 1)
				return -1;
			if (seen == -2)
				seen = r;
			else if (seen != r)
				return -1;
		}
	}
	return seen;
}

struct Racer
{
	int present, human, line, lapIndex, progress; // progress: units past the line
};

// The PlayLevel ranking loop over unfinished racers: highest lap, then most
// progress. An empty slot is skipped as a NULL driver is. Returns the human's
// rank (0 based).
static int sort_rank(const struct Racer *r)
{
	int rank[RACERS];
	for (int i = 0; i < RACERS; i++)
		rank[i] = -1;
	int humanRank = -1;
	for (int curr = 0; curr < 8; curr++)
	{
		int best = -1, bestLap = -10, bestProg = -1;
		for (int i = 0; i < RACERS; i++)
		{
			if (!r[i].present || rank[i] != -1)
				continue;
			if (r[i].lapIndex > bestLap || (r[i].lapIndex == bestLap && r[i].progress > bestProg))
			{
				best = i;
				bestLap = r[i].lapIndex;
				bestProg = r[i].progress;
			}
		}
		if (best != -1)
		{
			rank[best] = curr;
			if (r[best].human)
				humanRank = curr;
		}
	}
	return humanRank;
}

// One race. `counts[line]` says whether a CPU on that nav line counts the
// finish crossing. CPUs take 60 s a lap, the human 99, 69 and 84 s (sturg's
// 1:39.39, 1:08.83 and the rest of 4:12.29). *hud is the human's place on
// lap 3, as the HUD shows it; *paid the place the finish pays out: the
// number of racers that finish before the human (all CPUs that count laps,
// as a CPU that never counts a crossing never finishes). Both 0 based.
static void race(const int counts[3], int humanSlot, int missingSlot, int firstPath, int secondPath, int *hud, int *paid,
                 int *racers)
{
	int pathOrder[8] = {0, firstPath, secondPath + 1, 2, 0, firstPath ^ 1, (secondPath ^ 1) + 1, 2};
	struct Racer r[RACERS];
	*racers = 0;
	*paid = 0;
	for (int i = 0; i < RACERS; i++)
	{
		r[i].present = i != missingSlot;
		r[i].human = i == humanSlot;
		r[i].line = pathOrder[i];
		*racers += r[i].present;
		if (r[i].human)
		{
			// 12 s into lap 3 of the human's race, index 2.
			r[i].lapIndex = 2;
			r[i].progress = (int)(12.0 / 84.0 * 77776);
		}
		else
		{
			// At the same moment (180 s) a CPU is 59 s into its lap 3, two
			// crossings behind it when it counted them, none when it did not.
			r[i].lapIndex = counts[r[i].line] ? 2 : 0;
			r[i].progress = (int)(59.0 / 60.0 * 77776);
			*paid += r[i].present && counts[r[i].line];
		}
	}
	*hud = sort_rank(r);
}

// Without track data: the model alone, with line 0 not counting (the skip) or
// counting (retail and the rule). Human in slot 2, slot 6 empty, line draws 0 0:
// slots 0, 1 and 4 ride line 0, so three CPUs never finish under the skip.
static void standings_model(void)
{
	static const int skip[3] = {0, 1, 1}, ok[3] = {1, 1, 1};
	int hud, paid, racers;
	race(skip, 2, 6, 0, 0, &hud, &paid, &racers);
	CHECK(racers == 7 && hud == 3 && paid == 3, "model: with line 0 not counting, 3 line-0 CPUs of 7 racers leave the human 4th in the HUD and the payout");
	race(ok, 2, 6, 0, 0, &hud, &paid, &racers);
	CHECK(racers == 7 && hud == 6 && paid == 6, "model: with every line counting the slow human is 7th of 7");
}

static void papu_standings(void)
{
	standings_model();
	struct Lev l;
	if (!load_lev("CTR_LAP_PROGRESS_PAPU_LEV", "Papu's Pyramid", &l))
		return;
	int cnt = (int)rd32(&l, 0x148);
	uint32_t rp = rd32(&l, 0x14c);

	int mc[3][3];
	for (int m = 0; m < 3; m++)
		for (int lane = 0; lane < 3; lane++)
			mc[m][lane] = lane_counts(&l, rp, cnt, lane, m);
	CHECK(mc[RETAIL][0] == 1 && mc[RETAIL][1] == 1 && mc[RETAIL][2] == 1, "retail: every line counts the finish crossing");
	CHECK(mc[A3_26][0] == 0 && mc[A3_26][1] == 1 && mc[A3_26][2] == 1, "the A3-26 skip: only line 0 never counts it");
	CHECK(mc[RULE][0] == 1 && mc[RULE][1] == 1 && mc[RULE][2] == 1, "with the rule every line counts it");

	int skipPaid4 = 0, skipHud4 = 0, skipBad = 0, configs = 0, seven = 0, fullBad = 0, ruleBad = 0;
	for (int human = 0; human < RACERS; human++)
	{
		for (int missing = -1; missing < RACERS; missing++) // -1: full field
		{
			if (missing == human)
				continue;
			for (int fp = 0; fp < 2; fp++)
			{
				for (int sp = 0; sp < 2; sp++)
				{
					int hud, paid, racers, hudR, paidR, hudV, paidV;
					race(mc[A3_26], human, missing, fp, sp, &hud, &paid, &racers);
					race(mc[RULE], human, missing, fp, sp, &hudR, &paidR, &racers);
					race(mc[RETAIL], human, missing, fp, sp, &hudV, &paidV, &racers);
					configs++;
					seven += racers == 7;
					skipPaid4 += paid == 3 && racers == 7;
					skipHud4 += hud == 3 && racers == 7;
					skipBad += paid < racers - 1;
					fullBad += racers == 8 && paid < 7;
					ruleBad += hudR != racers - 1 || paidR != racers - 1 || hudV != hudR || paidV != paidR;
				}
			}
		}
	}
	printf("  %d grids (%d with 7 racers): the A3-26 skip paid a place ahead of last in %d (4th with 7 racers: %d paid, %d HUD); rule differs from retail or from last in %d\n",
	       configs, seven, skipBad, skipPaid4, skipHud4, ruleBad);
	CHECK(ruleBad == 0, "with the rule the slow human is last in the HUD and the payout, as retail, for every grid and path draw");
	CHECK(skipPaid4 > 0, "the A3-26 skip pays 4th with 7 racers (A4-78b)");
	CHECK(skipHud4 > 0, "the A3-26 skip shows 4th on lap 3 with 7 racers (A4-78b)");
	CHECK(skipBad > 0, "the A3-26 skip pays a slow human a place ahead of last");
	CHECK(fullBad > 0, "the skip pays a wrong place with a full field of 8 too: the empty slot is not the cause");
	free(l.d);
}

int main(void)
{
	synthetic();
	retail_ngin();
	retail_papu();
	papu_standings();
	printf("%d of %d checks passed\n", checks - fails, checks);
	return fails ? 1 : 0;
}
