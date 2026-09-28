// Host assertions for the title screen ring recolour (ap/ap_title_ring_logic.h):
// which sixth of the ring a vertex falls in, and how an original blue vertex
// colour becomes the Archipelago colour of that sixth with its shading kept.
// Synthetic data only: the only retail-derived numbers are the ring's centre
// and scale the logic header documents.
//
//   cc -Wall -Wextra -I ap -o /tmp/test-title-ring tools/test-title-ring.c -lm && /tmp/test-title-ring
//
// Exit 0 = every assertion held; the failing case is printed otherwise.

#include <stdio.h>

#include "ap_title_ring_logic.h"

static int g_fail;

static void expect(long got, long want, const char *what)
{
	if (got != want)
	{
		printf("FAIL %s: got %ld (0x%lx), want %ld (0x%lx)\n", what, got, (unsigned long)got, want, (unsigned long)want);
		g_fail = 1;
	}
}

static int channel(unsigned int c, int shift)
{
	return (int)((c >> shift) & 0xFF);
}

static void test_segments(void)
{
	// Sixths are centred on the top and count clockwise on screen.
	expect(AP_TitleRing_Segment(0, 1), AP_TITLE_RING_RED, "top -> red");
	expect(AP_TitleRing_Segment(1, 0), AP_TITLE_RING_PINK, "right (90 deg) is the green/pink boundary -> pink");
	expect(AP_TitleRing_Segment(0.866, 0.5), AP_TITLE_RING_GREEN, "60 deg -> green");
	expect(AP_TitleRing_Segment(0.866, -0.5), AP_TITLE_RING_PINK, "120 deg -> pink");
	expect(AP_TitleRing_Segment(0, -1), AP_TITLE_RING_ORANGE, "bottom -> orange");
	expect(AP_TitleRing_Segment(-0.866, -0.5), AP_TITLE_RING_BLUE, "240 deg -> blue");
	expect(AP_TitleRing_Segment(-0.866, 0.5), AP_TITLE_RING_YELLOW, "300 deg -> yellow");
	expect(AP_TitleRing_Segment(-0.2, 1), AP_TITLE_RING_RED, "just left of top -> red");
	expect(AP_TitleRing_Segment(0, 0), AP_TITLE_RING_RED, "centre -> red, no division");

	// Boundaries sit halfway between centres: 29 deg is red, 31 deg green.
	expect(AP_TitleRing_Segment(0.4848, 0.8746), AP_TITLE_RING_RED, "29 deg -> red");
	expect(AP_TitleRing_Segment(0.5150, 0.8572), AP_TITLE_RING_GREEN, "31 deg -> green");
	expect(AP_TitleRing_Segment(-0.4848, 0.8746), AP_TITLE_RING_RED, "-29 deg -> red");
	expect(AP_TitleRing_Segment(-0.5150, 0.8572), AP_TITLE_RING_YELLOW, "-31 deg -> yellow");

	// Every sixth is reached exactly once going round in 10 degree steps
	// starting at 5 degrees (never on a boundary), in clockwise order.
	{
		int deg, last = -1, changes = 0;

		for (deg = 5; deg < 365; deg += 10)
		{
			double rad = deg * 3.14159265358979323846 / 180.0;
			int seg = AP_TitleRing_Segment(sin(rad), cos(rad));

			if (seg < 0 || seg >= AP_TITLE_RING_SEGMENTS)
				expect(seg, 0, "segment in range");
			if (seg != last)
			{
				if (last >= 0)
					expect(seg, (last + 1) % AP_TITLE_RING_SEGMENTS, "clockwise order");
				changes++;
				last = seg;
			}
		}
		// red wraps: 5..25 red, 35..85 green, ... 335..355 red again
		expect(changes, AP_TITLE_RING_SEGMENTS + 1, "six sixths round the ring");
	}
}

