#ifndef CTR_RENDER_LEVEL_LOD_H
#define CTR_RENDER_LEVEL_LOD_H

#include <common.h>
#include <platform/native_draw_distance.h>

// LOD threshold setup for the 1P, 2P and 3P/4P level renderers and the BSP
// walk, in one place so every path scales with increase_draw_distance the same
// way. Ported from CTR Native Expanded (thecodingbob) 652c264e24. With the
// option off every value is retail's.

enum RenderLevelLod2PConstants
{
	OVR227_DEPTH_SCALE = 0x19c0,
	OVR227_TEXTURE_LOD_DEPTH_THRESHOLD0 = 0x1000,
	OVR227_TEXTURE_LOD_DEPTH_THRESHOLD1 = 0x800,
	OVR227_TOP_LEVEL_NEAR_DEPTH_THRESHOLD = 0x600,
	OVR227_RECURSIVE_NEAR_DEPTH_THRESHOLD = 0x300,
};

// Retail's BSP LOD threshold for every split-screen player count.
#define RENDER_LEVEL_LOD_SPLIT_SCREEN_BSP_THRESHOLD 0x1540

static inline s32 RenderLevelLod_ScaleDistanceShift8(s32 distToScreen, s32 scale)
{
	s32 product = CTR_MipsMulLo(distToScreen, scale);
	if (product < 0)
	{
		product = CTR_MipsAddLo(product, 0xff);
	}
	return CTR_MipsSra(product, 8);
}

static inline void RenderLevelLod_Init1P(struct MainRenderLevelGeometryScratch *scratch, int levelID, u32 gameMode1, s32 distToScreen,
                                         bool extended)
{
	// adventure character selection, and every cutscene except the Crash
	// Bandicoot intro where he sleeps on a hill
	if (levelID == ADVENTURE_GARAGE || ((gameMode1 & GAME_CUTSCENE) != 0 && levelID != INTRO_CRASH))
	{
		scratch->depthScale = 0x1e00;
		scratch->bspLodDistanceThreshold = NativeDrawDistance_BspLodThreshold(0x640, extended);
		scratch->textureLodDepthThreshold0 = NativeDrawDistance_TextureLodThreshold(0x640, extended);
		scratch->textureLodDepthThreshold1 = NativeDrawDistance_TextureLodThreshold(0x500, extended);
		scratch->topLevelNearDepthThreshold = NativeDrawDistance_TextureLodThreshold(0x280, extended);
		scratch->recursiveNearDepthThreshold = NativeDrawDistance_TextureLodThreshold(0x140, extended);
	}
	else
	{
		// distToScreen is 0x1c2 in 1P mode
		scratch->depthScale = RenderLevelLod_ScaleDistanceShift8(distToScreen, 0x2080);
		scratch->bspLodDistanceThreshold = NativeDrawDistance_BspLodThreshold(CTR_MipsMulLo(distToScreen, 0x1a), extended);
		scratch->textureLodDepthThreshold0 = NativeDrawDistance_TextureLodThreshold(CTR_MipsMulLo(distToScreen, 0x18), extended);
		scratch->textureLodDepthThreshold1 = NativeDrawDistance_TextureLodThreshold(CTR_MipsMulLo(distToScreen, 0xc), extended);
		scratch->topLevelNearDepthThreshold = NativeDrawDistance_TextureLodThreshold(CTR_MipsMulLo(distToScreen, 7), extended);
		// Mosaic high-resolution textures are reached through recursive subdivision.
		scratch->recursiveNearDepthThreshold =
		    NativeDrawDistance_TextureLodThreshold(RenderLevelLod_ScaleDistanceShift8(distToScreen, 0x380), extended);
	}
	scratch->fullDynamicFadeDepthStart = CTR_MipsAddLo(scratch->bspLodDistanceThreshold, MAIN_RENDER_LEVEL_GEOMETRY_FULL_DYNAMIC_FADE_OFFSET);
}

static inline void RenderLevelLod_Init2P(struct MainRenderLevelGeometryScratch *scratch, bool extended)
{
	scratch->depthScale = OVR227_DEPTH_SCALE;
	scratch->textureLodDepthThreshold0 = NativeDrawDistance_TextureLodThreshold(OVR227_TEXTURE_LOD_DEPTH_THRESHOLD0, extended);
	scratch->textureLodDepthThreshold1 = NativeDrawDistance_TextureLodThreshold(OVR227_TEXTURE_LOD_DEPTH_THRESHOLD1, extended);
	scratch->topLevelNearDepthThreshold = NativeDrawDistance_TextureLodThreshold(OVR227_TOP_LEVEL_NEAR_DEPTH_THRESHOLD, extended);
	scratch->recursiveNearDepthThreshold = NativeDrawDistance_TextureLodThreshold(OVR227_RECURSIVE_NEAR_DEPTH_THRESHOLD, extended);
}

// Shared by the 3P and 4P ground renderers.
static inline void RenderLevelLod_InitSplitGround(struct MainRenderLevelGeometryScratch *scratch, bool extended)
{
	scratch->depthScale = 0x780;
	scratch->textureLodDepthThreshold0 = NativeDrawDistance_TextureLodThreshold(0x640, extended);
	scratch->textureLodDepthThreshold1 = NativeDrawDistance_TextureLodThreshold(0x500, extended);
	scratch->topLevelNearDepthThreshold = NativeDrawDistance_TextureLodThreshold(0x280, extended);
	scratch->recursiveNearDepthThreshold = NativeDrawDistance_TextureLodThreshold(0x140, extended);
}

static inline int RenderLevelLod_BspThreshold(const struct MainRenderLevelGeometryScratch *scratch, u8 numPlyr, bool extended)
{
	return numPlyr == 1 ? scratch->bspLodDistanceThreshold : NativeDrawDistance_BspLodThreshold(RENDER_LEVEL_LOD_SPLIT_SCREEN_BSP_THRESHOLD, extended);
}

#endif
