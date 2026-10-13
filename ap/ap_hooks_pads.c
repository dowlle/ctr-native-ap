// ap_hooks_pads.c -- part of the ap_hooks unit (issue #305).
// Warp pads: the unified pad state (Warp-Pad State Model v2), trial and Cortex
// Vortex pad tracks, uncollected and glow bits, box / Wumpa / letter counts
// behind a pad, Hit Character pad opportunities and the pad route log.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

// ---------------------------------------------------------------------------
// UNIFIED PAD STATE (Warp-Pad State Model v2).
// One state function shared by the map colour (AH_Map.c), the in-hub look
// (AH_WarpPad_LInB), and the gate (AH_WarpPad_ThTick). A pad is in exactly one
// state; every surface reads it from the same source, no mode switch.
//
// LIFECYCLE keys off the DESTINATION content category (race dest = full 6-state;
// trial/arena/cup dest = reduced 1->2->5). REQUIREMENTS stay keyed to the
// PHYSICAL pad (stage1/stage2 come from warp_pad_unlock[phys]). GREEN means "a
// check you can do" -- ANY item (foreign included), NOT the old own-progression
// criterion (AP_BitIsUsefulUnchecked), which this model drops.
// ---------------------------------------------------------------------------

// Enumerate the still-UNCOLLECTED (unchecked on the server) AP reward locations
// of destination `destLevelID`, for ANY category, writing their global
// AdvProgress bits into `outBits` (capacity `cap`) and returning the count. This
// is the category-general sibling of AP_WarpPadUncollectedBits (race-only), added
// so the state model + the trial-pad glow can see trial/arena/cup locations too.
// "Collected" is decided ONLY by AP checked-state (never CHECK_ADV_BIT). Absent
// tiers (no location_code in this seed) are skipped. Categories:
//   race  0..15 : 5 tiers (trophy +0x06, sapphire +0x16, gold +0x28,
//                 platinum +0x3a, CTR token +0x4c)
//   trial 16,17 : 3 relic tiers (sapphire/gold/platinum). Their Trophy and
//                 CTR Challenge have no bit; AP_PadUncollectedGlowBits adds
//                 them as pseudo-bits (#343)
//   arena 18,19,21,23 : 1 crystal (battleTrackArr[dest-18] + FIRST_PURPLE_TOKEN)
//   cup   100..104     : 1 gem ((dest-100) + FIRST_GEM)
static int AP_PadBoxLive(long code, void *ctx);
static int AP_PadBoxChecked(long code, void *ctx);

static long AP_TrialTrackLocation(int levelID, int challenge)
{
	int track = levelID - 16;
	if (!ctr_cfg_active() || track < 0 || track >= CTR_CFG_TRIAL_TRACK_COUNT ||
	    challenge < 0 || challenge >= CTR_CFG_TRIAL_CHECK_COUNT ||
	    !ctr_cfg.trial_track_valid[track])
		return -1;
	return ctr_cfg.trial_track_locations[track][challenge];
}

int AP_TrialTrackConfigured(int levelID)
{
	int track = levelID - 16;
	return ctr_cfg_active() && track >= 0 && track < CTR_CFG_TRIAL_TRACK_COUNT &&
	       ctr_cfg.trial_track_valid[track];
}

int AP_TrialTrackLocationChecked(int levelID, int challenge)
{
	long code = AP_TrialTrackLocation(levelID, challenge);
	return code > 0 && ap_net_location_checked(code);
}

// One feed popup per session when the trial CTR Challenges cannot be offered
// because the retail letter assets were refused (ruling 2026-10-09). Waits for
// the BIGFILE directory: before it is read, AP_TrialLetters_Prepare returns 0
// without having tried.
static void AP_TrialLettersRefusedNotice(void)
{
	static int s_shown;

	if (s_shown || !sdata->ptrBigfile1)
		return;
	s_shown = 1;
	AP_FeedNoticeLine("TRIAL CTR CHALLENGES OFF:");
	AP_FeedNoticeLine("LETTER ASSETS NOT FOUND");
}

// Can this trial destination serve its CTR Challenge? It needs the seed's
// Trophy+CTR mode, a placed CTR Challenge location and the retail letter assets
// (AP_TrialLetters_Prepare, which latches its answer after the first read).
// The pad lifecycle (letter count, glow) and the entry route both ask this, so
// a refused asset load cannot leave the pad open for checks its entry route
// will never offer. Same predicate the CTR Challenge spawn path gates on.
int AP_TrialChallengeServable(int destLevelID)
{
	int track = destLevelID - 16;

	if (!AP_TrialTrackConfigured(destLevelID))
		return 0;
	if (ctr_cfg.trial_track_mode[track] < 2 ||
	    ctr_cfg.trial_track_locations[track][CTR_CFG_TRIAL_CTR] <= 0)
		return 0;
	if (AP_TrialLetters_Prepare())
		return 1;
	AP_TrialLettersRefusedNotice();
	return 0;
}

// ── Cortex Vortex pad track (schema 15) ─────────────────────────────────────
// The identity half is the loader's latch (platform/native_cortex_track_latch.h);
// the code half is ctr_cfg.cortex_track. A build without the custom-track
// loader can never serve the track, so the identity is constantly 0 there and
// every pad hosting 110 refuses entry (AP_CortexTrackEntryReady).
int AP_CortexTrackActive(void)
{
#ifdef CTR_CUSTOM_TRACKS
	struct GameTracker *gGT = sdata ? sdata->gGT : 0;
	if (gGT == 0)
		return 0;
	return CustomTrack_CortexTrackIntent((int)gGT->levelID,
	                                     (gGT->gameMode1 & ADVENTURE_BOSS) != 0);
#else
	return 0;
#endif
}

int AP_CortexTrackBit(int slot)
{
	return AP_CortexPseudoBit(slot);
}

static long AP_CortexTrackCode(int slot)
{
	return ctr_cfg_active() ? AP_CortexSlotCode(&ctr_cfg.cortex_track, slot) : -1;
}

int AP_CortexTrackChecked(int slot)
{
	long code = AP_CortexTrackCode(slot);
	return code > 0 && ap_net_location_checked(code);
}

static int AP_CortexDestValid(void)
{
	return ctr_cfg_active() && ctr_cfg.cortex_track.valid;
}

int AP_CupLegsCortexTrack(int cup)
{
	int leg;
	if (!ctr_cfg_active() || cup < 0 || cup >= 5 || ctr_cfg_cup_displaced(cup))
		return 0;
	for (leg = 0; leg < 4; leg++)
		if (ctr_cfg_cup_leg(cup, leg) == AP_CORTEX_DEST)
			return 1;
	return 0;
}

int AP_CortexTrackPrepareLoad(int destLevelID)
{
	int cortex = 0;
	int level = CortexTrackLatch_ResolveDest(destLevelID, &cortex);
#ifdef CTR_CUSTOM_TRACKS
	CustomTrack_CortexTrackSelectNextLoad(cortex);
#endif
	if (cortex)
	{
		char msg[96];
		snprintf(msg, sizeof msg,
		         "[AP CORTEX] loading destination 110 on host level %d\n", level);
		AP_AppendLog(msg);
	}
	return level;
}

int AP_RelicTimeFor(int levelID, int tier)
{
	if (tier < 0 || tier > 2)
		tier = 0;
	if (AP_CortexTrackActive())
		return AP_CortexRelicTime(tier);
	if (levelID < 0 || levelID >= 18)
		return data.RelicTime[tier];
	return data.RelicTime[levelID * 3 + tier];
}

