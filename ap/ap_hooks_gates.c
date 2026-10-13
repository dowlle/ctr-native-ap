// ap_hooks_gates.c -- part of the ap_hooks unit (issue #305).
// Gates: received-item gate counts, door history, requirement tints and
// colours, the gem-cup return hub, requirement comparators, boss and Oxide
// garages, requirement adverts and the warp-pad unlock checks.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

int AP_GateCount(int itemType)
{
	if (itemType < 0 || itemType >= AP_ITEM_INDEX_COUNT)
		return 0;
	return ap_recv_count[itemType];
}

int AP_DoorHistoryEnabled(void)
{
	return ctr_cfg_active();
}
int AP_DoorHistoryReady(void)
{
	return ctr_cfg_active() && !ctr_cfg.schema_newer && ap_net_doors_ready();
}
int AP_DoorHistoryOpen(int level, int door)
{
	unsigned bit = AP_DoorBitPure(level, door);
	unsigned opened = AP_DoorHistoryReady() ? ap_net_doors_history() : ap_net_doors_session();
	int open = bit && ctr_cfg_active() && !ctr_cfg.schema_newer && (opened & bit) &&
	           AP_GateCount(AP_IDX_KEY) >= AP_DoorKeysPure(bit);
	if (open && AP_DoorHistoryReady()) ap_net_doors_mark_session(bit);
	return open;
}
void AP_DoorHistoryRecord(int level, int door)
{
	unsigned bit = AP_DoorBitPure(level, door);
	if (bit && ctr_cfg_active() && !ctr_cfg.schema_newer && AP_GateCount(AP_IDX_KEY) >= AP_DoorKeysPure(bit))
		ap_net_doors_record(bit);
}
void AP_DoorHistoryReconcile(int level, int door)
{
	unsigned bit = AP_DoorBitPure(level, door);
	unsigned mask = AP_DoorStoryPure(bit);
	if (!ctr_cfg_active()) return;
	sdata->advProgress.storyFlags = (sdata->advProgress.storyFlags & ~mask) |
	                               (AP_DoorHistoryOpen(level, door) ? mask : 0);
}

// FOREIGN-only received count for one item index (issue #85). The seed verifier
// seeds its simulated tally from this (multiworld receipts + starting inventory)
// and banks OWN-world items from the scout cache as it opens each location, so the
// own component follows checked-state synchronously and cannot race the drain.
int AP_GateCountForeign(int itemType)
{
	if (itemType < 0 || itemType >= AP_ITEM_INDEX_COUNT)
		return 0;
	return ap_recv_count_foreign[itemType];
}

int AP_VerifyForeignItemCount(int itemIndex)
{
	if (itemIndex < 0 || itemIndex >= AP_VERIFY_ITEM_INDEX_COUNT)
		return 0;
	return ap_verify_recv_foreign[itemIndex];
}

int AP_GateCountTokenColour(int colour)
{
	if (colour < 0 || colour > 4)
		return 0;
	return ap_recv_count[AP_IDX_TOKEN_RED + colour]; // 4..8
}

int AP_GateCountGemColour(int colour)
{
	if (colour < 0 || colour > 4)
		return 0;
	return ap_recv_count[AP_IDX_GEM_RED + colour]; // 9..13
}

// ── any-of aggregate counters (requirement_specificity = any_of) ──
// Sum the received-item counts across a whole TYPE, so a type 6/7/8 gate means
// "any N of that type regardless of colour/tier". These read the same
// authoritative ap_recv_count[] counters as AP_GateCount above.
int AP_GateCountTokenSum(void)
{
	return AP_GateCountTokenColour(0) + AP_GateCountTokenColour(1) +
	       AP_GateCountTokenColour(2) + AP_GateCountTokenColour(3) +
	       AP_GateCountTokenColour(4);
}

int AP_GateCountRelicSum(void)
{
	return AP_GateCount(AP_IDX_SAPPHIRE) + AP_GateCount(AP_IDX_GOLD) +
	       AP_GateCount(AP_IDX_PLATINUM);
}

// Received count for a specific-tier (type 4) relic requirement. As of schema 4
// the apworld emits the tier in `colour`: 0 = Sapphire, 1 = Gold, 2 = Platinum.
// The tiers are INDEPENDENT (no downward hierarchy). A legacy schema-3 type-4 req
// arrives colour -1; the apworld's stage-1 relic rules have always meant Sapphire
// specifically, so -1 (and any out-of-range colour) resolves to Sapphire -- NOT
// the tri-tier sum, which was out-of-logic permissive. Type 7 (AnyRelic) keeps
// AP_GateCountRelicSum; this is only the type-4 tier selector.
int AP_GateCountRelicTier(int colour)
{
	switch (colour)
	{
	case 1:  return AP_GateCount(AP_IDX_GOLD);
	case 2:  return AP_GateCount(AP_IDX_PLATINUM);
	default: return AP_GateCount(AP_IDX_SAPPHIRE); // colour 0 or legacy -1
	}
}

