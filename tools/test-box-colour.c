// Host assertions for the AP box colours (ap/ap_box_colour_logic.h): which
// colour a box wears for the seed option and its scouted item flags, where each
// colour sits in the atlas, the recolour onto an Archipelago item colour, and
// how one colour slot is composed with and without the disc read. Synthetic
// data only: no retail pixel is in this file.
//
//   cc -Wall -Wextra -I ap -o /tmp/test-box-colour tools/test-box-colour.c && /tmp/test-box-colour
//
// Exit 0 = every assertion held; the failing case is printed otherwise.

#include <stdio.h>
#include <string.h>

#include "ap_box_colour_logic.h"

static int g_fail;

static void expect(int got, int want, const char *what)
{
	if (got != want)
	{
		printf("FAIL %s: got %d, want %d\n", what, got, want);
		g_fail = 1;
	}
}

static void test_pick(void)
{
	// option off: always pink, whatever the item
	expect(AP_BoxColour_Pick(0, 1, AP_ITEM_FLAG_PROGRESSION), AP_BOX_COLOUR_PINK, "off -> pink");
	expect(AP_BoxColour_Pick(0, 1, 0), AP_BOX_COLOUR_PINK, "off, filler -> pink");
	// option on, not scouted yet (or a location the scout does not cover): pink
	expect(AP_BoxColour_Pick(1, 0, AP_ITEM_FLAG_PROGRESSION), AP_BOX_COLOUR_PINK, "unscouted -> pink");
	// the four classes
	expect(AP_BoxColour_Pick(1, 1, AP_ITEM_FLAG_PROGRESSION), AP_BOX_COLOUR_PROGRESSION, "progression");
	expect(AP_BoxColour_Pick(1, 1, AP_ITEM_FLAG_USEFUL), AP_BOX_COLOUR_USEFUL, "useful");
	expect(AP_BoxColour_Pick(1, 1, AP_ITEM_FLAG_TRAP), AP_BOX_COLOUR_TRAP, "trap");
	expect(AP_BoxColour_Pick(1, 1, 0), AP_BOX_COLOUR_FILLER, "filler");
	// several flags: Archipelago's own order, progression > useful > trap
	expect(AP_BoxColour_Pick(1, 1, AP_ITEM_FLAG_PROGRESSION | AP_ITEM_FLAG_USEFUL), AP_BOX_COLOUR_PROGRESSION,
	       "progression + useful -> progression");
	expect(AP_BoxColour_Pick(1, 1, AP_ITEM_FLAG_PROGRESSION | AP_ITEM_FLAG_TRAP), AP_BOX_COLOUR_PROGRESSION,
	       "progression + trap -> progression");
	expect(AP_BoxColour_Pick(1, 1, AP_ITEM_FLAG_USEFUL | AP_ITEM_FLAG_TRAP), AP_BOX_COLOUR_USEFUL,
	       "useful + trap -> useful");
	expect(AP_BoxColour_Pick(1, 1, 0x10), AP_BOX_COLOUR_FILLER, "unknown bits alone -> filler");
}

static void test_override(void)
{
	// seed off + player on => off; seed on + player off => off; both on => colours
	expect(AP_BoxColour_Enabled(0, 1), 0, "seed off, player on -> off");
	expect(AP_BoxColour_Enabled(1, 0), 0, "seed on, player off -> off");
	expect(AP_BoxColour_Enabled(0, 0), 0, "both off -> off");
	expect(AP_BoxColour_Enabled(1, 1), 1, "both on -> colours");
	// and what the box then wears
	expect(AP_BoxColour_Pick(AP_BoxColour_Enabled(0, 1), 1, AP_ITEM_FLAG_TRAP), AP_BOX_COLOUR_PINK,
	       "seed off: a trap box stays pink even with the player row on");
	expect(AP_BoxColour_Pick(AP_BoxColour_Enabled(1, 0), 1, AP_ITEM_FLAG_TRAP), AP_BOX_COLOUR_PINK,
	       "player off: a trap box is pink");
	expect(AP_BoxColour_Pick(AP_BoxColour_Enabled(1, 1), 1, AP_ITEM_FLAG_TRAP), AP_BOX_COLOUR_TRAP,
	       "both on: a trap box is salmon");
	expect(AP_BoxColour_Pick(AP_BoxColour_Enabled(1, 1), 0, AP_ITEM_FLAG_TRAP), AP_BOX_COLOUR_PINK,
	       "both on, not scouted yet: pink");
	// the Options row
	expect(AP_BoxColour_RowState(1, 0), AP_BOX_COLOUR_ROW_SEED_OFF, "loaded seed with colours off locks the row");
	expect(AP_BoxColour_RowState(1, 1), AP_BOX_COLOUR_ROW_PLAYER, "loaded seed with colours on: player's row");
	expect(AP_BoxColour_RowState(0, 0), AP_BOX_COLOUR_ROW_PLAYER, "no seed loaded: player's row");
}