const char *AP_CortexTrackDisplayName(void)
{
	return AP_CortexTrackActive() ? "CORTEX VORTEX" : 0;
}

int AP_PadUncollectedBits(int destLevelID, int *outBits, int cap)
{
	static const int kRaceTierBit[5] = {
	    ADV_REWARD_FIRST_TROPHY,
	    ADV_REWARD_FIRST_SAPPHIRE_RELIC,
	    ADV_REWARD_FIRST_GOLD_RELIC,
	    ADV_REWARD_FIRST_PLATINUM_RELIC,
	    ADV_REWARD_FIRST_CTR_TOKEN};
	// Trial pads (Slide Coliseum 16 / Turbo Track 17) carry only the three relic
	// Time-Trial locations (dest + SAPPHIRE/GOLD/PLATINUM: bits 38/56/74 &
	// 39/57/75, verified against AP_LOCATION_TABLE) -- no trophy, no token.
	static const int kTrialTierBit[3] = {
	    ADV_REWARD_FIRST_SAPPHIRE_RELIC,
	    ADV_REWARD_FIRST_GOLD_RELIC,
	    ADV_REWARD_FIRST_PLATINUM_RELIC};
	int count = 0;
	int i;

	if (outBits == 0 || cap <= 0)
		return 0;

	if (destLevelID >= 0 && destLevelID < 16)
	{
		for (i = 0; i < 5 && count < cap; i++)
		{
			int bit = destLevelID + kRaceTierBit[i];
			// Issue #171/#28 R1: skip a below-count relic Time Trial this seed
			// never created, exactly like AP_WarpPadUncollectedBits above.
			if (!AP_LocationExistsByBit(bit))
				continue;
			if (!AP_LocationCheckedByBit(bit))
				outBits[count++] = bit;
		}
	}
	else if (destLevelID == 16 || destLevelID == 17)
	{
		for (i = 0; i < 3 && count < cap; i++)
		{
			int bit = destLevelID + kTrialTierBit[i];
			if (!AP_LocationExistsByBit(bit))
				continue;
			if (!AP_LocationCheckedByBit(bit))
				outBits[count++] = bit;
		}
	}
	else if (destLevelID == 18 || destLevelID == 19 ||
	         destLevelID == 21 || destLevelID == 23)
	{
		// Battle arena crystal (Crystal Bonus Round, bits 111..114). Same mapping
		// as the ThTick glow pass: battleTrackArr[dest-18] + FIRST_PURPLE_TOKEN.
		// Never a removed slot (crystal bonuses are unconditional locations --
		// see the package-3 build note), but checked the same way for consistency.
		int bit = R232.battleTrackArr[destLevelID - 18] + ADV_REWARD_FIRST_PURPLE_TOKEN;
		if (AP_LocationExistsByBit(bit) && !AP_LocationCheckedByBit(bit) && count < cap)
			outBits[count++] = bit;
	}
	else if (destLevelID == AP_CORTEX_DEST)
	{
		// Schema 15: Trophy, relic tiers and CTR token as direct codes. Never
		// LevelID-13 arithmetic, which would name Oxide Station's locations.
		if (AP_CortexDestValid())
			count = AP_CortexPadAppendOpen(&ctr_cfg.cortex_track, AP_CV_APPEND_TIERS,
			                               outBits, cap, count,
			                               AP_PadBoxLive, AP_PadBoxChecked, 0);
	}
	else if (destLevelID >= 100 && destLevelID < 105)
	{
#ifdef CTR_CUSTOM_TRACKS
		if (AP_CustomPadOwnsDestination(&ctr_cfg, destLevelID))
			return AP_CustomPadAppendUnchecked(
				&ctr_cfg, destLevelID, 0, outBits, cap, count,
				AP_PadBoxLive, AP_PadBoxChecked, 0);
#endif
		// Gem cup (Gem, bits 106..110), keyed by cup colour 0..4. Never a
		// removed slot -- shuffle_gems off PINS this location (place_locked_item),
		// it stays a real self-consistent AP check (see the package-3 build note).
		int bit = (destLevelID - 100) + ADV_REWARD_FIRST_GEM;
		if (AP_LocationExistsByBit(bit) && !AP_LocationCheckedByBit(bit) && count < cap)
			outBits[count++] = bit;
	}

	return count;
}

// Append the still-unchecked podium rung pseudo-bits of ONE race track (0..15) to
// outBits, advancing *count. Shared by the race-pad path and the cup-pad
// aggregation below. AP_LookupLocationCode returns -1 for a rung this seed does
// not carry (held_5th default-off, a rung the seed did not place), which skips it
// here exactly like an absent tier.
static void AP_AppendTrackRungGlow(int track, int *outBits, int cap, int *count)
{
	int rung;
#ifdef CTR_CUSTOM_TRACKS
	if (track < 0 || track >= AP_PODIUM_LOGICAL_TRACK_COUNT)
#else
	if (track < 0 || (track >= CTR_CFG_PODIUM_TRACK_COUNT &&
	                 (track < AP_TRIAL_PODIUM_LOGICAL_BASE || track >= AP_TRIAL_PODIUM_LOGICAL_BASE+2)))
#endif
		return;
	for (rung = 0; rung < CTR_CFG_PODIUM_RUNG_COUNT && *count < cap; rung++)
	{
		int bit = AP_PODIUM_PSEUDO_BASE + track * CTR_CFG_PODIUM_RUNG_COUNT + rung;
		int j;
		if (AP_LookupLocationCode(bit) < 0)
			continue;
		if (AP_LocationCheckedByBit(bit))
			continue;
		// Dedup within this pad's buffer: a cup pad aggregates four legs, and a
		// track can (in principle) appear more than once, which would else list the
		// same rung twice in the glow cycle. Cheap (buffer <= 24). Two DIFFERENT cup
		// pads advertising overlapping rungs is fine -- they are separate buffers.
		for (j = 0; j < *count; j++)
			if (outBits[j] == bit)
				break;
		if (j < *count)
			continue;
		outBits[(*count)++] = bit;
	}
}