// Oxide's Final Challenge door gate (issue #23; schema >= 5). Reads the per-seed
// relic-goal mode + count against the received relic-tier counts. Tiers are
// INDEPENDENT items (no downward hierarchy), so this mirrors the apworld
// FinalOxideUnlock completion condition exactly:
//   sapphire/gold/platinum -> that ONE tier's received count >= count
//   any    -> at least one tier's count >= count
//   total  -> Sapphire + Gold + Platinum summed >= count
// Phase-1 fallback (no slot_data active): the vanilla 18-Sapphire rule.
int AP_OxideFinalOpen(void)
{
	if (!ctr_cfg_active())
		return AP_GateCount(AP_IDX_SAPPHIRE) >= 18;

	int n = ctr_cfg.oxide_final_count;
	int s = AP_GateCount(AP_IDX_SAPPHIRE);
	int g = AP_GateCount(AP_IDX_GOLD);
	int p = AP_GateCount(AP_IDX_PLATINUM);

	return AP_RelicGoalMet(ctr_cfg.oxide_final_unlock, n, s, g, p);
}

// ── Requirement-hologram relic tint (closed pad; see the ap_hooks.h note) ──
// Tint tier for the relic icon a LOCKED pad shows for its relic REQUIREMENT. A
// specific-tier req (type 4) pins that tier's colour so it no longer cycles;
// AnyRelic (type 7) and any non-relic req keep the cycle (negative return). The
// colour->tier map mirrors AP_GateCountRelicTier exactly, so display and gate
// agree (legacy colour -1 = Sapphire).
int AP_ReqRelicTintTier(const ctr_req *r)
{
	if (r == 0 || r->type != 4)
		return -1; // AnyRelic (type 7) or non-relic -> cycle
	switch (r->colour)
	{
	case 1:  return 1; // Gold
	case 2:  return 2; // Platinum
	default: return 0; // Sapphire (colour 0 or legacy -1)
	}
}

// Per-physical-pad store of the above, written by LInB at the moment it births
// the closed requirement icon (it alone knows which pad/stage requirement is
// shown) and read by the destination-keyed ThTick. Split like ctr_cfg's own
// arrays: dense 0..27 for warp pads, 5-wide for gem cups (LevelID 100..104). The
// values are only read when the shown model is a relic, which is exactly when
// LInB has just written this pad's slot, so the zero-init (Sapphire) is never
// observed for an unwritten pad; unknown pads still report cycle defensively.
static signed char s_warpReqRelicTint[CTR_CFG_PAD_COUNT];
static signed char s_cupReqRelicTint[5];

void AP_SetWarpReqRelicTint(int physLevelID, int tier)
{
	if (physLevelID >= 0 && physLevelID < CTR_CFG_PAD_COUNT)
		s_warpReqRelicTint[physLevelID] = (signed char)tier;
	else if (physLevelID >= 100 && physLevelID < 105)
		s_cupReqRelicTint[physLevelID - 100] = (signed char)tier;
}

int AP_WarpReqRelicTint(int physLevelID)
{
	if (physLevelID >= 0 && physLevelID < CTR_CFG_PAD_COUNT)
		return s_warpReqRelicTint[physLevelID];
	if (physLevelID >= 100 && physLevelID < 105)
		return s_cupReqRelicTint[physLevelID - 100];
	return -1; // unknown pad -> cycle (matches pre-pin behaviour)
}

// Gem sibling of the relic-tint trio above (see ap_hooks.h): a type-5 req pins
// its ONE gem colour on the closed-pad hologram; AnyGem (type 8) and non-gem
// reqs keep the vanilla 5-colour cycle (negative). The colour index maps
// straight onto data.AdvCups[0..4], matching AP_GateCountGemColour's keying so
// display and gate agree.
int AP_ReqGemColour(const ctr_req *r)
{
	if (r == 0 || r->type != 5)
		return -1; // AnyGem (type 8) or non-gem -> cycle
	return (r->colour >= 0 && r->colour < 5) ? r->colour : -1;
}

// Per-physical-pad store, same layout + lifecycle as the relic-tint store:
// written by LInB at the closed-icon birth (it alone knows which pad/stage
// requirement is shown), read back by the destination-keyed ThTick. LInB
// records for EVERY closed pad (a non-gem requirement records -1 = cycle), and
// ThTick only reads it when the icon model IS a gem, so a stale slot is never
// consumed.
static signed char s_warpReqGemColour[CTR_CFG_PAD_COUNT];
static signed char s_cupReqGemColour[5];

void AP_SetWarpReqGemColour(int physLevelID, int colour)
{
	if (physLevelID >= 0 && physLevelID < CTR_CFG_PAD_COUNT)
		s_warpReqGemColour[physLevelID] = (signed char)colour;
	else if (physLevelID >= 100 && physLevelID < 105)
		s_cupReqGemColour[physLevelID - 100] = (signed char)colour;
}

int AP_WarpReqGemColour(int physLevelID)
{
	if (physLevelID >= 0 && physLevelID < CTR_CFG_PAD_COUNT)
		return s_warpReqGemColour[physLevelID];
	if (physLevelID >= 100 && physLevelID < 105)
		return s_cupReqGemColour[physLevelID - 100];
	return -1; // unknown pad -> cycle (matches pre-pin behaviour)
}

// Token sibling of the gem trio above (issue #19): a specific-colour token
// requirement (type 3) pins its ONE colour on the closed-pad token icon --
// the icon-birth clamp made every non-cup token gate read as Red (a 1x Purple
// gate shown red, live report 2026-07-16). AnyToken (type 6) returns -1 =
// cycle all five colours. Any OTHER requirement returns -2 = "no resolved
// token requirement", which the display treats as leave-the-birth-colour-
// alone: unlike gems (where -1 = cycle IS the vanilla look), a vanilla cup
// pad's token icon is a FIXED cup colour, so the no-opinion state must be
// distinct from the cycle. Colour indices map straight onto data.AdvCups[],
// matching AP_GateCountTokenColour's keying so display and gate agree.
int AP_ReqTokenColour(const ctr_req *r)
{
	if (r == 0 || (r->type != 3 && r->type != 6))
		return -2;
	if (r->type == 6)
		return -1; // AnyToken -> cycle
	return (r->colour >= 0 && r->colour < 5) ? r->colour : -1;
}

