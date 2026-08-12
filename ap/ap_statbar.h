#ifndef AP_STATBAR_H
#define AP_STATBAR_H

// ---------------------------------------------------------------------------
// The Garage stat bar, shared with the hub character picker (#54/#209, #220).
//
// WHY THIS EXISTS. The picker used to draw a raw table -- a GLOBAL READ-ONLY
// header over ACCEL / SPEED / ACCSPD / TURN with the numbers and a LOW..HIGH
// word. The Garage shows the same information as six-segment coloured bars, and
// that is the presentation players already know, so the ruling is that the two
// surfaces look the same. Imitating the Garage would have left two drawings of
// one thing, free to drift on the first tweak to either; this is the Garage's
// own drawing, called from both places.
//
// WHAT IS SHARED AND WHAT IS NOT. The renderer and every number in its geometry
// live here. The seven-stop colour gradient is passed IN, because the Garage's
// live array is gGarage.barColors, overlay-233 state that no game/232/ file
// reads anywhere in the tree; the Garage passes its own array and the hub passes
// the AP copy in ap_statbar.c. So the palette is duplicated, once, with the
// source cited -- and the drawing, which is the part that can actually drift,
// is not.
//
// THE VANILLA BUILD IS UNTOUCHED. CS_Garage_MenuProc keeps its retail block
// under #else, so the vanilla binary is byte-identical (the tree's own check:
// build the two binaries and diff them; only the build id, commit hash and
// debug-link CRC may move). Only the AP build routes the Garage through here,
// which is the build where the Garage bars are already AP-driven -- the rank
// override from PR #218 replaced their lengths. Vanilla's copy is frozen retail
// code that this change does not edit.
//
// THE PURE HALF IS HERE SO IT CAN BE DRIVEN. The segment arithmetic decides how
// six fixed 13-pixel cells render a length that lands anywhere between them, and
// it is the part where an off-by-one is invisible on a build machine and obvious
// on a screen: a segment drawn one pixel long, or a bar whose last cell never
// fills. tools/test-character-persistence.cpp drives exactly these functions.
// ---------------------------------------------------------------------------

// Retail geometry, all from the block this was lifted out of
// (game/233/CS_Garage.c, the `for (i = 0; i < 3; i++)` bar draw).
#define AP_STATBAR_SEGMENTS 6      // six coloured cells per bar
#define AP_STATBAR_SEGMENT_LEN 13  // each one 13 pixels wide
#define AP_STATBAR_MAX_LEN 78      // 6 * 13: a full bar, and the fill ceiling
#define AP_STATBAR_HEIGHT 7        // outline box height
#define AP_STATBAR_ROW_PITCH 15    // Y step from one stat row to the next
#define AP_STATBAR_RATE 3          // pixels per frame the fill grows (BAR_RATE)

// How much of segment `index` is drawn for a bar of length `barLen`.
//
// Retail walks the six cells with a running start/end and clamps the cell the
// length falls inside, so a bar is a run of full cells plus at most one partial.
// Returns 0 for a cell entirely past the end. Faithful to the original in the
// two cases that are easy to get wrong: a length of exactly one cell boundary
// fills that cell and starts nothing, and a negative length draws nothing at all
// rather than wrapping (retail's own `if ((int)currSegmentLen << 0x10 < 0)`
// guard, which is a sign test on the low half).
static inline int AP_StatBar_SegmentLen(int barLen, int index)
{
	int segmentStart;
	int segmentEnd;
	int len;

	if ((index < 0) || (index >= AP_STATBAR_SEGMENTS))
		return 0;

	segmentStart = index * AP_STATBAR_SEGMENT_LEN;
	segmentEnd = segmentStart + AP_STATBAR_SEGMENT_LEN;

	len = AP_STATBAR_SEGMENT_LEN;
	if (barLen <= segmentEnd)
		len = barLen - segmentStart;

	if (len < 0)
		len = 0;

	// Retail only submits the quad when the cell ends inside the bar; past the
	// end there is nothing to draw.
	if (segmentStart + len > barLen)
		return 0;

	return len;
}

// One frame of the Garage's fill animation.
//
// Retail runs BOTH tests, not an if/else: the fill grows by three pixels a frame
// and snaps DOWN instantly. That asymmetry is the feel, so it is preserved
// exactly -- a bar that drops to a lower value does not slide back.
static inline int AP_StatBar_Step(int current, int target)
{
	if (current < target)
		current += AP_STATBAR_RATE;
	if (target < current)
		current = target;
	return current;
}

// Bar length for a raw stat value against the ladder it is judged on.
//
// The Garage has a rank 0..4 to place; the picker has the live resolver's actual
// engine value and the vanilla spread for that stat (the min and max across the
// four classes, which is what the picker's LOW..HIGH wording already read). Both
// end up as a length in 0..78. A value at or under the floor still shows one
// pixel of bar rather than an empty box, because an empty box reads as "no data"
// where the floor of the ladder is real information.
static inline int AP_StatBar_LenForValue(int value, int lo, int hi)
{
	int span = hi - lo;
	int len;

	if (span <= 0)
		return AP_STATBAR_MAX_LEN / 2; // nothing to rank against: sit mid-bar

	if (value <= lo)
		return 1;
	if (value >= hi)
		return AP_STATBAR_MAX_LEN;

	len = ((value - lo) * AP_STATBAR_MAX_LEN) / span;
	if (len < 1)
		len = 1;
	if (len > AP_STATBAR_MAX_LEN)
		len = AP_STATBAR_MAX_LEN;
	return len;
}

#ifndef AP_STATBAR_PURE_ONLY

struct PrimMem;

// Draw one bar: the white outline, the black inner shadow and the six coloured
// cells, at (x, y) for a fill of `barLen`. `colors` is the seven-stop gradient
// (each cell is a left/right pair of adjacent stops), `ot` the UI ordering table
// this submits into. Exactly the retail draw, with the arguments the retail code
// read out of gGarage and its locals.
void AP_StatBar_Draw(int x, int y, int barLen, const unsigned int *colors, void *ot, struct PrimMem *primMem);

// The seven-stop gradient the Garage uses, for callers that cannot reach
// gGarage. See ap_statbar.c.
extern const unsigned int AP_STATBAR_COLORS[7];

#endif // AP_STATBAR_PURE_ONLY

#endif // AP_STATBAR_H
