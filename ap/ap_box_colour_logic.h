#ifndef AP_BOX_COLOUR_LOGIC_H
#define AP_BOX_COLOUR_LOGIC_H
// Which colour one AP box wears, and the atlas that holds every colour.
//
// Freestanding: no engine types, no allocation, no IO. The client
// (ap/ap_box_texture.c, ap/ap_boxes.c) consumes it and tools/test-box-colour.c
// pins it on the host with synthetic data.
//
// THE OPTION
// ----------
// ctr_options.color_boxes_by_item (apworld option "Item Box Colours", default
// off; ruling R11 of 2026-07-23 asked for a YAML toggle because colouring by
// importance tells the player which checks matter). Off: every AP box is pink.
// On: a box takes the Archipelago colour of the item it holds, from the item
// flags the client scouts silently for every location at connect
// (ap/ap_net.cpp). Until that scout reply has arrived, or for a location it
// does not cover, the box is pink.
//
// Colours are Archipelago's own item-name colours (NetUtils.py): progression
// plum AF99EF, useful slateblue 6D8BE8, filler cyan 00EEEE, trap salmon
// FA8072. An item with several flags resolves as Archipelago's own text
// colouring does: progression, then useful, then trap, then filler
// (AP_ItemFlagsClass, shared with the pad markers and the hub feed).
//
// THE ATLAS
// ---------
// One 80x64 slot per colour, three slots per row: the 16x16 wood tile at the
// slot's (0,0) and the 64x64 face at (16,0). Pink is slot 0, at the same
// place the compiled atlas keeps its wood and face, so the generated model
// layouts (ap_box_model_framed_data.h) address the pink box unchanged and the
// other colours are the same layouts shifted to their slot.

#include <string.h>

#include "ap_box_edge_logic.h"
#include "ap_item_flags.h"

enum
{
	AP_BOX_COLOUR_PINK = 0,
	AP_BOX_COLOUR_PROGRESSION,
	AP_BOX_COLOUR_USEFUL,
	AP_BOX_COLOUR_FILLER,
	AP_BOX_COLOUR_TRAP,
	AP_BOX_COLOUR_COUNT
};

#define AP_BOX_ATLAS_W      256
#define AP_BOX_ATLAS_H      128
#define AP_BOX_SLOT_W       80
#define AP_BOX_SLOT_H       64
#define AP_BOX_SLOTS_PER_ROW 3
#define AP_BOX_SLOT_WOOD_X  0
#define AP_BOX_SLOT_FACE_X  16

// Archipelago's item colours, indexed by colour. The pink row is unused: pink
// keeps its own hand-tuned ramp (AP_BoxEdge_Recolour).
static const unsigned char s_apBoxColourRgb[AP_BOX_COLOUR_COUNT][3] = {
	{0x00, 0x00, 0x00}, // pink: fixed ramp
	{0xAF, 0x99, 0xEF}, // progression, plum
	{0x6D, 0x8B, 0xE8}, // useful, slateblue
	{0x00, 0xEE, 0xEE}, // filler, cyan
	{0xFA, 0x80, 0x72}, // trap, salmon
};

// The colour for one box. colourByItem is the seed's option, scouted says
// whether the scout cache holds this location, flags are its item flags.
static inline int AP_BoxColour_Pick(int colourByItem, int scouted, unsigned flags)
{
	if (!colourByItem || !scouted)
		return AP_BOX_COLOUR_PINK;
	switch (AP_ItemFlagsClass(flags))
	{
	case AP_ITEM_CLASS_PROGRESSION: return AP_BOX_COLOUR_PROGRESSION;
	case AP_ITEM_CLASS_USEFUL:      return AP_BOX_COLOUR_USEFUL;
	case AP_ITEM_CLASS_TRAP:        return AP_BOX_COLOUR_TRAP;
	case AP_ITEM_CLASS_FILLER:      return AP_BOX_COLOUR_FILLER;
	}
	return AP_BOX_COLOUR_FILLER;
}

