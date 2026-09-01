#ifndef AP_PAD_STATE_H
#define AP_PAD_STATE_H

// The Warp-Pad State Model v2 decision, deliberately freestanding.
//
// AP_PadState (ap_hooks.c) gathers lifecycle facts about a pad from the seed
// config, location table and runtime maps; this header turns those facts into
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
// Arguments (all booleans except the final two counts):
//   destIsRace     the destination is one of the 16 shuffleable race tracks,
//                  the only category carrying the full two-stage lifecycle
//   stage1Met      the PHYSICAL pad's item requirement is satisfied
//   racerMet       the PHYSICAL pad's racer requirement is satisfied
//   trophyChecked  the DESTINATION track's Trophy Race location is checked
//   stage2Met      the PHYSICAL pad's stage-2 requirement is satisfied
//                  (a pad with no stage-2 requirement reports met)
//   uncCount       still-unchecked reward locations behind the destination
//                  (tier bits + podium rungs; cup pads aggregate their legs)
//   reRaceChecksStanding
//                  checks that require keeping a plain race route open before
//                  stage 2: unbroken AP boxes plus an unchecked per-track Wumpa
//                  check. They carry no AdvProgress bit and cannot identify the
//                  required route through uncCount alone.
static inline int AP_PadStateDecide(int destIsRace, int stage1Met, int racerMet,
                                    int trophyChecked, int stage2Met,
                                    int uncCount, int reRaceChecksStanding)
{
	// Done is terminal: every location behind the pad is settled. Hard-locking
	// it can never gate progression, which is only true while "nothing left"
	// also counts checks without an AdvProgress bit, including boxes and Wumpa.
	if (uncCount == 0 && reRaceChecksStanding == 0)
		return 5;

	if (!stage1Met || !racerMet)
		return 1;

	// Reduced lifecycle (trial / arena / cup destination): stage-1 met and
	// something is left, so the pad is simply enterable.
	if (!destIsRace)
		return 2;

	if (!trophyChecked)
		return 2; // the trophy race, this pad's primary check, is still available

	if (!stage2Met)
	{
		// Never Re-lock while a check still needs the phase-1 plain race.
		// AP boxes and per-track Wumpa both fire there before stage 2, so the
		// pad stays Raceable until they are settled. Issue #232 demonstrated
		// the failure mode: a green state without a matching entry route strands
		// locations behind the pad.
		if (reRaceChecksStanding > 0)
			return 2;
		return 3;
	}

	return 4; // relic Time Trial / CTR Token checks available
}

enum AP_PadTier2Route
{
	AP_PAD_TIER2_MENU,
	AP_PAD_TIER2_TOKEN,
	AP_PAD_TIER2_RELIC,
	AP_PAD_TIER2_BOX_RERACE,
	AP_PAD_TIER2_DONE
};

// The CTR Challenge side remains useful while its Token, any Lettersanity
// letter, or the track-owned Wumpa check is unchecked. Kept freestanding so the
// lifecycle harness pins the Wumpa-only case that originally stranded the check.
static inline int AP_PadTokenSideLeft(int tokenLeft, int lettersLeft, int wumpaLeft)
{
	return tokenLeft || lettersLeft || wumpaLeft;
}

// Count unchecked track-owned Wumpa locations across a Cup's four legs.
// `trackLeft` is parallel to `tracks`; repeated track ids are alternative
// occurrences of one location and therefore count once. The mask is sufficient
// for the current 18 retail destinations and deliberately rejects ids >= 32.
static inline int AP_PadCupWumpaCount(const int tracks[4], const int trackLeft[4],
                                     int trackCount)
{
	unsigned int seen = 0;
	int leg;
	int count = 0;

	for (leg = 0; leg < 4; leg++)
	{
		int track = tracks[leg];
		unsigned int bit;
		if (track < 0 || track >= trackCount || track >= 32)
			continue;
		bit = 1u << track;
		if (seen & bit)
			continue;
		seen |= bit;
		if (trackLeft[leg])
			count++;
	}
	return count;
}

// Once stage 2 is open, choose only an entry path that can still produce a
// check. Boxes are not represented by the token/relic bits, so the all-tier-
// checked case needs its own plain adventure re-race instead of a dead menu.
static inline int AP_PadTier2RouteDecide(int tokenLeft, int relicLeft, int boxesStanding)
{
	if (tokenLeft && relicLeft)
		return AP_PAD_TIER2_MENU;
	if (tokenLeft)
		return AP_PAD_TIER2_TOKEN;
	if (relicLeft)
		return AP_PAD_TIER2_RELIC;
	if (boxesStanding > 0)
		return AP_PAD_TIER2_BOX_RERACE;
	return AP_PAD_TIER2_DONE;
}

// Diagnostic route codes for AP_PadLogRoute (ap_hooks.c, issues #232 / #265).
// The two gates in AH_WarpPad.c that can route an AP pad report which branch
// they took through these; the tier-2 half is AP_PadTier2Route shifted by
// AP_PAD_ROUTE_TIER2_BASE, so the tier-2 gate logs the decision it already made
// instead of re-deriving it. The base is 16, far above the five tier-2 values,
// so the two halves cannot collide even if either grows.
//
// Kept here beside the decision it labels, not in ap_hooks.h, so
// tools/test-box-map.c can pin the mapping out of engine like the rest of the
// state model.
enum AP_PadRoute
{
	AP_PAD_ROUTE_S2LOCKED_PLAIN_RERACE = 0, // stage 2 locked, box/Wumpa -> plain re-race
	AP_PAD_ROUTE_S2LOCKED_INERT      = 1, // stage 2 locked, nothing re-raceable -> inert
	AP_PAD_ROUTE_TIER2_BASE          = 16 // + enum AP_PadTier2Route
};

#endif // CTR_AP
#endif // AP_PAD_STATE_H
