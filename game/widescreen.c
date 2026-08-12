// Widescreen support ported from thecodingbob/ctr-native (branch
// widescreen-option). See include/widescreen.h for what these do and why the
// whole 2D layer needs them.
//
// Includes only the headers these functions actually touch, so the module
// stays freestanding enough for the out-of-engine harness
// (tools/test-graphics-options.c) to compile it against the real game types.
#include <macros.h>
#include <psx/libgpu.h>
#include <prim.h>
#include <platform/native_config.h>
#include <widescreen.h>

int Widescreen_GetFactor(void)
{
	switch (g_config.aspectRatio)
	{
		case 1:  return 750;  // 16:9  -> 1000 * (4/3) / (16/9)  = 750
		case 2:  return 833;  // 16:10 -> 1000 * (4/3) / (16/10) ~ 833
		case 3:  return 563;  // 21:9 (64:27) -> 1000 * (4/3) / (64/27) ~ 563
		default: return 1000; // 4:3 (vanilla)
	}
}

int Widescreen_XShift(int width)
{
	const int factor = Widescreen_GetFactor();

	if (factor == 1000)
		return 0;

	return (width * (1000 - factor)) / 2000;
}

// Engine full-scale constant for the view-projection matrix X axis.
#define WIDESCREEN_R800 0x800

int Widescreen_GetFovScale(void)
{
	// Widescreen_GetFactor is 1000 * (4/3) / (aspectW/aspectH), scaled onto the
	// engine's 0x800 full-scale constant the same way the fork's original hunk
	// did (factor * 0x800 / 1000). 1000 -> exactly 0x800 (4:3 no-op).
	return Widescreen_GetFactor() * WIDESCREEN_R800 / 1000;
}

int Widescreen_ScaleFrustumX(int halfWidth)
{
	const int ws = Widescreen_GetFovScale();

	// Spread the frustum corner X by 0x800 / ws. At 4:3 ws == 0x800, so this is
	// the identity.
	return halfWidth * WIDESCREEN_R800 / ws;
}

int Widescreen_GetAspectRatio(int *effW, int *effH)
{
	switch (g_config.aspectRatio)
	{
		case 1: *effW = 16; *effH = 9;  return 1;
		case 2: *effW = 16; *effH = 10; return 1;
		case 3: *effW = 64; *effH = 27; return 1;
		default: return 0; // 4:3 — keep the window-derived presentation aspect
	}
}

void Widescreen_LetterboxViewport(int winW, int winH, int effW, int effH, int *outW, int *outH)
{
	int viewportW = winW;
	int viewportH = (viewportW * effH) / effW;

	if (viewportH > winH)
	{
		viewportH = winH;
		viewportW = (viewportH * effW) / effH;
	}

	if (viewportW < 1)
	{
		viewportW = 1;
	}
	if (viewportH < 1)
	{
		viewportH = 1;
	}

	*outW = viewportW;
	*outH = viewportH;
}

int Widescreen_DitherUniform(bool dithering)
{
	return dithering ? 0 : 1;
}

void Widescreen_CompressFT4(POLY_FT4 *p)
{
	int len = Widescreen_XShift(p->x1 - p->x0);
	p->x0 += len;
	p->x2 += len;
	p->x1 -= len;
	p->x3 -= len;
}

void Widescreen_CompressGT4(POLY_GT4 *p)
{
	int len = Widescreen_XShift(p->x1 - p->x0);
	p->x0 += len;
	p->x2 += len;
	p->x1 -= len;
	p->x3 -= len;
}

void Widescreen_CompressNative(PolyFT4 *p)
{
	int w = p->v[1].pos.x - p->v[0].pos.x;
	int len = Widescreen_XShift(w);
	p->v[0].pos.x += len;
	p->v[2].pos.x += len;
	p->v[1].pos.x -= len;
	p->v[3].pos.x -= len;
}