// Glow-only enumerator (see ap_hooks.h): the tier/category bits above PLUS the
// still-unchecked podium rung pseudo-bits. For a RACE destination (0..15) it
// appends that track's own rungs. For a CUP destination (100..104) it appends the
// rungs of ALL FOUR of the cup's leg tracks (data.advCupTrackIDs), because a cup
// pad's position checks live on its legs -- without this a cup pad under-advertises
// (decision 3, 2026-07-16 wayfinder). Kept separate from
// AP_PadUncollectedBits so the tier-2 menu stays tier-only. AP_PadState consumes
// the glow-complete enumeration because a Done lock may not strand a rung.
int AP_PadUncollectedGlowBits(int destLevelID, int *outBits, int cap)
{
	int count;

#ifdef CTR_CUSTOM_TRACKS
	if (AP_CustomPadOwnsDestination(&ctr_cfg, destLevelID))
		return AP_CustomPadAppendUnchecked(
			&ctr_cfg, destLevelID, ctr_cfg.podium_enabled,
			outBits, cap, 0, AP_PadBoxLive, AP_PadBoxChecked, 0);
#endif

	count = AP_PadUncollectedBits(destLevelID, outBits, cap);

	// #439 follow-up: an open Relic Race Perfect is advertised like the other
	// relic-side checks, right after the tier bits. Retail race pads and the two
	// trial pads only; Cortex Vortex and custom pads return around it.
	if (AP_PadUncollectedRelicPerfectCount(destLevelID) > 0 && count < cap)
		outBits[count++] = AP_RelicPerfectPseudoBit(destLevelID);

	// Schema 15: the Cortex Vortex pad also advertises its own podium rungs.
	if (destLevelID == AP_CORTEX_DEST)
		return AP_CortexDestValid()
		           ? AP_CortexPadAppendOpen(&ctr_cfg.cortex_track, AP_CV_APPEND_RUNGS,
		                                    outBits, cap, count,
		                                    AP_PadBoxLive, AP_PadBoxChecked, 0)
		           : count;

	// #343: a trial pad also offers its Trophy and CTR Challenge. They have no
	// AdvProgress bit, so they ride as pseudo-bits. Glow only: the tier-2
	// picker reads AP_PadUncollectedBits as its "relics left" test.
	// An unservable CTR Challenge (Trophy-only mode, or refused letter assets)
	// is left out, so the pad cannot stay open for a race it never offers.
	if (AP_TrialTrackConfigured(destLevelID))
	{
		long trialRow[CTR_CFG_TRIAL_CHECK_COUNT];
		trialRow[CTR_CFG_TRIAL_TROPHY] =
		    ctr_cfg.trial_track_locations[destLevelID - 16][CTR_CFG_TRIAL_TROPHY];
		trialRow[CTR_CFG_TRIAL_CTR] = AP_TrialChallengeServable(destLevelID)
		    ? ctr_cfg.trial_track_locations[destLevelID - 16][CTR_CFG_TRIAL_CTR]
		    : -1;
		count = AP_TrialPadAppendUnchecked(trialRow, destLevelID - 16,
			outBits, cap, count, AP_PadBoxChecked, 0);
	}

	if (!ctr_cfg.podium_enabled)
		return count;

	if (destLevelID >= 0 && destLevelID < 18)
	{
		AP_AppendTrackRungGlow(destLevelID < 16 ? destLevelID :
		                      AP_TRIAL_PODIUM_LOGICAL_BASE + destLevelID - 16,
		                      outBits, cap, &count);
	}
	else if (destLevelID >= 100 && destLevelID < 105)
	{
		// Aggregate the four leg tracks' uncollected rungs. ctr_cfg_cup_leg is
		// cup-major (cup, leg) and identity-safe (#166: returns the vanilla
		// data.advCupTrackIDs value when inactive or this seed left the leg at
		// its default); a leg whose track is a trial (16/17) or otherwise outside
		// 0..15 carries no podium rungs and is skipped by AP_AppendTrackRungGlow.
		int cup = destLevelID - 100;
		int leg;
		// A displaced cup runs one custom race and legs no retail track. Its
		// complete four-leg row remains on the wire as the seed's retail table,
		// but advertising those rungs on this pad would promise checks the race
		// cannot honestly emit.
		if (!AP_GlowSlots_CupLegRungsEligible(ctr_cfg_cup_displaced(cup)))
			return count;
		for (leg = 0; leg < 4 && count < cap; leg++)
		{
			int legTrack = ctr_cfg_cup_leg(cup, leg);
			// A Cortex Vortex leg exposes its own rungs, never Oxide Station's.
			if (legTrack == AP_CORTEX_DEST)
			{
				if (AP_CortexDestValid())
					count = AP_CortexPadAppendOpen(&ctr_cfg.cortex_track,
					                               AP_CV_APPEND_RUNGS, outBits, cap, count,
					                               AP_PadBoxLive, AP_PadBoxChecked, 0);
				continue;
			}
			AP_AppendTrackRungGlow(legTrack, outBits, cap, &count);
		}
	}
	return count;
}

// ── §6: boxes behind a pad ──────────────────────────────────────────────────
//
// RULED 2026-08-11 (rulings note §6). AP_PadUncollectedGlowBits enumerates tier
// bits and podium rungs, and neither of those is a box: itemsanity box locations
// (#109) carry no AdvProgress bit at all. So a trophy win with boxes still
// standing Re-locked the pad and stranded them until stage 2, and a track whose
// tier bits were all checked went Done, which HARD-LOCKS the pad and made those
// boxes unreachable for the rest of the seed.
//
// This is counted, not enumerated into outBits, and deliberately so: the glow
// cycle shows REWARD placeholders (trophy / relic / token models) and a box has
// no placeholder to show. The count feeds AP_PadState's lifecycle decisions only;
// the glow itself is byte-identical.
//
// THE PREDICATE IS THE SPAWN PREDICATE. A box counts as uncollected only if it
// could actually be broken: it needs GEOMETRY (a placement at that slot on that
// level) and a LIVE location (in this slot's seed, per §7) that is not yet
// checked. Counting bare locations instead would let a seed that created more box
// locations than the placement set covers keep a pad green forever.
static int AP_PadTrackBoxSlots(int levelID)
{
	// Per-level placement counts, rebuilt only when the placement table changes
	// (a load, or an author-mode drop/undo). AP_PadState runs per pad per frame,
	// so re-walking 241..512 placements for every pad every frame would be real
	// work for a number that changes about twice a session.
	static int s_slots[AP_BOX_TRACK_COUNT];
	static int s_gen = -1;

	int gen;

	if (levelID < 0 || levelID >= AP_BOX_TRACK_COUNT)
		return 0;

	AP_Author_EnsureLoaded();
	gen = AP_Author_PlacementGeneration();
	if (gen != s_gen)
	{
		int i, n = AP_Author_PlacementCount();

		for (i = 0; i < AP_BOX_TRACK_COUNT; i++)
			s_slots[i] = 0;

		for (i = 0; i < n; i++)
		{
			int lvl;
			if (!AP_Author_PlacementGet(i, &lvl, 0, 0, 0, 0))
				continue;
			if (lvl < 0 || lvl >= AP_BOX_TRACK_COUNT)
				continue;
			if (s_slots[lvl] >= AP_BOX_SLOTS_PER_TRACK)
				continue; // past the frozen ceiling: no name, never spawns
			s_slots[lvl]++;
		}
		s_gen = gen;
	}

	return s_slots[levelID];
}

// Server truth, in the shape AP_BoxMap_CountStanding wants. ap_boxes.c holds its
// own pair for the spawn set; these two ask the same two questions of the same
// two sets, which is what keeps the pad's idea of "still standing" identical to
// the track's.
static int AP_PadBoxLive(long code, void *ctx)
{
	(void)ctx;
	return ap_net_location_exists((long long)code); // §7: live in this slot's seed
}

static int AP_PadBoxChecked(long code, void *ctx)
{
	(void)ctx;
	return ap_net_location_checked((long long)code); // already broken
}

static int AP_PadTrackBoxesLeft(int levelID)
{
	// The count itself is AP_BoxMap_CountStanding (freestanding, and exercised by
	// tools/test-box-map.c); this only supplies the two server-truth predicates,
	// which are the SAME pair the spawn set is built from -- §7 liveness and the
	// checked set. One rule, one implementation, so the pad and the track can
	// never disagree about what is still standing.
	return AP_BoxMap_CountStanding(levelID, AP_PadTrackBoxSlots(levelID),
	                               AP_PadBoxLive, AP_PadBoxChecked, 0);
}

