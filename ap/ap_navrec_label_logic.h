#ifndef AP_NAVREC_LABEL_LOGIC_H
#define AP_NAVREC_LABEL_LOGIC_H

// Depth bands proven by Penguin-MODSK's Bot_Trackrom overhead-name renderer,
// expressed as a freestanding helper so boundary behavior stays testable.
//
// UNITS. `depth` is SZ3 straight out of gte_rtps. GTE_RotTransPers computes
// SZ3 = MAC3 >> 12 (platform/native_gte_core.c:330, Lm_D with sf 1), and MAC3 is
// TRZ << 12 plus the rotation row applied to the input vector, so SZ3 is
// view-space Z in the SAME units as the vector handed to the GTE. The caller
// projects posCurr >> 8, which makes one unit of depth one NavFrame unit: the
// space retail nav lanes are measured in, where lanes sit about 500 apart
// (AP_NAVREC_FALLBACK_LANE_OFFSET) and node spacing reaches 499.
//
// DRAW-DISTANCE CAP. 906 is the top of the measured ladder, and it is now where
// labels stop rather than the floor of an unbounded band. Two things put the cut
// there.
//
// The ladder itself. Widths step 13, 12, 11, 10, 9 across five bands measured at
// real distances, then drop two sizes at once to 7, skipping 8, for a band with
// no upper bound at all. That last entry is a catch-all rather than a measured
// step, and it is the only band whose text does not shrink with distance: every
// bot from 906 out to the SZ3 clamp at 0xFFFF drew the same 7-pixel name. The
// last band the ladder actually measures is the 9 that ends at 906.
//
// The scale it sits in. The 1P projection uses distanceToScreen 0x1c2 (450), and
// MainFrame_RenderFrame.c derives the level's own depth landmarks from it in
// these same units: recursive-near 1575, top-level near 3150, texture LOD 5400
// and 10800, BSP LOD switch 11700. The whole label ladder, 150 to 906, ends below
// even the nearest of those, while the old catch-all went on drawing names well
// past the depth at which the engine has already dropped the world itself to a
// coarser LOD. Against the lane separation above: 906 is under two lane widths of
// view depth, 11700 is over twenty.
//
// The cut is hard rather than a fade because there is nothing here to fade. These
// bands control font width, and DecalFont_DrawLine takes a packed style word
// carrying a colour constant, not an alpha, so the only ramp available is size,
// and shrinking below 7 is not a fade into the distance, it is an unreadable
// name. Returning 0 reuses the hidden path the caller already honours for the
// near clip.
#define AP_NAVREC_LABEL_MAX_DEPTH 906

static int AP_NavRec_LabelWidthForDepth(int depth)
{
	if (depth < 150)
		return 0;
	if (depth < 190)
		return 13;
	if (depth < 234)
		return 12;
	if (depth < 331)
		return 11;
	if (depth < 475)
		return 10;
	if (depth < AP_NAVREC_LABEL_MAX_DEPTH)
		return 9;
	return 0;
}

#endif // AP_NAVREC_LABEL_LOGIC_H