static void test_colours(void)
{
	// the Archipelago client colours (NetUtils.py)
	expect(s_apBoxColourRgb[AP_BOX_COLOUR_PROGRESSION][0] == 0xAF && s_apBoxColourRgb[AP_BOX_COLOUR_PROGRESSION][1] == 0x99 &&
	           s_apBoxColourRgb[AP_BOX_COLOUR_PROGRESSION][2] == 0xEF,
	       1, "progression is plum AF99EF");
	expect(s_apBoxColourRgb[AP_BOX_COLOUR_USEFUL][0] == 0x6D && s_apBoxColourRgb[AP_BOX_COLOUR_USEFUL][1] == 0x8B &&
	           s_apBoxColourRgb[AP_BOX_COLOUR_USEFUL][2] == 0xE8,
	       1, "useful is slateblue 6D8BE8");
	expect(s_apBoxColourRgb[AP_BOX_COLOUR_FILLER][0] == 0x00 && s_apBoxColourRgb[AP_BOX_COLOUR_FILLER][1] == 0xEE &&
	           s_apBoxColourRgb[AP_BOX_COLOUR_FILLER][2] == 0xEE,
	       1, "filler is cyan 00EEEE");
	expect(s_apBoxColourRgb[AP_BOX_COLOUR_TRAP][0] == 0xFA && s_apBoxColourRgb[AP_BOX_COLOUR_TRAP][1] == 0x80 &&
	           s_apBoxColourRgb[AP_BOX_COLOUR_TRAP][2] == 0x72,
	       1, "trap is salmon FA8072");
}

static void test_slots(void)
{
	int c, x, y, x2, y2, d;

	AP_BoxColour_SlotOrigin(AP_BOX_COLOUR_PINK, &x, &y);
	expect(x == 0 && y == 0, 1, "pink sits where the compiled atlas keeps its art");
	for (c = 0; c < AP_BOX_COLOUR_COUNT; c++)
	{
		AP_BoxColour_SlotOrigin(c, &x, &y);
		// every UV of the generated layouts (face up to 16+62, wood up to 14)
		// must still fit a u8 after the shift, and the slot the atlas
		expect(x + AP_BOX_SLOT_W <= AP_BOX_ATLAS_W && y + AP_BOX_SLOT_H <= AP_BOX_ATLAS_H, 1, "slot inside atlas");
		expect(x + AP_BOX_SLOT_FACE_X + 64 <= 255 && y + 64 <= 255, 1, "slot UVs fit a byte");
		for (d = 0; d < c; d++)
		{
			AP_BoxColour_SlotOrigin(d, &x2, &y2);
			expect(x2 + AP_BOX_SLOT_W <= x || x + AP_BOX_SLOT_W <= x2 || y2 + AP_BOX_SLOT_H <= y ||
			           y + AP_BOX_SLOT_H <= y2,
			       1, "slots do not overlap");
		}
	}
	AP_BoxColour_SlotOrigin(99, &x, &y);
	expect(x == 0 && y == 0, 1, "unknown colour falls back to pink's slot");
}

static void test_recolour_to(void)
{
	unsigned char px[4][4] = {
		{60, 20, 0, 255},    // dark wood
		{247, 115, 16, 255}, // light wood
		{90, 90, 90, 255},   // grey nail
		{200, 80, 0, 0},     // transparent
	};
	unsigned char in[4][4];

	memcpy(in, px, sizeof px);
	AP_BoxEdge_RecolourTo(&px[0][0], 4, 1, 4, 0x00, 0xEE, 0xEE); // cyan
	expect(px[0][0], 0, "cyan: no red in the shadow");
	expect(px[0][1] == px[0][2] && px[0][1] > 0, 1, "cyan: shadow is dark cyan");
	expect(px[1][0] < px[1][1] / 3 && px[1][1] > px[0][1], 1, "cyan: light wood lighter, still cyan");
	expect(memcmp(px[2], in[2], 4), 0, "grey nail untouched");
	expect(memcmp(px[3], in[3], 4), 0, "transparent untouched");
}

// One slot composed from synthetic parts.
static unsigned char s_atlas[AP_BOX_ATLAS_W * AP_BOX_ATLAS_H * 4];
static unsigned char s_builtin[128 * 64 * 4]; // the compiled atlas shape: wood at (0,0), face at (16,0)
static unsigned char s_wood[16 * 16 * 4], s_crate[64 * 64 * 4], s_mask[64 * 8];

static const unsigned char *atlas_px(int colour, int rx, int ry)
{
	int x, y;
	AP_BoxColour_SlotOrigin(colour, &x, &y);
	return &s_atlas[((y + ry) * AP_BOX_ATLAS_W + x + rx) * 4];
}

static void fill(unsigned char *p, int n, int r, int g, int b)
{
	int i;
	for (i = 0; i < n; i++)
	{
		p[i * 4 + 0] = (unsigned char)r;
		p[i * 4 + 1] = (unsigned char)g;
		p[i * 4 + 2] = (unsigned char)b;
		p[i * 4 + 3] = 255;
	}
}