// Top-left of a colour's slot in the atlas.
static inline void AP_BoxColour_SlotOrigin(int colour, int *x, int *y)
{
	if (colour < 0 || colour >= AP_BOX_COLOUR_COUNT)
		colour = AP_BOX_COLOUR_PINK;
	*x = (colour % AP_BOX_SLOTS_PER_ROW) * AP_BOX_SLOT_W;
	*y = (colour / AP_BOX_SLOTS_PER_ROW) * AP_BOX_SLOT_H;
}

static inline void AP_BoxColour_Recolour(int colour, unsigned char *rgba, int w, int h, int stride)
{
	if (colour <= AP_BOX_COLOUR_PINK || colour >= AP_BOX_COLOUR_COUNT)
		AP_BoxEdge_Recolour(rgba, w, h, stride);
	else
		AP_BoxEdge_RecolourTo(rgba, w, h, stride, s_apBoxColourRgb[colour][0], s_apBoxColourRgb[colour][1],
		                      s_apBoxColourRgb[colour][2]);
}

static inline void AP_BoxColour_CopyRect(unsigned char *dst, int dstStride, const unsigned char *src, int srcStride, int w,
                                  int h)
{
	int y;
	for (y = 0; y < h; y++)
		memcpy(dst + y * dstStride * 4, src + y * srcStride * 4, (size_t)w * 4);
}

// Build one colour's slot in the atlas (atlas is AP_BOX_ATLAS_W wide).
//
//   discWood   16x16 RGBA of the retail crate's wood tile as decoded from the
//              player's disc, NOT yet recoloured; 0 when the read failed.
//   discCrate  64x64 RGBA of the retail Wumpa crate's face, likewise; 0 when
//              the read failed.
//   builtinWood / builtinFace  JurnthReinal's compiled 16x16 border and 64x64
//              face, rows builtinStride pixels apart.
//   mask       which face pixels are the Archipelago logo (64 rows of 8 bytes).
//
// With the disc: the wood and the crate face recoloured to the colour, the
// logo on top of the face. Without it: JurnthReinal's border and face; pink
// keeps them exactly as drawn, other colours recolour them the same way
// (the logo circles still keep their art).
static inline void AP_BoxColour_BuildSlot(unsigned char *atlas, int colour, const unsigned char *discWood,
                                   const unsigned char *discCrate, const unsigned char *builtinWood,
                                   const unsigned char *builtinFace, int builtinStride, const unsigned char *mask)
{
	static unsigned char wood[16 * 16 * 4];
	static unsigned char face[64 * 64 * 4];
	static unsigned char crate[64 * 64 * 4];
	int sx, sy;

	AP_BoxColour_SlotOrigin(colour, &sx, &sy);

	if (discWood != 0)
	{
		memcpy(wood, discWood, sizeof wood);
		AP_BoxColour_Recolour(colour, wood, 16, 16, 16);
	}
	else
	{
		AP_BoxColour_CopyRect(wood, 16, builtinWood, builtinStride, 16, 16);
		if (colour != AP_BOX_COLOUR_PINK)
			AP_BoxColour_Recolour(colour, wood, 16, 16, 16);
	}

	AP_BoxColour_CopyRect(face, 64, builtinFace, builtinStride, 64, 64);
	if (discCrate != 0 || colour != AP_BOX_COLOUR_PINK)
	{
		if (discCrate != 0)
			memcpy(crate, discCrate, sizeof crate);
		else
			memcpy(crate, face, sizeof crate);
		AP_BoxColour_Recolour(colour, crate, 64, 64, 64);
		AP_BoxEdge_ComposeLogo(face, 64, crate, 64, mask);
	}

	AP_BoxColour_CopyRect(atlas + ((sy * AP_BOX_ATLAS_W) + sx + AP_BOX_SLOT_WOOD_X) * 4, AP_BOX_ATLAS_W, wood, 16,
	                      16, 16);
	AP_BoxColour_CopyRect(atlas + ((sy * AP_BOX_ATLAS_W) + sx + AP_BOX_SLOT_FACE_X) * 4, AP_BOX_ATLAS_W, face, 64,
	                      64, 64);
}

#endif // AP_BOX_COLOUR_LOGIC_H
