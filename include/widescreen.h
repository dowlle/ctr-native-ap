#ifndef WIDESCREEN_H
#define WIDESCREEN_H

#include <stdbool.h>

// Widescreen support ported from thecodingbob/ctr-native (branch
// widescreen-option). The PSX framebuffer is always 512 wide; wider aspect
// ratios widen the horizontal field of view instead (PushBuffer.c scales the
// view-projection matrix X axis), which means 2D HUD elements and icons drawn
// at fixed screen coordinates would sit closer to the edges of the visible
// viewport. These helpers re-fit a quad to the effective aspect so it keeps
// its relative on-screen position.
//
// The arithmetic helpers below are freestanding (they only touch g_config and
// plain ints), so tools/test-graphics-options.c compiles them against the real
// game types and pins the four aspect ratios.

// 1000 * (4/3) / (aspectW/aspectH), floored to a whole-number factor.
// 1000 = vanilla 4:3.
int Widescreen_GetFactor(void);

// Horizontal inset (in pixels) to squeeze one edge of a quad of the given
// width so it lands where it would on a 4:3 viewport. 0 for 4:3.
int Widescreen_XShift(int width);

// The horizontal field-of-view scale PushBuffer_SetMatrixVP applies to the
// view-projection matrix X axis (the engine's full-scale constant is 0x800).
// Exactly 0x800 at 4:3, so the default is a no-op.
int Widescreen_GetFovScale(void);

// Invert Widescreen_GetFovScale for a screen-space half width: the frustum
// corner X PushBuffer_UpdateFrustum culls against, widened to match the FOV
// PushBuffer_SetMatrixVP actually rendered. Identity at 4:3.
int Widescreen_ScaleFrustumX(int halfWidth);

// Effective presentation aspect for the aspect_ratio option, mapped to the
// 4:3 / 16:9 / 16:10 / 21:9 ladder (21:9 = 64:27). Returns 1 when the option
// overrides the window's own aspect and fills effW/effH; returns 0 for 4:3,
// where the window-derived presentation aspect stands.
int Widescreen_GetAspectRatio(int *effW, int *effH);

// Letterbox a window of winW x winH to the effective aspect effW:effH, same
// arithmetic NativeRenderer_UpdatePresentationViewport uses. Fills outW/outH
// (and returns them), never exceeding the window dimensions.
void Widescreen_LetterboxViewport(int winW, int winH, int effW, int effH, int *outW, int *outH);

// Map the dithering option onto the shader's disableDither uniform: on
// (default, PSX-authentic) -> 0, off -> 1. Same expression
// NativeRenderer_SetTexture uses.
int Widescreen_DitherUniform(bool dithering);

void Widescreen_CompressFT4(POLY_FT4 *p);
void Widescreen_CompressGT4(POLY_GT4 *p);
void Widescreen_CompressNative(PolyFT4 *p);

#endif
