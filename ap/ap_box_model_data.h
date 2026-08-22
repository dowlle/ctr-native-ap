// GENERATED SHAPE: a de-indexed, untextured cube for the AP box fallback.
// Each triangle restarts the renderer strip and consumes exactly three
// vertices. byte0 = horizontal, byte1 = depth, byte2 = vertical.

#define AP_BOX_MODEL_NUM_VERTS 36
#define AP_BOX_MODEL_NUM_TRIS 12
#define AP_BOX_MODEL_NUM_COLORS 6

// Face shades in PSX 0x00BBGGRR order. The orange/brown palette makes the
// fallback visibly crate-like without pretending to be a relic time crate.
static const u32 s_apBoxModelColors[AP_BOX_MODEL_NUM_COLORS] = {
	0x004090d0, 0x00285888, 0x003878b0,
	0x004c9ce0, 0x0060b8f0, 0x001c4068,
};

// The vertical byte is pre-decremented on x=224 vertices because the engine's
// packed XY add carries from the horizontal half when frame.pos is negative.
static const u8 s_apBoxModelVerts[AP_BOX_MODEL_NUM_VERTS * 3] = {
	// front
	 32, 32, 32, 224, 32, 31, 224, 32,223,
	 32, 32, 32, 224, 32,223,  32, 32,224,
	// back
	224,224, 31,  32,224, 32,  32,224,224,
	224,224, 31,  32,224,224, 224,224,223,
	// left
	 32,224, 32,  32, 32, 32,  32, 32,224,
	 32,224, 32,  32, 32,224,  32,224,224,
	// right
	224, 32, 31, 224,224, 31, 224,224,223,
	224, 32, 31, 224,224,223, 224, 32,223,
	// top
	 32, 32,224, 224, 32,223, 224,224,223,
	 32, 32,224, 224,224,223,  32,224,224,
	// bottom
	 32,224, 32, 224,224, 31, 224, 32, 31,
	 32,224, 32, 224, 32, 31,  32, 32, 32,
};

#define AP_BOX_TRI(ci) \
	(0x80010000u | ((u32)(ci) << 9)), \
	(0x00020000u | ((u32)(ci) << 9)), \
	(0x00030000u | ((u32)(ci) << 9))

static const u32 s_apBoxModelCommands[] = {
	AP_BOX_MODEL_NUM_COLORS,
	AP_BOX_TRI(0), AP_BOX_TRI(0),
	AP_BOX_TRI(1), AP_BOX_TRI(1),
	AP_BOX_TRI(2), AP_BOX_TRI(2),
	AP_BOX_TRI(3), AP_BOX_TRI(3),
	AP_BOX_TRI(4), AP_BOX_TRI(4),
	AP_BOX_TRI(5), AP_BOX_TRI(5),
	0xffffffffu,
};

// Textured variant. The texture index lives in the low 9 bits
// (RenderBucket_GetCommandTexture is 1-based into ptrTexLayout). Those bits do
// not disturb the colour offset, `(command >> 7) & 0x1fc`.
//
// UV CORNER ROLES, NOT ONE SHARED PAIR. The primitive writer pairs
// TextureLayout corner k with strip vertex k. Roles are assigned in the
// OUTSIDE VIEW of each face, not in an abstract face plane: texture top sits
// on the world +Y (up) corners of every side face, and texture left sits on
// the corner an outside viewer sees on their left (screen right = up x
// outwardNormal under the engine's proper-rotation camera with the PSX
// y-down screen). byte1 pairs with pos.z and byte2 with pos.y in the
// renderer's packed vertex path, so byte2 is the vertical -- the first
// alpha3 table read byte2 as depth and shipped every face upside down, with
// half of them horizontally mirrored on top of that (observed live
// 2026-08-21). In the outside view the twelve triangles reduce to two
// patterns for the four sides plus the top, and one pair for the bottom:
//
//   1  BR,BL,TL   triangle A of front / back / left / right / top
//   2  BR,TL,TR   triangle B of front / back / left / right / top
//   3  TL,TR,BR   triangle A of bottom
//   4  TL,BR,BL   triangle B of bottom
//
// The top face's art top points toward +Z, the bottom face's toward +Z seen
// from below; caps have no canonical up, these keep the seams consistent.
// Verified with the outside-view rasterizer replay
// (tools/apbox-texture/orientation-replay.py): every face renders the art
// exactly, upright and unmirrored, and the same replay reproduces the
// upside-down/mirrored pattern of the previous table as its anchor.
#define AP_BOX_TRI_TEX(ci, ti) \
	(0x80010000u | (u32)(ti) | ((u32)(ci) << 9)), \
	(0x00020000u | (u32)(ti) | ((u32)(ci) << 9)), \
	(0x00030000u | (u32)(ti) | ((u32)(ci) << 9))

static const u32 s_apBoxModelCommandsTex[] = {
	AP_BOX_MODEL_NUM_COLORS,
	AP_BOX_TRI_TEX(0, 1), AP_BOX_TRI_TEX(0, 2), // front
	AP_BOX_TRI_TEX(1, 1), AP_BOX_TRI_TEX(1, 2), // back
	AP_BOX_TRI_TEX(2, 1), AP_BOX_TRI_TEX(2, 2), // left
	AP_BOX_TRI_TEX(3, 1), AP_BOX_TRI_TEX(3, 2), // right
	AP_BOX_TRI_TEX(4, 1), AP_BOX_TRI_TEX(4, 2), // top
	AP_BOX_TRI_TEX(5, 3), AP_BOX_TRI_TEX(5, 4), // bottom
	0xffffffffu,
};

// The textured cube's colour table. The shader multiplies every sampled texel
// by the Gouraud colour, so the fallback cube's orange/brown palette above
// would tint the contributed art orange (observed live: the pink flower
// rendered cream/orange). 0x80 is the PSX neutral multiplier; six identical
// entries keep the command list's colour indices valid for both variants.
static const u32 s_apBoxModelColorsTex[AP_BOX_MODEL_NUM_COLORS] = {
	0x00808080, 0x00808080, 0x00808080,
	0x00808080, 0x00808080, 0x00808080,
};

#undef AP_BOX_TRI_TEX
#undef AP_BOX_TRI
