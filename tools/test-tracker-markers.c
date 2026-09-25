// Host checks for where the enlarged Adventure tracker puts its map markers.
//
// AP_TrackerMapPoint is the retail icon anchor, and retail draws every map icon
// from its top-left corner, so markers placed on the raw point sat up and left
// of the pads. AP_TrackerMarkerPoint adds one offset per marker kind:
//   WORLD  (pads, player): per-hub registration of the LEV ground quads onto the
//                          hub map texture
//   GARAGE (hub item):     centre of the retail 11x9 star icon
//   EXIT   (hub item):     retail's -0x200/-0x100 plus the arrow's 6/4 offset
//
// cc -std=c99 -Wall -Wextra -Wno-unused-function -I . tools/test-tracker-markers.c -o /tmp/test-tracker-markers
// /tmp/test-tracker-markers                               synthetic checks only
// /tmp/test-tracker-markers BIGFILE.BIG [out-dir]         also retail checks, and
//                                                         PPM overlays when out-dir is given
#include "ap/ap_tracker_assets.h"
#include <assert.h>
#include <stdio.h>

static int checks, failures;
#define EXPECT(expr, why) do { checks++; if (!(expr)) { printf("FAIL %s\n", why); failures++; } } while (0)

static void synthetic(void)
{
	AP_TrackerHubAsset h; int mode, x, y, x8, y8;
	memset(&h, 0, sizeof h);
	/* 128 world units per map pixel on both axes (the *2 in retail doubles y),
	 * so every point below divides exactly and truncation cannot differ. */
	h.map[0] = 1024; h.map[1] = 1024; h.map[2] = 0; h.map[3] = 0;
	h.map[4] = 8; h.map[5] = 4; h.map[6] = 500; h.map[7] = 195;
	h.width = 40; h.height = 30;
	for (mode = 0; mode < 4; mode++) {
		h.map[8] = (int16_t)mode;
		AP_TrackerMapPoint(&h, 1024, 512, &x, &y);
		AP_TrackerMarkerPoint(&h, -1, AP_TRACKER_MARK_WORLD, 1024, 512, &x8, &y8);
		EXPECT(x8 == x * 8 && y8 == y * 8, "an unknown hub adds no offset and matches the retail anchor");
		AP_TrackerMarkerPoint(&h, 4, AP_TRACKER_MARK_WORLD, 1024, 512, &x8, &y8);
		EXPECT(x8 == x * 8 + AP_TRACKER_MAP_FIT[4][0] && y8 == y * 8 + AP_TRACKER_MAP_FIT[4][1],
			"a world marker takes its hub's map registration");
		AP_TrackerMarkerPoint(&h, 4, AP_TRACKER_MARK_GARAGE, 1024, 512, &x8, &y8);
		EXPECT(x8 == x * 8 + 44 && y8 == y * 8 + 36, "a garage marker sits on the retail star centre");
		AP_TrackerMapPoint(&h, 1024 - 0x200, 512 - 0x100, &x, &y);
		AP_TrackerMarkerPoint(&h, 4, AP_TRACKER_MARK_EXIT, 1024, 512, &x8, &y8);
		EXPECT(x8 == x * 8 + 48 && y8 == y * 8 + 32, "an exit marker repeats retail's arrow placement");
	}
	/* Every registration points down and right, inside a few map pixels. */
	for (mode = 0; mode < AP_TRACKER_HUBS; mode++)
		EXPECT(AP_TRACKER_MAP_FIT[mode][0] > 0 && AP_TRACKER_MAP_FIT[mode][0] < 80 &&
		       AP_TRACKER_MAP_FIT[mode][1] > 0 && AP_TRACKER_MAP_FIT[mode][1] < 80, "registration is small and positive");
}

/* ---- retail checks ------------------------------------------------------ */

#define S 8 /* eighths of a map pixel */
#define MARGIN 16
typedef struct { int w, h; unsigned char *geo; } Raster;

static void tri(Raster *r, const int *xa, const int *ya)
{
	int minx = xa[0], maxx = xa[0], miny = ya[0], maxy = ya[0], i, px, py;
	for (i = 1; i < 3; i++) {
		if (xa[i] < minx) minx = xa[i];
		if (xa[i] > maxx) maxx = xa[i];
		if (ya[i] < miny) miny = ya[i];
		if (ya[i] > maxy) maxy = ya[i];
	}
	for (py = miny; py <= maxy; py++) for (px = minx; px <= maxx; px++) {
		long e0 = (long)(xa[1] - xa[0]) * (py - ya[0]) - (long)(ya[1] - ya[0]) * (px - xa[0]);
		long e1 = (long)(xa[2] - xa[1]) * (py - ya[1]) - (long)(ya[2] - ya[1]) * (px - xa[1]);
		long e2 = (long)(xa[0] - xa[2]) * (py - ya[2]) - (long)(ya[0] - ya[2]) * (px - xa[2]);
		int gx = px + MARGIN * S, gy = py + MARGIN * S;
		if (!((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0))) continue;
		if (gx >= 0 && gy >= 0 && gx < r->w && gy < r->h) r->geo[gy * r->w + gx] = 1;
	}
}