// How many breakable boxes still stand behind this pad's destination. A cup pad
// aggregates its four legs, exactly as AP_PadUncollectedGlowBits does for rungs
// and for the same reason: a cup leg is not independently raceable from the hub,
// so a Done cup pad would strand its legs' boxes permanently.
int AP_PadUncollectedBoxCount(int destLevelID)
{
	int leg, n = 0;

	if (!ctr_cfg_active())
		return 0;

	if (destLevelID >= 0 && destLevelID < AP_BOX_TRACK_COUNT)
		return AP_PadTrackBoxesLeft(destLevelID);

	if (destLevelID >= 100 && destLevelID < 105)
	{
		int cup = destLevelID - 100;
#ifdef CTR_CUSTOM_TRACKS
		// A displaced cup runs no retail legs.  Until custom box identities and
		// placements exist, counting the wire row's retail boxes here would keep
		// the custom pad open for checks it cannot serve.
		if (AP_CustomPadOwnsDestination(&ctr_cfg, destLevelID))
			return 0;
#endif
		for (leg = 0; leg < 4; leg++)
			n += AP_PadTrackBoxesLeft(ctr_cfg_cup_leg(cup, leg));
	}
	return n;
}

// Per-track Wumpa checks carry no AdvProgress bit, so they need the same
// count-only lifecycle bridge as AP boxes and Lettersanity. A standalone race
// owns its one destination code. A Gem Cup aggregates the distinct track-owned
// codes of all four legs so checking the Gem cannot hard-lock a remaining
// Wumpa route. Repeated legs are alternatives to one location and count once.
static int AP_PadTrackWumpaLeft(int levelID)
{
	long code;

	if (levelID < 0 || levelID >= CTR_CFG_WUMPA_TRACK_COUNT)
		return 0;
	code = ctr_cfg.wumpa.tracks[levelID];
	return code >= 0 && ap_net_location_exists(code) &&
	       !ap_net_location_checked(code);
}

int AP_PadUncollectedWumpaCount(int destLevelID)
{
	int tracks[4];
	int trackLeft[4];
	int leg;

	if (!ctr_cfg_active() || ctr_cfg.wumpa.mode != CTR_CFG_WUMPA_PER_TRACK)
		return 0;
	if (destLevelID >= 0 && destLevelID < CTR_CFG_WUMPA_TRACK_COUNT)
		return AP_PadTrackWumpaLeft(destLevelID);
	if (destLevelID == AP_CORTEX_DEST)
		return AP_CortexOpenCount(&ctr_cfg.cortex_track, AP_CV_SLOT_WUMPA, 1,
		                          AP_PadBoxLive, AP_PadBoxChecked, 0);
	if (destLevelID < 100 || destLevelID >= 105)
		return 0;

#ifdef CTR_CUSTOM_TRACKS
	// The direct Alpha6 custom destination already includes its Wumpa location
	// in AP_CustomPadAppendUnchecked. Its serialized retail leg row is ignored.
	if (AP_CustomPadOwnsDestination(&ctr_cfg, destLevelID))
		return 0;
#endif

	for (leg = 0; leg < 4; leg++)
	{
		tracks[leg] = ctr_cfg_cup_leg(destLevelID - 100, leg);
		// A Cortex Vortex leg counts its own 35016121, keyed one past the
		// retail rows so it never dedups against Oxide Station's row 13.
		if (tracks[leg] == AP_CORTEX_DEST)
		{
			tracks[leg] = CTR_CFG_WUMPA_TRACK_COUNT;
			trackLeft[leg] = AP_CortexOpenCount(&ctr_cfg.cortex_track, AP_CV_SLOT_WUMPA, 1,
			                                    AP_PadBoxLive, AP_PadBoxChecked, 0);
			continue;
		}
		trackLeft[leg] = AP_PadTrackWumpaLeft(tracks[leg]);
	}
	return AP_PadCupWumpaCount(tracks, trackLeft, CTR_CFG_WUMPA_TRACK_COUNT + 1);
}

int AP_PadUncollectedLetterCount(int destLevelID)
{
	int letter, n = 0;

	if (destLevelID == AP_CORTEX_DEST)
		return ctr_cfg_active()
		           ? AP_CortexOpenCount(&ctr_cfg.cortex_track, AP_CV_SLOT_LETTER0, 3,
		                                AP_PadBoxLive, AP_PadBoxChecked, 0)
		           : 0;
#ifdef CTR_CUSTOM_TRACKS
	// Custom CTR letters (2026-10-09): the custom pad must not go Done while one
	// is open, the same rule every other CTR Challenge pad follows.
	if (AP_CustomPadOwnsDestination(&ctr_cfg, destLevelID))
		return ctr_cfg_active()
		           ? AP_CustomPadLettersLeft(&ctr_cfg, destLevelID,
		                                     AP_PadBoxLive, AP_PadBoxChecked, 0)
		           : 0;
#endif
	if (!ctr_cfg_active() || destLevelID < 0 || destLevelID >= CTR_CFG_LETTER_TRACK_COUNT)
		return 0;
	// Trial letters are only reachable in a servable trial CTR Challenge.
	if ((destLevelID == 16 || destLevelID == 17) && !AP_TrialChallengeServable(destLevelID))
		return 0;

	for (letter = 0; letter < CTR_CFG_LETTER_COUNT; letter++)
	{
		long code = ctr_cfg.lettersanity_locations[destLevelID][letter];
		if (code >= 0 && ap_net_location_exists(code) && !ap_net_location_checked(code))
			n++;
	}
	return n;
}

// Relic Race Perfect (#49) has no AdvProgress bit, so like Lettersanity it is a
// count-only pad lifecycle input. 1 while this destination's perfect check is a
// live location that the server has not checked; the pad must not go Done or
// hide the relic race while it is (#439). Retail race pads and the two trial
// pads only: the Cortex Vortex perfect identity is not minted.
int AP_PadUncollectedRelicPerfectCount(int destLevelID)
{
	long code;

	if (!ctr_cfg_active() || !ctr_cfg.relic_perfect_enabled)
		return 0;
	if (destLevelID < 0 || destLevelID >= CTR_CFG_RELIC_PERFECT_COUNT)
		return 0;
	code = ctr_cfg.relic_perfect[destLevelID];
	return AP_RelicPerfectPadLeftPure(
	    1, code, code > 0 && ap_net_location_exists(code),
	    code > 0 && ap_net_location_checked(code));
}

