#ifndef AP_CUP_BOX_POLICY_H
#define AP_CUP_BOX_POLICY_H

// Alternate-route AP-box access policy (WO-A3, ruled 2026-08-24 10:51 CEST for
// Gem Cup legs; extended to boss races by the 2026-09-12 Discord ruling), and
// the hub-spine Key table it needs. Deliberately freestanding, exactly like
// ap_pad_state.h: the gather lives in engine (ap_boxes.c), the DECISION lives
// here so tools/test-cup-box-policy.c can pin the whole truth table out of
// engine, with no disc, no display and no seed.
//
// THE RULING, in one sentence: reaching a track by a route other than its own
// warp pad grants no AP-box logic. A Gem Cup leg or a boss race collides with
// and dispatches its track's authored AP boxes only while the corresponding
// INDIVIDUAL race is accessible through that track's randomized physical pad.
// One cup may therefore mix legs with collectable boxes and legs without them.
//
// Since issue #354 the refused case is still SHOWN: the boxes stand translucent
// and uncollectable, exactly as an unavailable Lettersanity letter does, so an
// empty-looking leg is no longer indistinguishable from a locked one. See the
// presentation enum at the bottom of this header. Collectability is unchanged.
//
// WHY BOSS RACES ARE THE SAME CASE. A boss race is entered from its hub garage,
// never from the track's warp pad, but it loads that track (Komodo Joe loads
// Dragon Mines) and through Alpha 4 it stood and dispatched that track's boxes
// on garage access alone. The apworld never put them in reach that way: a
// boss-derived entrance targets only the separate `<track>: Wumpa` region, and
// every item-box location stays parented to the track region reached through the
// physical pad. So the same four terms, and the same predicate, apply.
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
//
// Nothing here is cup-specific: a boss race asks the identical question about
// the track it loaded.
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

// How the current race reached the track whose boxes are being decided. Only the
// two ALTERNATE routes consult the pad terms; everything else is the track's own
// pad already, or a route this policy deliberately does not own.
//
// The two gated values are numerically 1 and 2 and OTHER is 0, so the older
// two-state isCupLeg calls (0 / 1) keep their exact meaning.
enum AP_BoxRaceRoute
{
	// The track's own warp pad, or a route outside this policy: ordinary
	// Adventure races, Relic Races, and the custom-track encounter overrides
	// (the event race, the custom Oxide final venue), whose host LevelID
	// resolves to a physical pad that has nothing to do with the race.
	AP_BOX_ROUTE_OWN_PAD = 0,
	AP_BOX_ROUTE_CUP_LEG = 1, // a Gem Cup leg (WO-A3)
	AP_BOX_ROUTE_BOSS    = 2, // a boss race entered from its hub garage
};

// Does this route have to prove the track's individual pad is open right now?
static inline int AP_BoxRouteIsAlternate(int route)
{
	return route == AP_BOX_ROUTE_CUP_LEG || route == AP_BOX_ROUTE_BOSS;
}

// THE policy. One call decides whether the AP boxes authored on the track being
// raced may stand, collide and dispatch, so the visuals, the collision walk and
// the check emission cannot disagree with each other -- hiding a model while
// its check stays earnable is exactly the divergence this replaces.
//
// AP_BOX_ROUTE_OWN_PAD keeps ordinary Adventure races, Relic Races and the
// custom encounter overrides byte-for-byte on the Alpha 4 rule: they never
// consult the pad terms at all. A boss race no longer takes that branch; the
// route argument, not the absence of ADVENTURE_CUP, is what decides.
static inline int AP_BoxPolicyAllows(int route, int physPad, int keysOwned,
                                     int stage1Met, int racerMet)
{
	if (!AP_BoxRouteIsAlternate(route))
		return 1;
	return AP_BoxPadAccessible(physPad, keysOwned, stage1Met, racerMet);
}

// WHAT THE ANSWER LOOKS LIKE ON SCREEN (issue #354). The policy above decides
// whether a box may be COLLECTED. Until #354 that decision also decided whether
// anything stood at all, so a refused Gem Cup leg was simply an empty track and
// players read it as a missing box rather than as a locked one -- the Discord
// confusion the issue reports.
//
// The fix is the Lettersanity treatment, and nothing more: an unavailable letter
// still STANDS and is drawn translucent while its collide callback refuses the
// pickup (game/231/RB_CtrLetter.c, AP_CtrLetter_UpdateVisual and the refusal at
// the top of RB_CtrLetter_ThCollide). An unavailable cup-leg or boss-race box now
// stands the same way: visible, translucent, and not collectable by contact, by a
// weapon or by an explosion.
//
// THREE VALUES, NOT TWO, because "stands" and "collectable" stopped being the
// same question:
//   * NONE   nothing is spawned. Reserved for the routes that must not show this
//            track's boxes AT ALL: the Cortex Vortex pad track (whose host
//            LevelID carries another track's box identity) and a custom-track
//            DENY verdict. Showing a translucent box there would advertise a box
//            that does not belong to the track being raced.
//   * SOLID  the Alpha 4 behaviour: stands, collides, dispatches.
//   * GHOST  stands translucent, collides with nothing, dispatches nothing.
//
// SOLID is deliberately 1 and NONE is 0, so every older truthiness test of
// AP_BoxPolicyAllows keeps its meaning; GHOST is the only new state and callers
// have to ask for it by name. Collectability is a separate predicate from
// standing for exactly that reason: an engine caller that forgets the difference
// fails to compile rather than quietly paying a check for a ghost.
enum AP_BoxPresentation
{
	AP_BOX_PRESENT_NONE  = 0,
	AP_BOX_PRESENT_SOLID = 1,
	AP_BOX_PRESENT_GHOST = 2,
};

// The policy, expressed as presentation. An allowed route is SOLID; a refused
// one stands its boxes as ghosts instead of standing them down. NONE is never
// produced here: it is the caller's own answer for the two routes that own no
// boxes on this track at all.
static inline int AP_BoxPresentationFor(int route, int physPad, int keysOwned,
                                        int stage1Met, int racerMet)
{
	if (AP_BoxPolicyAllows(route, physPad, keysOwned, stage1Met, racerMet))
		return AP_BOX_PRESENT_SOLID;
	return AP_BOX_PRESENT_GHOST;
}

// Does anything spawn for this presentation?
static inline int AP_BoxPresentationStands(int present)
{
	return present != AP_BOX_PRESENT_NONE;
}

// May a standing box be broken and its check sent? THE gate every break path
// asks, so the visual and the wire cannot disagree.
static inline int AP_BoxPresentationCollectable(int present)
{
	return present == AP_BOX_PRESENT_SOLID;
}

#endif // CTR_AP
#endif // AP_CUP_BOX_POLICY_H
