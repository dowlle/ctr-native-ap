#ifndef AP_CUP_BOX_POLICY_H
#define AP_CUP_BOX_POLICY_H

// Gem Cup AP-box access policy (WO-A3, ruled 2026-08-24 10:51 CEST), and the
// hub-spine Key table it needs. Deliberately freestanding, exactly like
// ap_pad_state.h: the gather lives in engine (ap_boxes.c), the DECISION lives
// here so tools/test-cup-box-policy.c can pin the whole truth table out of
// engine, with no disc, no display and no seed.
//
// THE RULING, in one sentence: access to a Gem Cup grants no AP-box logic. A
// leg shows, collides with and dispatches its authored AP boxes only while the
// corresponding INDIVIDUAL race is accessible through its randomized physical
// pad. One cup may therefore mix legs with boxes and legs without them.
//
// WHY A SEPARATE PREDICATE AND NOT ctr_cfg_warp_unlocked. That helper is the
// pad's ITEM gate: racer lock ANDed onto the pad's stage-1 requirement
// (ap_hooks.c, ctr_cfg_warp_unlocked). It contains no Key term and no hub term
// at all, so it answers "would this pad open if I were standing at it", not
// "can I get to it". A race pad in Citadel City with a met stage-1 reads
// unlocked through that helper on a zero-Key file, while the player cannot
// physically leave N. Sanity Beach. Structural hub reachability therefore has
// to be ANDed on top, and the authoritative predicate for it is the Key spine
// below -- the same table ap_verify.c sweeps seed completability with, which is
// why that table now lives here and is included there rather than copied.
//
// Keys required to physically STAND at a pad: the minimum received-Key count
// that opens every hub door between the N. Sanity Beach spawn and that pad's
// hub. Mirrors AH_Door.c (NSB->GSV 1 key, NSB->Glacier doorID 4 = 2 keys, other
// doors D232.arrKeysNeeded = {2,1,2,3,4}) and matches the apworld's world.json
// hub spine: N. Sanity 0, Gem Stone Valley 1, Lost Ruins 1, Glacier Park 2,
// Citadel City 3, Cups Room 2. Keyed by PHYSICAL pad LevelID; -1 = not an
// adventure warp pad (battle maps 20/22/24..27).
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

#define AP_HUB_PAD_COUNT     28
#define AP_HUB_CUP_KEYS      2 // Cups Room (physical cup pads 100..104)
#define AP_HUB_CUP_PAD_FIRST 100
#define AP_HUB_CUP_PAD_LAST  104

static const int AP_HubPadKeys[AP_HUB_PAD_COUNT] = {
	/*  0 Dingo Canyon   (Lost Ruins)   */ 1,
	/*  1 Dragon Mines   (Glacier Park) */ 2,
	/*  2 Blizzard Bluff (Glacier Park) */ 2,
	/*  3 Crash Cove     (N. Sanity)    */ 0,
	/*  4 Tiger Temple   (Lost Ruins)   */ 1,
	/*  5 Papu's Pyramid (Lost Ruins)   */ 1,
	/*  6 Roo's Tubes    (N. Sanity)    */ 0,
	/*  7 Hot Air Skyway (Citadel City) */ 3,
	/*  8 Sewer Speedway (N. Sanity)    */ 0,
	/*  9 Mystery Caves  (N. Sanity)    */ 0,
	/* 10 Cortex Castle  (Citadel City) */ 3,
	/* 11 N. Gin Labs    (Citadel City) */ 3,
	/* 12 Polar Pass     (Glacier Park) */ 2,
	/* 13 Oxide Station  (Citadel City) */ 3,
	/* 14 Coco Park      (Lost Ruins)   */ 1,
	/* 15 Tiny Arena     (Glacier Park) */ 2,
	/* 16 Slide Coliseum (Gem Stone V.) */ 1,
	/* 17 Turbo Track    (Gem Stone V.) */ 1,
	/* 18 Nitro Court    (Citadel City) */ 3,
	/* 19 Rampage Ruins  (Lost Ruins)   */ 1,
	/* 20 (battle map)                  */ -1,
	/* 21 Skull Rock     (N. Sanity)    */ 0,
	/* 22 (battle map)                  */ -1,
	/* 23 Rocky Road     (Glacier Park) */ 2,
	/* 24..27 (battle maps)             */ -1, -1, -1, -1,
};

// Received Keys needed to stand at physical pad `physPad`, or -1 when that id
// is not an adventure warp pad at all. Accepts the full shuffle ID space, so a
// caller may hand it whatever ctr_cfg_warp_phys returned without a range test
// of its own.
static inline int AP_HubKeysForPad(int physPad)
{
	if (physPad >= AP_HUB_CUP_PAD_FIRST && physPad <= AP_HUB_CUP_PAD_LAST)
		return AP_HUB_CUP_KEYS;
	if (physPad < 0 || physPad >= AP_HUB_PAD_COUNT)
		return -1;
	return AP_HubPadKeys[physPad];
}

// Is the individual race behind physical pad `physPad` GENUINELY accessible
// right now: reachable through the hub geography AND enterable once there?
//
// Three terms, all required, and each one is a distinct failure mode a mixed
// cup can show at the same moment:
//   * hub          received Keys open every door between the spawn and the
//                  pad's hub (this predicate's own contribution),
//   * stage1Met    the pad's own stage-1 requirement, randomized or the class
//                  fallback -- gathered from AP_PadStage1Met, which routes
//                  race / trial / arena / cup pad classes,
//   * racerMet     the pad's racer lock (#54/#209), gathered from
//                  ctr_cfg_racer_lock_met, which reports met for an unlocked
//                  pad so it can be ANDed unconditionally.
//
// Stage 2 is deliberately absent. Boxes are stage-1 locations on the track's
// own region; the relic / token tier-2 menu gates nothing about them.
static inline int AP_BoxPadAccessible(int physPad, int keysOwned,
                                      int stage1Met, int racerMet)
{
	int keysNeeded = AP_HubKeysForPad(physPad);

	if (keysNeeded < 0)
		return 0; // not an adventure pad: no individual route to this track
	if (keysOwned < keysNeeded)
		return 0; // structurally behind a shut hub door
	if (!stage1Met)
		return 0;
	if (!racerMet)
		return 0;
	return 1;
}

// THE policy. One call decides whether the AP boxes authored on the track being
// raced may stand, collide and dispatch, so the visuals, the collision walk and
// the check emission cannot disagree with each other -- hiding a model while
// its check stays earnable is exactly the divergence this replaces.
//
// isCupLeg = 0 keeps every non-cup race byte-for-byte on the Alpha 4 rule:
// ordinary Adventure races, boss races and Relic Races are unaffected by this
// policy and never consult the pad terms at all.
static inline int AP_BoxPolicyAllows(int isCupLeg, int physPad, int keysOwned,
                                     int stage1Met, int racerMet)
{
	if (!isCupLeg)
		return 1;
	return AP_BoxPadAccessible(physPad, keysOwned, stage1Met, racerMet);
}

#endif // CTR_AP
#endif // AP_CUP_BOX_POLICY_H