static signed char s_warpReqTokenColour[CTR_CFG_PAD_COUNT];
static signed char s_cupReqTokenColour[5];

void AP_SetWarpReqTokenColour(int physLevelID, int colour)
{
	if (physLevelID >= 0 && physLevelID < CTR_CFG_PAD_COUNT)
		s_warpReqTokenColour[physLevelID] = (signed char)colour;
	else if (physLevelID >= 100 && physLevelID < 105)
		s_cupReqTokenColour[physLevelID - 100] = (signed char)colour;
}

int AP_WarpReqTokenColour(int physLevelID)
{
	if (physLevelID >= 0 && physLevelID < CTR_CFG_PAD_COUNT)
		return s_warpReqTokenColour[physLevelID];
	if (physLevelID >= 100 && physLevelID < 105)
		return s_cupReqTokenColour[physLevelID - 100];
	return -2; // unknown pad -> leave the birth colour (matches pre-pin behaviour)
}

int AP_GateCountGemSum(void)
{
	return AP_GateCountGemColour(0) + AP_GateCountGemColour(1) +
	       AP_GateCountGemColour(2) + AP_GateCountGemColour(3) +
	       AP_GateCountGemColour(4);
}

// ── Gem-cup return hub (destination-shuffle correctness; see the ap_hooks.h note) ──
// Physical hub the player entered the current/last gem cup from, captured at cup
// entry before the four cup-track loads clobber gGT->prevLEV. Read only by the
// ADVENTURE_CUP exit-to-map paths, which run immediately after a cup, so a value
// left over from an earlier cup can never be consumed by a non-cup return. -1 =
// never set this session. AP_CupReturnHub gates on ctr_cfg_active() so a vanilla
// (no-slot_data) run always falls back to the hard-coded GEM_STONE_VALLEY return.
static int ap_cup_return_hub = -1;

void AP_CupEnterFromHub(int hubLevelID)
{
	ap_cup_return_hub = hubLevelID;
	AP_WinStakesOnCupEnter(); // #449: the cup's first leg fixes its stakes
}

int AP_CupReturnHub(void)
{
	if (!ctr_cfg_active())
		return -1;
	return ap_cup_return_hub;
}

// ── Phase 2: per-seed resolved-requirement comparators ──
// These live C-side (not in ap_seedcfg.cpp) because they read the received-item
// counters above (AP_GateCount*) and the per-track numTrophiesToOpen threshold
// from data.metaDataLEV[], both of which are unreachable from the isolated C++
// slot_data parser. ctr_cfg + ctr_cfg_active()/ctr_cfg_warp_dest() come from
// ap_seedcfg.{h,cpp}; the gate sites (AH_*.c) call these by name.

// Returns owned >= count for a resolved requirement. colour-aware for tokens
// (type 3), relics (type 4), and gems (type 5): for tokens/gems colour 0..4
// selects one colour; for relics colour 0/1/2 selects Sapphire/Gold/Platinum
// (independent tiers, schema 4). A -1 colour is the legacy schema-3 shape:
// token/gem -1 sums the vanilla "all colours" interpretation (defensive -- the
// apworld emits a concrete colour under specific_colour), and relic -1 resolves
// to Sapphire (the standing stage-1 relic meaning), via AP_GateCountRelicTier.
// The genuine any-of aggregates the apworld emits under requirement_specificity =
// any_of are the dedicated type 6/7/8 codes, which sum the whole type via
// AP_GateCount*Sum().
// Typed requirement comparator against an ARBITRARY counts array (indexed by
// the 15 AP_IDX_* item types). AP_BossReqMet delegates here with the live
// received tallies; the seed-verify sweep (ap_verify.c) calls it with its
// simulated counts, so the live gates and the verifier share one comparator
// and cannot drift apart.
int AP_ReqMetCounts(const ctr_req *r, const int *counts)
{
	int i, sum;
	if (r == 0)
		return 1;
	switch (r->type)
	{
	case 0:
		return 1; // no requirement -> always met (native vanilla rule applies elsewhere)
	case 1: // trophies
		return counts[AP_IDX_TROPHY] >= r->count;
	case 2: // keys
		return counts[AP_IDX_KEY] >= r->count;
	case 3: // tokens (colour 0..4 = one colour; -1 = any token, summed)
		if (r->colour >= 0 && r->colour <= 4)
			return counts[AP_IDX_TOKEN_RED + r->colour] >= r->count;
		/* fall through to the AnyToken sum */
	case 6: // AnyToken: any N tokens summed across all 5 colours
		for (sum = 0, i = 0; i < 5; i++)
			sum += counts[AP_IDX_TOKEN_RED + i];
		return sum >= r->count;
	case 4: // relic, tier by colour (0=Sapphire, 1=Gold, 2=Platinum; legacy -1=Sapphire)
		i = (r->colour >= 0 && r->colour <= 2) ? r->colour : 0;
		return counts[AP_IDX_SAPPHIRE + i] >= r->count;
	case 5: // gems (colour 0..4 = one colour; -1 = any gem, summed)
		if (r->colour >= 0 && r->colour <= 4)
			return counts[AP_IDX_GEM_RED + r->colour] >= r->count;
		/* fall through to the AnyGem sum */
	case 8: // AnyGem: any N gems summed across all 5 colours
		for (sum = 0, i = 0; i < 5; i++)
			sum += counts[AP_IDX_GEM_RED + i];
		return sum >= r->count;
	case 7: // AnyRelic: any N relics summed across Sapphire+Gold+Platinum
		return counts[AP_IDX_SAPPHIRE] + counts[AP_IDX_GOLD] +
		       counts[AP_IDX_PLATINUM] >= r->count;
	default:
		return 1;
	}
}

