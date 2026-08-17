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
// per-primitive and retail draws are untouched. Bit 15 is free: every existing
// tpage decoder reads below it (page 0-4, blend 5-6, depth 7-8, dither 9).
#define AP_TPAGE_SIDELOAD_BIT 0x8000

void NativeGpu_SetSideloadTexture(unsigned int texture, int width, int height);
void ClearSplits(void);
void DrawAllSplits(void);
void ParsePrimitivesLinkedList(u32 *p, int singlePrimitive);
int NativeGpu_GetStateSize(void);
int NativeGpu_CaptureState(void *dst, int dstSize);
int NativeGpu_RestoreState(const void *src, int srcSize);

#endif