static void test_vertex_space(void)
{
	// The ring centre maps to the centre.
	expect(AP_TitleRing_SegmentForVertex(AP_TITLE_RING_CENTRE_X, AP_TITLE_RING_CENTRE_UP, 5814, 7251), AP_TITLE_RING_RED,
	       "vertex at centre");
	// Points on the settled ring (packed model units): top of the ring,
	// right, bottom and left.
	expect(AP_TitleRing_SegmentForVertex(4, 264, 5814, 7251), AP_TITLE_RING_RED, "top of ring");
	expect(AP_TitleRing_SegmentForVertex(180, 170, 5814, 7251), AP_TITLE_RING_GREEN, "upper right");
	expect(AP_TitleRing_SegmentForVertex(168, 64, 5814, 7251), AP_TITLE_RING_PINK, "lower right");
	expect(AP_TitleRing_SegmentForVertex(4, -24, 5814, 7251), AP_TITLE_RING_ORANGE, "bottom of ring");
	expect(AP_TitleRing_SegmentForVertex(-164, 64, 5814, 7251), AP_TITLE_RING_BLUE, "lower left");
	expect(AP_TitleRing_SegmentForVertex(-164, 176, 5814, 7251), AP_TITLE_RING_YELLOW, "upper left");

	// The scale is applied before the angle: a point at 45 degrees in raw
	// units is green with equal scales, and drops into the red sixth (14
	// degrees) once the up axis is stretched four times more than X.
	expect(AP_TitleRing_SegmentForVertex(100, AP_TITLE_RING_CENTRE_UP + 100, 1000, 1000), AP_TITLE_RING_GREEN, "45 deg unscaled");
	expect(AP_TitleRing_SegmentForVertex(100, AP_TITLE_RING_CENTRE_UP + 100, 1000, 4000), AP_TITLE_RING_RED, "up stretched -> red");
}

static void test_recolour(void)
{
	int s;

	for (s = 0; s < AP_TITLE_RING_SEGMENTS; s++)
	{
		unsigned int target = AP_TITLE_RING_COLOURS[s];
		// The fully lit plain step (tint value/6, no whiteness) is the target.
		unsigned int full = AP_TitleRing_Recolour(0x002A2AFFu, s);
		unsigned int mid = AP_TitleRing_Recolour(0x002020C0u, s);
		unsigned int dark = AP_TitleRing_Recolour(0x00101060u, s);
		unsigned int shine = AP_TitleRing_Recolour(0x008080FFu, s);
		int ch;

		for (ch = 0; ch <= 16; ch += 8)
		{
			expect(channel(full, ch), channel(target, ch), "full plain step is the target");
			// darker originals stay darker, and never black
			if (!(channel(dark, ch) < channel(mid, ch) && channel(mid, ch) < channel(full, ch)))
				expect(channel(dark, ch) * 1000 + channel(mid, ch), -1, "dark < mid < full");
			if (channel(dark, ch) < channel(target, ch) / 3)
				expect(channel(dark, ch), channel(target, ch) / 3, "dark step keeps the hue");
			// the highlight moves toward white, not past it
			if (!(channel(shine, ch) >= channel(full, ch)))
				expect(channel(shine, ch), channel(full, ch), "highlight >= full");
		}
		// The highlight keeps the hue: its brightest channel is still the
		// target's brightest.
		{
			int tr = channel(target, 0), tg = channel(target, 8), tb = channel(target, 16);
			int hr = channel(shine, 0), hg = channel(shine, 8), hb = channel(shine, 16);
			int tMax = tr > tg ? (tr > tb ? 0 : 16) : (tg > tb ? 8 : 16);
			int hMax = hr >= hg ? (hr >= hb ? 0 : 16) : (hg >= hb ? 8 : 16);

			expect(hMax, tMax, "highlight keeps the hue");
		}
	}

	// The retail highlight, 0x80 on a 0xFF blue, is about 40 % white: red's
	// target channel 201 goes about 40 % of the way to 255.
	{
		unsigned int shine = AP_TitleRing_Recolour(0x008080FFu, AP_TITLE_RING_RED);
		int r = channel(shine, 0);

		if (r < 201 + 18 || r > 201 + 26)
			expect(r, 223, "highlight about 40 % toward white");
	}

	// The top byte (unused by the table, kept as is) passes through.
	expect((long)(AP_TitleRing_Recolour(0x342020C0u, AP_TITLE_RING_GREEN) >> 24), 0x34, "top byte kept");
	// An out-of-range segment leaves the colour alone.
	expect((long)AP_TitleRing_Recolour(0x00601010u, -1), 0x00601010L, "segment -1 untouched");
	expect((long)AP_TitleRing_Recolour(0x00601010u, AP_TITLE_RING_SEGMENTS), 0x00601010L, "segment 6 untouched");
	// Black stays black.
	expect((long)AP_TitleRing_Recolour(0x00000000u, AP_TITLE_RING_ORANGE), 0L, "black stays black");
	// Deterministic.
	expect((long)AP_TitleRing_Recolour(0x00ff4040u, AP_TITLE_RING_BLUE), (long)AP_TitleRing_Recolour(0x00ff4040u, AP_TITLE_RING_BLUE),
	       "same input, same output");
}

int main(void)
{
	test_segments();
	test_vertex_space();
	test_recolour();
	if (g_fail)
		return 1;
	printf("test-title-ring: all assertions held\n");
	return 0;
}