int AP_BossReqMet(const ctr_req *r)
{
	return AP_ReqMetCounts(r, ap_recv_count);
}

// Per-mode boss-garage gate for the four boss hubs (bossIdx 0..3 = Roo, Papu,
// Komodo, Pinstripe). bossgarage_mode 0 (Original4Tracks) / 1 (SameHubTracks):
// the garage opens once the player has WON each required race track for that boss
// (ctr_cfg.boss_tracks[bossIdx][0..n-1], emitted by the apworld -- vanilla
// LevelIDs for mode 0, destination-remapped LevelIDs for mode 1). A track counts
// as WON when its trophy-race AP location has been checked --
// AP_LocationCheckedByBit(trackLevelID + ADV_REWARD_FIRST_TROPHY), NOT
// CHECK_ADV_BIT: in AP the AdvProgress trophy bit reflects RECEIVED items, not the
// races the player actually won, so only the checked-location signal is correct
// here. mode 2 (Trophies) and any boss with no track list fall back to the flat
// trophy-count requirement via AP_BossReqMet. Oxide (bossIdx 4) is a key gate and
// is never routed through this function.
int AP_BossGarageOpen(int bossIdx)
{
	if (bossIdx < 0 || bossIdx >= CTR_CFG_BOSS_COUNT)
		return 1;

	int mode = ctr_cfg.bossgarage_mode;
	int n = ctr_cfg.boss_n_tracks[bossIdx];

	if ((mode == 0 || mode == 1) && n > 0)
	{
		for (int i = 0; i < n; i++)
		{
			int track = ctr_cfg.boss_tracks[bossIdx][i];
			if (track < 0)
				continue;
			if (!AP_LocationCheckedByBit(track + ADV_REWARD_FIRST_TROPHY))
				return 0;
		}
		return 1;
	}

	// mode 2 (Trophies) or no track list -> flat trophy-count requirement.
	return AP_BossReqMet(&ctr_cfg.boss_req[bossIdx]);
}

// Has the player personally cleared Oxide's FIRST challenge in this seed?
//
// Read from the #458 boss-won flags (ap/ap_boss_won_flags.h), kept per seed,
// team and slot in server data storage, never from the ap_oxide_first_beaten
// session boolean: that boolean is reset on every connect (see the
// slot-reset block), so a reconnecting player would be offered the first
// challenge again when the Final one is what comes next. The flags survive
// reconnect, profile load and hub re-entry, which is what issue #321's
// implementation check 2 asks for.
//
// Not the checked N. Oxide's Challenge location either (#458): another
// player's Collect can check it, and under any_percent that made the garage
// offer the Final Challenge, whose win cannot complete any_percent.
int AP_OxideFirstChallengeCleared(void)
{
	return (ap_net_boss_won_bits() & AP_BOSS_WON_OXIDE_FIRST) != 0;
}

// Gather the inputs the one Oxide decision needs. Factored out so the gate,
// the encounter selector and the locked-door advert cannot assemble different
// answers to the same questions -- the drift this rework exists to end.
static AP_OxideGarageInputs AP_OxideInputs(void)
{
	AP_OxideGarageInputs in;

	in.garageReqMet = AP_BossReqMet(&ctr_cfg.boss_req[4]) != 0;
	in.goalOxide = ctr_cfg.goal_oxide;
	in.firstCleared = AP_OxideFirstChallengeCleared();
	in.finalRelicMet = AP_OxideFinalOpen() != 0;
	in.goalBosses = ctr_cfg.goal_bosses;
	in.bossesWon = AP_ComposedBossesWon();
	in.goalGems = ctr_cfg.goal_gems;
	in.gemsHeld = AP_GateCountGemSum();
	in.firstOptional = ctr_cfg.oxide_1_optional;
	return in;
}

// THE canonical Oxide garage entry gate. Every Oxide entry surface -- the map
// icon (AH_Map.c), the in-hub door presentation (AH_Garage_LInB), the
// collision/refusal and the actual level load (AH_Garage_ThTick) -- asks this
// one function, so a fix cannot land on the door while another path stays open.
//
// Composition is now ENCOUNTER-AWARE (issues #320/#321, ruling of 2026-09-03);
// the whole rule and the reasoning behind it live in ap/ap_oxide_encounter.h.
// In short: the door opens for the encounter the garage is currently OFFERING,
// which is the first challenge until it has been cleared. The companion goal
// arms gate only the selected finale, and goal_oxide 3 (`disabled`) shuts the
// garage in every state.
//
// Without slot_data this is the Phase-1 fallback the two gate sites already
// carried (four received Keys), unchanged -- there is no goal to compose.
int AP_OxideGarageOpen(void)
{
	AP_OxideGarageInputs in;

	if (!ctr_cfg_active())
		return AP_GateCount(AP_IDX_KEY) >= 4;

	in = AP_OxideInputs();
	if (AP_OxideGarageOffersFinal(&in) && !AP_OxideFinalVenueReady())
		return 0;
	return AP_OxideGarageIsOpen(&in);
}

