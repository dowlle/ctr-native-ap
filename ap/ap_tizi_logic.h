#ifndef AP_TIZI_LOGIC_H
#define AP_TIZI_LOGIC_H

// ---------------------------------------------------------------------------
// Pure, engine-independent rules for the #223 Tizi Helper.
//
// Two decisions live here, both free of the engine, the network and the config
// so tools/test-tizi-helper.c can drive every case a build machine with no disc
// can never reach:
//
//   1. IS THE HELPER ACTIVE -- the ruled itemsanity truth table.
//   2. WHICH FOUR BOXES -- the first row after the Papu's Pyramid start line,
//      selected from per-crate lap progress rather than from hardcoded indices
//      or coordinates nobody on this side can verify.
//
// Why (2) is a rule and not a table. #223 item 8 says to verify the hook against
// the CURRENT Papu's Pyramid box identities before treating the mapping as
// implementation-ready, and no LEV data ships in this repo -- the boxes live in
// the disc's level file. A hardcoded "instances 0..3" or a coordinate box would
// be a guess with no way to fail loudly if it is wrong. So the selection is
// derived at runtime from data the level itself carries (the checkpoint node
// chain's distance-to-finish), and it REFUSES rather than guesses whenever the
// layout does not match the shape the rule describes: four crates, together, in
// the opening stretch, clearly separated from the next crate. A refusal leaves
// every box vanilla and logs the census it saw, which is the evidence an
// in-game verification pass needs.
// ---------------------------------------------------------------------------

#define AP_TIZI_ROW_SIZE 4

// Selection outcomes. Anything other than OK means "stay vanilla".
enum
{
	AP_TIZI_ROW_OK = 0,
	// Fewer than four weapon crates in the opening stretch. Either the track
	// has no such row, or the caller collected nothing.
	AP_TIZI_ROW_TOO_FEW,
	// The four leaders are not a row: they are spread further apart along the
	// lap than a side-by-side set of boxes can be.
	AP_TIZI_ROW_NOT_A_ROW,
	// A fifth crate sits as close to the four as they sit to each other, so
	// "the first row of FOUR" is ambiguous on this layout.
	AP_TIZI_ROW_AMBIGUOUS,
	// The level reported no usable track length, so no relative tolerance can
	// be derived and every threshold below would be meaningless.
	AP_TIZI_ROW_NO_TRACK_LENGTH
};

// How much of the lap counts as "immediately after the start line". distToFinish
// starts at the track length on the start line and counts DOWN, so the opening
// stretch is the high end. An eighth of a lap is generous for a first box row
// and still refuses anything sitting mid-track.
#define AP_TIZI_OPENING_FRACTION 8

// How far apart, along the lap, four boxes of one row may read. Checkpoint nodes
// are sparse, so a side-by-side row usually resolves to a single node and a
// spread of zero; a 64th of a lap absorbs a row that straddles two nodes without
// letting a genuinely separate box join it.
#define AP_TIZI_ROW_SPAN_FRACTION 64

// The ruled activation gate (Stef, 2026-08-10; #223 items 3 and 4).
//
//   itemsanity off -> the helper item alone activates.
//   itemsanity on  -> the helper item AND the separate Mask weapon item.
//
// Everything is an int flag so the harness can drive the full 2x2x2 table.
static int AP_TiziHelperActive(int helperReceived, int itemsanityOn,
	int maskReceived)
{
	if (!helperReceived)
		return 0;
	if (itemsanityOn && !maskReceived)
		return 0;
	return 1;
}

// Select the first row of AP_TIZI_ROW_SIZE crates after the start line.
//
// `progress[i]` is crate i's distance-to-finish, taken from the checkpoint node
// nearest that crate: HIGH near the start line, falling toward the finish.
// `trackLength` is restart point 0's distance-to-finish, which the engine itself
// treats as the lap length. `outIndex` receives the chosen crate indices in
// track order (first encountered first) on AP_TIZI_ROW_OK, and is untouched
// otherwise.
//
// O(n * ROW_SIZE) with no allocation and no sort: n is the crate count on one
// track, and this runs once per level load.
static int AP_TiziSelectRow(const long *progress, int count, long trackLength,
	int *outIndex)
{
	long opening;
	long span;
	int chosen[AP_TIZI_ROW_SIZE];
	int used[AP_TIZI_ROW_SIZE];
	long best;
	int bestIndex;
	int picked;
	int i;
	int k;
	int fifth = -1;
	long fifthProgress = 0;

	if (trackLength <= 0)
		return AP_TIZI_ROW_NO_TRACK_LENGTH;

	opening = trackLength - trackLength / AP_TIZI_OPENING_FRACTION;
	span = trackLength / AP_TIZI_ROW_SPAN_FRACTION;

	// Pick the four highest progress values inside the opening stretch, by
	// repeated max: the row is four entries, so a sort would cost more code
	// than it saves.
	for (picked = 0; picked < AP_TIZI_ROW_SIZE; picked++)
	{
		bestIndex = -1;
		best = 0;
		for (i = 0; i < count; i++)
		{
			if (progress[i] < opening)
				continue;
			for (k = 0; k < picked; k++)
				if (chosen[k] == i)
					break;
			if (k < picked)
				continue;
			if (bestIndex < 0 || progress[i] > best)
			{
				best = progress[i];
				bestIndex = i;
			}
		}
		if (bestIndex < 0)
			return AP_TIZI_ROW_TOO_FEW;
		chosen[picked] = bestIndex;
		used[picked] = bestIndex;
	}

	// A row, not a scatter: the four must sit within one row span of each other.
	if (progress[chosen[0]] - progress[chosen[AP_TIZI_ROW_SIZE - 1]] > span)
		return AP_TIZI_ROW_NOT_A_ROW;

	// And the row must be unambiguous: the next crate in the opening stretch,
	// if there is one, has to be clearly behind it.
	for (i = 0; i < count; i++)
	{
		if (progress[i] < opening)
			continue;
		for (k = 0; k < AP_TIZI_ROW_SIZE; k++)
			if (used[k] == i)
				break;
		if (k < AP_TIZI_ROW_SIZE)
			continue;
		if (fifth < 0 || progress[i] > fifthProgress)
		{
			fifth = i;
			fifthProgress = progress[i];
		}
	}
	if (fifth >= 0 &&
	    progress[chosen[AP_TIZI_ROW_SIZE - 1]] - fifthProgress <= span)
		return AP_TIZI_ROW_AMBIGUOUS;

	for (k = 0; k < AP_TIZI_ROW_SIZE; k++)
		outIndex[k] = chosen[k];
	return AP_TIZI_ROW_OK;
}

#endif // AP_TIZI_LOGIC_H
