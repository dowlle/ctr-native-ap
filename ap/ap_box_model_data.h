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

// Textured variant: identical geometry, but every command carries texture index
// 1 in its low 9 bits (RenderBucket_GetCommandTexture reads
// `texIndex = command & 0x1ff`, 1-based into ptrTexLayout). Bit 0 does not
// disturb the colour offset, which is `(command >> 7) & 0x1fc` and therefore
// reads only bit 9 upwards.
#define AP_BOX_TRI_TEX(ci) \
	(0x80010001u | ((u32)(ci) << 9)), \
	(0x00020001u | ((u32)(ci) << 9)), \
	(0x00030001u | ((u32)(ci) << 9))

static const u32 s_apBoxModelCommandsTex[] = {
	AP_BOX_MODEL_NUM_COLORS,
	AP_BOX_TRI_TEX(0), AP_BOX_TRI_TEX(0),
	AP_BOX_TRI_TEX(1), AP_BOX_TRI_TEX(1),
	AP_BOX_TRI_TEX(2), AP_BOX_TRI_TEX(2),
	AP_BOX_TRI_TEX(3), AP_BOX_TRI_TEX(3),
	AP_BOX_TRI_TEX(4), AP_BOX_TRI_TEX(4),
	AP_BOX_TRI_TEX(5), AP_BOX_TRI_TEX(5),
	0xffffffffu,
};

#undef AP_BOX_TRI_TEX
#undef AP_BOX_TRI
