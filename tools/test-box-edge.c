// Host assertions for the AP box's wood border (ap/ap_box_edge_logic.h): the
// VRM lookup, the 4bpp decode and the pink recolour that turn the retail
// crate's wood tile, read from the player's own disc at runtime, into the
// border of the AP box. Synthetic data only: no retail pixel is in this file,
// and none is needed to pin the logic.
//
//   cc -Wall -Wextra -I ap -o /tmp/test-box-edge tools/test-box-edge.c && /tmp/test-box-edge
//
// Exit 0 = every assertion held; the failing case is printed otherwise.

#include <stdio.h>
#include <string.h>

#include "ap_box_edge_logic.h"

static int g_fail;

static void expect(int got, int want, const char *what)
{
	if (got != want)
	{
		printf("FAIL %s: got %d, want %d\n", what, got, want);
		g_fail = 1;
	}
}

static void put16(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char)(v & 0xFF);
	p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, unsigned v)
{
	put16(p, v & 0xFFFF);
	put16(p + 2, v >> 16);
}

// Append one TIM to a packed VRM: { size; header[0xC]; rect; pixels }.
static int add_tim(unsigned char *vrm, int pos, int x, int y, int w, int h, const unsigned short *pix)
{
	int i, bytes = w * h * 2;

	put32(vrm + pos, (unsigned)(0x14 + bytes));
	memset(vrm + pos + 4, 0, 0xC);
	put16(vrm + pos + 4 + 0xC, (unsigned)x);
	put16(vrm + pos + 4 + 0xE, (unsigned)y);
	put16(vrm + pos + 4 + 0x10, (unsigned)w);
	put16(vrm + pos + 4 + 0x12, (unsigned)h);
	for (i = 0; i < w * h; i++)
		put16(vrm + pos + 4 + 0x14 + i * 2, pix[i]);
	return pos + 4 + 0x14 + bytes;
}

// A palette of 16 orange-ish BGR555 colours, entry 0 transparent, entry 15 grey.
static unsigned short pal_colour(int i)
{
	int r, g, b;

	if (i == 0)
		return 0;
	if (i == 15)
		return (unsigned short)(12 | (12 << 5) | (12 << 10)); // grey nail
	r = 10 + i;           // 11..24
	g = 3 + i / 2;        // 3..10
	b = i / 7;            // 0..2
	return (unsigned short)(r | (g << 5) | (b << 10));
}

static unsigned char s_vrm[4096];

static int build_vrm(int palW)
{
	unsigned short pal[16], tex[4 * 16];
	int i, x, y, pos;

	for (i = 0; i < 16; i++)
		pal[i] = pal_colour(i);
	// texel index at (x, y) is (x + y) & 15, four per halfword, low nibble first
	for (y = 0; y < 16; y++)
		for (x = 0; x < 4; x++)
		{
			unsigned w = 0;
			int k;
			for (k = 0; k < 4; k++)
				w |= (unsigned)(((x * 4 + k + y) & 15) << (k * 4));
			tex[y * 4 + x] = (unsigned short)w;
		}
	memset(s_vrm, 0, sizeof s_vrm);
	put32(s_vrm, 0x20);
	pos = add_tim(s_vrm, 4, 320, 480, palW, 1, pal);   // clut 0x3F6A-ish: (320, 480)
	pos = add_tim(s_vrm, pos, 640 + 56, 16, 4, 16, tex); // page 10, texels 224..239, rows 16..31
	put32(s_vrm + pos, 0);
	return pos + 4;
}

static void test_rect(void)
{
	// retail-shaped wood layout: page 10 (x 640), 4bpp, texels 224..239 x 16..31
	unsigned char wood[8] = {224, 16, 224, 31, 239, 16, 239, 16};
	unsigned char face[8] = {128, 0, 128, 63, 191, 0, 191, 0};
	unsigned clut = (320 / 16) | (480 << 6);
	AP_BoxEdgeRect r;

	expect(AP_BoxEdge_RectFromLayout(wood, 0x000A, clut, &r), 1, "rect from layout");
	expect(r.pageX, 640, "pageX");
	expect(r.pageY, 0, "pageY");
	expect(r.minU, 224, "minU");
	expect(r.minV, 16, "minV");
	expect(r.w, 16, "w");
	expect(r.h, 16, "h");
	expect(r.clutX, 320, "clutX");
	expect(r.clutY, 480, "clutY");
	expect(AP_BoxEdge_IsWoodRect(&r), 1, "16x16 4bpp is the wood");

	AP_BoxEdge_RectFromLayout(face, 0x000A, clut, &r);
	expect(AP_BoxEdge_IsWoodRect(&r), 0, "64x64 face is not the wood");

	AP_BoxEdge_RectFromLayout(wood, 0x000A | (1 << 7), clut, &r);
	expect(AP_BoxEdge_IsWoodRect(&r), 0, "8bpp 16x16 is not the wood");

	AP_BoxEdge_RectFromLayout(wood, 0x001A, clut, &r);
	expect(r.pageY, 256, "tpage bit 4 selects the lower page row");
}

static AP_BoxEdgeRect wood_rect(void)
{
	unsigned char wood[8] = {224, 16, 224, 31, 239, 16, 239, 16};
	AP_BoxEdgeRect r;

	AP_BoxEdge_RectFromLayout(wood, 0x000A, (320 / 16) | (480 << 6), &r);
	return r;
}