static void test_build_slot(void)
{
	const unsigned char *p;
	int c;

	fill(s_builtin, 128 * 64, 217, 109, 179); // built-in pink
	fill(s_wood, 16 * 16, 189, 66, 0);        // orange disc wood
	fill(s_crate, 64 * 64, 132, 41, 0);       // orange disc crate
	memset(s_mask, 0, sizeof s_mask);
	s_mask[10 * 8 + 2] = 1 << 4; // one logo pixel at (20, 10)
	s_builtin[(10 * 128 + 16 + 20) * 4 + 0] = 7; // marked so the logo pixel is recognisable

	// with the disc
	memset(s_atlas, 0, sizeof s_atlas);
	for (c = 0; c < AP_BOX_COLOUR_COUNT; c++)
		AP_BoxColour_BuildSlot(s_atlas, c, s_wood, s_crate, s_builtin, s_builtin + 16 * 4, 128, s_mask);

	p = atlas_px(AP_BOX_COLOUR_PINK, 3, 3);
	expect(p[0] > p[1] + 40 && p[2] > p[1] + 40, 1, "disc: pink wood is pink");
	p = atlas_px(AP_BOX_COLOUR_FILLER, 3, 3);
	expect(p[0] == 0 && p[1] > 0 && p[1] == p[2], 1, "disc: filler wood is cyan");
	p = atlas_px(AP_BOX_COLOUR_FILLER, AP_BOX_SLOT_FACE_X + 1, 1);
	expect(p[0] == 0 && p[1] == p[2], 1, "disc: filler face background is the cyan crate");
	p = atlas_px(AP_BOX_COLOUR_TRAP, AP_BOX_SLOT_FACE_X + 1, 1);
	expect(p[0] > p[1] && p[1] > p[2], 1, "disc: trap face background is salmon");
	for (c = 0; c < AP_BOX_COLOUR_COUNT; c++)
	{
		p = atlas_px(c, AP_BOX_SLOT_FACE_X + 20, 10);
		expect(p[0], 7, "disc: the logo pixel keeps the art in every colour");
		p = atlas_px(c, AP_BOX_SLOT_FACE_X + 1, 1);
		expect(p[3], 255, "disc: face opaque");
	}
	p = atlas_px(AP_BOX_COLOUR_PROGRESSION, 3, 3);
	expect(p[2] > p[1] && p[0] > p[1], 1, "disc: progression wood is plum");

	// without the disc
	memset(s_atlas, 0, sizeof s_atlas);
	for (c = 0; c < AP_BOX_COLOUR_COUNT; c++)
		AP_BoxColour_BuildSlot(s_atlas, c, 0, 0, s_builtin, s_builtin + 16 * 4, 128, s_mask);
	p = atlas_px(AP_BOX_COLOUR_PINK, 3, 3);
	expect(p[0] == 217 && p[1] == 109 && p[2] == 179, 1, "fallback: pink border exactly as built in");
	p = atlas_px(AP_BOX_COLOUR_PINK, AP_BOX_SLOT_FACE_X + 1, 1);
	expect(p[0] == 217 && p[1] == 109 && p[2] == 179, 1, "fallback: pink face exactly as built in");
	p = atlas_px(AP_BOX_COLOUR_USEFUL, 3, 3);
	expect(p[2] > p[1] && p[1] > p[0], 1, "fallback: useful border recoloured slateblue");
	p = atlas_px(AP_BOX_COLOUR_USEFUL, AP_BOX_SLOT_FACE_X + 1, 1);
	expect(p[2] > p[1] && p[1] > p[0], 1, "fallback: useful face recoloured slateblue");
	p = atlas_px(AP_BOX_COLOUR_USEFUL, AP_BOX_SLOT_FACE_X + 20, 10);
	expect(p[0], 7, "fallback: logo pixel keeps the art");

	// the wood-only case (face read failed): disc wood, built-in face
	memset(s_atlas, 0, sizeof s_atlas);
	AP_BoxColour_BuildSlot(s_atlas, AP_BOX_COLOUR_PINK, s_wood, 0, s_builtin, s_builtin + 16 * 4, 128, s_mask);
	p = atlas_px(AP_BOX_COLOUR_PINK, AP_BOX_SLOT_FACE_X + 1, 1);
	expect(p[0] == 217 && p[1] == 109 && p[2] == 179, 1, "wood only: pink face is the built-in face");
	p = atlas_px(AP_BOX_COLOUR_PINK, 3, 3);
	expect(p[0] > p[1] + 40, 1, "wood only: border is the recoloured disc wood");
}

int main(void)
{
	test_pick();
	test_override();
	test_colours();
	test_slots();
	test_recolour_to();
	test_build_slot();
	if (g_fail)
		return 1;
	printf("test-box-colour: all assertions held\n");
	return 0;
}
