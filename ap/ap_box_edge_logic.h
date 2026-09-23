#ifndef AP_BOX_EDGE_LOGIC_H
#define AP_BOX_EDGE_LOGIC_H

// The AP box's wood border, taken from the player's own disc and recoloured.
//
// Freestanding on purpose: no engine types, no allocation, no IO. The AP client
// (ap/ap_box_texture.c) and the editor (editor/editor_apbox.c) both include it,
// and tools/test-box-edge.c pins it on the host with synthetic data.
//
// WHY THE EDGE COMES FROM THE DISC
// --------------------------------
// The retail "?" crate frames its face with a 16x16 wood tile. The AP box uses
// the same frame (ap/ap_box_model_framed_data.h) but must not ship retail
// pixels, so the build carries none: at runtime the client reads the tile out
// of the player's own BIGFILE (the 1p level of track 0, whose `crate_question`
// names the tile's texture page, palette and texels), decodes it here, and
// recolours it to the AP box's pink before it goes into the box atlas. The
// tile is the same on every track, so one read at startup serves every race,
// including relic races whose level files carry no crate at all. When anything
// in that chain fails, the compiled atlas keeps its own 16x16 border tile by
// JurnthReinal (tools/apbox-texture/box_pink_highres_outer.png).

// One 4bpp texture rect in PSX VRAM, as a retail TextureLayout names it.
typedef struct
{
	int pageX, pageY; // texture page origin in VRAM halfword units
	int minU, minV;   // top-left texel inside the page
	int w, h;         // texels
	int clutX, clutY; // palette origin in VRAM halfword units
	int depth;        // tpage bits 7-8: 0 = 4bpp, 1 = 8bpp, 2 = 15bpp
} AP_BoxEdgeRect;

// The rect one layout samples: the bounding box of its four corners. `uv` is
// u0,v0,u1,v1,u2,v2,u3,v3 as stored in the layout.
static int AP_BoxEdge_RectFromLayout(const unsigned char uv[8], unsigned tpage, unsigned clut, AP_BoxEdgeRect *out)
{
	int k, minU = uv[0], maxU = uv[0], minV = uv[1], maxV = uv[1];

	if (out == 0)
		return 0;
	for (k = 1; k < 4; k++)
	{
		int u = uv[k * 2], v = uv[k * 2 + 1];
		if (u < minU) minU = u;
		if (u > maxU) maxU = u;
		if (v < minV) minV = v;
		if (v > maxV) maxV = v;
	}
	out->pageX = (int)(tpage & 0x0F) * 64;
	out->pageY = (int)((tpage >> 4) & 1) * 256;
	out->minU = minU;
	out->minV = minV;
	out->w = maxU - minU + 1;
	out->h = maxV - minV + 1;
	out->clutX = (int)(clut & 0x3F) * 16;
	out->clutY = (int)((clut >> 6) & 0x1FF);
	out->depth = (int)((tpage >> 7) & 3);
	return 1;
}

// The retail crate's wood tile is its only 16x16 rect (the others are the
// 64x64 face and the 32x32 far face), and it is 4bpp like the rest.
static int AP_BoxEdge_IsWoodRect(const AP_BoxEdgeRect *r)
{
	return r != 0 && r->w == 16 && r->h == 16 && r->depth == 0;
}

// Locate VRAM pixel (x, y) inside a VRM file. Format from
// LOAD_VramFileCallback: word 0 == 0x20 means several TIMs packed, then
// repeating { int size; header[0xC]; s16 rect[4]; pixels }, where size is the
// byte step from the header to the next size word. Every step and every rect's
// pixel block is checked against vrmSize.
static const unsigned char *AP_BoxEdge_VramAt(const unsigned char *vrm, int vrmSize, int x, int y, int *strideOut)
{
	int pos = 4;

	if (vrm == 0 || vrmSize < 8 || vrm[0] != 0x20 || vrm[1] != 0 || vrm[2] != 0 || vrm[3] != 0)
		return 0;

	while (pos + 4 + 0x14 <= vrmSize)
	{
		const unsigned char *p = vrm + pos;
		int size = (int)(p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24));
		const unsigned char *rect = p + 4 + 0xC;
		int rx = (short)(rect[0] | (rect[1] << 8));
		int ry = (short)(rect[2] | (rect[3] << 8));
		int rw = (short)(rect[4] | (rect[5] << 8));
		int rh = (short)(rect[6] | (rect[7] << 8));
		int pix = pos + 4 + 0x14;

		if (size <= 0 || size > vrmSize)
			return 0;
		if (rw > 0 && rh > 0 && x >= rx && y >= ry && x < rx + rw && y < ry + rh)
		{
			if (pix + rw * rh * 2 > vrmSize)
				return 0;
			if (strideOut != 0)
				*strideOut = rw;
			return vrm + pix + ((y - ry) * rw + (x - rx)) * 2;
		}
		pos = pos + 4 + size;
	}
	return 0;
}

static unsigned AP_BoxEdge_Half(const unsigned char *p)
{
	return (unsigned)(p[0] | (p[1] << 8));
}