/* Ground quadblocks (QUADBLOCK_FLAG_GROUND, not kill planes) projected with no
 * offset, in eighths of a map pixel. */
static int rasterize(const AP_TrackerHubAsset *h, const unsigned char *file, size_t n, Raster *r)
{
	const unsigned char *p = file + 4; size_t size = n - 4;
	unsigned mesh = AP_TrackerU32(p), nq, qa, va, q, k;
	if (!AP_TrackerSpan(size, mesh, 1, 20)) return 0;
	nq = AP_TrackerU32(p + mesh); qa = AP_TrackerU32(p + mesh + 12); va = AP_TrackerU32(p + mesh + 16);
	if (!AP_TrackerSpan(size, qa, nq, 0x5c)) return 0;
	r->w = (h->width + 2 * MARGIN) * S; r->h = (h->height + 2 * MARGIN) * S;
	r->geo = (unsigned char *)calloc((size_t)r->w * r->h, 1);
	if (!r->geo) return 0;
	for (q = 0; q < nq; q++) {
		const unsigned char *b = p + qa + q * 0x5c; unsigned flags = AP_TrackerU16(b + 0x12);
		int x[4], y[4], t0x[3], t0y[3], t1x[3], t1y[3];
		if (!(flags & 0x1000) || (flags & 0x0200)) continue;
		for (k = 0; k < 4; k++) {
			unsigned vi = AP_TrackerU16(b + k * 2);
			if (!AP_TrackerSpan(size, va + vi * 16, 1, 16)) return 0;
			AP_TrackerMarkerPoint(h, -1, AP_TRACKER_MARK_WORLD, (int16_t)AP_TrackerU16(p + va + vi * 16),
				(int16_t)AP_TrackerU16(p + va + vi * 16 + 4), &x[k], &y[k]);
		}
		t0x[0] = x[0]; t0y[0] = y[0]; t0x[1] = x[1]; t0y[1] = y[1]; t0x[2] = x[2]; t0y[2] = y[2];
		t1x[0] = x[1]; t1y[0] = y[1]; t1x[1] = x[3]; t1y[1] = y[3]; t1x[2] = x[2]; t1y[2] = y[2];
		tri(r, t0x, t0y); tri(r, t1x, t1y);
	}
	return 1;
}

static int lit(const AP_TrackerHubAsset *h, int x, int y)
{
	unsigned char b[4];
	if (x < 0 || y < 0 || x >= h->width || y >= h->height) return 0;
	memcpy(b, &h->pixels[y * h->width + x], 4); return b[0] >= 128;
}

/* Intersection over union of the geometry shifted by (dx, dy) eighths and the
 * lit map pixels. */
static double iou(const AP_TrackerHubAsset *h, const Raster *r, int dx, int dy)
{
	long inter = 0, a = 0, b = 0; int x, y;
	for (y = 0; y < r->h; y++) for (x = 0; x < r->w; x++) {
		int mx = x - MARGIN * S + dx, my = y - MARGIN * S + dy;
		int g = r->geo[y * r->w + x], m = mx >= 0 && my >= 0 ? lit(h, mx / S, my / S) : 0;
		a += g;
		if (g && m) inter++;
	}
	for (y = 0; y < h->height; y++) for (x = 0; x < h->width; x++) b += lit(h, x, y) * S * S;
	return (double)inter / (double)(a + b - inter);
}

static void put_px(unsigned char *img, int w, int hgt, int x, int y, int r, int g, int b)
{
	if (x < 0 || y < 0 || x >= w || y >= hgt) return;
	img[(y * w + x) * 3] = (unsigned char)r; img[(y * w + x) * 3 + 1] = (unsigned char)g; img[(y * w + x) * 3 + 2] = (unsigned char)b;
}
static void ring(unsigned char *img, int w, int hgt, int cx, int cy, int rad, int r, int g, int b)
{
	int x, y;
	for (y = -rad - 2; y <= rad + 2; y++) for (x = -rad - 2; x <= rad + 2; x++) {
		int d = x * x + y * y;
		if (d <= (rad + 1) * (rad + 1) && d >= (rad - 2) * (rad - 2)) put_px(img, w, hgt, cx + x, cy + y, r, g, b);
	}
}

/* Where a playtester marked the Citadel City pads and garage on a 1730x1005
 * screenshot of the tracker (2026-09-25), converted to map eighths through the
 * tracker frame border: N. Gin Labs, Nitro Court, Oxide Station, garage. */
