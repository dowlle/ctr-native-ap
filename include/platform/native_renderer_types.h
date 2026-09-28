/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/include/PsyX/PsyX_render.h
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#ifndef NATIVE_RENDERER_TYPES_H
#define NATIVE_RENDERER_TYPES_H

#include <macros.h>
#include <psx/libgte.h>
#include <psx/libgpu.h>

#define LUT_WIDTH              (256)
#define LUT_HEIGHT             (256)

#define VRAM_WIDTH             (1024)
#define VRAM_HEIGHT            (512)

#define TPAGE_WIDTH            (256)
#define TPAGE_HEIGHT           (256)

// Vertices per frame batch. 1 << 16 overflowed on Saphi's Papu Cave (a custom
// track, 1 MiB primitive arena per frame). 1 << 18 is what a full 1 MiB arena
// of POLY_F4, the densest polygon (24 bytes, 6 vertices), produces. Cost: 5 MiB
// for the CPU array plus 5 MiB for each of the two GL vertex buffers. Frames
// that still do not fit drop whole primitives (native_gpu_vertex_budget_logic.h).
#define MAX_VERTEX_BUFFER_SIZE (1u << 18)

#pragma pack(push, 1)
typedef struct
{
	s16 x, y, page, clut;

	u8 u, v, bright, dither;
	u8 r, g, b, a;

	s8 tcx, tcy, _p0, _p1;
} GrVertex;
#pragma pack(pop)

typedef enum
{
	a_position,
	a_texcoord,
	a_color,
	a_extra,
} ShaderAttrib;

typedef enum
{
	BM_NONE,
	BM_AVERAGE,
	BM_ADD,
	BM_SUBTRACT,
	BM_ADD_QUATER_SOURCE
} BlendMode;

typedef enum
{
	TF_4_BIT,
	TF_8_BIT,
	TF_16_BIT,

	TF_32_BIT_RGBA
} TexFormat;

typedef u32 TextureID;
typedef u32 ShaderID;

#endif