// Decode one 4bpp rect through its palette into RGBA8 at dst (dstStride pixels
// per row). PSX colour 0x0000 is the transparent entry. 0 on any lookup miss,
// in which case dst may be partly written and must be discarded.
static int AP_BoxEdge_Decode4bpp(const unsigned char *vrm, int vrmSize, const AP_BoxEdgeRect *r, unsigned char *dst,
                                 int dstStride)
{
	const unsigned char *pal;
	int yy, xx, stride;

	if (r == 0 || dst == 0 || r->depth != 0 || r->w <= 0 || r->h <= 0)
		return 0;
	// All 16 palette entries, and every row's texel words, must be contiguous
	// in one TIM: the last one looked up on its own must sit exactly where the
	// first one's row says it should.
	pal = AP_BoxEdge_VramAt(vrm, vrmSize, r->clutX, r->clutY, &stride);
	if (pal == 0 || AP_BoxEdge_VramAt(vrm, vrmSize, r->clutX + 15, r->clutY, &stride) != pal + 15 * 2)
		return 0;
	for (yy = 0; yy < r->h; yy++)
	{
		const unsigned char *row;
		const unsigned char *rowEnd;

		row = AP_BoxEdge_VramAt(vrm, vrmSize, r->pageX + (r->minU >> 2), r->pageY + r->minV + yy, &stride);
		rowEnd = AP_BoxEdge_VramAt(vrm, vrmSize, r->pageX + ((r->minU + r->w - 1) >> 2), r->pageY + r->minV + yy,
		                           &stride);
		if (row == 0 || rowEnd != row + (((r->minU + r->w - 1) >> 2) - (r->minU >> 2)) * 2)
			return 0;
		for (xx = 0; xx < r->w; xx++)
		{
			int texel = r->minU + xx;
			unsigned packed = AP_BoxEdge_Half(row + ((texel >> 2) - (r->minU >> 2)) * 2);
			unsigned c = AP_BoxEdge_Half(pal + ((packed >> ((texel & 3) * 4)) & 0xF) * 2);
			unsigned char *o = dst + (yy * dstStride + xx) * 4;

			if (c == 0)
			{
				o[0] = o[1] = o[2] = o[3] = 0;
				continue;
			}
			o[0] = (unsigned char)((c & 0x1F) << 3);
			o[1] = (unsigned char)(((c >> 5) & 0x1F) << 3);
			o[2] = (unsigned char)(((c >> 10) & 0x1F) << 3);
			o[3] = 0xFF;
		}
	}
	return 1;
}

// THE RECOLOUR. Orange wood becomes magenta wood that sits with
// JurnthReinal's face art: every coloured pixel keeps only its brightness, and
// that brightness picks a colour on a four-stop ramp from deep magenta to the
// pink of the face's bevel (217,109,179 and 251,153,220 in the face art),
// kept a step darker than the face so the frame reads as a frame. So the grain,
// the dark joints and the highlights stay where the disc has them, only in
// pink. Near-grey pixels (the nail heads) are left alone so they still read as
// metal. Transparent pixels stay transparent. The ramp spans brightness 56 to
// 160, which covers the retail tile's wood (about 63 to 143) with headroom.
#define AP_BOX_EDGE_LUMA_LO 56
#define AP_BOX_EDGE_LUMA_HI 160

static void AP_BoxEdge_Recolour(unsigned char *rgba, int w, int h, int stride)
{
	// t in 0..256 along the ramp -> colour
	static const int stops[4][4] = {
		{0, 96, 28, 80},      // shadow: deep magenta
		{96, 165, 58, 130},   // dark grain
		{176, 206, 92, 166},  // wood, a step below the face's bevel pink
		{256, 240, 138, 208}, // highlights, just below the face's light pink
	};
	int x, y;

	if (rgba == 0 || w <= 0 || h <= 0 || stride < w)
		return;
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			unsigned char *p = rgba + (y * stride + x) * 4;
			int r = p[0], g = p[1], b = p[2];
			int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
			int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
			int luma, t, s;

			if (p[3] == 0 || mx - mn < 24)
				continue; // transparent, or a grey nail head
			luma = (77 * r + 150 * g + 29 * b) >> 8;
			t = (luma - AP_BOX_EDGE_LUMA_LO) * 256 / (AP_BOX_EDGE_LUMA_HI - AP_BOX_EDGE_LUMA_LO);
			if (t < 0) t = 0;
			if (t > 256) t = 256;
			for (s = 0; s < 2 && t > stops[s + 1][0]; s++)
				;
			{
				int span = stops[s + 1][0] - stops[s][0];
				int f = t - stops[s][0];
				p[0] = (unsigned char)(stops[s][1] + (stops[s + 1][1] - stops[s][1]) * f / span);
				p[1] = (unsigned char)(stops[s][2] + (stops[s + 1][2] - stops[s][2]) * f / span);
				p[2] = (unsigned char)(stops[s][3] + (stops[s + 1][3] - stops[s][3]) * f / span);
			}
		}
	}
}

