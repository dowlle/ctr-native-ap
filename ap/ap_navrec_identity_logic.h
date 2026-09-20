#ifndef AP_NAVREC_IDENTITY_LOGIC_H
#define AP_NAVREC_IDENTITY_LOGIC_H

// ============================================================================
// Which track's recordings a level load may replay, decided from the serving
// facts alone.
//
// FREESTANDING, like ap_navrec_format.h and ap_navrec_lane_logic.h: no engine
// headers, no config, no logging. game/BOTS.c gathers the facts from the
// CustomTrack_* predicates and calls this; tools/test-navrec.c drives the same
// function with facts held in memory, so the rule the client runs is the rule
// the harness asserts.
//
// The problem it solves (issue #356): a custom track is served through a
// BORROWED retail slot, so gGT->levelID during a custom race is the host
// track's ID. Recordings are filed and matched by that ID, so a load that
// resolves to the retail identity replays the host track's lines on custom
// geometry, and stamps laps driven on custom geometry as the host track's.
//
// Three predicates say a load is not the retail track for its slot, and a
// consumer that asks only some of them gets exactly that bug for the loads it
// left out. They are gathered here once:
//
//   eventRaceServing   the gem-cup event race is serving custom bytes
//                      (CustomTrack_ServingLoad)
//   oxideFinalServing  N. Oxide's Final Challenge is serving the bundled pair
//                      (CustomTrack_OxideFinalServing)
//   cortexTrackIntent  the load was admitted as the Cortex Vortex pad track
//                      (CustomTrack_CortexTrackIntent), which is an INTENT and
//                      not a bytes predicate on purpose: a race the entry site
//                      admitted as Cortex Vortex must never credit or replay
//                      Oxide Station, even if the bytes were refused later.
//
// Only the event race carries a recording identity today: the NAV3 UUID in
// config.ini describes the custom track PACKAGE bound by the seed, and the
// bundled Cortex Vortex pair is not that package. A served load with no
// identity of its own is BLOCKED rather than retail, which is the difference
// between "no recorded lanes here" and "the host slot's recorded lanes here".
// ============================================================================

#include <stddef.h> // NULL

// The three answers, in the order of decreasing information.
#define AP_NAVREC_LOAD_RETAIL  0 // this slot's own retail track: its recordings apply
#define AP_NAVREC_LOAD_CUSTOM  1 // custom bytes with a NAV3 identity: match and stamp by UUID
#define AP_NAVREC_LOAD_BLOCKED 2 // custom bytes without one: no lanes, no recording, no corridor

struct AP_NavRecLoadFacts
{
	int eventRaceServing;      // CustomTrack_ServingLoad
	int eventRaceNavIdentity;  // CustomTrack_NavIdentityForLoad filled a UUID
	int oxideFinalServing;     // CustomTrack_OxideFinalServing
	int cortexTrackIntent;     // CustomTrack_CortexTrackIntent
};

static inline int AP_NavRecIdentity_ForLoad(const struct AP_NavRecLoadFacts *f)
{
	if (f == NULL)
		return AP_NAVREC_LOAD_RETAIL;

	// A UUID is only ever handed over for a load the byte-serving predicate
	// already admitted (CustomTrack_NavIdentityForLoad asks ShouldServe first),
	// so identity and geometry cannot disagree. Checking eventRaceServing here
	// as well keeps that true even if a caller gathers the two separately.
	if (f->eventRaceServing && f->eventRaceNavIdentity)
		return AP_NAVREC_LOAD_CUSTOM;

	if (f->eventRaceServing || f->oxideFinalServing || f->cortexTrackIntent)
		return AP_NAVREC_LOAD_BLOCKED;

	return AP_NAVREC_LOAD_RETAIL;
}

// May recorded lanes be loaded and pointed at the bots for this load?
static inline int AP_NavRecIdentity_LanesMayLoad(int outcome)
{
	return outcome != AP_NAVREC_LOAD_BLOCKED;
}

// May a lap driven on this load be written to a file?
static inline int AP_NavRecIdentity_MayRecord(int outcome)
{
	return outcome != AP_NAVREC_LOAD_BLOCKED;
}

// May the level's own nav table be snapshotted as the corridor that classifies
// a recorded lap's shortcut flags? Only where a lap can be written at all: a
// blocked load records nothing, and the corridor it would take belongs to a
// track whose identity this build cannot name.
static inline int AP_NavRecIdentity_CorridorApplies(int outcome)
{
	return outcome != AP_NAVREC_LOAD_BLOCKED;
}

#endif // AP_NAVREC_IDENTITY_LOGIC_H
