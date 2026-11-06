// Host harness for lap progress on quads without a checkpoint (A3-26).
// The rule under test lives in ap/ap_lap_progress_logic.h, which
// game/Vehicle/VehLap.c calls from VehLap_UpdateProgress.
// tools/test-lap-progress-production.py checks that wiring.
//
// Build:
//   gcc -std=c99 -Wall -Wextra -Werror tools/test-lap-progress.c -o /tmp/test-lap-progress -lm
//   /tmp/test-lap-progress
//
// Synthetic cases always run. Retail track data is read only from a file
// named in the environment, never from the repository:
//   CTR_LAP_PROGRESS_NGIN_LEV   N. Gin Labs 1P LEV (retail BIGFILE entry 89)
// With the file, a bot is driven off its nav line down each AI lane of the
// barrel ramp, its checkpoint taken from the quad under it as BOTS.c does.
// The retail measurement counts a lap on lane 0; with the rule no lane does.

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

// VehLap_UpdateProgress, in floating point: progress node's distance plus the
// kart's projection on the progress node -> next node direction (reversed),
// signed remainder by the track length. guarded = the A3-26 rule.
static int measure(const struct Lev *l, uint32_t rp, int cnt, int cp, int x, int y, int z, int guarded, int *out)
{
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
	return 1;
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
static int drive(const struct Lev *l, uint32_t rp, int cnt, int lane, double shift, int guarded, int *lowest)
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
			if (!measure(l, rp, cnt, cp, (int)x, (int)y, (int)z, guarded, &cur))
				continue; // the update is skipped: progress is kept
			if (cur < *lowest)
				*lowest = cur;
			if (have)
			{
				if (prev < 1200 && cur > 32000)
				{
					if (behind)
						behind = 0;
					else
						laps++;
				}
				else if (cur < 1200 && prev > 32000)
				{
					behind = 1;
				}
			}
			prev = cur;
			have = 1;
		}
	}
	return laps;
}

static void retail_ngin(void)
{
	const char *path = getenv("CTR_LAP_PROGRESS_NGIN_LEV");
	if (path == NULL || path[0] == 0)
	{
		printf("  skip retail N. Gin Labs: CTR_LAP_PROGRESS_NGIN_LEV is not set\n");
		return;
	}
	FILE *f = fopen(path, "rb");
	CHECK(f != NULL, "the N. Gin Labs LEV opens");
	if (f == NULL)
		return;
	uint32_t size = 0;
	CHECK(fread(&size, 4, 1, f) == 1, "the LEV has a size");
	struct Lev l;
	l.size = size;
	l.d = (uint8_t *)malloc(size);
	CHECK(l.d != NULL && fread(l.d, 1, size, f) == size, "the LEV data reads");
	fclose(f);
	if (l.d == NULL)
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
		int retailLaps = drive(&l, rp, cnt, cases[i].lane, cases[i].shift, 0, &lowRetail);
		int ruleLaps = drive(&l, rp, cnt, cases[i].lane, cases[i].shift, 1, &lowRule);
		printf("  lane %d shift %+5.0f: retail laps %d (lowest progress %d), with the rule %d (lowest %d)\n", cases[i].lane,
		       cases[i].shift, retailLaps, lowRetail, ruleLaps, lowRule);
		CHECK(retailLaps == cases[i].retailLaps, "retail counts the expected laps on the ramp");
		CHECK(ruleLaps == 0, "with the rule the ramp counts no lap");
		CHECK(lowRule > 32000, "with the rule the ramp keeps mid-track progress");
	}
	free(l.d);
}

int main(void)
{
	synthetic();
	retail_ngin();
	printf("%d of %d checks passed\n", checks - fails, checks);
	return fails ? 1 : 0;
}