int AP_OxideFinalVenueReady(void)
{
	if (!ctr_cfg_active() || ctr_cfg.schema_version < 11)
		return 1;
	if (!ctr_cfg.oxide_final_venue.valid)
		return 0;
	if (ctr_cfg.oxide_final_venue.track == CTR_CFG_OXIDE_FINAL_OXIDE_STATION)
		return 1;
#ifdef CTR_CUSTOM_TRACKS
	return !ap_oxide_final_content_required && CustomTrack_OxideFinalReady();
#else
	return 0;
#endif
}

int AP_OxideFinalVenueEntryReady(void)
{
	if (!AP_OxideFinalVenueReady())
		return 0;
#ifdef CTR_CUSTOM_TRACKS
	if (ctr_cfg_active() && ctr_cfg.schema_version >= 11 &&
	    ctr_cfg.oxide_final_venue.track == CTR_CFG_OXIDE_FINAL_CORTEX_VORTEX &&
	    !CustomTrack_ReverifyOxideFinalContent())
	{
		ap_oxide_final_content_required = 1;
		return 0;
	}
#endif
	return 1;
}

// Schema 15: may the player enter a pad (or a Gem Cup) that leads to the
// Cortex Vortex pad track? Needs a valid block, no refusal latched, and the
// armed pair. forceVerify re-hashes both files, as the Oxide 2 entry does, so
// a same-size swap after connect is refused before the load begins.
int AP_CortexTrackEntryReady(int forceVerify)
{
	if (!ctr_cfg_active() || !ctr_cfg.cortex_track.valid ||
	    ap_cortex_track_content_required)
		return 0;
#ifdef CTR_CUSTOM_TRACKS
	// The full re-hash costs a few MB of SHA-256, so a player standing on the
	// pad (or re-entering within two seconds) reuses the last good answer.
	static unsigned int s_cvVerifiedAt = 0;
	static int s_cvVerifiedOnce = 0;
	unsigned int now = (sdata && sdata->gGT) ? (unsigned int)sdata->gGT->timer : 0;
	if (forceVerify && s_cvVerifiedOnce && now - s_cvVerifiedAt < 120)
		forceVerify = 0;
	if (forceVerify && CustomTrack_ReverifyOxideFinalContent())
	{
		s_cvVerifiedAt = now;
		s_cvVerifiedOnce = 1;
	}
	else if (forceVerify)
	{
		ap_cortex_track_content_required = 1;
		AP_AppendLog("[AP CORTEX] bundled Cortex Vortex pair failed its entry re-hash; "
		             "the pad track stays closed\n");
		return 0;
	}
	return CustomTrack_CortexTrackReady();
#else
	(void)forceVerify;
	return 0; // this build cannot serve custom track bytes
#endif
}

// Which encounter the garage is offering. THE selector: an uncleared first
// challenge takes priority even when the relic requirement for the Final
// Challenge is already satisfied, so a relic-rich player cannot skip the first
// check (the second half of issue #321's defect). Without slot_data the
// vanilla rule stands, and AP_OxideFinalOpen() already falls back to it.
int AP_OxideOffersFinalChallenge(void)
{
	AP_OxideGarageInputs in;

	if (!ctr_cfg_active())
		return AP_OxideFinalOpen();

	in = AP_OxideInputs();
	return AP_OxideGarageOffersFinal(&in);
}

// Issue #377: is the player in go mode for Oxide's Final Challenge, i.e. would
// the apworld (and so Universal Tracker) put "N. Oxide's Final Challenge" in
// logic from what this slot holds right now? The term-by-term mapping lives in
// ap/ap_oxide_cutscene.h next to AP_OxideFinalGoModePure. 0 without slot_data;
// the retail rule is applied by AP_OxideFinalSceneWanted instead.
int AP_OxideFinalGoMode(void)
{
	int boostLive, venueStation, boostMin, best, c;

	if (!ctr_cfg_active())
		return 0;

	boostLive = ctr_cfg.boost_mode == AP_CAP_MODE_SHARED_GLOBAL ||
	            ctr_cfg.boost_mode == AP_CAP_MODE_PER_CHARACTER;
	// Seeds before schema 11 always race the Final on Oxide Station. A newer
	// seed with an unreadable venue block keeps the garage shut anyway
	// (AP_OxideFinalVenueReady), so the venue default here cannot open it.
	venueStation = ctr_cfg.schema_version < 11 ||
	               !ctr_cfg.oxide_final_venue.valid ||
	               ctr_cfg.oxide_final_venue.track == CTR_CFG_OXIDE_FINAL_OXIDE_STATION;
	boostMin = AP_OxideFinalBoostMin(boostLive, venueStation,
	                                 ctr_cfg.shortcut_knowledge);

	best = -1;
	if (boostMin > 0)
		for (c = 0; c < AP_CAP_ROSTER_COUNT; c++)
		{
			int tier;
			if (!AP_CharacterUnlocked(c))
				continue;
			tier = AP_CapabilityBoostTierForCharacter(c);
			if (tier > best)
				best = tier;
		}

	return AP_OxideFinalGoModePure(AP_OxideOffersFinalChallenge(),
	                               AP_OxideGarageOpen(), boostMin, best);
}

