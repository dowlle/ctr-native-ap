#ifndef NATIVE_DRAW_DISTANCE_H
#define NATIVE_DRAW_DISTANCE_H

#include <stdbool.h>

// The increase_draw_distance option (g_config.increaseDrawDistance, declared in
// include/platform/native_config.h) lets the renderer submit geometry up to
// NATIVE_DRAW_DISTANCE_SCALE times farther than retail, so distant geometry is
// still drawn and a model keeps its high-poly LOD header out to the distance it
// was authored for.
//
// Ported from CTR Native Expanded (thecodingbob), commits 0fd84de084,
// e0fedbf27f and a81bf65bf9. Before this port only the BSP LOD threshold was
// scaled (x3). The texture thresholds stayed at retail values, so neighbouring
// quadblocks rendered at different levels and left T-junction cracks with the
// option on. The BSP and texture thresholds now scale together; the two
// near-subdivision thresholds stay retail (A5-13c, see
// NativeDrawDistance_SubdivisionThreshold).
//
// Each helper is pure: it takes retail's own value plus the flag, reads no
// global, and returns the value the caller should use. That is what lets them be
// tested off-line, see tools/test-draw-distance.c.

#define NATIVE_DRAW_DISTANCE_SCALE 4

// Retail's far clip: how far the frustum corner direction is extrapolated to
// land on the plane. A length in world units, not a ratio, so it is the one
// number here that starts from a constant instead of a caller-supplied value.
#define NATIVE_DRAW_DISTANCE_RETAIL_FAR_CLIP 0x100

static inline int NativeDrawDistance_FarClip(bool extended)
{
	return NATIVE_DRAW_DISTANCE_RETAIL_FAR_CLIP * (extended ? NATIVE_DRAW_DISTANCE_SCALE : 1);
}

// GTE H/2, the depth past which a projected bounds test would overflow the
// divide. Dividing rather than shifting keeps the expression valid for any
// factor; the argument is a non-negative register read, so both agree.
static inline int NativeDrawDistance_GeomScreenHalf(int geomScreenHalf, bool extended)
{
	return extended ? geomScreenHalf / NATIVE_DRAW_DISTANCE_SCALE : geomScreenHalf;
}

// A model's projected depth, used to pick its LOD header. Left unscaled the walk
// exhausts every header on a distant instance and drops it entirely, which is
// why this one is a divide: it delays that exhaustion rather than moving the
// cutoff.
static inline int NativeDrawDistance_ProjectedLodDistance(int projectedDistance, bool extended)
{
	return extended ? projectedDistance / NATIVE_DRAW_DISTANCE_SCALE : projectedDistance;
}

// The projected depth past which a BSP leaf draws through the full-dynamic
// emitter instead of the subdivided grids.
static inline int NativeDrawDistance_BspLodThreshold(int retailThreshold, bool extended)
{
	return extended ? retailThreshold * NATIVE_DRAW_DISTANCE_SCALE : retailThreshold;
}

// The two texture LOD thresholds (textureLodDepthThreshold0 and 1): which
// texture level a quadblock face uses. Scaled with the BSP threshold, so the
// faces drawn whole at a distance pick their levels consistently.
static inline int NativeDrawDistance_TextureLodThreshold(int retailThreshold, bool extended)
{
	return extended ? retailThreshold * NATIVE_DRAW_DISTANCE_SCALE : retailThreshold;
}

// The top-level and recursive near-subdivision thresholds (topLevelNear and
// recursiveNear) keep retail's value with the option on too (A5-13c). Scaled,
// distant faces only one or two screen pixels tall were split into
// sub-triangles on PS1 integer screen coordinates. Where a face met a
// neighbour split one level deeper, the rounded T-junction covered or uncovered
// whole one-pixel rows: turbo pads broke into detached green and yellow
// slivers (Mystery Caves start straight), and the ground showed more gaps to
// the background. Expanded scales these as well and shows the same slivers.
// Subdivision only adds detail next to the camera, where retail already does
// it, and the topLevelNear value also gates the mosaic texture level, so both
// stay together. The parameter stays so the call sites read like the others
// and a later change is one line.
static inline int NativeDrawDistance_SubdivisionThreshold(int retailThreshold, bool extended)
{
	(void)extended;
	return retailThreshold;
}

// Per-level render arenas (primitive memory and the ordering-table arena) are
// sized by retail for retail's draw distance, and the level renderer stops
// mid-walk when one runs out. Expanded (e86365e10b) grows them by the same
// factor on every load. Here they grow on a 1P load only when the option is on
// at level load, so a 1P load with it off allocates exactly what it did before
// (and the custom-track 1P floor wins over the scaled size anyway). A
// split-screen load (2P to 4P) always grows, whatever the option says, so
// switching the option on mid-race works at once (maintainer ruling 2026-11-15,
// A5-4b). The main menu and cutscenes are not split screen even though the
// player count can read 2 to 4 there (the main menu loads with 4), so they
// follow the option like 1P: the caller passes notSplitScreenScene for them.
// Only in builds with the 8 MiB MEMPACK (CTR_CUSTOM_TRACKS), see
// MainInit_PrimMem and MainInit_OTMem.
static inline bool NativeDrawDistance_ArenaGrows(bool extended, int numPlyrCurrGame, bool notSplitScreenScene)
{
	return extended || (numPlyrCurrGame >= 2 && !notSplitScreenScene);
}

static inline int NativeDrawDistance_ArenaBytes(int retailBytes, bool grow)
{
	return grow ? retailBytes * NATIVE_DRAW_DISTANCE_SCALE : retailBytes;
}

#endif
