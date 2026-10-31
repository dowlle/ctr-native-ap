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

#include "ap_boss_adapter.h" // J03 boss adapter decisions and glue prototypes

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

// The same FOREIGN tally by raw item code, for the content-plan verifier
// (#133 PR 8): the dense index above, else the sparse table. -1 when the
// sparse table overflowed, so the caller can withhold its verdict.
int AP_VerifyForeignItemCodeCount(long long code)
{
	long long idx = code - AP_ITEM_BASE;
	int k;
	if (idx >= 0 && idx < AP_VERIFY_ITEM_INDEX_COUNT)
		return ap_verify_recv_foreign[idx];
	for (k = 0; k < ap_verify_sparse_foreign_n; k++)
		if (ap_verify_sparse_foreign_code[k] == code)
			return ap_verify_sparse_foreign_count[k];
	return ap_verify_sparse_foreign_overflow ? -1 : 0;
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
	AP_CupSweepEnterFromHub();
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

int AP_ContentPadOpen(const ctr_content_pad *pad)
{
	// Racer lock (#133 PR 4b): ANDed onto the pad's own gates, never in place
	// of them, like ctr_cfg_warp_unlocked on a normal room. Every content-plan
	// consumer (pad state, glow, capture, the load gate, the pad text) asks
	// this, so a locked pad reads closed until its racer is received.
	return ap_content_pad_open(pad, ap_recv_count) &&
	       (pad->racer_lock < 0 || AP_CharacterUnlocked(pad->racer_lock));
}

// The racer lock on its own, for the tracker's list of missing requirements (#375).
int AP_ContentPadRacerMet(const ctr_content_pad *pad)
{
	return pad->racer_lock < 0 || AP_CharacterUnlocked(pad->racer_lock);
}

int AP_ContentPadRuleOwned(const ctr_content_pad *pad)
{
	return pad && pad->has_rule ? ap_content_rule_owned(pad, ap_recv_count) : 0;
}

int AP_ContentModeOpen(const ctr_content_pad *pad, int mode)
{
	return ap_content_mode_open(pad, mode, ap_recv_count, AP_ContentCodeChecked);
}

int AP_ContentStage2Owned(const ctr_content_pad *pad)
{
	return pad && pad->has_stage2 ? ap_content_stage2_owned(pad, ap_recv_count) : 0;
}

int AP_ContentSelectorBuild(const ctr_content_pad *pad, ap_content_selector *out)
{
	return ap_content_selector_build(pad, ap_recv_count, AP_ContentCodeChecked, out);
}

int AP_ContentTrackPadState(const ctr_content_pad *pad)
{
	return ap_content_track_pad_state(pad, ap_recv_count, AP_ContentCodeChecked, AP_ContentPadPlainLeft(pad));
}

// The race type menu of a plan pad (one generator row 6).
void AP_ContentMenuRows(const ctr_content_pad *pad, ap_rs_rows *rows)
{
	ap_content_menu_rows(pad, ap_recv_count, AP_ContentCodeChecked, AP_ContentPadPlainLeft(pad), rows);
}

// The Options row over the seed's value, read at every pad entry (decision 15).
int AP_RaceTypeSelectorSetting(void)
{
	const ctr_content_pad_client *client = ap_content_plan_pad_client();
	return AP_RaceSelectorEffective(g_config.raceTypeSelector,
	                                client->present ? client->race_type_selector : AP_RS_SELECTOR_ALWAYS);
}

int AP_ContentBoxesLine(const ctr_content_pad *pad)
{
	return ap_content_boxes_line(pad, AP_ContentCodeChecked);
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
	// A content plan's garages are empty unless the plan has its `bosses`
	// record (#133 PR 6); with it the parser filled boss_req and the venues
	// like a normal room's, so the normal path below decides.
	if (ap_content_plan_active() && !ap_content_plan_bosses()->present) return 0;
	if (bossIdx < 0 || bossIdx >= CTR_CFG_BOSS_COUNT)
		return 1;
	// #169: a seed whose boss_encounters record this build refused keeps the
	// four garages shut (map icon, door, tracker and load alike) rather than
	// racing the retail venue in its place.
	if (bossIdx < AP_BOSS_VENUE_GARAGES && AP_BossGarageVenue(bossIdx) < 0)
		return 0;

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

// ── #169 tier 1: boss track shuffle ─────────────────────────────────────────
// The decisions are ap/ap_boss_venue.h; these wrappers read the live seed and
// GameTracker. gGT->bossID stays the physical garage on every venue, so the
// Key, the Boss Race check, the loss counter, the driving table, music and
// cutscenes are untouched; only the loaded level changes.

int AP_BossGarageVenue(int bossIdx)
{
	if (bossIdx < 0 || bossIdx >= AP_BOSS_VENUE_GARAGES)
		return -1;
	if (!ctr_cfg_active())
		return AP_BOSS_VENUE_GARAGE[bossIdx].homeLevel;
#ifdef CTR_CUSTOM_TRACKS
	// A custom venue (#133 PR 6b) races only once its verified bytes are
	// armed for this garage; otherwise the garage stays shut.
	if (ctr_boss_encounters.custom[bossIdx] && !CustomTrack_GarageVenueReady(bossIdx))
		return -1;
	// A boss cup races only once every leg's files are ready (#133 PR 6b).
	if (AP_ContentGarageCupReady(bossIdx) == 0)
		return -1;
#else
	if (ctr_boss_encounters.custom[bossIdx])
		return -1;
#endif
	return AP_BossEncounterVenue(&ctr_boss_encounters, bossIdx);
}

// "Track: Crash Cove" for a garage whose seed drew its venue, open or shut.
// Returns 0 for a retail seed (the garage races its own track, as the retail
// panel already implies) and for a refused record (AP_BossGateAdvert says why).
int AP_BossVenueAdvert(int bossIdx, char *out, int cap)
{
	int venue;

	if (out == 0 || cap <= 0 || !ctr_cfg_active())
		return 0;
#ifdef CTR_CUSTOM_TRACKS
	// #133 PR 6b: a garage holding a boss cup says so.
	if (bossIdx >= 0 && bossIdx < 4 && ap_content_plan_pad(AP_CONTENT_GARAGE_CUP_PAD + bossIdx))
	{
		snprintf(out, (size_t)cap, "Boss cup: four races, won on points");
		return 1;
	}
#endif
	if (ctr_boss_encounters.state != AP_BOSS_ENC_VALID)
		return 0;
#ifdef CTR_CUSTOM_TRACKS
	// J11: a content-plan venue on custom bytes is served on the garage's
	// home LevelID; name the track it really is, so the Rival and Kit lines
	// that follow sit under the right venue.
	if (bossIdx >= 0 && bossIdx < AP_BOSS_VENUE_GARAGES && ctr_boss_encounters.custom[bossIdx] &&
	    ap_content_plan_active())
	{
		const ctr_content_bosses *b = ap_content_plan_bosses();
		const ctr_content_package *pkg = b->venue_cortex[bossIdx] ? NULL : ap_content_plan_package(b->venue_package[bossIdx]);
		if (b->venue_cortex[bossIdx])
			snprintf(out, (size_t)cap, "Track: Cortex Vortex");
		else if (pkg && pkg->display_name[0])
			snprintf(out, (size_t)cap, "Track: %s", pkg->display_name);
		else
			snprintf(out, (size_t)cap, "Track: custom track");
		return 1;
	}
#endif
	venue = AP_BossGarageVenue(bossIdx);
	if (!AP_BossVenueTrackKnown(venue))
		return 0;
	snprintf(out, (size_t)cap, "Track: %s", sdata->lngStrings[data.metaDataLEV[venue].name_LNG]);
	return 1;
}

// #169 tier 2: "Rival: <name>" for a garage whose seed drew its opponent,
// open or shut; 0 otherwise.
int AP_BossRivalAdvert(int bossIdx, char *out, int cap)
{
	int c;

	if (out == 0 || cap <= 0 || !ctr_cfg_active())
		return 0;
	c = AP_BossEncounterOpponent(&ctr_boss_encounters, bossIdx);
	if (c < 0 || c >= 16)
		return 0;
	snprintf(out, (size_t)cap, "Rival: %s", sdata->lngStrings[data.MetaDataCharacters[c].name_LNG_short]);
	return 1;
}

// #169 tier 3: "Kit: TNT, potion" for a garage with a composed kit; 0
// otherwise. The families in the order the schedule first uses them.
int AP_BossKitAdvert(int bossIdx, char *out, int cap)
{
	// J03: every family a composed kit can carry, one name per family.
	static const struct
	{
		int weapon;
		int family;
	} kCodes[] = {{0x01, 0}, {0x02, 1}, {0x03, 2}, {0x64, 2}, {0x04, 3}, {0x66, 3}, {0x00, 4},
	              {0x06, 5}, {0x07, 6}, {0x08, 7}, {0x09, 8}, {0x0c, 9}};
	static const char *const names[10] = {"bomb", "missile", "TNT",   "potion",   "turbo",
	                                      "shield", "mask",   "clock", "warp orb", "invisibility"};
	int seen[10] = {0};
	int i, k, used, any = 0;

	if (out == 0 || cap <= 0 || !ctr_cfg_active() || bossIdx < 0 || bossIdx >= AP_BOSS_VENUE_GARAGES ||
	    ctr_boss_encounters.state != AP_BOSS_ENC_VALID || !ctr_boss_encounters.composed[bossIdx])
		return 0;
	used = snprintf(out, (size_t)cap, "Kit:");
	for (i = 0; i < AP_BOSS_VENUE_SEGMENTS && used > 0 && used < cap; i++)
	{
		int w = ctr_boss_encounters.schedule[bossIdx][i].weapon;
		int f = -1;
		for (k = 0; k < (int)(sizeof kCodes / sizeof kCodes[0]); k++)
			if (kCodes[k].weapon == w)
				f = kCodes[k].family;
		if (f < 0 || seen[f])
			continue;
		used += snprintf(out + used, (size_t)(cap - used), "%s %s", any ? "," : "", names[f]);
		seen[f] = 1;
		any = 1;
	}
	return 1;
}

// #169 tier 2: the character garage bossIdx races, for the garage load. -1
// keeps the retail boss (no record, tier 1, refused, or a content plan).
// 1 while garage `g`'s boss cup is running.
static int AP_BossCupGarageNow(int g)
{
#ifdef CTR_CUSTOM_TRACKS
	return g >= 0 && g < AP_BOSS_VENUE_GARAGES && AP_ContentGarageCupRunning() == g;
#else
	(void)g;
	return 0;
#endif
}

// 1 when garage `g` holds a boss cup.
static int AP_BossCupGarageHeld(int g)
{
#ifdef CTR_CUSTOM_TRACKS
	return g >= 0 && g < AP_BOSS_VENUE_GARAGES && AP_ContentGarageCupReady(g) >= 0;
#else
	(void)g;
	return 0;
#endif
}

int AP_BossGarageOpponent(int bossIdx)
{
	if (!ctr_cfg_active())
		return -1;
	return AP_BossEncounterOpponent(&ctr_boss_encounters, bossIdx);
}

// #169 tier 2: the models of the boss race being loaded. Returns the
// time-trial pack character (the garage's own boss) and writes the extra hi
// model's character to *extra (-1 for none), or returns -1 for every load
// this does not own: the retail path then runs unchanged.
int AP_BossRaceOpponentModels(int player, int *extra)
{
	struct GameTracker *gGT = sdata->gGT;
	int g, opp;

	*extra = -1;
	if (gGT == NULL || !ctr_cfg_active() || (gGT->gameMode1 & ADVENTURE_BOSS) == 0)
		return -1;
	g = gGT->bossID;
	// A boss cup seats the garage's drawn racer on every leg. This runs during
	// the load, before the leg's race context answers AP_BossCupGarageNow.
	if ((gGT->gameMode1 & ADVENTURE_CUP) != 0 && !AP_BossCupGarageHeld(g))
		return -1;
	opp = AP_BossEncounterOpponent(&ctr_boss_encounters, g);
	if (opp < 0)
		return -1;
	return AP_BossOpponentModels(AP_BOSS_VENUE_GARAGE[g].character, opp, player, extra);
}

static int AP_BossKitGarageNow(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if (gGT == NULL || !ctr_cfg_active())
		return -1;
	// A boss cup carries the garage's one composed kit on every leg.
	if (AP_BossCupGarageNow(gGT->bossID))
		return (gGT->gameMode1 & ADVENTURE_BOSS) && ctr_boss_encounters.composed[(int)gGT->bossID] ? gGT->bossID : -1;
	return AP_BossEncounterKitGarage(&ctr_boss_encounters, (gGT->gameMode1 & ADVENTURE_BOSS) != 0,
	                                 (gGT->gameMode1 & ADVENTURE_CUP) != 0, gGT->bossID, gGT->levelID);
}

// #169 tier 3: 0 when the running garage boss race has a composed kit with
// boss_loss_easing off, so its cooldowns ignore the garage's loss count;
// 1 (retail easing) for every other race.
int AP_BossLossEasingNow(void)
{
	if (AP_BossKitGarageNow() < 0)
		return 1;
	return ctr_boss_encounters.lossEasing;
}

static int AP_BossShuffledGarageNow(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if (gGT == NULL || !ctr_cfg_active())
		return -1;
#ifdef CTR_CUSTOM_TRACKS
	// A boss cup leg (#133 PR 6b) carries the garage's attack too; only a
	// retail leg on the garage's own home track keeps the exact retail path.
	{
		int g = AP_ContentGarageCupRunning();
		if (g >= 0 && (gGT->gameMode1 & ADVENTURE_BOSS) && gGT->bossID == g)
		{
			const struct CustomTrackRaceContext *race = CustomTrack_StandaloneContext(gGT->levelID);
			if (race && race->retail && gGT->levelID == AP_BOSS_VENUE_GARAGE[g].homeLevel)
				return -1;
			return g;
		}
	}
#endif
	return AP_BossEncounterShuffledGarage(&ctr_boss_encounters,
	                                      (gGT->gameMode1 & ADVENTURE_BOSS) != 0,
	                                      (gGT->gameMode1 & ADVENTURE_CUP) != 0,
	                                      gGT->bossID, gGT->levelID);
}

int AP_BossRetailRulesLevel(int levelID)
{
	int g = AP_BossShuffledGarageNow();
	return g < 0 ? levelID : AP_BOSS_VENUE_GARAGE[g].homeLevel;
}

// The per-race copy of a shuffled garage's retail schedule. Retail writes a
// preserved throw flag back into whichever table is live, so the venue gets
// its own copy: refilled from the garage's data.BossWeapon* rows at race init
// (`fill`, from PickupBots_Init), and returned unchanged when the schedule
// wraps to its first segment each lap (PickupBots_GetInitialBossMeta), the
// same object identity retail keeps. The home track never takes this path.
static struct MetaDataBOSS s_apBossVenueAttack[8];

// The restart point count the running venue of garage `g` was certified on:
// a boss cup leg's (#133 PR 6b), a custom venue's, or the retail table's.
static int AP_BossVenueCertifiedNow(int g)
{
#ifdef CTR_CUSTOM_TRACKS
	if (AP_ContentGarageCupRunning() == g)
		return AP_ContentGarageCupLegPoints();
#endif
	return AP_BossEncounterCertifiedPoints(&ctr_boss_encounters, g, sdata->gGT->levelID);
}

// The restart point count a composed kit was written for: the running venue's,
// or the garage's home track on a boss cup, whose kit keeps the retail segment
// starts and maps them onto each leg (apworld content_bosses.kit_points).
static int AP_BossKitCertifiedNow(int g)
{
	if (AP_BossCupGarageNow(g))
		return AP_BOSS_VENUE_RESTART_POINTS[AP_BOSS_VENUE_GARAGE[g].homeLevel];
	return AP_BossVenueCertifiedNow(g);
}

// J03 decision 8: a refused schedule is one inert row. The boss keeps its
// timers and throws nothing that race.
static struct MetaDataBOSS *AP_BossInertSchedule(struct MetaDataBOSS *dst, int rows)
{
	memset(dst, 0, sizeof *dst * (size_t)rows);
	dst[0].throwFlag = BOSS_WEAPON_NORMAL;
	dst[0].weaponType = BOSS_WEAPON_NONE;
	dst[0].weaponCooldown = 0x0f;
	return dst;
}

// Copy composed segments into a MetaDataBOSS table (row n stays zero, the
// retail terminator), with the checkpoints in `cps`.
static void AP_BossFillKit(struct MetaDataBOSS *dst, const AP_BossKitSegment *seg, int n, const unsigned char *cps)
{
	int i;
	for (i = 0; i < n; i++)
	{
		dst[i].trackCheckpoint = cps[i];
		dst[i].throwFlag = seg[i].throwFlag;
		dst[i].weaponType = seg[i].weapon;
		dst[i].pathChangeDisabled = seg[i].pathDisabled;
		dst[i].weaponCooldown = seg[i].cooldown;
		dst[i].juiceFlag = seg[i].juice;
	}
}

// Decision 8: a composed five-segment schedule recorded for `certified`
// restart points, raced on `loaded`. Same count: the recorded checkpoints.
// Different count: the proportional remap (ap_boss_adapter.h), kept only when
// it still rises strictly from 0 inside the loaded track. Returns 1 and fills
// `dst`, or 0.
static int AP_BossFitKit(struct MetaDataBOSS *dst, const AP_BossKitSegment *seg, int certified, int loaded,
                         const char *what)
{
	unsigned char cps[AP_BOSS_VENUE_SEGMENTS], mapped[AP_BOSS_VENUE_SEGMENTS];
	AP_BossKitSegment copy[AP_BOSS_VENUE_SEGMENTS];
	int i;
	char msg[192];

	for (i = 0; i < AP_BOSS_VENUE_SEGMENTS; i++)
		cps[i] = seg[i].checkpoint;
	if (loaded == certified)
	{
		if (AP_BossKitScheduleProblem(seg, AP_BOSS_VENUE_SEGMENTS, loaded))
			return 0;
		AP_BossFillKit(dst, seg, AP_BOSS_VENUE_SEGMENTS, cps);
		return 1;
	}
	if (!AP_BossAdapterRemap(cps, AP_BOSS_VENUE_SEGMENTS, certified, loaded, mapped))
		return 0;
	for (i = 0; i < AP_BOSS_VENUE_SEGMENTS; i++)
	{
		copy[i] = seg[i];
		copy[i].checkpoint = mapped[i];
	}
	if (AP_BossKitScheduleProblem(copy, AP_BOSS_VENUE_SEGMENTS, loaded))
		return 0;
	AP_BossFillKit(dst, seg, AP_BOSS_VENUE_SEGMENTS, mapped);
	snprintf(msg, sizeof msg, "[AP BOSS] checkpoint fallback: %s recorded for %d restart points, remapped to %d\n",
	         what, certified, loaded);
	AP_AppendLog(msg);
	return 1;
}

// ── N. Oxide's composed final record (J03, decision 5 and 7) ───────────────
// Both Oxide races (bossID 4 and 5) with oxide_attack_profile all_compatible.
// The 13 segments keep the retail Oxide starts, so the venue mapping is the
// retail one: as recorded on Oxide Station, AP_OxideWeaponCheckpoint on the
// Cortex Vortex venue or any graph the 0x7f scale does not fit, exactly as
// the inherited Cortex Vortex Oxide table.
static struct MetaDataBOSS s_apBossFinalAttack[AP_BOSS_FINAL_SEGMENTS + 1];

int AP_BossFinalComposedNow(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const AP_BossEncounters *e = &ctr_boss_encounters;

	if (gGT == NULL || !ctr_cfg_active() || (gGT->gameMode1 & ADVENTURE_BOSS) == 0 ||
	    (gGT->gameMode1 & ADVENTURE_CUP) != 0 || (gGT->bossID != 4 && gGT->bossID != 5))
		return 0;
	return e->state == AP_BOSS_ENC_VALID && e->finalPresent && e->finalComposed;
}

static struct MetaDataBOSS *AP_BossFinalAttackSchedule(int fill)
{
	struct GameTracker *gGT = sdata->gGT;
	int i, points, remap;

	if (!AP_BossFinalComposedNow() || gGT->level1 == NULL)
		return NULL;
	if (!fill)
		return s_apBossFinalAttack;
	points = gGT->level1->cnt_restart_points;
	if (points < 2 || AP_BossFinalScheduleProblem(ctr_boss_encounters.finalSchedule, AP_BOSS_FINAL_SEGMENTS))
	{
		AP_AppendLog("[AP BOSS] final record schedule refused at race init\n");
		return AP_BossInertSchedule(s_apBossFinalAttack, AP_BOSS_FINAL_SEGMENTS + 1);
	}
	remap = points <= 0x7f;
#ifdef CTR_CUSTOM_TRACKS
	if (CustomTrack_OxideFinalServing((int)gGT->levelID, gGT->bossID, 1))
		remap = 1;
#endif
	{
		unsigned char cps[AP_BOSS_FINAL_SEGMENTS];
		for (i = 0; i < AP_BOSS_FINAL_SEGMENTS; i++)
		{
			int cp = ctr_boss_encounters.finalSchedule[i].checkpoint;
			cps[i] = (unsigned char)(remap ? AP_OxideWeaponCheckpoint(cp, points - 1) : cp);
			// Decision 8: a remap that reorders segments or starts outside the
			// loaded track throws nothing (review round 1). Two neighbouring
			// rows on one checkpoint are allowed, as the retail Cortex Vortex
			// Oxide remap already does: the boss just passes through one.
			if ((int)cps[i] >= points || (i > 0 && cps[i] < cps[i - 1]))
			{
				AP_AppendLog("[AP BOSS] final record does not fit the loaded track: no attacks\n");
				return AP_BossInertSchedule(s_apBossFinalAttack, AP_BOSS_FINAL_SEGMENTS + 1);
			}
		}
		memset(s_apBossFinalAttack, 0, sizeof s_apBossFinalAttack);
		AP_BossFillKit(s_apBossFinalAttack, ctr_boss_encounters.finalSchedule, AP_BOSS_FINAL_SEGMENTS, cps);
	}
	return s_apBossFinalAttack;
}

// ── Purple Gem Cup gauntlet (J03, design answer 3, decision 5) ─────────────
// Leg i of an adventure Purple Gem Cup arms gauntletSchedule[i] on the AI
// that races gauntletAttacker[i]. Only on the leg's recorded retail track.
static struct MetaDataBOSS s_apBossGauntletAttack[AP_BOSS_VENUE_SEGMENTS + 1];
static int s_apBossGauntletLeg = -1; // leg whose schedule fitted at race init

static int AP_BossGauntletLegNow(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const AP_BossEncounters *e = &ctr_boss_encounters;
	int leg;

	if (gGT == NULL || !ctr_cfg_active() || e->state != AP_BOSS_ENC_VALID || !e->gauntletOn)
		return -1;
	if ((gGT->gameMode1 & (ADVENTURE_MODE | ADVENTURE_CUP | ADVENTURE_BOSS)) != (ADVENTURE_MODE | ADVENTURE_CUP))
		return -1;
#ifdef CTR_CUSTOM_TRACKS
	// Plan cups run on engine cup 0, so the pad names the Purple Gem Cup.
	{
		const ctr_content_pad *pad = AP_ContentCupPad();
		if (pad == NULL || pad->gem_cup != 104)
			return -1;
	}
#else
	if (gGT->cup.cupID != 4)
		return -1;
#endif
	leg = gGT->cup.trackIndex;
	if (leg < 0 || leg >= AP_BOSS_GAUNTLET_LEGS || !e->gauntletComposed[leg] || e->gauntletTrack[leg] < 0 ||
	    e->gauntletTrack[leg] != gGT->levelID || !AP_BossVenueTrackKnown(e->gauntletTrack[leg]))
		return -1;
#ifdef CTR_CUSTOM_TRACKS
	{
		// A custom track served on the leg's host slot is not the recorded leg.
		const struct CustomTrackRaceContext *race = CustomTrack_StandaloneContext(gGT->levelID);
		if (race && !race->retail)
			return -1;
	}
#endif
	return leg;
}

int AP_BossGauntletAttacker(void)
{
	int leg = AP_BossGauntletLegNow();
	if (leg < 0 || leg != s_apBossGauntletLeg)
		return -1;
	return ctr_boss_encounters.gauntletAttacker[leg];
}

static struct MetaDataBOSS *AP_BossGauntletAttackSchedule(int fill)
{
	struct GameTracker *gGT = sdata->gGT;
	int leg = AP_BossGauntletLegNow();
	char what[48];

	if (leg < 0 || gGT->level1 == NULL)
	{
		if (fill)
			s_apBossGauntletLeg = -1;
		return NULL;
	}
	if (!fill)
		return s_apBossGauntletLeg == leg ? s_apBossGauntletAttack : NULL;
	memset(s_apBossGauntletAttack, 0, sizeof s_apBossGauntletAttack);
	snprintf(what, sizeof what, "gauntlet leg %d", leg + 1);
	if (!AP_BossFitKit(s_apBossGauntletAttack, ctr_boss_encounters.gauntletSchedule[leg],
	                   AP_BOSS_VENUE_RESTART_POINTS[ctr_boss_encounters.gauntletTrack[leg]],
	                   gGT->level1->cnt_restart_points, what))
	{
		char msg[160];
		snprintf(msg, sizeof msg, "[AP BOSS] gauntlet leg %d on venue %d (%d restart points) refused\n", leg + 1,
		         (int)gGT->levelID, gGT->level1->cnt_restart_points);
		AP_AppendLog(msg);
		s_apBossGauntletLeg = -1;
		return NULL;
	}
	s_apBossGauntletLeg = leg;
	sdata->bossWeaponCooldown = s_apBossGauntletAttack[0].weaponCooldown + 0x3c;
	sdata->bossJuiceCounter = 0;
	return s_apBossGauntletAttack;
}

struct MetaDataBOSS *AP_BossVenueAttackSchedule(int fill)
{
	struct GameTracker *gGT = sdata->gGT;
	int g = AP_BossShuffledGarageNow();
	const struct MetaDataBOSS *src;
	int venuePoints, homeLast, i;

	int k = AP_BossKitGarageNow();

	{
		struct MetaDataBOSS *fin = AP_BossFinalAttackSchedule(fill);
		if (fin != NULL)
			return fin;
	}
	{
		struct MetaDataBOSS *leg = AP_BossGauntletAttackSchedule(fill);
		if (leg != NULL)
			return leg;
	}

	// #169 tier 3: a composed kit replaces the garage's retail attack on any
	// venue, the home track included. Its checkpoints were composed for the
	// recorded venue's restart point count; a loaded graph that differs takes
	// the decision 8 remap, or throws nothing when even that does not fit.
	if (k >= 0 && gGT->level1 != NULL)
	{
		if (!fill)
			return s_apBossVenueAttack;
		venuePoints = gGT->level1->cnt_restart_points;
		memset(s_apBossVenueAttack, 0, sizeof s_apBossVenueAttack);
		if (!AP_BossFitKit(s_apBossVenueAttack, ctr_boss_encounters.schedule[k], AP_BossKitCertifiedNow(k),
		                   venuePoints, "composed kit"))
		{
			char msg[160];
			snprintf(msg, sizeof msg,
			         "[AP BOSS] venue %d has %d restart points, certified %d: composed kit refused\n",
			         (int)gGT->levelID, venuePoints, AP_BossKitCertifiedNow(k));
			AP_AppendLog(msg);
			return AP_BossInertSchedule(s_apBossVenueAttack, 8);
		}
		// Row 5 stays zero: the terminator retail tables end with.
		return s_apBossVenueAttack;
	}
	if (g < 0 || gGT->level1 == NULL)
		return NULL;
	if (!fill)
		return s_apBossVenueAttack;
	venuePoints = gGT->level1->cnt_restart_points;
	// The certification saw the retail LEV's restart point count, or a custom
	// venue's measured one (#133 PR 6b). The inherited attack is mapped from
	// the garage's home track, so a loaded graph with a different count
	// (modified disc or files) only needs the same mapping onto the count it
	// really has (decision 8). When even that does not rise strictly the
	// boss keeps its timers but throws nothing. A room without a plan keeps its
	// refusal of any other count.
	if (!AP_BossVenueScheduleFits(g, venuePoints) ||
	    (!ap_content_plan_active() && venuePoints != AP_BossVenueCertifiedNow(g)))
	{
		char msg[160];
		snprintf(msg, sizeof msg,
		         "[AP BOSS] venue %d has %d restart points, certified %d: inherited attack refused\n",
		         (int)gGT->levelID, venuePoints, AP_BossVenueCertifiedNow(g));
		AP_AppendLog(msg);
		return AP_BossInertSchedule(s_apBossVenueAttack, 8);
	}
	if (venuePoints != AP_BossVenueCertifiedNow(g))
	{
		char msg[160];
		snprintf(msg, sizeof msg,
		         "[AP BOSS] checkpoint fallback: venue %d has %d restart points, certified %d: inherited attack remapped\n",
		         (int)gGT->levelID, venuePoints, AP_BossVenueCertifiedNow(g));
		AP_AppendLog(msg);
	}
	src = data.bossWeaponMetaPtr[g + 1];
	homeLast = AP_BOSS_VENUE_RESTART_POINTS[AP_BOSS_VENUE_GARAGE[g].homeLevel] - 1;
	for (i = 0; i < 8; i++)
	{
		s_apBossVenueAttack[i] = src[i];
		if (src[i].throwFlag == 0)
			break;
		s_apBossVenueAttack[i].trackCheckpoint =
		    (u8)AP_BossVenueCheckpoint(src[i].trackCheckpoint, homeLast, venuePoints - 1);
	}
	return s_apBossVenueAttack;
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

// Hub map star for boss garage bossIdx 0..3 (game/232/AH_Map.c): non-zero
// when the boss counts as beaten there, which removes the star and its
// "accessible" glow. Under a Bosses goal that still needs bosses this is the
// player's own win (#458 flags), so a boss checked by another player's
// Collect keeps its glow; otherwise the checked boss race location, as
// before. The rule is ap/ap_boss_map_star.h.
int AP_BossMapStarBeaten(int bossIdx)
{
	return AP_BossMapBeaten(
	    bossIdx, AP_LocationCheckedByBit(bossIdx + ADV_REWARD_FIRST_BOSS_KEY),
	    ctr_cfg.goal_bosses, ap_net_boss_won_known(), ap_net_boss_won_bits());
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
	// A content plan's Gem arm counts its distinct base Gem colours (#133 PR 6).
	if (ap_content_plan_active())
	{
		int gems[5], i;
		for (i = 0; i < 5; ++i) gems[i] = AP_GateCountGemColour(i);
		in.gemsHeld = ap_content_plan_gems_held(gems);
	}
	in.firstOptional = ctr_cfg.oxide_1_optional;
	// #249 decision 19: N. Oxide's garage waits for the Wumpa target, on
	// content-plan rooms too (J11: the parser sets ctr_wumpa_shop.goal from
	// the plan's `wumpa_banked` arm, 0 without one).
	in.wumpaShort = !AP_WumpaGoalMet(ctr_wumpa_shop.goal, ap_net_wumpa_lifetime());
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

	// #169: a refused boss_encounters record is why the door is shut.
	if (AP_BossGarageVenue(bossIdx) < 0)
	{
		// #133 PR 6b: a custom venue whose files are missing or changed.
		if ((ctr_boss_encounters.state == AP_BOSS_ENC_VALID && ctr_boss_encounters.custom[bossIdx])
#ifdef CTR_CUSTOM_TRACKS
		    || AP_ContentGarageCupReady(bossIdx) == 0
#endif
		    )
		{
			snprintf(out, (size_t)cap, "Boss track files missing: see Current Seed");
			return 1;
		}
		snprintf(out, (size_t)cap, "Boss track refused: %s",
		         ctr_boss_encounters.reason[0] ? ctr_boss_encounters.reason : "update the client");
		return 1;
	}

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
	if (ap_content_plan_active())
	{
		int owned = 0, i;
		for (i = 0; i < 5; ++i)
		{
			const ctr_content_item *row = ap_content_plan_item(AP_ITEM_BASE + AP_IDX_GEM_RED + i);
			if (row && row->base && AP_GateCountGemColour(i) > 0) ++owned;
		}
		// J11: the plan's Wumpa arm follows the Gem arm; a Wumpa-only
		// goal shows the Wumpa counter alone. Without a Wumpa arm the line
		// is the one content plans always showed.
		if (ctr_cfg.goal_gems > 0 || ctr_wumpa_shop.goal <= 0)
		{
			int n = snprintf(out, cap, "Goal: %d/%d selected Gem colours", owned, ctr_cfg.goal_gems);
			if (ctr_wumpa_shop.goal > 0 && n > 0 && n < cap)
				snprintf(out + n, (size_t)(cap - n), " - WUMPA %d/%d",
				         ap_net_wumpa_lifetime() > ctr_wumpa_shop.goal ? ctr_wumpa_shop.goal
				                                                       : (int)ap_net_wumpa_lifetime(),
				         ctr_wumpa_shop.goal);
			return 1;
		}
		return AP_GoalLineAppendWumpa(out, cap, 0, ap_net_wumpa_lifetime(), ctr_wumpa_shop.goal);
	}

	// The Oxide segment is driven by goal_oxide alone, so `optional` (0) and
	// `disabled` (3) both omit it: neither is a completion condition. Whether
	// the Oxide race has been BEATEN is deliberately not shown -- under the
	// approved encounter flow the enabled arms gate the selected finale, so
	// winning it completes the composed goal in the same moment.
	return AP_GoalLineAppendWumpa(out, cap,
	                              AP_GoalLineFormat(out, cap, ctr_cfg.goal_oxide,
	                                                ctr_cfg.goal_bosses, AP_ComposedBossesWon(),
	                                                ctr_cfg.goal_gems, AP_GateCountGemSum()),
	                              ap_net_wumpa_lifetime(), ctr_wumpa_shop.goal);
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
	if (ctr_cfg_arena_pad_disabled(levelID))
		return 0;
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