// ── #24: plain-text requirement advert for the boss-class gates ──
// Warp pads advertise their configured requirement with an icon + count, but the
// four boss garages and Oxide's garage door only ever spoke through the vanilla
// Aku hints, whose wording is hardcoded to the RETAIL rules ("four trophies",
// "four keys") and therefore misstates every per-seed requirement. Ruled shape:
// a plain TEXT line, deliberately not the pads' icon vocabulary, because text
// can carry the relic-tier and gem/token-colour wording an icon cannot.
// Hub doors stay out of scope (they remain key-required, so the vanilla key hints
// are still accurate for them).

// Owned tally a requirement is judged against. The branch structure mirrors
// AP_ReqMetCounts case for case -- including the type-3/5 fall-through to the
// any-of sum on an out-of-range colour -- so the advert can never disagree with
// the gate about which counter is being compared.
static int AP_ReqOwned(const ctr_req *r)
{
	int i, sum;

	switch (r->type)
	{
	case 1: // trophies
		return ap_recv_count[AP_IDX_TROPHY];
	case 2: // keys
		return ap_recv_count[AP_IDX_KEY];
	case 3: // tokens (colour 0..4 = one colour)
		if (r->colour >= 0 && r->colour <= 4)
			return ap_recv_count[AP_IDX_TOKEN_RED + r->colour];
		/* fall through to the AnyToken sum */
	case 6: // AnyToken
		for (sum = 0, i = 0; i < 5; i++)
			sum += ap_recv_count[AP_IDX_TOKEN_RED + i];
		return sum;
	case 4: // relic, tier by colour (legacy -1 = Sapphire)
		i = (r->colour >= 0 && r->colour <= 2) ? r->colour : 0;
		return ap_recv_count[AP_IDX_SAPPHIRE + i];
	case 5: // gems (colour 0..4 = one colour)
		if (r->colour >= 0 && r->colour <= 4)
			return ap_recv_count[AP_IDX_GEM_RED + r->colour];
		/* fall through to the AnyGem sum */
	case 8: // AnyGem
		for (sum = 0, i = 0; i < 5; i++)
			sum += ap_recv_count[AP_IDX_GEM_RED + i];
		return sum;
	case 7: // AnyRelic
		return ap_recv_count[AP_IDX_SAPPHIRE] + ap_recv_count[AP_IDX_GOLD] +
		       ap_recv_count[AP_IDX_PLATINUM];
	default:
		return 0;
	}
}

static const char *AP_ADVERT_COLOURS[5] = {"Red", "Green", "Blue", "Yellow",
                                           "Purple"};

static int AP_ReqPhrase(const ctr_req *r, char *out, int cap);
static int AP_OxideGateAdvert(char *out, int cap);

// Writes the advert line for boss `bossIdx` (0..3 = Roo, Papu, Komodo, Pinstripe;
// 4 = Oxide) into `out` and returns 1, or returns 0 when there is nothing to
// advertise. Returning 0 deliberately leaves the vanilla Aku hint as the only
// message: with no slot_data the retail rules ARE the live rules, so the retail
// wording is correct and inventing a second line would be noise.
int AP_BossGateAdvert(int bossIdx, char *out, int cap)
{
	if (out == 0 || cap <= 0)
		return 0;
	if (!ctr_cfg_active())
		return 0;
	if (bossIdx < 0 || bossIdx >= CTR_CFG_BOSS_COUNT)
		return 0;

	// Oxide's door fronts two encounters with different requirements, so it
	// gets its own encounter-aware line (#321). Everything below is the
	// single-requirement shape the four boss garages still use.
	if (bossIdx == 4)
		return AP_OxideGateAdvert(out, cap);

	// Modes 0/1 gate on RACES WON, not on items held (see AP_BossGarageOpen), so
	// the advert has to speak in races or it would advertise a requirement that
	// is not the one being enforced.
	if ((ctr_cfg.bossgarage_mode == 0 || ctr_cfg.bossgarage_mode == 1) &&
	    ctr_cfg.boss_n_tracks[bossIdx] > 0)
	{
		int listed = ctr_cfg.boss_n_tracks[bossIdx];
		int required = 0;
		int won = 0;
		int i;

		for (i = 0; i < listed; i++)
		{
			int track = ctr_cfg.boss_tracks[bossIdx][i];
			if (track < 0)
				continue; // an absent track is skipped by the gate too
			required++;
			if (AP_LocationCheckedByBit(track + ADV_REWARD_FIRST_TROPHY))
				won++;
		}

		if (required == 0)
			return 0;

		snprintf(out, (size_t)cap, "Requires: win %d %s (have %d)", required,
		         (required == 1) ? "hub race" : "hub races", won);
		return 1;
	}

	return AP_ReqPhrase(&ctr_cfg.boss_req[bossIdx], out, cap);
}