// Same recolour, onto a ramp derived from one target colour instead of the
// fixed pink: shadow 40%, dark grain 66%, wood 84% of the colour, highlights
// the colour lifted 15% toward white. For trying the Archipelago item
// classification colours (progression AF99EF, useful 6D8BE8, filler 00EEEE,
// trap FA8072, from the Archipelago client's NetUtils.py / data/client.kv).
static inline void AP_BoxEdge_RecolourTo(unsigned char *rgba, int w, int h, int stride, int cr, int cg, int cb)
{
	int c[3] = {cr, cg, cb}, stops[4][3], x, y, k;
	static const int pos[4] = {0, 96, 176, 256};

	if (rgba == 0 || w <= 0 || h <= 0 || stride < w)
		return;
	for (k = 0; k < 3; k++)
	{
		stops[0][k] = c[k] * 40 / 100;
		stops[1][k] = c[k] * 66 / 100;
		stops[2][k] = c[k] * 84 / 100;
		stops[3][k] = c[k] + (255 - c[k]) * 15 / 100;
	}
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			unsigned char *p = rgba + (y * stride + x) * 4;
			int r = p[0], g = p[1], b = p[2];
			int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
			int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
			int t, s, span, f;

			if (p[3] == 0 || mx - mn < 24)
				continue;
			t = (((77 * r + 150 * g + 29 * b) >> 8) - AP_BOX_EDGE_LUMA_LO) * 256 /
			    (AP_BOX_EDGE_LUMA_HI - AP_BOX_EDGE_LUMA_LO);
			if (t < 0) t = 0;
			if (t > 256) t = 256;
			for (s = 0; s < 2 && t > pos[s + 1]; s++)
				;
			span = pos[s + 1] - pos[s];
			f = t - pos[s];
			for (k = 0; k < 3; k++)
				p[k] = (unsigned char)(stops[s][k] + (stops[s + 1][k] - stops[s][k]) * f / span);
		}
}

// ── the crate face behind the logo ──────────────────────────────────────────
// AP box face variants (AP_BOX_FACE_BASE, chosen at build time):
//   0  JurnthReinal's face as drawn (the framed box without a crate face)
//   1  the retail Wumpa crate face (slats), recoloured, logo on top
//   2  the plain side panel of the Naughty Dog intro crate (planks and a
//      cross plank, no text), recoloured, logo on top
// For 1 and 2 the crate face is read from the player's disc like the wood
// border; if that read fails the face stays JurnthReinal's (variant 0).
#define AP_BOX_FACE_JURNTH 0
#define AP_BOX_FACE_WUMPA  1
#define AP_BOX_FACE_PLAIN  2

// The retail Wumpa crate's face is its only 64x64 4bpp rect.
static int AP_BoxEdge_IsFaceRect(const AP_BoxEdgeRect *r)
{
	return r != 0 && r->w == 64 && r->h == 64 && r->depth == 0;
}

// Grow `acc` to also cover `r` (same page and palette required; 0 if not).
// `first` non-zero starts a new union.
static int AP_BoxEdge_RectUnion(AP_BoxEdgeRect *acc, const AP_BoxEdgeRect *r, int first)
{
	int x0, y0, x1, y1;

	if (acc == 0 || r == 0)
		return 0;
	if (first)
	{
		*acc = *r;
		return 1;
	}
	if (acc->pageX != r->pageX || acc->pageY != r->pageY || acc->clutX != r->clutX || acc->clutY != r->clutY ||
	    acc->depth != r->depth)
		return 0;
	x0 = acc->minU < r->minU ? acc->minU : r->minU;
	y0 = acc->minV < r->minV ? acc->minV : r->minV;
	x1 = acc->minU + acc->w > r->minU + r->w ? acc->minU + acc->w : r->minU + r->w;
	y1 = acc->minV + acc->h > r->minV + r->h ? acc->minV + acc->h : r->minV + r->h;
	acc->minU = x0;
	acc->minV = y0;
	acc->w = x1 - x0;
	acc->h = y1 - y0;
	return 1;
}

// Put the logo on a crate face: where the mask bit is set (the six circles and
// their outline, from JurnthReinal's face) the face keeps its art, elsewhere it
// takes the crate pixel. mask is 64 rows of 8 bytes, bit x&7 of byte x>>3.
static void AP_BoxEdge_ComposeLogo(unsigned char *face, int faceStride, const unsigned char *crate, int crateStride,
                                   const unsigned char *mask)
{
	int x, y;

	if (face == 0 || crate == 0 || mask == 0)
		return;
	for (y = 0; y < 64; y++)
		for (x = 0; x < 64; x++)
			if (!(mask[y * 8 + (x >> 3)] & (1 << (x & 7))))
			{
				const unsigned char *c = crate + (y * crateStride + x) * 4;
				unsigned char *f = face + (y * faceStride + x) * 4;
				f[0] = c[0];
				f[1] = c[1];
				f[2] = c[2];
				f[3] = 0xFF; // the face is opaque; a transparent crate texel shows as black
				if (c[3] == 0)
					f[0] = f[1] = f[2] = 0;
			}
}

#endif // AP_BOX_EDGE_LOGIC_H