static const int CITADEL_TARGETS[4][2] = {{355, 15}, {503, 47}, {667, 91}, {280, 132}};

static void retail(const char *path, const char *out)
{
	/* D232.hubItems_hub1..5 (game/232/D232.c) as signed x, z, type. */
	static const int16_t items[5][6][3] = {
		{{-18560, 15616, 4}, {-14300, 1134, -2}, {-1200, 17600, -3}, {-17200, 5250, 100}, {-1}},
		{{-16008, -7021, 0}, {-10500, 4243, -1}, {-6000, -8500, -4}, {-13300, -6300, 100}, {-1}},
		{{4249, 14515, 1}, {9161, 8188, -4}, {-8551, 16051, -1}, {3750, 17750, 100}, {-1}},
		{{11000, -4589, 2}, {-10760, -13933, -2}, {5752, -14957, -5}, {12000, 13000, -3}, {4250, -9000, 100}, {-1}},
		{{2552, -21357, 3}, {1500, -11117, -4}, {5500, -21700, 100}, {-1}},
	};
	FILE *f = fopen(path, "rb"); unsigned char header[8192]; int hb;
	uint16_t *vram = (uint16_t *)malloc(AP_TRACKER_VRAM_WORDS * sizeof *vram);
	assert(f && vram); assert(fread(header, 1, sizeof header, f) == sizeof header);
	for (hb = 0; hb < AP_TRACKER_HUBS; hb++) {
		unsigned sizes[2], offs[2]; unsigned char *files[2]; int k, i, dx, dy, bx = 0, by = 0;
		AP_TrackerHubAsset h; Raster r; double at, best = -1; char why[160];
		for (k = 0; k < 2; k++) {
			unsigned entry = 200 + hb * 3 + k;
			offs[k] = AP_TrackerU32(header + 8 + entry * 8); sizes[k] = AP_TrackerU32(header + 12 + entry * 8);
			files[k] = (unsigned char *)malloc(sizes[k]); assert(files[k]);
			assert(!fseek(f, (long)offs[k] * 2048, SEEK_SET)); assert(fread(files[k], 1, sizes[k], f) == sizes[k]);
		}
		assert(AP_TrackerDecodeVram(vram, files[0], sizes[0]));
		assert(AP_TrackerParseHub(&h, files[1], sizes[1], vram));
		assert(rasterize(&h, files[1], sizes[1], &r));
		/* The shipped registration must be the best fit within one map pixel. */
		at = iou(&h, &r, AP_TRACKER_MAP_FIT[hb][0], AP_TRACKER_MAP_FIT[hb][1]);
		for (dy = -S; dy <= S; dy++) for (dx = -S; dx <= S; dx++) {
			double v = iou(&h, &r, AP_TRACKER_MAP_FIT[hb][0] + dx, AP_TRACKER_MAP_FIT[hb][1] + dy);
			if (v > best) { best = v; bx = dx; by = dy; }
		}
		printf("hub %d: fit (%d,%d)/8 IoU %.3f, best nearby %+d,%+d IoU %.3f\n", hb,
			AP_TRACKER_MAP_FIT[hb][0], AP_TRACKER_MAP_FIT[hb][1], at, bx, by, best);
		snprintf(why, sizeof why, "hub %d registration is the local best fit", hb);
		EXPECT(at >= best - 0.002, why);
		snprintf(why, sizeof why, "hub %d ground geometry registers onto its map", hb);
		EXPECT(at >= 0.65, why);
		for (i = 0; i < h.pad_count; i++) {
			int x8, y8;
			AP_TrackerMarkerPoint(&h, hb, AP_TRACKER_MARK_WORLD, h.pads[i].world_x, h.pads[i].world_z, &x8, &y8);
			snprintf(why, sizeof why, "hub %d pad %d marker lands on the lit map", hb, h.pads[i].physical);
			EXPECT(lit(&h, x8 / 8, y8 / 8), why);
		}
		if (hb == 4) {
			/* Citadel City: every orange mark has a new marker within one map
			 * pixel, and the new marker is closer than the old one. */
			for (k = 0; k < 4; k++) {
				int best_new = 1 << 30, best_old = 1 << 30;
				for (i = 0; i < h.pad_count + 1; i++) {
					int x8, y8, ox, oy, d;
					if (i < h.pad_count) {
						AP_TrackerMarkerPoint(&h, hb, AP_TRACKER_MARK_WORLD, h.pads[i].world_x, h.pads[i].world_z, &x8, &y8);
						AP_TrackerMapPoint(&h, h.pads[i].world_x, h.pads[i].world_z, &ox, &oy);
					} else {
						AP_TrackerMarkerPoint(&h, hb, AP_TRACKER_MARK_GARAGE, items[hb][0][0], items[hb][0][1], &x8, &y8);
						AP_TrackerMapPoint(&h, items[hb][0][0], items[hb][0][1], &ox, &oy);
					}
					d = (x8 - CITADEL_TARGETS[k][0]) * (x8 - CITADEL_TARGETS[k][0]) + (y8 - CITADEL_TARGETS[k][1]) * (y8 - CITADEL_TARGETS[k][1]);
					if (d < best_new) { best_new = d; best_old = (ox * 8 - CITADEL_TARGETS[k][0]) * (ox * 8 - CITADEL_TARGETS[k][0]) +
						(oy * 8 - CITADEL_TARGETS[k][1]) * (oy * 8 - CITADEL_TARGETS[k][1]); }
				}
				EXPECT(best_new <= 8 * 8, "a Citadel City orange mark has a marker within one map pixel");
				EXPECT(best_new < best_old, "and the new marker is closer than the old one");
			}
		}
		if (out) {
			/* Overlay at 8x: map, ground outline (blue), old markers (red),
			 * new markers (green), garage (yellow), exits (cyan), and the
			 * playtester's Citadel marks (orange). */
			int W = r.w, H = r.h, x, y; char name[512]; FILE *o;
			unsigned char *img = (unsigned char *)calloc((size_t)W * H * 3, 1); assert(img);
			for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
				int mx = x - MARGIN * S, my = y - MARGIN * S, v = mx >= 0 && my >= 0 ? lit(&h, mx / S, my / S) : 0;
				int gx = x - AP_TRACKER_MAP_FIT[hb][0], gy = y - AP_TRACKER_MAP_FIT[hb][1];
				int g = gx >= 0 && gy >= 0 && gx < W && gy < H && r.geo[gy * W + gx];
				int edge = g && (gx < 1 || gy < 1 || !r.geo[gy * W + gx - 1] || !r.geo[(gy - 1) * W + gx]);
				if (edge) put_px(img, W, H, x, y, 60, 140, 255);
				else put_px(img, W, H, x, y, v ? 235 : 22, v ? 235 : 22, v ? 235 : 32);
			}
			for (i = 0; i < h.pad_count; i++) {
				int x8, y8, ox, oy;
				AP_TrackerMapPoint(&h, h.pads[i].world_x, h.pads[i].world_z, &ox, &oy);
				AP_TrackerMarkerPoint(&h, hb, AP_TRACKER_MARK_WORLD, h.pads[i].world_x, h.pads[i].world_z, &x8, &y8);
				ring(img, W, H, ox * 8 + MARGIN * S, oy * 8 + MARGIN * S, 9, 255, 40, 40);
				ring(img, W, H, x8 + MARGIN * S, y8 + MARGIN * S, 9, 40, 220, 80);
			}
			for (i = 0; items[hb][i][0] != -1; i++) {
				int x8, y8, ox, oy, t = items[hb][i][2];
				if (t >= 0 && t <= 4) {
					AP_TrackerMapPoint(&h, items[hb][i][0], items[hb][i][1], &ox, &oy);
					AP_TrackerMarkerPoint(&h, hb, AP_TRACKER_MARK_GARAGE, items[hb][i][0], items[hb][i][1], &x8, &y8);
					ring(img, W, H, ox * 8 + MARGIN * S, oy * 8 + MARGIN * S, 12, 255, 40, 40);
					ring(img, W, H, x8 + MARGIN * S, y8 + MARGIN * S, 12, 240, 210, 0);
				} else if (t < 0) {
					AP_TrackerMapPoint(&h, items[hb][i][0] - 0x200, items[hb][i][1] - 0x100, &ox, &oy);
					AP_TrackerMarkerPoint(&h, hb, AP_TRACKER_MARK_EXIT, items[hb][i][0], items[hb][i][1], &x8, &y8);
					ring(img, W, H, ox * 8 + MARGIN * S, oy * 8 + MARGIN * S, 6, 255, 40, 40);
					ring(img, W, H, x8 + MARGIN * S, y8 + MARGIN * S, 6, 0, 220, 230);
				}
			}
			if (hb == 4) for (k = 0; k < 4; k++)
				ring(img, W, H, CITADEL_TARGETS[k][0] + MARGIN * S, CITADEL_TARGETS[k][1] + MARGIN * S, 4, 255, 150, 0);
			snprintf(name, sizeof name, "%s/tracker-markers-hub%d.ppm", out, hb);
			o = fopen(name, "wb"); assert(o);
			fprintf(o, "P6\n%d %d\n255\n", W, H); fwrite(img, 1, (size_t)W * H * 3, o); fclose(o); free(img);
		}
		free(r.geo); free(h.pixels); free(files[0]); free(files[1]);
	}
	free(vram); fclose(f);
}

int main(int argc, char **argv)
{
	synthetic();
	if (argc > 1) retail(argv[1], argc > 2 ? argv[2] : NULL);
	printf("tracker markers: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
