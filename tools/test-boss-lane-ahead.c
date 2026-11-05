// Host harness for the boss lane switch on custom tracks (A2-28).
// The rule under test lives in ap/ap_boss_lane_logic.h, which game/BOTS.c
// calls from the boss lane switch. tools/test-boss-lane-production.py checks
// that wiring.
//
// Build:
//   gcc -std=c99 -Wall -Wextra -Werror tools/test-boss-lane-ahead.c -o /tmp/test-boss-lane-ahead -lm
//   /tmp/test-boss-lane-ahead
//
// Synthetic lanes always run. Real track nav data is read only from files
// named in the environment, never from the repository:
//   CTR_BOSS_LANE_CUSTOM_LEV   a custom track LEV (Out of Time), must improve
//   CTR_BOSS_LANE_RETAIL_LEVS  colon-separated retail boss track LEVs, must not change
// Each part is skipped with a message when its variable is unset.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../ap/ap_boss_lane_logic.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int s_checks, s_fail;
#define CHECK(c)                                                                 \
	do                                                                           \
	{                                                                            \
		s_checks++;                                                              \
		if (!(c))                                                                \
		{                                                                        \
			s_fail++;                                                            \
			printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                   \
		}                                                                        \
	} while (0)

// struct NavFrame layout: pos s16[3] at 0, pathChangeOpcode s16 at 0x10, 0x14 bytes.
#define FRAME_STRIDE 0x14
#define MAX_FRAMES 1024
#define NO_LINK 0x7fff

struct Lane
{
	int numPoints;
	unsigned char bytes[(MAX_FRAMES + 1) * FRAME_STRIDE];
};

static struct Lane s_lanes[3];

static short *frame_pos(struct Lane *l, int i) { return (short *)(l->bytes + i * FRAME_STRIDE); }
static short *frame_op(struct Lane *l, int i) { return (short *)(l->bytes + i * FRAME_STRIDE + 0x10); }

// The four switches the boss asks for (PickupBots.c).
static const int s_pairs[4][2] = {{2, 1}, {1, 0}, {0, 1}, {1, 2}};

// Forward distance of target frame j on lane dst from (bx, bz), along the
// source lane's direction at frame i: the A2-21 measure.
static double forward_along_source(struct Lane *src, int i, struct Lane *dst, int j, double bx, double bz)
{
	short *a = frame_pos(src, i), *b = frame_pos(src, (i + 1) % src->numPoints), *t = frame_pos(dst, j);
	double dx = b[0] - a[0], dz = b[2] - a[2], m = hypot(dx, dz);
	if (m == 0)
		m = 1;
	return ((t[0] - bx) * dx + (t[2] - bz) * dz) / m;
}

struct Tally
{
	int points, behindOld, behindNew, changed, a221Old, a221New;
	int maxStep;
};

// Walk every boss switch link. The boss is placed where it is when the link
// fires: past the previous frame, short of the frame carrying the opcode
// (navFrameNext). where = 0 puts it on the previous frame (botNavFrame), 1 on
// the frame itself. The boss asks on a timer, so any point between happens.
static struct Tally walk(int gated, double where)
{
	struct Tally t;
	memset(&t, 0, sizeof t);
	for (int p = 0; p < 4; p++)
	{
		struct Lane *src = &s_lanes[s_pairs[p][0]], *dst = &s_lanes[s_pairs[p][1]];
		for (int i = 0; i < src->numPoints; i++)
		{
			int op = *frame_op(src, i);
			if (op < 0 || op >= 0xc00 || (op >> 10) != s_pairs[p][1])
				continue;
			int linked = op & 0x3ff;
			if (linked >= dst->numPoints)
				continue;
			short *prev = frame_pos(src, (i + src->numPoints - 1) % src->numPoints), *cur = frame_pos(src, i);
			double bx = prev[0] + (cur[0] - prev[0]) * where, bz = prev[2] + (cur[2] - prev[2]) * where;
			int ibx = (int)lround(bx), ibz = (int)lround(bz);
			int got = gated ? AP_BossLane_AheadIndex(dst->bytes, FRAME_STRIDE, dst->numPoints, linked, ibx, ibz) : linked;
			int step = (got - linked + dst->numPoints) % dst->numPoints;
			t.points++;
			t.behindOld += !AP_BossLane_FrameAhead(dst->bytes, FRAME_STRIDE, dst->numPoints, linked, ibx, ibz);
			t.behindNew += !AP_BossLane_FrameAhead(dst->bytes, FRAME_STRIDE, dst->numPoints, got, ibx, ibz);
			// The A2-21 measure: forward along the boss's lane at the switch frame.
			t.a221Old += forward_along_source(src, i, dst, linked, bx, bz) < 0;
			t.a221New += forward_along_source(src, i, dst, got, bx, bz) < 0;
			t.changed += got != linked;
			if (step > t.maxStep)
				t.maxStep = step;
		}
	}
	return t;
}

