#ifndef AP_TRACKER_ASSETS_H
#define AP_TRACKER_ASSETS_H

/* Read-only views of retail hub assets. Never patch LEV pointers or upload
 * another hub's VRAM into the running game. All offsets are bounds checked. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#define AP_TRACKER_HUBS 5
#define AP_TRACKER_PADS 8
#define AP_TRACKER_VRAM_WORDS (1024 * 512)

typedef struct {
	int physical, world_x, world_z;
} AP_TrackerAssetPad;
typedef struct {
	int16_t map[9];
	unsigned char icons[2][12];
	int width, height, pad_count;
	AP_TrackerAssetPad pads[AP_TRACKER_PADS];
	uint32_t *pixels;
} AP_TrackerHubAsset;

static uint16_t AP_TrackerU16(const unsigned char *p)
{ return (uint16_t)(p[0] | (unsigned)p[1] << 8); }
static uint32_t AP_TrackerU32(const unsigned char *p)
{ return (uint32_t)AP_TrackerU16(p) | (uint32_t)AP_TrackerU16(p + 2) << 16; }
static int AP_TrackerSpan(size_t size, size_t offset, size_t count, size_t stride)
{ return offset <= size && stride && count <= (size - offset) / stride; }

/* RGBA bytes on either host endian. PSX zero is transparent, 0x8000 black
 * is opaque. The texture page and CLUT follow the actual GPU addressing. */
static uint32_t AP_TrackerRGBA(unsigned r, unsigned g, unsigned b, unsigned a)
{
	unsigned char bytes[4]; uint32_t result;
	bytes[0] = (unsigned char)r; bytes[1] = (unsigned char)g;
	bytes[2] = (unsigned char)b; bytes[3] = (unsigned char)a;
	memcpy(&result, bytes, 4); return result;
}
static uint32_t AP_TrackerTexel(const uint16_t *vram, const unsigned char *t, int x, int y)
{
	unsigned page = AP_TrackerU16(t + 6), clut = AP_TrackerU16(t + 2);
	unsigned depth = (page >> 7) & 3, u = (t[0] + x) & 255, v = (t[1] + y) & 255;
	unsigned px = (page & 15) * 64, py = ((page >> 4) & 1) * 256 + v;
	unsigned value, index;
	if (depth > 2) return 0;
	px += depth == 0 ? u / 4 : depth == 1 ? u / 2 : u;
	value = vram[py * 1024 + (px & 1023)];
	if (depth < 2) {
		index = depth == 0 ? (value >> ((u & 3) * 4)) & 15 : (value >> ((u & 1) * 8)) & 255;
		value = vram[((clut >> 6) & 511) * 1024 + (((clut & 63) * 16 + index) & 1023)];
	}
	if (!value) return 0;
	return AP_TrackerRGBA((value & 31) * 255 / 31, ((value >> 5) & 31) * 255 / 31,
	                      ((value >> 10) & 31) * 255 / 31, 255);
}

static int AP_TrackerVramBlock(uint16_t *dst, const unsigned char *p, size_t n)
{
	unsigned x, y, w, h, i, j;
	if (n < 20) return 0;
	x = AP_TrackerU16(p + 12); y = AP_TrackerU16(p + 14);
	w = AP_TrackerU16(p + 16); h = AP_TrackerU16(p + 18);
	if (!w || !h || x + w > 1024 || y + h > 512 || !AP_TrackerSpan(n, 20, w * h, 2)) return 0;
	for (j = 0; j < h; j++) for (i = 0; i < w; i++)
		dst[(y + j) * 1024 + x + i] = AP_TrackerU16(p + 20 + (j * w + i) * 2);
	return 1;
}
static int AP_TrackerDecodeVram(uint16_t *dst, const unsigned char *p, size_t n)
{
	size_t pos; unsigned size; int count = 0;
	if (n < 4) return 0;
	memset(dst, 0, AP_TRACKER_VRAM_WORDS * sizeof *dst);
	if (AP_TrackerU32(p) != 0x20) return AP_TrackerVramBlock(dst, p, n);
	pos = 4;
	while (AP_TrackerSpan(n, pos, 1, 4)) {
		size = AP_TrackerU32(p + pos); pos += 4;
		if (!size) return count > 0;
		if (!AP_TrackerSpan(n, pos, size, 1) || !AP_TrackerVramBlock(dst, p + pos, size)) return 0;
		pos += size; count++;
	}
	return 0;
}