// "4 Keys (have 2)" for a resolved requirement, written into `out`. Returns 0
// when the requirement resolves to nothing to say. Factored out of
// AP_BossGateAdvert so Oxide's encounter-aware advert can name the SAME door
// requirement in the SAME words without a second copy of the noun table.
static int AP_ReqPhrase(const ctr_req *r, char *out, int cap)
{
	const char *noun;
	const char *tint = "";
	int owned, need;

	if (out == 0 || cap <= 0 || r == 0)
		return 0;
	if (r->type == 0 || r->count <= 0)
		return 0; // no requirement resolved -> nothing to advertise

	need = r->count;

	switch (r->type)
	{
	case 1:
		noun = (need == 1) ? "Trophy" : "Trophies";
		break;
	case 2:
		noun = (need == 1) ? "Key" : "Keys";
		break;
	case 3:
		noun = (need == 1) ? "CTR Token" : "CTR Tokens";
		if (r->colour >= 0 && r->colour <= 4)
			tint = AP_ADVERT_COLOURS[r->colour];
		break;
	case 6:
		noun = (need == 1) ? "CTR Token" : "CTR Tokens";
		break;
	case 4:
		noun = (need == 1) ? "Relic" : "Relics";
		// Tier wording is the whole reason this advert is text and not an icon.
		tint = (r->colour == 1) ? "Gold" : (r->colour == 2) ? "Platinum" : "Sapphire";
		break;
	case 5:
		noun = (need == 1) ? "Gem" : "Gems";
		if (r->colour >= 0 && r->colour <= 4)
			tint = AP_ADVERT_COLOURS[r->colour];
		break;
	case 7:
		noun = (need == 1) ? "Relic" : "Relics";
		break;
	case 8:
		noun = (need == 1) ? "Gem" : "Gems";
		break;
	default:
		return 0;
	}

	owned = AP_ReqOwned(r);

	if (tint[0] != '\0')
		snprintf(out, (size_t)cap, "Requires: %d %s %s (have %d)", need, tint,
		         noun, owned);
	else
		snprintf(out, (size_t)cap, "Requires: %d %s (have %d)", need, noun, owned);

	return 1;
}

// Oxide's locked-door advert (issue #321 implementation check 5). The four
// boss garages keep AP_ReqPhrase's single-requirement line; Oxide's door is
// different because it fronts TWO encounters with different requirements, and
// the shipped line could describe neither: it always led with the shared Key
// requirement, even when the Key requirement was long satisfied and the real
// blocker was relics, boss wins or Gems.
//
// 2026-09-03 REPAIR (Sonnet review of the #320-322 candidate): this used to
// join every blocking term into ONE line with " + " and draw it with the
// non-wrapping DecalFont_DrawLine into a 128-byte buffer. The worst case
// (Final Challenge naming plus a relic term plus Boss and Gem terms) measured
// roughly 1489 px against the ~460 px safe width the same pixel model gives
// the pause checklist -- worse than the advert this branch replaced, because
// it added a "Final Challenge " header and the relic phrase on top of what
// was already there.
//
// The fix moved to ap/ap_oxide_garage_advert.h's pure AP_OxideGarageAdvertFormat
// (mirrors ap_goal_line.h's split: pure formatting logic tools/test-oxide-
// garage-advert.c can drive without an engine, plus this thin function that
// resolves the real ctr_cfg/AP_GateCount*/AP_ReqOwned values). This function's
// only job now is gathering those real numbers; see the header for the panel
// shape, the omission rule and the width proof.
static int AP_OxideGateAdvert(char *out, int cap)
{
	AP_OxideGarageInputs in = AP_OxideInputs();
	AP_OxideGarageState st = AP_OxideGarageEvaluate(&in);
	const ctr_req *door = &ctr_cfg.boss_req[4];
	int n = ctr_cfg.oxide_final_count;

	if (n <= 0)
		n = 18; // the vanilla rule, same fallback ap_verify.c uses

	return AP_OxideGarageAdvertFormat(
		st, door->type, door->count, door->colour, AP_ReqOwned(door),
		in.finalRelicMet, ctr_cfg.oxide_final_unlock, n,
		AP_GateCount(AP_IDX_SAPPHIRE), AP_GateCount(AP_IDX_GOLD),
		AP_GateCount(AP_IDX_PLATINUM), in.goalBosses, in.bossesWon,
		in.goalGems, in.gemsHeld, out, cap);
}

// #152/#322: the pause menu's composed-goal readout. Reads the SAME ctr_cfg
// fields and AP_GateCount*/AP_ComposedBossesWon helpers AP_EvaluateGoal
// reads, so the two cannot disagree about progress.
//
// The SHAPE changed in #322. It used to build one prose sentence -- "Goal: beat
// N. Oxide's Final Challenge (not yet) and win 2 of 4 boss races (have 1) and
// hold 3 of 5 Gems (have 1)" -- which AH_Pause.c draws once with
// DecalFont_DrawLine. That does not wrap, and the longest shape is about three
// times the safe pause-page width, so a tester on the 1 September Alpha 7
// stream could not read his own goal. It is now the approved compact
// checklist, "GOAL: OXIDE 2 - BOSS 3/4 - GEM 4/5", built by
// ap/ap_goal_line.h -- see that header for the labels, the omission rules and
// why a wrap and a marquee were both rejected.
//
// Returns 1 when a line was written, 0 when there is nothing to advertise (no
// slot_data -- the vanilla Aku hints stay the only message -- or no active
// goal arm, which generate_early already rejects on a real seed).
int AP_GoalAdvert(char *out, int cap)
{
	if (out == 0 || cap <= 0)
		return 0;
	if (!ctr_cfg_active())
		return 0;

	// The Oxide segment is driven by goal_oxide alone, so `optional` (0) and
	// `disabled` (3) both omit it: neither is a completion condition. Whether
	// the Oxide race has been BEATEN is deliberately not shown -- under the
	// approved encounter flow the enabled arms gate the selected finale, so
	// winning it completes the composed goal in the same moment.
	return AP_GoalLineFormat(out, cap, ctr_cfg.goal_oxide,
	                         ctr_cfg.goal_bosses, AP_ComposedBossesWon(),
	                         ctr_cfg.goal_gems, AP_GateCountGemSum());
}