// Display-only pad enumeration (see ap_hooks.h): AP_PadUncollectedGlowBits plus
// the open item boxes, CTR letters and per-track Wumpa. Each family uses the
// predicate of its pad-state count (AP_PadUncollectedBoxCount, ...LetterCount,
// ...WumpaCount), so the glow lists exactly what the state counts, and a cup pad
// aggregates its four legs the same way (boxes and Wumpa; letters are per track
// and a cup has none). A leg track repeated inside one cup is listed once. A
// custom cup carries its Trophy and Wumpa in the base enumeration already and no
// boxes or letters, so it returns unchanged. Cortex Vortex lists its letters and
// Wumpa; as a cup leg only its Wumpa, like the count.
int AP_PadUncollectedDisplayBits(int destLevelID, int *outBits, int cap)
{
	int count = AP_PadUncollectedGlowBits(destLevelID, outBits, cap);
	int wumpaOn;

	if (!ctr_cfg_active())
		return count;
#ifdef CTR_CUSTOM_TRACKS
	if (AP_CustomPadOwnsDestination(&ctr_cfg, destLevelID))
	{
		// The custom CTR letters, with the predicate of their pad-state count.
		int letter;
		if (AP_CustomPadLettersLeft(&ctr_cfg, destLevelID, AP_PadBoxLive,
		                            AP_PadBoxChecked, 0) > 0)
			for (letter = 0; letter < CTR_CFG_LETTER_COUNT && count < cap; letter++)
			{
				long code = ctr_cfg.custom_letter_locations[letter];
				if (code > 0 && ap_net_location_exists(code) &&
				    !ap_net_location_checked(code))
					outBits[count++] = AP_PadGlowCustomLetterBit(letter);
			}
		return count;
	}
#endif
	wumpaOn = ctr_cfg.wumpa.mode == CTR_CFG_WUMPA_PER_TRACK;

	if (destLevelID >= 0 && destLevelID < AP_BOX_TRACK_COUNT)
	{
		count = AP_PadGlowAppendBoxes(destLevelID, AP_PadTrackBoxSlots(destLevelID),
		                              outBits, cap, count,
		                              AP_PadBoxLive, AP_PadBoxChecked, 0);
		// Same gate as AP_PadUncollectedLetterCount: trial letters only show
		// while the trial CTR Challenge can be served.
		if ((destLevelID != 16 && destLevelID != 17) ||
		    AP_TrialChallengeServable(destLevelID))
			count = AP_PadGlowAppendLetters(ctr_cfg.lettersanity_locations[destLevelID],
			                                destLevelID, outBits, cap, count,
			                                AP_PadBoxLive, AP_PadBoxChecked, 0);
		if (wumpaOn && destLevelID < CTR_CFG_WUMPA_TRACK_COUNT)
			count = AP_PadGlowAppendWumpa(ctr_cfg.wumpa.tracks[destLevelID], destLevelID,
			                              outBits, cap, count,
			                              AP_PadBoxLive, AP_PadBoxChecked, 0);
	}
	else if (destLevelID == AP_CORTEX_DEST)
	{
		if (AP_CortexDestValid())
			count = AP_CortexPadAppendOpen(&ctr_cfg.cortex_track,
			                               AP_CV_APPEND_LETTERS |
			                                   (wumpaOn ? AP_CV_APPEND_WUMPA : 0),
			                               outBits, cap, count,
			                               AP_PadBoxLive, AP_PadBoxChecked, 0);
	}
	else if (destLevelID >= 100 && destLevelID < 105)
	{
		int cup = destLevelID - 100;
		int leg, prev;
		int seen[4];

		for (leg = 0; leg < 4; leg++)
		{
			int track = ctr_cfg_cup_leg(cup, leg);
			int dup = 0;

			seen[leg] = track;
			for (prev = 0; prev < leg; prev++)
				if (seen[prev] == track)
					dup = 1;
			if (dup)
				continue; // repeated leg: one location, listed once
			if (track == AP_CORTEX_DEST)
			{
				if (wumpaOn && AP_CortexDestValid())
					count = AP_CortexPadAppendOpen(&ctr_cfg.cortex_track,
					                               AP_CV_APPEND_WUMPA, outBits, cap, count,
					                               AP_PadBoxLive, AP_PadBoxChecked, 0);
				continue;
			}
			count = AP_PadGlowAppendBoxes(track, AP_PadTrackBoxSlots(track),
			                              outBits, cap, count,
			                              AP_PadBoxLive, AP_PadBoxChecked, 0);
			if (wumpaOn && track >= 0 && track < CTR_CFG_WUMPA_TRACK_COUNT)
				count = AP_PadGlowAppendWumpa(ctr_cfg.wumpa.tracks[track], track,
				                              outBits, cap, count,
				                              AP_PadBoxLive, AP_PadBoxChecked, 0);
		}
	}
	return count;
}

// The display buffer must hold a Gem Cup's four legs and the busiest single pad.
CTR_STATIC_ASSERT(AP_PAD_DISPLAY_BITS_MAX >= AP_PADGLOW_WORST_CASE_BITS);
CTR_STATIC_ASSERT(AP_PAD_DISPLAY_BITS_MAX >=
                  5 + 1 + 2 + CTR_CFG_PODIUM_RUNG_COUNT + AP_BOX_SLOTS_PER_TRACK +
                  CTR_CFG_LETTER_COUNT + 1);

// Reward-type group of a glow bit -> the prize slot that owns it under
// by_reward_type (issue #59). The three groups are exactly the three slots the
// pad already births as trophy / relic / token placeholders
// (s_warpPadRewardModelIDs in AH_WarpPad.c), so a grouped slot keeps showing the
// kind of check its placeholder was born for:
//   0 RACE   trophy bit + that track's podium rung pseudo-bits (>= the pseudo
//            base) -- one race result, one slot, which is why this slot can hold
//            six items (trophy + 5 rungs) on a rung seed.
//   1 RELIC  the sapphire/gold/platinum Time-Trial tiers, i.e. the contiguous
//            span from the first sapphire bit up to the first CTR token bit.
//   2 TOKEN  everything else the glow enumerators can produce: the CTR Token
//            challenge, a gem-cup gem and an arena crystal (purple token).
// A trial pad's Trophy and CTR Challenge pseudo-bits (#343) join groups 0 and 2.
// Item boxes and per-track Wumpa join group 0, CTR letters group 2 (ruling,
// 2026-09-30); Cortex Vortex Wumpa follows its retail sibling into group 0.
// Bit ranges follow the enumerators above, which build every bit as
// <first-bit-of-its-block> + index, so the block bases ARE the boundaries.
static int AP_GlowBitRewardGroup(int globalBit)
{
	int cortexGroup = AP_CortexPseudoRewardGroup(globalBit);
	int trialGroup = AP_TrialPseudoRewardGroup(globalBit);
	int perfectGroup = AP_RelicPerfectPseudoRewardGroup(globalBit);
	int itemGroup = AP_PadGlowItemRewardGroup(globalBit);
#ifdef CTR_CUSTOM_TRACKS
	if (globalBit == AP_CUSTOM_CTR_PSEUDO_BIT)
		return 2; // custom CTR Challenge -> token slot, like every CTR Challenge
#endif
	if (itemGroup >= 0)
		return itemGroup; // boxes + Wumpa -> race slot, CTR letters -> token slot
	if (perfectGroup >= 0)
		return perfectGroup; // #439: Relic Race Perfect rides in the relic slot
	if (cortexGroup >= 0)
		return cortexGroup; // schema 15: Cortex Vortex pseudo-bits
	if (trialGroup >= 0)
		return trialGroup; // #343: trial Trophy -> race slot, CTR Challenge -> token slot
	if (globalBit >= AP_PODIUM_PSEUDO_BASE)
		return 0; // podium rung -- rides with its track's trophy
	if (globalBit < ADV_REWARD_FIRST_SAPPHIRE_RELIC)
		return 0; // trophy (ADV_REWARD_FIRST_TROPHY + track)
	if (globalBit < ADV_REWARD_FIRST_CTR_TOKEN)
		return 1; // sapphire / gold / platinum relic tiers
	return 2;     // CTR token, gem-cup gem, arena crystal
}