static int AP_TrackerParseHub(AP_TrackerHubAsset *hub, const unsigned char *file, size_t n,
	                          const uint16_t *vram)
{
	const unsigned char *p; size_t size; unsigned lookup, icons, count, spawn, map, inst, i;
	int found = 0, top_h, bottom_h, x, y;
	AP_TrackerHubAsset result;
	memset(&result, 0, sizeof result);
	if (n < 4 + 0x138) return 0;
	/* Retail hub LEVs carry a negative sentinel and a separate PTR file;
	 * some extracted formats append an inline pointer map at a positive offset. */
	p = file + 4; size = AP_TrackerU32(file) & 0x80000000u ? n - 4 : AP_TrackerU32(file);
	if (size > n - 4) return 0;
	if (size < 0x138) return 0;
	lookup = AP_TrackerU32(p + 0x3c);
	if (!AP_TrackerSpan(size, lookup, 1, 16)) return 0;
	count = AP_TrackerU32(p + lookup); icons = AP_TrackerU32(p + lookup + 4);
	if (!AP_TrackerSpan(size, icons, count, 32)) return 0;
	for (i = 0; i < count; i++) {
		const unsigned char *icon = p + icons + i * 32; unsigned id = AP_TrackerU32(icon + 16);
		if (id == 3 || id == 4) { memcpy(result.icons[id - 3], icon + 20, 12); found |= 1 << (id - 3); }
	}
	if (found != 3) return 0;
	spawn = AP_TrackerU32(p + 0x134);
	if (!AP_TrackerSpan(size, spawn, 1, 8) || !AP_TrackerU32(p + spawn)) return 0;
	map = AP_TrackerU32(p + spawn + 4);
	if (!AP_TrackerSpan(size, map, 9, 2)) return 0;
	for (i = 0; i < 9; i++) result.map[i] = (int16_t)AP_TrackerU16(p + map + i * 2);
	if (result.map[0] == result.map[2] || result.map[1] == result.map[3] || result.map[8] < 0 || result.map[8] > 3) return 0;
	count = AP_TrackerU32(p + 0xc); inst = AP_TrackerU32(p + 0x10);
	if (!AP_TrackerSpan(size, inst, count, 64)) return 0;
	for (i = 0; i < count; i++) {
		const unsigned char *def = p + inst + i * 64; unsigned k; int physical = 0;
		if (memcmp(def, "warppad#", 8)) continue;
		for (k = 8; k < 16 && def[k] >= '0' && def[k] <= '9'; k++) physical = physical * 10 + def[k] - '0';
		if (k == 8 || k == 16 || def[k] || result.pad_count >= AP_TRACKER_PADS) return 0;
		result.pads[result.pad_count].physical = physical;
		result.pads[result.pad_count].world_x = (int16_t)AP_TrackerU16(def + 0x30);
		result.pads[result.pad_count++].world_z = (int16_t)AP_TrackerU16(def + 0x34);
	}
	result.width = (int)result.icons[1][4] - result.icons[1][0];
	top_h = (int)result.icons[0][9] - result.icons[0][1];
	bottom_h = (int)result.icons[1][9] - result.icons[1][1];
	result.height = top_h + bottom_h;
	if (result.width < 1 || result.width > 256 || top_h < 1 || bottom_h < 1 || result.height > 512) return 0;
	result.pixels = (uint32_t *)malloc((size_t)result.width * result.height * sizeof *result.pixels);
	if (!result.pixels) return 0;
	for (y = 0; y < result.height; y++) for (x = 0; x < result.width; x++)
		result.pixels[y * result.width + x] = AP_TrackerTexel(vram, result.icons[y >= top_h], x, y >= top_h ? y - top_h : y);
	*hub = result; return 1;
}

/* Original minimap coordinate mapping, including its bottom-right anchor. */
static void AP_TrackerMapPoint(const AP_TrackerHubAsset *h, int wx, int wz, int *x, int *y)
{
	const int16_t *m = h->map; int ax, ay, rx = m[0] - m[2], ry = m[1] - m[3];
	if (m[8] == 0) { ax = wx * m[4] / rx; ay = wz * m[5] * 2 / ry; }
	else if (m[8] == 1) { ax = -wz * m[4] / ry; ay = wx * m[5] * 2 / rx; }
	else if (m[8] == 2) { ax = -wx * m[4] / rx; ay = -wz * m[5] * 2 / ry; }
	else { ax = wz * m[4] / ry; ay = -wx * m[5] * 2 / rx; }
	*x = m[6] + ax - (500 - h->width);
	*y = m[7] + ay - 16 - (195 - h->height);
}
#endif
