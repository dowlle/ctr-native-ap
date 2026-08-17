/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/gpu/PsyX_GPU.h
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#ifndef NATIVE_GPU_H
#define NATIVE_GPU_H

#include <macros.h>
#include <psx/libgte.h>
#include <psx/libgpu.h>

extern DISPENV activeDispEnv;
extern DRAWENV activeDrawEnv;
extern int g_GPUDisabledState;

int NativeGpu_HasPendingSplits(void);

// AP sideloaded texture. Register once; an individual primitive opts in by
// setting AP_TPAGE_SIDELOAD_BIT in its own tpage word, so the scope is
// per-primitive and retail draws are untouched.
//
// Bit 15 is free FOR THE PRIMITIVES THAT CAN CARRY IT. Every decoder a flagged
// primitive reaches reads below it (page 0-4, blend 5-6, depth 7-8, dither 9),
// and DR_TPAGE masks to 0x1ff. It is NOT true that nothing in the codebase
// looks above bit 9: DrawLevelOvr1P_IsPlausibleTextureLayout tests
// `(tpage & 0xfe00) == 0` and would reject a flagged word. That check only ever
// sees level quad-block TextureLayouts, which are never AP-owned, so it cannot
// see this flag -- but do not widen where the flag is set without re-checking
// it. Retail primitives cannot set the bit at all: their tpage words come from
// fixed disc data or from code-synthesized values that top out at bit 11.
//
// Sprites are the one path that reads a tpage they do not own -- the persistent
// activeDrawEnv one -- so the SPRT handlers strip this bit before use.
#define AP_TPAGE_SIDELOAD_BIT 0x8000

void NativeGpu_SetSideloadTexture(unsigned int texture, int width, int height);
void ClearSplits(void);
void DrawAllSplits(void);
void ParsePrimitivesLinkedList(u32 *p, int singlePrimitive);
int NativeGpu_GetStateSize(void);
int NativeGpu_CaptureState(void *dst, int dstSize);
int NativeGpu_RestoreState(const void *src, int srcSize);

#endif