// Three concentric circular lanes, radius 2000 + 300 * lane, n frames, the
// boss driving counter-clockwise. Each frame links to the neighbouring lanes
// at frame i + offset.
static void synth(int n, int offset)
{
	memset(s_lanes, 0, sizeof s_lanes);
	for (int l = 0; l < 3; l++)
	{
		s_lanes[l].numPoints = n;
		for (int i = 0; i <= n; i++)
		{
			double a = 2 * M_PI * (i % n) / n, r = 2000 + 300 * l;
			short *pos = frame_pos(&s_lanes[l], i);
			pos[0] = (short)lround(r * cos(a));
			pos[1] = 0;
			pos[2] = (short)lround(r * sin(a));
			*frame_op(&s_lanes[l], i) = NO_LINK;
		}
	}
	// Alternate links so every pair the boss asks for has targets.
	for (int i = 0; i < n; i++)
	{
		int j = ((i + offset) % n + n) % n;
		*frame_op(&s_lanes[0], i) = (short)((1 << 10) | j);
		*frame_op(&s_lanes[2], i) = (short)((1 << 10) | j);
		*frame_op(&s_lanes[1], i) = (short)(((i & 1) ? 2 : 0) << 10 | j);
	}
}

static int load_lev(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *buf = malloc((size_t)size);
	if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size)
	{
		fclose(f);
		free(buf);
		return 0;
	}
	fclose(f);
	const unsigned char *p = buf + 4; // the payload after the size word
	long plen = size - 4;
#define U32(o) ((unsigned)p[o] | (unsigned)p[(o) + 1] << 8 | (unsigned)p[(o) + 2] << 16 | (unsigned)p[(o) + 3] << 24)
	int ok = plen > 0x18c;
	unsigned nav = ok ? U32(0x188) : 0;
	memset(s_lanes, 0, sizeof s_lanes);
	for (int l = 0; ok && l < 3; l++)
	{
		if ((long)nav + 4 * l + 4 > plen)
		{
			ok = 0;
			break;
		}
		unsigned h = U32(nav + 4 * l);
		if ((long)h + 4 > plen)
		{
			ok = 0;
			break;
		}
		int n = p[h + 2] | p[h + 3] << 8;
		if (n < 2 || n > MAX_FRAMES || (long)h + 0x4c + (long)(n + 1) * FRAME_STRIDE > plen)
		{
			ok = 0;
			break;
		}
		s_lanes[l].numPoints = n;
		memcpy(s_lanes[l].bytes, p + h + 0x4c, (size_t)(n + 1) * FRAME_STRIDE);
	}
#undef U32
	free(buf);
	return ok;
}

static const double s_where[] = {0, 0.25, 0.5, 0.75, 1};
#define WHERE_COUNT 5

static void print_tally(const char *name, double where, struct Tally t)
{
	char arm[16];
	snprintf(arm, sizeof arm, "at %.2f", where);
	printf("  %-24s %-9s points %4d  not ahead %3d -> %3d  A2-21 behind %3d -> %3d  changed %3d  max step %d\n", name, arm,
	       t.points, t.behindOld, t.behindNew, t.a221Old, t.a221New, t.changed, t.maxStep);
}