// Decide WHICH of the `n` uncollected bits each of the pad's three prize slots
// advertises this frame, writing the per-slot bit (or -1 = hide the slot) into
// outSlot3. `phase` is the display tick (frame counter / 0x3C, so it steps once
// every 2 seconds); the caller owns the frame clock. Sole consumer is the AP glow
// pass in AH_WarpPad_ThTick.
//
// Layout follows ctr_cfg.warp_pad_item_display (issue #59):
//   ONE_PILE (default, and every seed predating the option): all n items share
//     the three slots. n > 3 cycles a 3-wide window over the whole list, n in
//     1..3 puts item i in slot i, and the leftover slots hide. Unchanged from
//     what the glow has always done, which is what the absent-key fallback owes.
//   BY_REWARD_TYPE: each reward type keeps its own slot (see
//     AP_GlowBitRewardGroup) and rotates only within that type, so a floating
//     sapphire in the relic slot is the relic check and never the CTR check. A
//     type with nothing left hides its slot, so a destination carrying a single
//     reward (arena crystal, cup gem) shows it in the token slot rather than
//     slot 0 -- the price of a fixed slot per type, and the point of the option.
void AP_PadGlowSlots(const int *bits, int n, int phase, int *outSlot3)
{
	AP_GlowSlots_Select(
	    bits, n, phase,
	    ctr_cfg_active() &&
	        ctr_cfg.warp_pad_item_display == WARP_PAD_DISPLAY_BY_REWARD_TYPE,
	    AP_GlowBitRewardGroup, outSlot3);
}

// Retail races and admitted trial Trophy destinations share the full lifecycle.
// Relic-only/legacy trials remain in the reduced non-race category.
static int AP_DestIsRace(int destLevelID)
{
	if (destLevelID == AP_CORTEX_DEST)
		return AP_CortexDestValid() && AP_CortexTrackCode(AP_CV_SLOT_TROPHY) > 0;
	return (destLevelID >= 0 && destLevelID < 16) ||
	       (AP_TrialTrackConfigured(destLevelID) &&
	        AP_TrialTrackLocation(destLevelID, CTR_CFG_TRIAL_TROPHY) > 0);
}

static int AP_DestTrophyChecked(int destLevelID)
{
	if (destLevelID == AP_CORTEX_DEST)
		return AP_CortexTrackChecked(AP_CV_SLOT_TROPHY);
	if (AP_TrialTrackConfigured(destLevelID))
		return AP_TrialTrackLocationChecked(destLevelID, CTR_CFG_TRIAL_TROPHY);
	return AP_LocationCheckedByBit(destLevelID + ADV_REWARD_FIRST_TROPHY);
}

// Is destination `destLevelID` a recognised warp-pad destination at all (race /
// trial / arena / cup)? Anything else -> AP_PadState returns 0 (vanilla/untouched).
static int AP_DestKnown(int destLevelID)
{
	return AP_DestIsRace(destLevelID) ||
	       destLevelID == 16 || destLevelID == 17 ||
	       destLevelID == 18 || destLevelID == 19 ||
	       destLevelID == 21 || destLevelID == 23 ||
	       (destLevelID >= 100 && destLevelID < 105);
}

// Is the PHYSICAL pad's stage-1 (entry) requirement satisfied? Faithful copy of
// the per-category unlock predicate AH_WarpPad_LInB computes for each pad class
// so the state model agrees with the pad
// the game actually births. Race pads reuse ctr_cfg_warp_unlocked verbatim (the
// same helper ThTick's load gate uses). NON-race categories mirror LInB's
// per-class branch: randomized requirement when slot_data provides one, else the
// vanilla fallback for that class. Keyed by the PHYSICAL pad LevelID.
//
// Non-static since the Gem Cup AP-box policy (WO-A3): ap_boxes.c needs the SAME
// per-class stage-1 answer for the pad that hosts a cup leg's individual race,
// and a second implementation of this routing is exactly the divergence the
// policy exists to remove. Declared in ap_hooks.h.
int AP_PadStage1Met(int physLevelID)
{
	int i, owned;

	// Race tracks (0..15): the canonical helper (shared with ThTick + LInB).
	if (physLevelID >= 0 && physLevelID < 16)
		return ctr_cfg_warp_unlocked(physLevelID);

	// Trial pads: Slide Coliseum (16) = vanilla 10 Sapphire relics; Turbo Track
	// (17) = vanilla all 5 Gem colours. An emitted single-stage req overrides:
	// randomized modes, and for Slide Coliseum also a vanilla-mode seed that
	// lowered the pad to the Sapphires it created (ctr_cfg_slide_coliseum_req).
	if (physLevelID == CTR_CFG_SLIDE_COLISEUM_PAD)
	{
		const ctr_req *slide = ctr_cfg_slide_coliseum_req();
		if (slide)
			return AP_BossReqMet(slide);
		return AP_GateCount(AP_IDX_SAPPHIRE) >= CTR_CFG_SLIDE_COLISEUM_RETAIL_SAPPHIRES;
	}
	if (physLevelID == 17)
	{
		if (ctr_cfg_active() && physLevelID < CTR_CFG_PAD_COUNT &&
		    ctr_cfg.warp_pad_unlock[physLevelID].stage1.type != 0)
			return AP_BossReqMet(&ctr_cfg.warp_pad_unlock[physLevelID].stage1);
		owned = 0;
		for (i = 0; i < 5; i++)
			if (AP_GateCountGemColour(i) >= 1)
				owned++;
		return owned >= 5;
	}

	// Battle arenas (18/19/21/23): randomized req overrides; else the vanilla
	// hub-key gate (LInB GetKeysRequirement: received Keys >= arrKeysNeeded[hub]).
	// Open-when-free: a randomized seed that includes the arenas ALWAYS sends a
	// non-type-0 stage-1 for them -- a FREE arena arrives as the explicit
	// always-true "0 trophies" ({type 1, count 0}), so it opens here with no
	// vanilla-key AND (only its hub door still gates it physically). type 0 is
	// reserved for vanilla unlock mode / arenas-not-included, where the vanilla
	// gate below is the correct behaviour.
	if (physLevelID == 18 || physLevelID == 19 ||
	    physLevelID == 21 || physLevelID == 23)
	{
		if (ctr_cfg_active() && physLevelID < CTR_CFG_PAD_COUNT &&
		    ctr_cfg.warp_pad_unlock[physLevelID].stage1.type != 0)
			return AP_BossReqMet(&ctr_cfg.warp_pad_unlock[physLevelID].stage1);
		return AP_GateCount(AP_IDX_KEY) >=
		       D232.arrKeysNeeded[data.metaDataLEV[physLevelID].hubID];
	}

	// Gem cups (100..104): randomized req overrides; else vanilla 4 Tokens of the
	// cup's colour. gem_cup_unlock is keyed by cup colour 0..4 (identity pad).
	if (physLevelID >= 100 && physLevelID < 105)
	{
		int cupIdx = physLevelID - 100;
		if (ctr_cfg_active() && ctr_cfg.gem_cup_unlock[cupIdx].stage1.type != 0)
			return AP_BossReqMet(&ctr_cfg.gem_cup_unlock[cupIdx].stage1);
		return AP_GateCountTokenColour(cupIdx) >= 4;
	}

	return 1; // unknown pad -> treat as enterable (defensive; AP_PadState gates)
}

// The effective player at a physical pad. A met racer lock OVERRIDES the chosen
// racer, because AH_WarpPad.c calls AP_RacerLock_ForceForWarp immediately before
// the load reads characterIDs[0] -- so the hub-time chosen racer is not what
// will actually race. Predicting from the chosen racer alone could advertise a
// locked guest as an opponent, or seat the player as their own opponent.
static int AP_HitEffectivePlayer(int physLevelID)
{
	int chosen = (data.characterIDs[0] >= 0) ? (int)data.characterIDs[0] : -1;
	int lock = ctr_cfg_racer_lock(physLevelID);
	return AP_HitEffectivePlayerPure(chosen, lock, ctr_cfg_racer_lock_met(physLevelID));
}

