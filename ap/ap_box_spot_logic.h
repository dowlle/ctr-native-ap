#ifndef AP_BOX_SPOT_LOGIC_H
#define AP_BOX_SPOT_LOGIC_H

// Hand-placed AP box spots (alpha3 row A3-14), the freestanding arithmetic.
//
// A track with a spot file in the apworld (all 18 retail tracks, and a named
// custom track whose LEV the file pins) carries its picked spots in the plan:
// `item_boxes.spot_sets` names the numbers, and each box check carries its
// `spot` (number, position, angle). The client stands the box at that position
// and hides no weapon crate (ruled 2026-11-06). These helpers re-derive each
// spot's location code from its track and number so a plan that names a code
// the number does not give is refused.
//
// Numbers are permanent per track, 1 to 50 (ruled 2026-11-06):
//   * retail: 1 to 15 keep the frozen block 35014000 + 15 x track + (n - 1);
//     16 to 50 are the alpha3 block 35014300 + 35 x track + (n - 16);
//   * named custom block: offset 19 + n, so 1 to 15 are the old Item Box
//     offsets 20 to 34 and 16 to 50 are 35 to 69.
// `track` is the apworld box-track index (BOX_TRACKS order), not a LevelID.

#define AP_BOX_SPOT_MAX_NUMBER      50
#define AP_BOX_SPOT_PICK            15
#define AP_BOX_SPOT_RETAIL_BASE     35014000L
#define AP_BOX_SPOT_RETAIL_STRIDE   15
#define AP_BOX_SPOT_RETAIL_EXT_BASE 35014300L
#define AP_BOX_SPOT_RETAIL_EXT_STRIDE 35
#define AP_BOX_SPOT_RETAIL_TRACKS   18
#define AP_BOX_SPOT_NAMED_OFFSET_0  19

// The location code of retail Item Box <number> on box track `track`, or -1.
static inline long AP_BoxSpot_RetailCode(int track, int number)
{
	if (track < 0 || track >= AP_BOX_SPOT_RETAIL_TRACKS || number < 1 || number > AP_BOX_SPOT_MAX_NUMBER)
		return -1;
	if (number <= AP_BOX_SPOT_RETAIL_STRIDE)
		return AP_BOX_SPOT_RETAIL_BASE + (long)track * AP_BOX_SPOT_RETAIL_STRIDE + (number - 1);
	return AP_BOX_SPOT_RETAIL_EXT_BASE + (long)track * AP_BOX_SPOT_RETAIL_EXT_STRIDE + (number - 16);
}

// The named-block location offset of Item Box <number>, or -1.
static inline int AP_BoxSpot_NamedOffset(int number)
{
	if (number < 1 || number > AP_BOX_SPOT_MAX_NUMBER)
		return -1;
	return AP_BOX_SPOT_NAMED_OFFSET_0 + number;
}

// 1 when `picked` is a valid pick from a pool of `pool` eligible spots:
// strictly ascending numbers 1 to 50, all of the pool when it holds 15 or
// fewer, else exactly 15. Which numbers are eligible only the apworld knows
// (the files ship there); the client checks the shape.
static inline int AP_BoxSpot_PickShape(int pool, const int *picked, int count)
{
	int i;
	int want = pool < AP_BOX_SPOT_PICK ? pool : AP_BOX_SPOT_PICK;

	if (pool < 0 || pool > AP_BOX_SPOT_MAX_NUMBER || count != want || (count > 0 && picked == 0))
		return 0;
	for (i = 0; i < count; i++)
	{
		if (picked[i] < 1 || picked[i] > AP_BOX_SPOT_MAX_NUMBER)
			return 0;
		if (i > 0 && picked[i] <= picked[i - 1])
			return 0;
	}
	return 1;
}

#endif // AP_BOX_SPOT_LOGIC_H