// Trophy-track warp pad LOAD gate. When a per-seed requirement applies use it;
// otherwise fall back verbatim to the Phase-1 rule (received trophies vs the
// per-track numTrophiesToOpen). levelID is the physical pad LevelID (retail
// adventure numbering), valid for the 28-wide ctr_cfg arrays for trophy tracks.
// Racer lock for a physical pad, or -1 when it has none (#54/#209). Dense pads
// 0..27 via racer_lock, cup pads 100..104 via gem_cup_racer_lock -- the same
// two-array routing warp_pad_unlock uses, for the same reason.
int ctr_cfg_racer_lock(int physPadLevelID)
{
	if (!ctr_cfg_active())
		return -1;
	if (physPadLevelID >= 100 && physPadLevelID <= 104)
		return ctr_cfg.gem_cup_racer_lock[physPadLevelID - 100];
	if (physPadLevelID < 0 || physPadLevelID >= CTR_CFG_PAD_COUNT)
		return -1;
	return ctr_cfg.racer_lock[physPadLevelID];
}

// Is this pad's racer lock satisfied? 1 when the pad has no lock, so every
// caller can AND this unconditionally.
int ctr_cfg_racer_lock_met(int physPadLevelID)
{
	int required = ctr_cfg_racer_lock(physPadLevelID);
	if (required < 0)
		return 1;
	return AP_CharacterUnlocked(required);
}

int ctr_cfg_warp_unlocked(int levelID)
{
	// Racer lock (#54/#209): ANDed on top of whatever the pad already asks for,
	// never in place of it -- the apworld's rule does exactly the same thing
	// (Rules.add_racer_lock_rules captures the base rule and ANDs), and the two
	// sides have to agree or the golden rule is broken. Checked first so a
	// locked pad reads closed at every call site that goes through this gate:
	// the load gate, the spawn visual and the map advert are all one source.
	if (!ctr_cfg_racer_lock_met(levelID))
		return 0;

	if (ctr_cfg_active() && levelID >= 0 && levelID < CTR_CFG_PAD_COUNT &&
	    ctr_cfg.warp_pad_unlock[levelID].stage1.type != 0)
		return AP_BossReqMet(&ctr_cfg.warp_pad_unlock[levelID].stage1);

	// Cup PHYSICAL pads (100..104) live outside the dense warp_pad_unlock array.
	// Under `merged` destination shuffle a race destination can be hosted on a cup
	// pad, so ThTick's load gate (AH_WarpPad.c:713) recovers physLevelID = a cup
	// LevelID and calls this. Key by cup colour into gem_cup_unlock (randomized
	// stage1) else the vanilla "4 tokens of the cup's colour" rule -- the SAME
	// predicate AP_PadStage1Met's cup branch uses. MUST run before the trophy
	// fallback below, which would read data.metaDataLEV[100] out of bounds.
	if (levelID >= 100 && levelID <= 104)
	{
		int cupIdx = levelID - 100;
		if (ctr_cfg_active() && ctr_cfg.gem_cup_unlock[cupIdx].stage1.type != 0)
			return AP_BossReqMet(&ctr_cfg.gem_cup_unlock[cupIdx].stage1);
		return AP_GateCountTokenColour(cupIdx) >= 4;
	}

	// Phase-1 fallback: received trophies >= per-track threshold.
	return AP_GateCount(AP_IDX_TROPHY) >= data.metaDataLEV[levelID].numTrophiesToOpen;
}

// Trophy-track warp pad STAGE-2 gate (open-rando two-stage). Mirrors
// ctr_cfg_warp_unlocked but reads stage2: when active and the pad has a stage2
// requirement (type != 0) it returns owned >= count via AP_BossReqMet
// (colour-aware). type 0 (no stage2 / collapsed-to-stage1 / flat-v1 slot_data /
// inactive) returns 1 (always open), so the relic-Time-Trial + CTR-Token
// menu opens the moment stage1 is met -- identical to the pre-two-stage flow.
// physPadLevelID is the PHYSICAL pad LevelID, the same key stage1 uses.
int ctr_cfg_warp_stage2_unlocked(int physPadLevelID)
{
	const ctr_req *r = ctr_cfg_warp_stage2_req(physPadLevelID);
	if (r != 0)
		return AP_BossReqMet(r);

	// No stage2 requirement -> tier 2 opens as soon as stage1 is satisfied.
	return 1;
}

// The stage-2 requirement record for a physical pad, or NULL when it has none.
// Cup PHYSICAL pads (100..104): under `merged` shuffle a cup pad hosting a
// trophy-race destination carries a REAL stage2 (contract §2 stage-2 rule -- the
// apworld emits it into gem_cup_unlock), so it resolves here too. Single source
// for the stage-2 key logic; the load gate above and the LInB re-lock advert
// (AH_WarpPad.c) both resolve through it so gate and display cannot diverge.
const ctr_req *ctr_cfg_warp_stage2_req(int physPadLevelID)
{
	if (!ctr_cfg_active())
		return 0;
	if (physPadLevelID >= 0 && physPadLevelID < CTR_CFG_PAD_COUNT &&
	    ctr_cfg.warp_pad_unlock[physPadLevelID].stage2.type != 0)
		return &ctr_cfg.warp_pad_unlock[physPadLevelID].stage2;
	if (physPadLevelID >= 100 && physPadLevelID <= 104 &&
	    ctr_cfg.gem_cup_unlock[physPadLevelID - 100].stage2.type != 0)
		return &ctr_cfg.gem_cup_unlock[physPadLevelID - 100].stage2;
	return 0;
}

#endif // CTR_AP
