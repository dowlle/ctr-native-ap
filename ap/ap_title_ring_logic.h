#ifndef AP_TITLE_RING_LOGIC_H
#define AP_TITLE_RING_LOGIC_H

// The title screen ring in the six Archipelago colours.
//
// Freestanding on purpose: no engine types, no allocation, no IO. The renderer
// (game/RenderBucket/RenderBucket_QueueExecute.c) calls it for the two ring
// models only, and tools/test-title-ring.c pins it on the host.
//
// WHAT IT DOES
// ------------
// The blue ring behind Crash and the CTR logo is two retail models,
// STATIC_RINGTOP (0x68) and STATIC_RINGBOTTOM (0x69), in the main menu level.
// Each is one flat triangle strip whose vertices carry blue vertex colours
// from a five-entry colour table: a dark-to-light ramp that gives the ring its
// lit, bevelled look. The build ships no art: the client recolours the
// player's own ring as it is drawn.
//
// Each vertex takes the Archipelago colour of the sixth of the ring it sits
// in, clockwise from the top: red, green, pink, orange, blue, yellow, the six
// circles of the Archipelago logo. The red sixth is centred on the top of the
// ring. The brightness of the original blue (its brightest channel) scales
// the new colour (see AP_TitleRing_Recolour), and the whiteness of the original (its dimmest channel,
// above the ramp's normal tint) blends it toward white, so the shading and the
// highlight stripe of the retail ring survive the recolour.
//
// The colour is per vertex and the GPU shades each triangle smoothly between
// its three vertex colours, so the change from one colour to the next is a
// soft blend across the one strip quad that straddles a boundary (about 10
// degrees of the ring) rather than a hard edge.
//
// MODEL SPACE
// -----------
// Positions are the renderer's packed model-space vertex (VXY: X in the low
// half, the model's up axis in the high half, both signed, frame origin
// included). The ring lies in that plane, centred at AP_TITLE_RING_CENTRE_*,
// measured from the settled title frame of both models (the two arcs span
// X -180..184 and up -24..264 together). The model is scaled non-uniformly
// (header scale X 5814, up 7251), so the angle is taken after scaling, which
// makes the six sixths equal on screen. During the fly-in the ring is
// vertex-animated; its centre stays within a few units of this point, so the
// colours hold still while it grows.

#include <math.h>

#define AP_TITLE_RING_CENTRE_X  0
#define AP_TITLE_RING_CENTRE_UP 120
#define AP_TITLE_RING_LIGHT_GAMMA 0.6

enum
{
	AP_TITLE_RING_RED = 0,
	AP_TITLE_RING_GREEN,
	AP_TITLE_RING_PINK,
	AP_TITLE_RING_ORANGE,
	AP_TITLE_RING_BLUE,
	AP_TITLE_RING_YELLOW,
	AP_TITLE_RING_SEGMENTS
};

// 0x00BBGGRR, the model colour table's own layout.
static const unsigned int AP_TITLE_RING_COLOURS[AP_TITLE_RING_SEGMENTS] = {
	0x008276C9u, // red    201,118,130
	0x0075C275u, // green  117,194,117
	0x00C294CAu, // pink   202,148,194
	0x007DA0D9u, // orange 217,160,125
	0x00BD7C77u, // blue   119,124,189
	0x0091E3EEu, // yellow 238,227,145
};

// Which sixth a point lies in, from the ring centre: right and up are already
// scaled to screen proportions. 0 is the top sixth (red), counting clockwise
// as seen on screen. The centre itself counts as the top.
static inline int AP_TitleRing_Segment(double right, double up)
{
	double deg;
	int seg;

	if (right == 0.0 && up == 0.0)
		return AP_TITLE_RING_RED;

	// 0 at the top, growing clockwise (toward +right).
	deg = atan2(right, up) * (180.0 / 3.14159265358979323846);
	// Sixths are centred on 0, 60, ...: shift by half a sixth.
	deg += 30.0;
	while (deg < 0.0)
		deg += 360.0;
	while (deg >= 360.0)
		deg -= 360.0;
	seg = (int)(deg / 60.0);
	return (seg >= AP_TITLE_RING_SEGMENTS) ? AP_TITLE_RING_SEGMENTS - 1 : seg;
}

// Sixth for a packed model-space vertex (see MODEL SPACE), with the model
// header's X and up scale.
static inline int AP_TitleRing_SegmentForVertex(int x, int up, int scaleX, int scaleUp)
{
	double right = (double)(x - AP_TITLE_RING_CENTRE_X) * (double)scaleX;
	double upS = (double)(up - AP_TITLE_RING_CENTRE_UP) * (double)scaleUp;

	return AP_TitleRing_Segment(right, upS);
}

static inline unsigned int AP_TitleRing_Clamp8(int v)
{
	return (unsigned int)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// Recolour one original vertex colour (0xXXBBGGRR, top byte kept) to the
// Archipelago colour of `segment`, keeping its shading.
//
// The retail ring colours are one blue at five brightnesses, some mixed with
// white: (dim, dim, value) with dim = value / 6 for the plain steps
// (0x10/0x60 .. 0x20/0xC0) and more for the two highlight steps (0x40/0xFF and
// 0x80/0xFF). So each original splits into
//   value     brightest channel / 255: how lit the vertex is, and
//   whiteness how far dim sits from value / 6 toward value, 0 for the plain
//             steps, about 0.1 and 0.4 for the highlights,
// and the recolour is light * lerp(target, white, whiteness): the same light
// and the same highlight stripe, in the new colour. light is value raised to
// AP_TITLE_RING_LIGHT_GAMMA: the retail ramp's darkest step (0x60, 38 %) still
// reads as blue because the blue is saturated, but the paler Archipelago
// colours at 38 % turn to mud (orange reads as brown), so the dark end is
// lifted (0x60 -> 56 %, 0xC0 -> 84 %, 0xFF unchanged) while the order of the
// steps, and so the 3D look, stays.
static inline unsigned int AP_TitleRing_Recolour(unsigned int original, int segment)
{
	int r = (int)(original & 0xFF);
	int g = (int)((original >> 8) & 0xFF);
	int b = (int)((original >> 16) & 0xFF);
	int value = r > g ? (r > b ? r : b) : (g > b ? g : b);
	int dim = r < g ? (r < b ? r : b) : (g < b ? g : b);
	int tint = value / 6;
	int wNum = 0, wDen = 1; // whiteness as a fraction
	unsigned int target;
	unsigned int out = original & 0xFF000000u;
	int shift;
	double light;

	if (segment < 0 || segment >= AP_TITLE_RING_SEGMENTS)
		return original;
	if (dim > tint && value > tint)
	{
		wNum = dim - tint;
		wDen = value - tint;
	}
	target = AP_TITLE_RING_COLOURS[segment];
	light = pow((double)value / 255.0, AP_TITLE_RING_LIGHT_GAMMA);

	for (shift = 0; shift <= 16; shift += 8)
	{
		int t = (int)((target >> shift) & 0xFF);
		int lit = t + ((255 - t) * wNum) / wDen;

		out |= AP_TitleRing_Clamp8((int)(lit * light + 0.5)) << shift;
	}
	return out;
}

#endif