static void test_shape(void)
{
	// The rule never applies to a retail load, and applies to both custom answers.
	CHECK(!AP_BossLane_Applies(0)); // AP_NAVREC_LOAD_RETAIL
	CHECK(AP_BossLane_Applies(1));  // AP_NAVREC_LOAD_CUSTOM
	CHECK(AP_BossLane_Applies(2));  // AP_NAVREC_LOAD_BLOCKED

	synth(300, 0);
	struct Lane *l = &s_lanes[1];
	// Out of shape input keeps the linked frame.
	CHECK(AP_BossLane_AheadIndex(NULL, FRAME_STRIDE, 300, 5, 0, 0) == 5);
	CHECK(AP_BossLane_AheadIndex(l->bytes, 0, 300, 5, 0, 0) == 5);
	CHECK(AP_BossLane_AheadIndex(l->bytes, FRAME_STRIDE, 1, 0, 0, 0) == 0);
	CHECK(AP_BossLane_AheadIndex(l->bytes, FRAME_STRIDE, 300, -1, 0, 0) == -1);
	CHECK(AP_BossLane_AheadIndex(l->bytes, FRAME_STRIDE, 300, 300, 0, 0) == 300);

	// A linked frame already ahead is kept.
	short *f10 = frame_pos(l, 10), *f8 = frame_pos(l, 8);
	CHECK(AP_BossLane_AheadIndex(l->bytes, FRAME_STRIDE, 300, 10, f8[0], f8[2]) == 10);
	// A linked frame behind steps to the first frame ahead.
	CHECK(AP_BossLane_AheadIndex(l->bytes, FRAME_STRIDE, 300, 8, f10[0], f10[2]) == 11);
	// The boss exactly on the linked frame is not ahead of itself.
	CHECK(AP_BossLane_AheadIndex(l->bytes, FRAME_STRIDE, 300, 10, f10[0], f10[2]) == 11);
	// The search wraps across the lap line.
	short *f1 = frame_pos(l, 1);
	CHECK(AP_BossLane_AheadIndex(l->bytes, FRAME_STRIDE, 300, 298, f1[0], f1[2]) == 2);
	// Up to AP_BOSS_LANE_MAX_STEP frames behind the boss: found.
	int nearIndex = 10 + AP_BOSS_LANE_MAX_STEP - 1;
	short *near = frame_pos(l, nearIndex);
	CHECK(AP_BossLane_AheadIndex(l->bytes, FRAME_STRIDE, 300, 10, near[0], near[2]) == nearIndex + 1);
	// Further: left as linked, never carried round a bend.
	short *far = frame_pos(l, 10 + AP_BOSS_LANE_MAX_STEP + 2);
	CHECK(AP_BossLane_AheadIndex(l->bytes, FRAME_STRIDE, 300, 10, far[0], far[2]) == 10);
	// A boss at the centre of a tight bend (circle of radius 600, link to
	// the nearest frame) is not carried a quarter lap.
	{
		static struct Lane bend;
		bend.numPoints = 100;
		for (int i = 0; i <= 100; i++)
		{
			double a = 2 * M_PI * (i % 100) / 100;
			frame_pos(&bend, i)[0] = (short)lround(600 * cos(a));
			frame_pos(&bend, i)[2] = (short)lround(600 * sin(a));
		}
		int got = AP_BossLane_AheadIndex(bend.bytes, FRAME_STRIDE, 100, 0, 15, 0);
		CHECK(got == 0 || got <= AP_BOSS_LANE_MAX_STEP);
	}
	// A sharp corner: the boss drives -x on its old lane, the new lane runs -z
	// beside the corner. Frame 0 of the new lane is still ahead of the boss
	// along the new lane, although it is "behind" along the boss's heading;
	// the rule keeps it (an Out of Time link has this shape).
	{
		static struct Lane corner;
		corner.numPoints = 8;
		for (int i = 0; i <= 8; i++)
		{
			frame_pos(&corner, i)[0] = 200;
			frame_pos(&corner, i)[2] = (short)(-300 * i);
		}
		CHECK(AP_BossLane_FrameAhead(corner.bytes, FRAME_STRIDE, 8, 0, 0, 100));
		CHECK(AP_BossLane_AheadIndex(corner.bytes, FRAME_STRIDE, 8, 0, 0, 100) == 0);
		// Past frame 0 along the new lane: step to frame 1.
		CHECK(AP_BossLane_AheadIndex(corner.bytes, FRAME_STRIDE, 8, 0, 0, -100) == 1);
	}
	// No frame ahead at all (every frame on one point): linked.
	{
		static struct Lane flat;
		flat.numPoints = 8;
		CHECK(AP_BossLane_AheadIndex(flat.bytes, FRAME_STRIDE, 8, 3, 0, 0) == 3);
	}
}