// Is there a Hit opportunity behind this pad? Under the pool draw every eligible
// target rotates into every supported destination's field, so any eligible
// racer other than the effective player with an unchecked Hit location is an
// opportunity at the sixteen retail tracks (0..15) and the two trial Trophy
// tracks (16/17). A trial whose trial row is invalid has no Trophy route, so it never
// reports an opportunity (the pad must not stay open with nothing to earn);
// logged once per trial. Cups are ticket 11. Counted by AP_PadState as BOTH a
// remaining location and a plain-rerace check, so Done cannot hard-lock the pad
// while a target can still be encountered, and a stage-2-locked pad keeps its
// phase-1 plain rerace. Also consumed by AH_WarpPad.c's tier-2 choosers.
int AP_HitPadOpportunity(int physLevelID, int destLevelID)
{
	int trialValid = 1;

	if (!AP_HitEncounterEnabled())
		return 0;
	if (destLevelID == 16 || destLevelID == 17)
	{
		int trial = destLevelID - 16;
		trialValid = ctr_cfg.trial_track_valid[trial];
		if (!AP_HitPadDestEligiblePure(destLevelID, trialValid))
		{
			static unsigned s_trialInvalidLogged;
			if ((s_trialInvalidLogged & (1u << trial)) == 0)
			{
				char msg[128];
				s_trialInvalidLogged |= (1u << trial);
				snprintf(msg, sizeof msg,
				         "[AP HIT] trial destination %d has no valid trial row; "
				         "no Hit opportunity\n",
				         destLevelID);
				AP_LogLine(msg);
			}
			return 0;
		}
	}
	else if (!AP_HitPadDestEligiblePure(destLevelID, 1))
		return 0;

	return AP_HitEncounterOpportunity(destLevelID, AP_HitEffectivePlayer(physLevelID)) >= 0;
}

void AP_HitLogChooser(int physLevelID, int destLevelID, int action, int route,
                      int tokenLeft, int relicLeft)
{
	static const char *const kNames[] = {"none", "open", "wait", "plain",
	                                     "apply", "cancel", "vanish"};
	char line[160];
	if (action == AP_HIT_CHOOSER_NONE || action == AP_HIT_CHOOSER_WAIT)
		return;
	if (action < 0 || action > AP_HIT_CHOOSER_VANISH)
		return;
	snprintf(line, sizeof line,
	         "[AP HIT] chooser pad=%d dest=%d action=%s route=%d token=%d relic=%d\n",
	         physLevelID, destLevelID, kNames[action],
	         action == AP_HIT_CHOOSER_APPLY ? route : (action == AP_HIT_CHOOSER_PLAIN ? 0 : -1),
	         tokenLeft, relicLeft);
	AP_LogLine(line);
}

// The unified pad state (Warp-Pad State Model v2). Returns:
//   1 = Locked      (stage-1 unmet)                         -> RED
//   2 = Raceable    (stage-1 met, primary check available)  -> GREEN (flicker)
//   3 = Re-locked   (race dest only: trophy checked,
//                    stage-2 unmet)                          -> ORANGE
//   4 = Tier-2 open (race dest only: stage-2 met, checks
//                    remain)                                 -> PERIWINKLE
//   5 = Done        (all destination locations checked;
//                    HARD-LOCKED, pure UX)                   -> GRAY
//   0 = vanilla / not applicable (no slot_data, or an
//       unrecognised destination)                           -> untouched
// physLevelID = the physical pad (requirement key); destLevelID = the loaded
// destination track (location + lifecycle-category key). Under no shuffle these
// are equal; the callers pass both so it is correct under destination shuffle.
int AP_PadState(int physLevelID, int destLevelID)
{
	// Sized for the widest case: a cup pad aggregates its four legs' rungs
	// (4 * CTR_CFG_PODIUM_RUNG_COUNT) plus its own gem bit.
	int uncBits[24];
	int uncN;
	int boxesLeft;
	int lettersLeft;
	int wumpaLeft;
	int perfectLeft;
	int hitOpp;
	int weaponOpp;

	if (!ctr_cfg_active())
		return 0; // vanilla mode -> caller leaves the pad untouched
	if (!AP_DestKnown(destLevelID))
		return 0;

	// Rung-AWARE count (issue #61). The Done state below hard-locks the pad, so it
	// must not trigger while ANY location behind this pad is still checkable. The
	// podium rungs are exactly such locations, and for a cup they live on the four
	// LEG tracks rather than on the cup itself. Counting only the plain tier bits
	// meant a Gem Cup went Done the moment its gem was checked, hard-locking the pad
	// while its legs' held/finish rungs were still uncollected -- and because a cup
	// leg is not independently raceable from the hub, those checks became
	// unreachable for the rest of the seed. The same held for a race pad whose tier
	// bits were all checked while a rung remained.
	// AP_PadUncollectedGlowBits already enumerates rungs and already aggregates the
	// four cup legs, and it degrades to the plain count when podium checks are off,
	// so seeds without the feature are byte-identical.
	uncN = AP_PadUncollectedGlowBits(destLevelID,
	                                 uncBits, (int)(sizeof uncBits / sizeof uncBits[0]));

	// Box-AWARE, and for the same reason the count above is rung-aware (§6, ruled
	// 2026-08-11): both Done and Re-locked strand whatever is left behind the pad,
	// and an itemsanity box is exactly such a location. Boxes carry no AdvProgress
	// bit, so they cannot ride in uncBits and are counted separately.
	boxesLeft = AP_PadUncollectedBoxCount(destLevelID);
	// Letter locations carry no AdvProgress reward bit, just like boxes. They
	// belong to this destination's CTR Challenge and must keep the pad out of
	// Done even after the ordinary Token location is checked.
	lettersLeft = AP_PadUncollectedLetterCount(destLevelID);
	// Per-track Wumpa is track-owned but has several physical routes. Keep a
	// standalone pad and every legging Cup out of Done until the server checks
	// it. Before stage 2 it needs the plain Trophy-race route; after stage 2 the
	// entry chooser treats it as a CTR Challenge-side reason to remain open.
	wumpaLeft = AP_PadUncollectedWumpaCount(destLevelID);
	// The Relic Race Perfect check has no AdvProgress bit either. Without this
	// the pad went Done (hard-locked) once all three relics were checked, even
	// with the perfect still open (#439).
	perfectLeft = AP_PadUncollectedRelicPerfectCount(destLevelID);
	// The trial Trophy and CTR Challenge are already in uncBits (#343).

	// Hit Character encounter (pool draw): an eligible racer other than the
	// player with an unchecked Hit location is a remaining check AND needs a
	// plain race, so it joins both counters. Without the reRaceChecksStanding half a stage-2-locked pad would
	// Re-lock and strand the guest; without the uncCount half the pad would go
	// Done and hard-lock it.
	hitOpp = AP_HitPadOpportunity(physLevelID, destLevelID);
	uncN += hitOpp;

	// Itemsanity use check (#452): same two-counter shape as the Hit
	// opportunity. A received weapon is fired in any retail race, and logic
	// expects one to be available, so the pad must neither go Done nor
	// Re-lock while a received weapon still has an unchecked use check.
	weaponOpp = AP_ItemsanityPadOpportunity(destLevelID);
	uncN += weaponOpp;

	// The table itself lives in ap_pad_state.h so the harness can pin it out of
	// engine; everything above is the gather. Requirements key off the PHYSICAL
	// pad, lifecycle facts off the DESTINATION, which is the model's whole
	// keying invariant.
	return AP_PadStateDecide(AP_DestIsRace(destLevelID),
	                         AP_PadStage1Met(physLevelID),
	                         ctr_cfg_racer_lock_met(physLevelID),
	                         AP_DestTrophyChecked(destLevelID),
	                         ctr_cfg_warp_stage2_unlocked(physLevelID),
	                         uncN + lettersLeft + wumpaLeft + perfectLeft,
	                         boxesLeft + wumpaLeft + hitOpp + weaponOpp);
}