static void test_decode(void)
{
	AP_BoxEdgeRect r = wood_rect();
	unsigned char out[16 * 16 * 4];
	int size = build_vrm(16), x, y, bad = 0;

	expect(AP_BoxEdge_Decode4bpp(s_vrm, size, &r, out, 16), 1, "decode ok");
	for (y = 0; y < 16; y++)
		for (x = 0; x < 16; x++)
		{
			unsigned c = pal_colour((x + y) & 15);
			const unsigned char *p = out + (y * 16 + x) * 4;
			if (c == 0)
				bad += p[3] != 0;
			else
				bad += p[0] != ((c & 31) << 3) || p[1] != (((c >> 5) & 31) << 3) || p[2] != (((c >> 10) & 31) << 3) ||
				       p[3] != 255;
		}
	expect(bad, 0, "decoded texels match palette");

	expect(AP_BoxEdge_Decode4bpp(s_vrm, size - 8, &r, out, 16), 0, "truncated pixel block fails");
	s_vrm[0] = 0x21;
	expect(AP_BoxEdge_Decode4bpp(s_vrm, size, &r, out, 16), 0, "wrong VRM marker fails");
	s_vrm[0] = 0x20;
	r.minU = 228; // row runs past the 4-halfword TIM
	expect(AP_BoxEdge_Decode4bpp(s_vrm, size, &r, out, 16), 0, "row past its TIM fails");
	r = wood_rect();
	size = build_vrm(8); // palette TIM holds only 8 entries
	expect(AP_BoxEdge_Decode4bpp(s_vrm, size, &r, out, 16), 0, "short palette fails");
	r.minV = 40; // rows not present at all
	size = build_vrm(16);
	expect(AP_BoxEdge_Decode4bpp(s_vrm, size, &r, out, 16), 0, "missing rows fail");
	put32(s_vrm + 4, 0x7FFFFFFF); // corrupt step size
	r = wood_rect();
	expect(AP_BoxEdge_Decode4bpp(s_vrm, size, &r, out, 16), 0, "corrupt TIM size fails");
}

static int luma(const unsigned char *p)
{
	return (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8;
}

static void test_recolour(void)
{
	unsigned char px[8][4] = {
		{60, 20, 0, 255},    // darkest wood
		{132, 41, 0, 255},
		{189, 66, 0, 255},
		{222, 90, 0, 255},
		{247, 115, 16, 255}, // lightest wood
		{255, 170, 60, 255}, // beyond the ramp
		{90, 90, 90, 255},   // grey nail: untouched
		{200, 80, 0, 0},     // transparent: untouched
	};
	unsigned char in[8][4];
	int i, pinkOk = 1, mono = 1;

	memcpy(in, px, sizeof px);
	AP_BoxEdge_Recolour(&px[0][0], 8, 1, 8);

	for (i = 0; i < 6; i++)
	{
		// pink/magenta: red and blue both clearly above green, red on top
		if (!(px[i][0] > px[i][1] + 40 && px[i][2] > px[i][1] + 40 && px[i][0] >= px[i][2]))
			pinkOk = 0;
		if (i > 0 && luma(px[i]) < luma(px[i - 1]))
			mono = 0;
		expect(px[i][3], 255, "alpha kept");
	}
	expect(pinkOk, 1, "wood becomes pink");
	expect(mono, 1, "brightness order kept");
	expect(memcmp(px[6], in[6], 4), 0, "grey nail untouched");
	expect(memcmp(px[7], in[7], 4), 0, "transparent pixel untouched");
	expect(px[5][0] == 240 && px[5][1] == 138 && px[5][2] == 208, 1, "above the ramp clamps to the top stop");
	expect(px[0][0] == 96 && px[0][1] == 28 && px[0][2] == 80, 1, "below the ramp clamps to the bottom stop");
	// stride is honoured: a 1x1 recolour of an 8-wide row touches one pixel
	memcpy(px, in, sizeof px);
	AP_BoxEdge_Recolour(&px[0][0], 1, 1, 8);
	expect(memcmp(px[1], in[1], 4), 0, "only w pixels per row");
}

static void test_compose(void)
{
	static unsigned char face[64 * 64 * 4], crate[64 * 64 * 4], mask[64 * 8];
	AP_BoxEdgeRect a;

	memset(face, 0x11, sizeof face);
	memset(crate, 0x22, sizeof crate);
	crate[(5 * 64 + 6) * 4 + 3] = 0; // one transparent crate texel
	memset(mask, 0, sizeof mask);
	mask[3 * 8 + 1] = 1 << 2; // logo pixel (x 10, y 3)
	AP_BoxEdge_ComposeLogo(face, 64, crate, 64, mask);
	expect(face[(3 * 64 + 10) * 4], 0x11, "logo pixel keeps the face art");
	expect(face[(3 * 64 + 11) * 4], 0x22, "background takes the crate");
	expect(face[(3 * 64 + 11) * 4 + 3], 0xFF, "composed face is opaque");
	expect(face[(5 * 64 + 6) * 4], 0, "transparent crate texel shows black");
	expect(face[(5 * 64 + 6) * 4 + 3], 0xFF, "and stays opaque");

	{
		unsigned char f64[8] = {192, 0, 192, 63, 255, 0, 255, 0};
		AP_BoxEdge_RectFromLayout(f64, 0x0069, 16171, &a);
		expect(AP_BoxEdge_IsFaceRect(&a), 1, "64x64 4bpp is a crate face");
		expect(AP_BoxEdge_IsWoodRect(&a), 0, "and not the wood");
	}
}

int main(void)
{
	test_rect();
	test_decode();
	test_recolour();
	test_compose();
	if (g_fail)
		return 1;
	printf("test-box-edge: all assertions held\n");
	return 0;
}