static void test_synthetic(void)
{
	// Out of Time shape: links at the same index or one or two behind.
	for (int offset = -2; offset <= 0; offset++)
	{
		synth(303, offset);
		for (int w = 0; w < WHERE_COUNT; w++)
		{
			struct Tally old = walk(0, s_where[w]), now = walk(1, s_where[w]);
			char name[48];
			snprintf(name, sizeof name, "synthetic offset %d", offset);
			print_tally(name, s_where[w], now);
			CHECK(old.points > 0);
			if (offset < 0 || s_where[w] == 1)
				CHECK(old.behindOld > 0); // fails on today's code: the boss lands beside or behind itself
			CHECK(now.behindNew == 0);
			CHECK(now.maxStep <= 3);
		}
	}
	// Retail shape: links two to five frames ahead. The search keeps every one.
	for (int offset = 2; offset <= 5; offset++)
	{
		synth(444, offset);
		for (int w = 0; w < WHERE_COUNT; w++)
		{
			struct Tally now = walk(1, s_where[w]);
			CHECK(now.points > 0);
			CHECK(now.changed == 0);
			CHECK(now.behindOld == 0);
		}
	}
}

static void test_custom_file(void)
{
	const char *path = getenv("CTR_BOSS_LANE_CUSTOM_LEV");
	if (!path || !*path)
	{
		printf("  skip custom track nav data: CTR_BOSS_LANE_CUSTOM_LEV is not set\n");
		return;
	}
	CHECK(load_lev(path));
	printf("  custom lanes %d/%d/%d frames\n", s_lanes[0].numPoints, s_lanes[1].numPoints, s_lanes[2].numPoints);
	for (int w = 0; w < WHERE_COUNT; w++)
	{
		struct Tally old = walk(0, s_where[w]), now = walk(1, s_where[w]);
		print_tally("custom, today", s_where[w], old);
		print_tally("custom, fixed", s_where[w], now);
		CHECK(old.points > 0);
		if (s_where[w] == 1)
			CHECK(old.behindOld > 0); // the data defect A2-21 measured
		CHECK(now.behindNew == 0);
		CHECK(now.maxStep <= 3);
		if (s_where[w] == 1)
		{
			CHECK(old.a221Old > 400); // A2-21: 441 of 907
			CHECK(now.a221New == 0);
		}
	}
}

static void test_retail_files(void)
{
	const char *list = getenv("CTR_BOSS_LANE_RETAIL_LEVS");
	if (!list || !*list)
	{
		printf("  skip retail boss track nav data: CTR_BOSS_LANE_RETAIL_LEVS is not set\n");
		return;
	}
	char *copy = malloc(strlen(list) + 1);
	strcpy(copy, list);
	int tracks = 0;
	for (char *path = strtok(copy, ":"); path; path = strtok(NULL, ":"))
	{
		CHECK(load_lev(path));
		const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
		for (int w = 0; w < WHERE_COUNT; w++)
		{
			// What the engine does on a retail load: AP_BossLane_Applies is
			// false, so the linked frame is taken as the track gives it.
			struct Tally engine = walk(AP_BossLane_Applies(0), s_where[w]);
			CHECK(engine.points > 0);
			CHECK(engine.changed == 0);
			// For the record: what the search alone would do on these links.
			struct Tally search = walk(1, s_where[w]);
			print_tally(name, s_where[w], search);
		}
		tracks++;
	}
	free(copy);
	printf("  retail boss tracks checked: %d\n", tracks);
}

int main(void)
{
	test_shape();
	test_synthetic();
	test_custom_file();
	test_retail_files();
	printf("%d checks, %d failed\n", s_checks, s_fail);
	return s_fail ? 1 : 0;
}
