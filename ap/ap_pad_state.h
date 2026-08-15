#ifndef AP_PAD_STATE_H
#define AP_PAD_STATE_H

// The Warp-Pad State Model v2 decision, deliberately freestanding.
//
// AP_PadState (ap_hooks.c) gathers five facts about a pad from the seed config,
// the location table and the box map; this header turns those five facts into
// the state. Splitting the gather from the decision is what lets
// tools/test-box-map.c pin the whole state table out of engine -- including the
// §6 box cell, which is the one that decides whether a pad is enterable while
// unbroken item boxes remain (issue #232).
//
// Every consumer of the model (the map colour in AH_Map.c, the pad's look in
// AH_WarpPad_BuildInstances, the entry gate in AH_WarpPad_ThTick) goes through
// AP_PadState, so there is exactly one place this table is written down.
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

// The five states, as the model names them:
//   1 Locked      stage-1 unmet                                        RED
//   2 Raceable    stage-1 met, a check is available through entry      GREEN
//   3 Re-locked   race dest, trophy checked, stage-2 unmet             ORANGE
//   4 Tier-2 open race dest, stage-2 met, checks remain                PERIWINKLE
//   5 Done        nothing left behind the pad; HARD-LOCKED, pure UX    GRAY
//
// Arguments (all booleans except uncCount/boxesStanding, which are counts):
//   destIsRace     the destination is one of the 16 shuffleable race tracks,
//                  the only category carrying the full two-stage lifecycle
//   stage1Met      the PHYSICAL pad's entry requirement is satisfied
//   trophyChecked  the DESTINATION track's Trophy Race location is checked
//   stage2Met      the PHYSICAL pad's stage-2 requirement is satisfied
//                  (a pad with no stage-2 requirement reports met)
//   uncCount       still-unchecked reward locations behind the destination
//                  (tier bits + podium rungs; cup pads aggregate their legs)
//   boxesStanding  unbroken AP item boxes behind the destination (same
//                  aggregation), which carry no AdvProgress bit and so cannot
//                  ride in uncCount
static inline int AP_PadStateDecide(int destIsRace, int stage1Met, int trophyChecked,
                                    int stage2Met, int uncCount, int boxesStanding)
{
	// Done is terminal: every location behind the pad is settled. Hard-locking
	// it can never gate progression, which is only true while "nothing left"
	// also counts the boxes.
	if (uncCount == 0 && boxesStanding == 0)
		return 5;

	if (!stage1Met)
		return 1;

	// Reduced lifecycle (trial / arena / cup destination): stage-1 met and
	// something is left, so the pad is simply enterable.
	if (!destIsRace)
		return 2;

	if (!trophyChecked)
		return 2; // the trophy race, this pad's primary check, is still available

	if (!stage2Met)
	{
		// §6: never Re-lock a pad with boxes still standing behind it.
		// Re-locked means "come back after stage 2", and for a box that is
		// simply wrong -- the box is breakable on any adventure race of this
		// track right now, so the pad stays Raceable and enterable until they
		// are gone. Issue #232 is what happens when a surface ignores this:
		// the map painted the pad green off this branch while the entry gate
		// refused, stranding another player's items behind the boxes.
		if (boxesStanding > 0)
			return 2;
		return 3;
	}

	return 4; // relic Time Trial / CTR Token checks available
}

#endif // CTR_AP
#endif // AP_PAD_STATE_H