// Is this pad in the phase-1 re-entry window? True exactly when the
// destination's trophy race is already checked and the pad is STILL state 2
// Raceable. For a race destination with the trophy checked, that means an AP
// box or per-track Wumpa check still needs a plain adventure re-race. Any other
// route to 2 has an unchecked trophy or a non-race destination, both excluded.
//
// Defined ON TOP of AP_PadState rather than beside it deliberately: the entry
// gate and the pad's look in AH_WarpPad.c consume this, so they cannot drift
// from the state the map paints. That drift IS issue #232 -- the state model
// promised the pad stayed enterable and the gate had no path for it.
int AP_PadPhase1ReRaceable(int physLevelID, int destLevelID)
{
	if (!ctr_cfg_active())
		return 0;
	if (!AP_DestIsRace(destLevelID))
		return 0;
	if (!AP_DestTrophyChecked(destLevelID))
		return 0;
	return AP_PadState(physLevelID, destLevelID) == 2;
}

// The warp-pad entry fail-safe (2026-10-09). AH_WarpPad.c calls this when a
// pad that is not Done reached an entry route with nothing to load. The kart is
// released instead of being held in the warp beam; this line names the pad so a
// player log shows which counted check had no route.
void AP_PadLogEntryRefused(int physLevelID, int destLevelID)
{
	char msg[160];

	snprintf(msg, sizeof msg,
	         "[AP PAD] pad %d -> dest %d: entry refused, no route can load a race "
	         "for the open checks (state=%d); kart released\n",
	         physLevelID, destLevelID, AP_PadState(physLevelID, destLevelID));
	AP_LogLine(msg);
}

// Human-readable name for an AP_PadRoute code. The tier-2 half is written out
// as BASE + the tier-2 enumerator rather than as bare numbers, so a route added
// to AP_PadTier2RouteDecide shows up here as a missing case, not a wrong label.
static const char *AP_PadRouteName(int route)
{
	switch (route)
	{
	case AP_PAD_ROUTE_S2LOCKED_PLAIN_RERACE:
		return "plain-re-race(stage2-locked)";
	case AP_PAD_ROUTE_S2LOCKED_INERT:
		return "inert(stage2-locked)";
	case AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_MENU:
		return "tier2-menu";
	case AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_TOKEN:
		return "tier2-token";
	case AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_RELIC:
		return "tier2-relic";
	case AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_BOX_RERACE:
		return "box-re-race(tier2)";
	case AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_DONE:
		return "done-defensive";
	default:
		break;
	}
	return "unknown";
}

// One "[AP PAD]" line per pad-decision TRANSITION (issues #232 / #265). Both
// gates that call this sit inside AH_WarpPad_ThTick and therefore run every
// frame the player stands on the pad, so the whole reported tuple is remembered
// and an unchanged decision stays silent. Only one pad can be in the entry
// sequence at a time, so a single remembered tuple cannot thrash between pads.
void AP_PadLogRoute(int physLevelID, int destLevelID, int route)
{
	static int s_phys = -1;
	static int s_dest = -1;
	static int s_route = -1;
	static int s_state = -1;
	static int s_trophy = -1;
	static int s_token = -1;
	static int s_relic = -1;
	static int s_boxes = -1;
	static int s_wumpa = -1;

	int uncBits[24];
	int uncN;
	int k;
	int state;
	int trophyChecked;
	int tokenLeft = 0;
	int relicLeft = 0;
	int boxesLeft;
	int wumpaLeft;
	char msg[192];

	if (!ctr_cfg_active())
		return; // vanilla mode -- no AP pad lifecycle to describe
	if (!AP_DestKnown(destLevelID))
		return;

	state = AP_PadState(physLevelID, destLevelID);
	trophyChecked = AP_DestTrophyChecked(destLevelID) ? 1 : 0;
	boxesLeft = AP_PadUncollectedBoxCount(destLevelID);
	wumpaLeft = AP_PadUncollectedWumpaCount(destLevelID);

	// The same enumerator and the same offsets the tier-2 chooser itself reads,
	// so the logged flags cannot disagree with the decision they explain. These
	// are "an unchecked location of this tier still exists" -- a tier this seed
	// never placed reports 0 here exactly as it does to the chooser.
	uncN = AP_PadUncollectedBits(destLevelID, uncBits,
	                             (int)(sizeof uncBits / sizeof uncBits[0]));
	for (k = 0; k < uncN; k++)
	{
		int off = uncBits[k] - destLevelID;
		if (destLevelID == AP_CORTEX_DEST)
		{
			if (uncBits[k] == AP_CortexPseudoBit(AP_CV_SLOT_TOKEN))
				tokenLeft = 1;
			else if (AP_CortexPseudoRelicTier(uncBits[k]) >= 0)
				relicLeft = 1;
			continue;
		}
		if (off == ADV_REWARD_FIRST_CTR_TOKEN)
			tokenLeft = 1;
		else if (off == ADV_REWARD_FIRST_SAPPHIRE_RELIC ||
		         off == ADV_REWARD_FIRST_GOLD_RELIC ||
		         off == ADV_REWARD_FIRST_PLATINUM_RELIC)
			relicLeft = 1;
	}
	tokenLeft = AP_PadTokenSideLeft(
	    tokenLeft, AP_PadUncollectedLetterCount(destLevelID), wumpaLeft);
	relicLeft = AP_PadRelicSideLeft(relicLeft,
	                                AP_PadUncollectedRelicPerfectCount(destLevelID));

	if (physLevelID == s_phys && destLevelID == s_dest && route == s_route &&
	    state == s_state && trophyChecked == s_trophy && tokenLeft == s_token &&
	    relicLeft == s_relic && boxesLeft == s_boxes && wumpaLeft == s_wumpa)
		return; // nothing the line reports has changed since it was last emitted

	s_phys = physLevelID;
	s_dest = destLevelID;
	s_route = route;
	s_state = state;
	s_trophy = trophyChecked;
	s_token = tokenLeft;
	s_relic = relicLeft;
	s_boxes = boxesLeft;
	s_wumpa = wumpaLeft;

	snprintf(msg, sizeof msg,
	         "[AP PAD] pad %d -> dest %d: state=%d trophy_checked=%d token_left=%d "
	         "relic_left=%d boxes_left=%d wumpa_left=%d route=%s\n",
	         physLevelID, destLevelID, state, trophyChecked, tokenLeft, relicLeft,
	         boxesLeft, wumpaLeft, AP_PadRouteName(route));
	AP_LogLine(msg);
}

#endif // CTR_AP
