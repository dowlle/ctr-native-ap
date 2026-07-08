#ifdef CTR_AP

#include <common.h>
#include <stdio.h>

#include "ap_hooks.h"
#include "ap_locations.h" // generated bit_index -> AP location_code table (99 locs)
#include "ap_net.h"       // C API into the apclientpp network client (ap_net.cpp)
#include "ap_items.h"     // item-id -> AdvProgress category bit pools
#include "ap_traps.h"     // trap-effect framework (per-frame tick + config trigger)
#include "ap_shortcut.h"  // Shortcutless mechanism (key poll + config trigger)

// ============================================================================
// Archipelago integration. Parts:
//
//   NETWORK (ap_net.cpp / apclientpp): connect once at boot, pump every frame
//   in all game modes so the handshake completes at the title screen and items
//   can arrive anytime. Received items are surfaced here; checks and the goal
//   are sent from the LOCATION EVENTS below.
//
//   LOCATION EVENTS (option A -- AUTHORITATIVE): the game's reward-grant sites
//   call AP_NotifyAdvReward() the instant the player earns a checkable reward
//   (trophy/relic/token/gem/boss), and AP_NotifyGoal() when Oxide is beaten.
//   Reliable regardless of load/menu state. Each fires a network LocationChecks
//   / StatusUpdate(GOAL).
//
//   DEBUG poll: per-frame scan logging *unmapped* AdvProgress bit changes
//   (story/hint flags) for visibility only -- never drives checks.
//
// TODO(ap): apply received items into AdvProgress -- needs the item-id -> bit
// map (ap_items.h, next step). Today received items are logged, not granted.
// ============================================================================

// Raw keyboard probe for the map-overlay hotkey (platform/native_input.c, CTR_AP
// only). Declared here rather than including <SDL3/SDL.h> so the AP module stays
// free of SDL headers in the unity build.
int Platform_InputRawKeyDown(int scancode);

// SDL_SCANCODE_M, anchored to externals/SDL/include/SDL3/SDL_scancode.h:75
// (SDL_SCANCODE_M = 16). Not in s_keyboardMapping defaults (native_input.c:236),
// so toggling the overlay never disturbs gameplay input.
#define AP_MAP_OVERLAY_SCANCODE 16

#define AP_READ_LOG "D:\\pythonProjects\\ctr-archipelago\\reference" \
                    "\\ctr-native\\build-ap\\ap-read.log"

static void AP_AppendLog(const char *msg)
{
	fputs(msg, stderr);
	FILE *f = fopen(AP_READ_LOG, "a");
	if (f)
	{
		fputs(msg, f);
		fclose(f);
	}
}

// Non-static shim so game-side gate files can emit AP log lines (AP_AppendLog is
// static to this module). Declared in ap_hooks.h.
void AP_LogLine(const char *msg)
{
	AP_AppendLog(msg);
}

// Skip Aku Aku mask hints (QoL / testing). Set from ap-config.txt at connect;
// read by MainFrame_RequestMaskHint to early-return. Default off.
static int ap_skip_hints = 0;
int AP_SkipHints(void)
{
	// Precedence (migration window, one release): if the in-game options menu has
	// written a config.ini, its skip_hints value wins and takes effect live (the
	// menu mutates g_config in place). Without a config.ini we fall back to the
	// legacy ap-config.txt "skip_hints=" key parsed into ap_skip_hints, so users
	// who never open the menu keep their current behaviour.
	if (NativeConfig_HasIni())
		return g_config.skipHints;
	return ap_skip_hints;
}

// Map flash (Warp-Pad State Model v2): the vanilla-style two-tone flicker that
// signals "Raceable" (state 2 GREEN) on the hub map. Default ON. Turned off via
// ap-config.txt ("map_flash=0") -> state 2 renders a static GREEN. Delivery
// mirrors skip_hints (ap-config.txt first; a YAML option can write it later).
static int ap_map_flash = 1;
int AP_MapFlashOn(void)
{
	// Same config.ini-over-ap-config.txt precedence as AP_SkipHints (see there).
	if (NativeConfig_HasIni())
		return g_config.mapFlash;
	return ap_map_flash;
}

// Diagnostic (crash investigation): format a compact game-state breadcrumb for a
// log line -- frame timer, in-adventure flag, loading stage, and current levelID.
// Lets a flushed log show what the game was doing (e.g. an item arriving while a
// hub transition is mid-load). Logging only -- reads state, never mutates it.
static void AP_FmtState(struct GameTracker *gGT, char *buf, int n)
{
	snprintf(buf, n, "t=%u adv=%d load=%d lvl=%d",
	         (unsigned)gGT->timer,
	         (gGT->gameMode1 & ADVENTURE_MODE) != 0 ? 1 : 0,
	         (int)sdata->Loading.stage,
	         (int)gGT->levelID);
}

// Friendly names for the 16 track trophies (AdvProgress word 0, bits 6..21),
// for easy manual verification in the log. Everything else resolves by code.
static const char *AP_TrophyName(int globalBit)
{
	switch (globalBit)
	{
	case 6:  return "Dingo Canyon Trophy";
	case 7:  return "Dragon Mines Trophy";
	case 8:  return "Blizzard Bluff Trophy";
	case 9:  return "Crash Cove Trophy";
	case 10: return "Tiger Temple Trophy";
	case 11: return "Papu Pyramid Trophy";
	case 12: return "Roo's Tubes Trophy";
	case 13: return "Hot Air Skyway Trophy";
	case 14: return "Sewer Speedway Trophy";
	case 15: return "Mystery Caves Trophy";
	case 16: return "Cortex Castle Trophy";
	case 17: return "N. Gin Labs Trophy";
	case 18: return "Polar Pass Trophy";
	case 19: return "Oxide Station Trophy";
	case 20: return "Coco Park Trophy";
	case 21: return "Tiny Arena Trophy";
	default: return 0;
	}
}

// Resolve a global AdvProgress bit to its AP location code, or -1 if not a
// checkable location.
static long AP_LookupLocationCode(int globalBit)
{
	int i;
	for (i = 0; i < AP_LOCATION_TABLE_LEN; i++)
	{
		if (AP_LOCATION_TABLE[i].bit_index == globalBit)
			return AP_LOCATION_TABLE[i].location_code;
	}
	return -1;
}

// 1 if the AP location at `globalBit` has been CHECKED on the server for our own
// slot. Use this -- NOT CHECK_ADV_BIT on the AdvProgress bits -- to ask "has the
// player completed this location" in AP mode: AP_ApplyItems clears any location
// bit not backed by a received item every frame, so a local win is wiped and the
// raw bit can never reflect it. Returns 0 if not a checkable bit / not connected.
int AP_LocationCheckedByBit(int globalBit)
{
	long code = AP_LookupLocationCode(globalBit);
	if (code < 0)
		return 0;
	return ap_net_location_checked(code);
}

// ---------------------------------------------------------------------------
// MAP overlay retirement: the old debug-era per-pad map state
// (AP_BitIsUsefulUnchecked / AP_PadUsefulness / AP_BitUnchecked / AP_PadMapState)
// and its SDL_SCANCODE_M colour-override toggle (g_ap_map_overlay /
// AP_MapOverlayOn) are fully retired -- superseded by the unified AP_PadState
// (Warp-Pad State Model v2), which drives an always-on 5-state map colour and
// drops the own-progression-only GREEN criterion. The M key is now reserved for
// the deferred hub-map zoom (see AP_OnFrame).
// ---------------------------------------------------------------------------

// Tint for a FOREIGN multiworld item, rendered with the generic gem marker
// (STATIC_GEM) so it reads as "someone else's AP item" (Icebound's standalone
// uses a white gem for the same purpose) and can't be mistaken for an own boss
// Key. Pure white (255,255,255) packed (R<<0x14)|(G<<0xc)|(B<<0x4) -> a bright
// white gem, distinct from own COLOURED gem-cup gems (which keep their colour).
// Interim until the foreign marker gets the real Archipelago-logo model
// (backlogged custom-asset task).
#define AP_FOREIGN_TINT 0x0ffffff0

// ---------------------------------------------------------------------------
// REWARD GLOW -- map a location (by its AdvProgress global bit) to the model of
// the AP item placed there, so each warp pad's glow shows its real reward.
// ---------------------------------------------------------------------------

int AP_WarpPadRewardModel(int globalBit)
{
	long code;
	long long item = 0;
	int player = -1;
	unsigned flags = 0;

	code = AP_LookupLocationCode(globalBit);
	if (code < 0)
		return -1; // not a checkable location -> keep vanilla model

	if (!ap_net_scout_known(code, &item, &player, &flags))
		return -1; // not scouted yet (not connected / pre-scout) -> vanilla

	// A foreign multiworld item (placed for a different slot) -> generic marker.
	// Rendered as a gem (tinted white by AP_WarpPadRewardTint) so it reads as
	// "another player's AP item" and isn't mistaken for an own boss Key. Real
	// Archipelago-logo model is the backlogged end goal.
	if (player != ap_net_self_slot())
		return STATIC_GEM;

	// Own CTR reward -> its category's model.
	switch (AP_ItemCategory(item))
	{
	case AP_CAT_TROPHY:
		return STATIC_TROPHY;
	case AP_CAT_SAPPHIRE:
	case AP_CAT_GOLD:
	case AP_CAT_PLATINUM:
		return STATIC_RELIC;
	case AP_CAT_TOKEN:
		return STATIC_TOKEN;
	case AP_CAT_GEM:
		return STATIC_GEM;
	case AP_CAT_KEY:
		return STATIC_KEY;
	default:
		// Own Wumpa / filler / unmapped -> the generic white-gem marker, SAME as a
		// foreign item. Previously STATIC_KEY, which after
		// the golden-key render fix made every filler location look like a real boss
		// Key ("extra keys" in the glows). A boss Key must ONLY show for AP_CAT_KEY.
		return STATIC_GEM;
	}
}

// Tier-specific relic tint for the reward glow (see header). colorRGBA is packed
// as (R<<0x14)|(G<<0xc)|(B<<0x4); 0x020a5ff0 is the vanilla relic blue. Returns 0
// to leave the caller's default colour untouched.
int AP_WarpPadRewardTint(int globalBit)
{
	long code;
	long long item = 0;
	int player = -1;
	unsigned flags = 0;

	code = AP_LookupLocationCode(globalBit);
	if (code < 0)
		return 0;
	if (!ap_net_scout_known(code, &item, &player, &flags))
		return 0;
	if (player != ap_net_self_slot())
		// Foreign multiworld item: until it has its own model (the Archipelago
		// logo is a backlogged custom-asset task) it renders as STATIC_GEM. Tint
		// it pure white so it reads as a "white gem = someone else's AP item"
		// marker, distinct from own coloured gem-cup gems and from own boss Keys.
		// The render applies this to the gem model (see AH_WarpPad.c). Packed
		// (R<<0x14)|(G<<0xc)|(B<<0x4).
		return AP_FOREIGN_TINT; // (255,255,255) white

	switch (AP_ItemCategory(item))
	{
	case AP_CAT_SAPPHIRE:
		return 0x020a5ff0; // blue (vanilla relic colour)
	case AP_CAT_GOLD:
		return 0x0ffc6290; // gold
	case AP_CAT_PLATINUM:
		return 0x0ebebf50; // platinum / pale silver
	case AP_CAT_GEM:
		return 0; // own gem-cup gem -> keep its natural born colour (untinted)
	default:
		// Own Wumpa / filler / unmapped now renders on STATIC_GEM (see
		// AP_WarpPadRewardModel) -- tint it white so it reads as the same generic
		// marker as a foreign item. Relic/trophy/token/key
		// items never reach here (they hit their own cases / models).
		return AP_FOREIGN_TINT; // white
	}
}

// Enumerate the still-UNCOLLECTED (unchecked) AP reward locations of race track
// `destLevelID` (0..15), writing their global AdvProgress bits into `outBits`
// (caller array, capacity `cap`) in fixed tier order -- Trophy +0x06, Sapphire
// +0x16, Gold +0x28, Platinum +0x3a, CTR Token +0x4c -- and returning the count
// written (0..5). A location counts as collected purely by AP CHECKED-STATE
// (AP_LocationCheckedByBit / ap_net_location_checked), NEVER by CHECK_ADV_BIT or
// modelIndex: AP_ApplyItems clears local-win bits every frame, so only the
// server's checked set is authoritative. Returns 0 for a non-race destination or
// when every tier is checked (caller then shows nothing). This is the sole input
// to the warp-pad glow's "which rewards to advertise + cycle" decision.
int AP_WarpPadUncollectedBits(int destLevelID, int *outBits, int cap)
{
	static const int kTierBit[5] = {
	    ADV_REWARD_FIRST_TROPHY,
	    ADV_REWARD_FIRST_SAPPHIRE_RELIC,
	    ADV_REWARD_FIRST_GOLD_RELIC,
	    ADV_REWARD_FIRST_PLATINUM_RELIC,
	    ADV_REWARD_FIRST_CTR_TOKEN};
	int count = 0;
	int i;

	if (outBits == 0 || cap <= 0)
		return 0;
	if (destLevelID < 0 || destLevelID >= 16)
		return 0; // only race tracks 0..15 carry this 5-location pool

	for (i = 0; i < 5 && count < cap; i++)
	{
		int bit = destLevelID + kTierBit[i];
		// AP_LookupLocationCode == -1 (location not in this seed's table, e.g. a
		// tier the apworld didn't place) is treated as "not advertisable" -> skip,
		// so we never show a glow for a non-existent location.
		if (AP_LookupLocationCode(bit) < 0)
			continue;
		if (!AP_LocationCheckedByBit(bit))
			outBits[count++] = bit;
	}

	return count;
}

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
//   trial 16,17 : 3 relic tiers (sapphire/gold/platinum) -- Slide Coliseum +
//                 Turbo Track carry no trophy/token location
//   arena 18,19,21,23 : 1 crystal (battleTrackArr[dest-18] + FIRST_PURPLE_TOKEN)
//   cup   100..104     : 1 gem ((dest-100) + FIRST_GEM)
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
			if (AP_LookupLocationCode(bit) < 0)
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
			if (AP_LookupLocationCode(bit) < 0)
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
		int bit = R232.battleTrackArr[destLevelID - 18] + ADV_REWARD_FIRST_PURPLE_TOKEN;
		if (AP_LookupLocationCode(bit) >= 0 && !AP_LocationCheckedByBit(bit) && count < cap)
			outBits[count++] = bit;
	}
	else if (destLevelID >= 100 && destLevelID < 105)
	{
		// Gem cup (Gem, bits 106..110), keyed by cup colour 0..4.
		int bit = (destLevelID - 100) + ADV_REWARD_FIRST_GEM;
		if (AP_LookupLocationCode(bit) >= 0 && !AP_LocationCheckedByBit(bit) && count < cap)
			outBits[count++] = bit;
	}

	return count;
}

// Is destination `destLevelID` one of the 16 shuffleable race tracks (the only
// category with the full 6-state two-stage lifecycle)?
static int AP_DestIsRace(int destLevelID)
{
	return destLevelID >= 0 && destLevelID < 16;
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
static int AP_PadStage1Met(int physLevelID)
{
	int i, owned;

	// Race tracks (0..15): the canonical helper (shared with ThTick + LInB).
	if (physLevelID >= 0 && physLevelID < 16)
		return ctr_cfg_warp_unlocked(physLevelID);

	// Trial pads: Slide Coliseum (16) = vanilla 10 Sapphire relics; Turbo Track
	// (17) = vanilla all 5 Gem colours. Randomized single-stage req overrides.
	if (physLevelID == 16 || physLevelID == 17)
	{
		if (ctr_cfg_active() && physLevelID < CTR_CFG_PAD_COUNT &&
		    ctr_cfg.warp_pad_unlock[physLevelID].stage1.type != 0)
			return AP_BossReqMet(&ctr_cfg.warp_pad_unlock[physLevelID].stage1);
		if (physLevelID == 16)
			return AP_GateCount(AP_IDX_SAPPHIRE) >= 10;
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
	int uncBits[5];
	int uncN;

	if (!ctr_cfg_active())
		return 0; // vanilla mode -> caller leaves the pad untouched
	if (!AP_DestKnown(destLevelID))
		return 0;

	uncN = AP_PadUncollectedBits(destLevelID,
	                             uncBits, (int)(sizeof uncBits / sizeof uncBits[0]));

	// Done is terminal: every destination location checked. A done pad has
	// nothing left by definition, so hard-locking it never gates progression.
	if (uncN == 0)
		return 5;

	if (!AP_PadStage1Met(physLevelID))
		return 1; // Locked: entry requirement unmet

	if (!AP_DestIsRace(destLevelID))
		return 2; // reduced lifecycle (trial/arena/cup): stage-1 met + checks left

	// Race destination: full two-stage lifecycle.
	if (!AP_LocationCheckedByBit(destLevelID + ADV_REWARD_FIRST_TROPHY))
		return 2; // Raceable: trophy race (primary check) still available

	// Trophy checked, more checks remain -> stage-2 phase (keyed by physical pad).
	if (!ctr_cfg_warp_stage2_unlocked(physLevelID))
		return 3; // Re-locked: stage-2 requirement not yet met
	return 4;     // Tier-2 open: relic TT / CTR token checks available
}

// ---------------------------------------------------------------------------
// AP STATE GENERATION (Warp-Pad State Model v2, foundation for live re-birth).
// A monotonically increasing counter bumped whenever something that can change a
// pad's AP_PadState happens: a fresh slot-connect (slot_data activation), a
// received item that changes a gate count, or a location-checked notification.
// A future in-hub re-birth consumer tags each pad with the generation it was
// born at and, on mismatch, rebuilds the pad's in-hub instances so the 3D look
// tracks state changes in real time (item arrives / check completes elsewhere
// while standing in the hub) -- and re-remaps a pad born before a late connect.
//
// NOT yet consumed for re-birth: AH_WarpPad_LInB is a LEVEL-LOAD one-shot (see
// INSTANCE_LevLInBs), and the only teardown pattern the codebase proves is
// destruction (inst->thread=0 + THREAD_FLAG_DEAD, e.g. RB_Crate). A safe in-hub
// re-birth needs a lifecycle this code does not yet establish, so that consumer
// is deferred to a focused, in-game-verified change rather than inferred blind
// (crew verify-first rule). The MAP colour and every gate already recompute
// AP_PadState live each frame, so those two surfaces are honest in real time
// today; only the in-hub 3D structural look and a late-connect pad's destination
// remap wait on this re-birth.
// ---------------------------------------------------------------------------
static unsigned ap_state_gen = 0;

unsigned AP_StateGen(void)
{
	return ap_state_gen;
}

// ---------------------------------------------------------------------------
// LOCATION EVENTS (option A) -- authoritative. Called from the game's reward
// grant sites; logs the check and sends it to the server.
// ---------------------------------------------------------------------------

// Goal completion state. The Oxide beats are game EVENTS (set by AP_NotifyGoal);
// item-based goals read received counts. ap_goal_sent makes the send idempotent.
static int ap_oxide_first_beaten = 0;
static int ap_oxide_final_beaten = 0;
static int ap_goal_sent = 0;

static int AP_AllFiveGems(void)
{
	int c;
	for (c = 0; c < 5; c++)
		if (AP_GateCountGemColour(c) < 1)
			return 0;
	return 1;
}

// Send StatusUpdate(GOAL) once when the per-seed goal (ctr_cfg.goal) is met.
// Mirrors the apworld completion_condition per goal (goal enum keeps a gap at 2:
// the old "everythingplusone" goal was dropped, the ints are NOT renumbered):
//   0 oxide      -> beat N. Oxide (first challenge)
//   1 oxidefinal -> beat N. Oxide (final challenge)
//   3 allbosses  -> won all 4 boss races (Ripper Roo, Papu Papu, Komodo Joe,
//                   Pinstripe). "Won" == the boss-race LOCATION is CHECKED
//                   (sent only on an actual live win, game/222.c). NOT
//                   AP_GateCount(Trophy)>=16, and NOT CHECK_ADV_BIT on the boss
//                   key bits 94-97: those are the Key item pool (AP_POOL_KEY),
//                   so AP_ApplyItems sets them from RECEIVED keys -- holding 4
//                   shuffled Keys is not beating 4 bosses (the BUG-D class).
//   4 allgemcups -> all 5 gems received (i.e. won every gem cup)
// Without slot_data (vanilla / no AP config) we keep the legacy behaviour: the
// first Oxide beat is the goal.
void AP_EvaluateGoal(void)
{
	int done = 0;

	if (ap_goal_sent)
		return;

	if (!ctr_cfg_active())
	{
		done = ap_oxide_first_beaten;
	}
	else
	{
		switch (ctr_cfg.goal)
		{
		case 0: done = ap_oxide_first_beaten; break;
		case 1: done = ap_oxide_final_beaten; break;
		case 3:
		{
			// All 4 boss races personally won == each boss-race location checked.
			// Durable (server checked-set, resent on connect) + reliable (checked
			// only at the live win site, never by received Keys). Same signal
			// AP_BossGarageOpen uses for trophy tracks.
			int allWon = 1, b;
			for (b = 0; b < 4; b++)
				if (!AP_LocationCheckedByBit(ADV_REWARD_FIRST_BOSS_KEY + b))
					allWon = 0;
			done = allWon;
			break;
		}
		case 4: done = AP_AllFiveGems(); break;
		default: done = ap_oxide_first_beaten; break;
		}
	}

	if (done)
	{
		char msg[96];
		ap_goal_sent = 1;
		snprintf(msg, sizeof msg,
		         "[AP GOAL] goal %d complete -- StatusUpdate(GOAL)\n",
		         ctr_cfg_active() ? ctr_cfg.goal : -1);
		AP_AppendLog(msg);
		ap_net_send_goal();
	}
}

// Per-session dedup so a grant site that re-enters (an end-event drawn every
// frame, or a reward path without its own CHECK_ADV_BIT guard) only produces one
// check. 192 bits = the 6 AdvProgress reward words.
static u32 ap_notified_mask[6] = {0};

// Podium fan-out lives with the race listener below; forward-declared here so
// the trophy-award path (a win => 1st => every rung) and the connect-time
// reconciliation sweep can both drive it as crash/miss backstops.
static void AP_SendPodiumChecks(int track, int placement);
static void AP_ReconcilePodiumFromTrophies(void);

void AP_NotifyAdvReward(int rewardBit)
{
	char msg[192];

	if (rewardBit < 0 || rewardBit >= 192)
		return;
	int w = rewardBit >> 5, b = rewardBit & 31;
	if (ap_notified_mask[w] & (1u << b))
		return; // already notified this session
	ap_notified_mask[w] |= (1u << b);

	long code = AP_LookupLocationCode(rewardBit);
	const char *name = AP_TrophyName(rewardBit);

	if (code >= 0)
	{
		snprintf(msg, sizeof msg,
		         "[AP CHECK] location: AP code %ld (bit %d)%s%s\n",
		         code, rewardBit, name ? " -- " : "", name ? name : "");
	}
	else
	{
		snprintf(msg, sizeof msg,
		         "[AP CHECK] non-location reward granted: bit %d (no AP code)\n",
		         rewardBit);
	}
	AP_AppendLog(msg);

	if (code >= 0)
	{
		ap_net_send_location(code); // LocationChecks([code])
		ap_state_gen++; // a location was checked -> the owning pad's state may shift
	}

	// Podium backstop (event-time): winning a trophy race == finishing 1st ==
	// every podium rung earned. Fire them from THIS reliable award path, not only
	// the live finish-line capture, so a crash/reload/missed edge after the line
	// still delivers the rungs. Trophy bits are the contiguous block
	// [ADV_REWARD_FIRST_TROPHY, ADV_REWARD_FIRST_SAPPHIRE_RELIC); levelID is the
	// offset within it. Deduped inside AP_SendPodiumChecks.
	if (rewardBit >= ADV_REWARD_FIRST_TROPHY &&
	    rewardBit < ADV_REWARD_FIRST_SAPPHIRE_RELIC)
		AP_SendPodiumChecks(rewardBit - ADV_REWARD_FIRST_TROPHY, 1);
}

void AP_NotifyGoal(int oxideSecond)
{
	// Record the Oxide beat as a game EVENT. Whether it completes the seed
	// depends on ctr_cfg.goal -- for non-Oxide goals (all-bosses, all-gem-cups)
	// beating Oxide is NOT the win -- so defer the StatusUpdate(GOAL) decision to
	// AP_EvaluateGoal (which also fires per-frame for the item-based goals).
	if (oxideSecond)
		ap_oxide_final_beaten = 1;
	else
		ap_oxide_first_beaten = 1;

	// As of schema 4 the two Oxide beats are also REAL location checks
	// (35011104 / 35011105). AP_NotifyAdvReward looks up the bit's code in
	// AP_LOCATION_TABLE and sends the LocationCheck with per-session dedup, so a
	// pre-rework seed (bits not in its table) simply no-ops here.
	AP_NotifyAdvReward(oxideSecond ? AP_GOAL_BIT_OXIDE_SECOND
	                               : AP_GOAL_BIT_OXIDE_FIRST);

	AP_EvaluateGoal();
}

// ---------------------------------------------------------------------------
// NETWORK glue: connect once, pump every frame, surface received items.
// ---------------------------------------------------------------------------

static int ap_net_started = 0;

// AI-difficulty option sync: 1 once this connection's override has been pulled
// into the local config (one-shot per connect; re-armed on each fresh connect).
static int ap_diff_pulled = 0;

// Count of received items per bit-pool category (index by AP_ItemCat 0..COUNT-1).
// Kept ONLY to drive the cosmetic AdvProgress bits (podium / profile % / hub-map
// icons) in AP_ApplyItems. The adventure GATES no longer read these bits.
static int ap_item_count[AP_CAT_COUNT] = {0};

// Per-item-TYPE received count (Option B). This is the AUTHORITATIVE gate input:
// the boss garages, hub doors, gem cups, Turbo Track, Slide Coliseum, and trophy
// warp pads read these via AP_GateCount* (see game/232/AH_*.c, #ifdef CTR_AP).
// Keyed by raw item index 0..14 (id - AP_ITEM_BASE). Decoupled from location
// bits, so generic item fill never collides with the game's own location grants.
static int ap_recv_count[AP_ITEM_INDEX_COUNT] = {0};

int AP_GateCount(int itemType)
{
	if (itemType < 0 || itemType >= AP_ITEM_INDEX_COUNT)
		return 0;
	return ap_recv_count[itemType];
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

int AP_GateCountGemSum(void)
{
	return AP_GateCountGemColour(0) + AP_GateCountGemColour(1) +
	       AP_GateCountGemColour(2) + AP_GateCountGemColour(3) +
	       AP_GateCountGemColour(4);
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
int AP_BossReqMet(const ctr_req *r)
{
	if (r == 0)
		return 1;
	switch (r->type)
	{
	case 0:
		return 1; // no requirement -> always met (native vanilla rule applies elsewhere)
	case 1: // trophies
		return AP_GateCount(AP_IDX_TROPHY) >= r->count;
	case 2: // keys
		return AP_GateCount(AP_IDX_KEY) >= r->count;
	case 3: // tokens (colour 0..4 = one colour; -1 = any token, summed)
		return ((r->colour >= 0) ? AP_GateCountTokenColour(r->colour)
		                         : AP_GateCountTokenSum()) >= r->count;
	case 4: // relic, tier by colour (0=Sapphire, 1=Gold, 2=Platinum; legacy -1=Sapphire)
		return AP_GateCountRelicTier(r->colour) >= r->count;
	case 5: // gems (colour 0..4 = one colour; -1 = any gem, summed)
		return ((r->colour >= 0) ? AP_GateCountGemColour(r->colour)
		                         : AP_GateCountGemSum()) >= r->count;
	case 6: // AnyToken: any N tokens summed across all 5 colours
		return AP_GateCountTokenSum() >= r->count;
	case 7: // AnyRelic: any N relics summed across Sapphire+Gold+Platinum
		return AP_GateCountRelicSum() >= r->count;
	case 8: // AnyGem: any N gems summed across all 5 colours
		return AP_GateCountGemSum() >= r->count;
	default:
		return 1;
	}
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

// Trophy-track warp pad LOAD gate. When a per-seed requirement applies use it;
// otherwise fall back verbatim to the Phase-1 rule (received trophies vs the
// per-track numTrophiesToOpen). levelID is the physical pad LevelID (retail
// adventure numbering), valid for the 28-wide ctr_cfg arrays for trophy tracks.
int ctr_cfg_warp_unlocked(int levelID)
{
	if (ctr_cfg_active() && levelID >= 0 && levelID < CTR_CFG_PAD_COUNT &&
	    ctr_cfg.warp_pad_unlock[levelID].stage1.type != 0)
		return AP_BossReqMet(&ctr_cfg.warp_pad_unlock[levelID].stage1);

	// Cup PHYSICAL pads (100..104) live outside the dense warp_pad_unlock array.
	// Under `merged` destination shuffle a race destination can be hosted on a cup
	// pad, so ThTick's load gate (AH_WarpPad.c:679) recovers physLevelID = a cup
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
	if (ctr_cfg_active() && physPadLevelID >= 0 && physPadLevelID < CTR_CFG_PAD_COUNT &&
	    ctr_cfg.warp_pad_unlock[physPadLevelID].stage2.type != 0)
		return AP_BossReqMet(&ctr_cfg.warp_pad_unlock[physPadLevelID].stage2);

	// Cup PHYSICAL pads (100..104): under `merged` shuffle a cup pad hosting a
	// trophy-race destination carries a REAL stage2 (contract §2 stage-2 rule -- the
	// apworld emits it into gem_cup_unlock), so honour it. type 0 (cup hosting
	// non-race content, or vanilla) falls through to the always-open return below.
	if (ctr_cfg_active() && physPadLevelID >= 100 && physPadLevelID <= 104 &&
	    ctr_cfg.gem_cup_unlock[physPadLevelID - 100].stage2.type != 0)
		return AP_BossReqMet(&ctr_cfg.gem_cup_unlock[physPadLevelID - 100].stage2);

	// No stage2 requirement -> tier 2 opens as soon as stage1 is satisfied.
	return 1;
}

// Reconcile AdvProgress category bits to EXACTLY the received-item counts: set
// the top `count` bits of each pool (high-end), CLEAR the rest.
//
// OPTION B NOTE: as of Phase 1 the adventure GATES no longer read these bits --
// they read the per-item-TYPE counters via AP_GateCount* (see game/232/AH_*.c).
// These bits are now COSMETIC ONLY: they still feed GAMEPROG_AdvPercent so the
// progress %, podium, and hub-map icons render, but they no longer gate anything.
// Because the gates are independent of the bits, the old item-bit / location-bit
// collision (BL-2/BL-3) is dissolved regardless of what these bits hold. Kept as
// the original high-end reconcile so the cosmetic UI behaves exactly as before.
// Consequences:
//  - BL-2 fix: a local race win sets a vanilla reward bit, but it's not backed
//    by a received item, so it's cleared on the next tick -> a win never
//    double-counts toward the gates. Progression comes only from received items.
//  - Any save works: stale bits from a non-AP save are wiped and the received
//    set re-applied; on (re)connect the server re-sends your items so the counts
//    rebuild from scratch. Load any file and your progress == your AP inventory.
// Location checks are still emitted by the grant-site events (AP_NotifyAdvReward),
// independent of these bits. Only the 7 managed location-bit pools are touched;
// story/hint/door flags are left to the game.
// Idempotent + self-healing across loads. Refreshes the live counters via
// GAMEPROG_AdvPercent so gates that read currAdvProfile see the change.
static void AP_ApplyItems(struct AdvProgress *adv)
{
	int changed = 0, granted = 0;
	int c, k;
	for (c = 0; c < AP_CAT_COUNT; c++)
	{
		const AP_CatPool *p = &AP_CATEGORY_POOLS[c];
		int want = ap_item_count[c];
		if (want > p->size)
			want = p->size;
		for (k = 0; k < p->size; k++)
		{
			int bit = p->bits[p->size - 1 - k]; // high-end first
			int isSet = CHECK_ADV_BIT(adv->rewards, bit) != 0;
			if (k < want)
			{
				if (!isSet)
				{
					UNLOCK_ADV_BIT(adv->rewards, bit);
					changed = 1;
					granted = 1;
				}
			}
			else if (isSet)
			{
				// bit not backed by a received item -> clear (local-win leak /
				// stale save bit)
				adv->rewards[bit >> 5] &= ~(1u << (bit & 31));
				changed = 1;
			}
		}
	}
	if (changed)
		GAMEPROG_AdvPercent(adv);
	if (granted)
		AP_AppendLog("[AP ITEM] applied received items to AdvProgress\n");
}

// Read ws uri / slot / password from "ap-config.txt" in the working dir if it
// exists. Lines: "uri=...", "slot=...", "password=...". Missing file or keys
// keep the defaults.
static void AP_ReadConfig(char *uri, int uriN, char *slot, int slotN,
                          char *pass, int passN)
{
	FILE *f = fopen("ap-config.txt", "r");
	if (!f)
		return;
	char line[256];
	while (fgets(line, sizeof line, f))
	{
		size_t n = strlen(line);
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		if (!strncmp(line, "uri=", 4))
			snprintf(uri, uriN, "%s", line + 4);
		else if (!strncmp(line, "slot=", 5))
			snprintf(slot, slotN, "%s", line + 5);
		else if (!strncmp(line, "password=", 9))
			snprintf(pass, passN, "%s", line + 9);
		else if (!strncmp(line, "skip_hints=", 11))
			ap_skip_hints = (line[11] == '1'); // QoL: suppress Aku Aku mask hints
		else if (AP_TrapConfigLine(line))
			; // debug_trap=... -> prime a trap for testing (see ap_traps.c)
		else if (AP_ShortcutConfigLine(line))
			; // shortcutless=... / shortcut_capture=... (see ap_shortcut.c)
		else if (!strncmp(line, "map_flash=", 10))
			ap_map_flash = (line[10] != '0'); // 0 = static GREEN (no Raceable flicker)
	}
	fclose(f);
}

static void AP_NetTick(struct GameTracker *gGT)
{
	if (!ap_net_started)
	{
		char uri[128] = "";
		char slot[64] = "";
		char pass[64] = "";
		char msg[256];
		int haveLegacy = 0;
		int haveIni = (g_config.uri[0] != '\0');

		// Always parse ap-config.txt when it exists: besides uri/slot/password it
		// primes the trap / shortcut / skip_hints test hooks (see AP_ReadConfig). If
		// present it seeds the legacy connection defaults, then its own keys override.
		{
			FILE *f = fopen("ap-config.txt", "r");
			if (f)
			{
				fclose(f);
				snprintf(uri, sizeof uri, "ws://localhost:38281");
				snprintf(slot, sizeof slot, "CtrSmokeTest");
				AP_ReadConfig(uri, sizeof uri, slot, sizeof slot, pass, sizeof pass);
				haveLegacy = 1;
			}
		}

		// config.ini [Connection] wins over ap-config.txt when set (same precedence
		// as AP_SkipHints / AP_MapFlashOn).
		if (haveIni)
		{
			snprintf(uri, sizeof uri, "%s", g_config.uri);
			snprintf(slot, sizeof slot, "%s", g_config.slot);
			snprintf(pass, sizeof pass, "%s", g_config.password);
		}

		// Approved startup change: with NEITHER a config.ini [Connection] uri NOR an
		// ap-config.txt present, do NOT auto-dial the localhost default. Skip the dial
		// and surface a hint (log + the menu's status line show "not connected"); the
		// player sets up the room in OPTIONS > Connection and hits Connect. With any
		// config present, startup auto-connect behaves exactly as before.
		if (haveIni || haveLegacy)
		{
			snprintf(msg, sizeof msg, "[AP NET] init uri=%s slot=%s\n", uri, slot);
			AP_AppendLog(msg);
			if (ap_net_init("ctr-native", "Crash Team Racing", uri) == 0)
				ap_net_connect_slot(slot, pass);
		}
		else
		{
			AP_AppendLog("[AP NET] not connected - set up your room in "
			             "OPTIONS > Connection (no config.ini [Connection], no ap-config.txt)\n");
		}
		ap_net_started = 1; // attempt once
	}

	ap_net_poll();

	// On a fresh slot-connect (new seed, reconnect, or server switch) the server
	// resends the FULL ReceivedItems list from index 0. Zero the per-session
	// tallies + goal/notify state FIRST so everything rebuilds from the
	// authoritative list, instead of accumulating on top of the previous
	// connection (which double-counted on reconnect and carried a prior seed's
	// items across a server switch). AP_ApplyItems reconciles AdvProgress bits to
	// ap_recv_count each frame, so zeroing here clears the stale in-game counts.
	// Foundation for a future in-game connection manager (clean per-connect reset).
	if (ap_net_take_recv_reset())
	{
		int k;
		for (k = 0; k < AP_ITEM_INDEX_COUNT; k++)
			ap_recv_count[k] = 0;
		for (k = 0; k < AP_CAT_COUNT; k++)
			ap_item_count[k] = 0;
		for (k = 0; k < 6; k++)
			ap_notified_mask[k] = 0;
		ap_oxide_first_beaten = 0;
		ap_oxide_final_beaten = 0;
		ap_goal_sent = 0;
		ap_state_gen++; // fresh connect: slot_data (re)activates -> pad states may all shift
		AP_AppendLog("[AP NET] fresh connect -> reset received-item tally + session state\n");

		// AI-difficulty option sync: subscribe to (and fetch) the per-slot override,
		// seeded by this seed's slot_data default. ap_diff_pulled re-arms the one-shot
		// pull below so a reconnect / server switch re-reads the authoritative value.
		ap_net_difficulty_subscribe(ctr_cfg.ai_difficulty_default);
		ap_diff_pulled = 0;
	}

	// One-shot connect pull: as soon as a difficulty value is known from the server
	// (the slot_data default seed, or the Get reply that overrides it), mirror it
	// into the local config so the menu slider and the effect read one source. Done
	// exactly once per connect so it never clobbers a live in-menu edit mid-session
	// (cross-device changes land on the next reconnect, not live -- v1 limitation).
	if (!ap_diff_pulled)
	{
		int dv;
		if (ap_net_difficulty_known(&dv))
		{
			g_config.aiDifficulty = dv;
			ap_diff_pulled = 1;
		}
	}

	// Podium backstop (connect-time): reconcile rungs for any trophy race whose
	// win was already recorded (a prior session, or a crash that lost the live
	// capture). Winning == 1st == all rungs, so the durable trophy-checked signal
	// is sufficient. Deduped + cheap (16 tracks) so it is safe to run every tick;
	// this is what unblocks an existing stuck save on reconnect WITHOUT re-racing.
	AP_ReconcilePodiumFromTrophies();

	// Received items: tally by category. Applied to AdvProgress bits in
	// AP_ApplyItems() (adventure + save-safe only). The tally is zeroed on each
	// fresh connect (above), so the resent full list rebuilds counts exactly.
	long long items[32];
	int n = ap_net_drain_items(items, 32);
	int i;
	char st[64];
	AP_FmtState(gGT, st, sizeof st); // game-state breadcrumb for this drain (crash diag)
	for (i = 0; i < n; i++)
	{
		// Authoritative gate counter: tally by raw item TYPE index 0..14.
		long long idx = items[i] - AP_ITEM_BASE;
		if (idx >= 0 && idx < AP_ITEM_INDEX_COUNT)
		{
			ap_recv_count[idx]++;
			ap_state_gen++; // a gate-relevant count changed -> pad states may shift
		}

		// Coarse category tally: kept only to drive the cosmetic AdvProgress
		// bits (progress %, podium) in AP_ApplyItems -- gates ignore these.
		AP_ItemCat c = AP_ItemCategory(items[i]);
		char msg[160];
		if (c >= 0 && c < AP_CAT_COUNT)
		{
			ap_item_count[c]++;
			snprintf(msg, sizeof msg, "[AP ITEM] received %s (id %lld) -> have %d | %s\n",
			         AP_CATEGORY_POOLS[c].name, items[i], ap_item_count[c], st);
		}
		else
		{
			snprintf(msg, sizeof msg,
			         "[AP ITEM] received item id %lld (filler/unmapped) | %s\n", items[i], st);
		}
		AP_AppendLog(msg);
	}
}

// In-game connection manager: tear down the current client and re-dial. The menu
// is reachable only from the main menu, so there is no in-flight item or live
// adventure state to strand -- AP_NetTick keeps polling, and the fresh slot-connect
// resets the per-session tallies through ap_net_take_recv_reset() as usual.
void AP_Net_Reconnect(const char *uri, const char *slot, const char *password)
{
	char msg[256];
	snprintf(msg, sizeof msg, "[AP NET] reconnect uri=%s slot=%s\n",
	         uri ? uri : "", slot ? slot : "");
	AP_AppendLog(msg);

	ap_net_shutdown();
	if (ap_net_init("ctr-native", "Crash Team Racing", uri) == 0)
		ap_net_connect_slot(slot, password);
	ap_net_started = 1; // suppress the boot-time auto-dial from re-running
}

// One-line status for the menu's read-only status row.
const char *AP_Net_StatusLine(void)
{
	static char line[128];
	switch (ap_net_status())
	{
	case AP_NET_STATUS_CONNECTED:
		snprintf(line, sizeof line, "Connected");
		break;
	case AP_NET_STATUS_CONNECTING:
		snprintf(line, sizeof line, "Connecting...");
		break;
	case AP_NET_STATUS_ERROR:
	{
		const char *e = ap_net_last_error();
		if (e && *e)
			snprintf(line, sizeof line, "Error: %s", e);
		else
			snprintf(line, sizeof line, "Connection error");
		break;
	}
	default:
		snprintf(line, sizeof line, "Not connected");
		break;
	}
	return line;
}

// ── AI-difficulty preset ──
// The local config value is the single source of truth for both the menu row and
// the effect; the connect pull mirrors any server override into it. It holds the
// raw engine difficulty VALUE (0 = vanilla; presets 0x50/0xA0/0xF0/0x140/0x280).
// An out-of-ladder value from slot_data is used as-is (only display snaps to the
// nearest preset). Returns it clamped non-negative.
int AP_AiDifficultyValue(void)
{
	int v = g_config.aiDifficulty;
	if (v < 0)
		v = 0;
	return v;
}

void AP_AiDifficultyCommit(void)
{
	// No-op when not connected (ap_net_difficulty_set guards on state). When
	// connected this persists the local value to the per-slot data-storage key.
	ap_net_difficulty_set(AP_AiDifficultyValue());
}

// ---------------------------------------------------------------------------
// DEBUG poll: log *unmapped* AdvProgress bit changes (non-location state like
// story/hint flags). Mapped locations + goal bits are owned by the events above
// and skipped here. Re-baselines on entering adventure / after loads.
// ---------------------------------------------------------------------------

static u32 ap_dbg_snapshot[6];
static int ap_dbg_armed = 0;

static void AP_PollDebug(struct AdvProgress *adv)
{
	int w, b;

	if (!ap_dbg_armed)
	{
		for (w = 0; w < 6; w++)
			ap_dbg_snapshot[w] = adv->rewards[w];
		ap_dbg_armed = 1;
		return;
	}

	for (w = 0; w < 6; w++)
	{
		u32 cur = adv->rewards[w];
		u32 newly = cur & ~ap_dbg_snapshot[w]; // bits that went 0 -> 1
		if (newly)
		{
			for (b = 0; b < 32; b++)
			{
				if (!(newly & (1u << b)))
					continue;
				int globalBit = (w * 32) + b;
				if (AP_LookupLocationCode(globalBit) >= 0)
					continue; // a mapped location -- owned by the grant-site events
				              // (schema 4: the Oxide bits 115/116 are now real codes
				              // too, so this clause already covers them)
				char msg[160];
				snprintf(msg, sizeof msg,
				         "[AP DBG] unmapped bit set: rewards[%d] bit %d "
				         "(globalBit %d, mask 0x%08x)\n",
				         w, b, globalBit, (u32)(1u << b));
				AP_AppendLog(msg);
			}
		}
		ap_dbg_snapshot[w] = cur;
	}
}

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// RACE-PLACEMENT LISTENER -- observation-only groundwork for the decided
// "podium check ladder" (per-race nested rungs: 1st / 2nd-or-3rd / any-finish).
// The game does NOT persist race placement, so the apworld can't derive it from
// AdvProgress -- this listener captures it LIVE. It sends NOTHING to the AP
// server: it only mirrors the player's live position and records the final
// placement of each finished race into the diagnostic log (ap-read.log) + the
// state dump (ap-state.json), so the apworld location design can be made with a
// full picture of what the engine actually exposes.
//
// Engine surfaces (source-anchored, all in the same unity build):
//   * live rank      -- gGT->drivers[0]->driverRank  (s16, 0 = 1st place;
//                        namespace_Vehicle.h:1528, offset 0x482). Recomputed
//                        every frame by PlayLevel_UpdateLapStats (PlayLevel.c:4).
//   * race finished  -- drivers[0]->actionsFlagSet & ACTION_RACE_FINISHED
//                        (0x2000000; namespace_Vehicle.h:576). Set the frame the
//                        player crosses the line on the final lap, regardless of
//                        placement -- so 2nd/3rd/4th finishes are observable here
//                        even though adventure mode itself only "wins" (awards a
//                        trophy) on 1st (AA_EndEvent_DrawMenu, 222.c:100/449).
//   * P1 == local    -- gGT->drivers[0] is the human player in adventure/boss
//                        races (222.c:69, UI_Rank.c single-player branch).
// ---------------------------------------------------------------------------

// Race-type classification from the mode words at the finish moment, so the
// apworld design knows which surface each placement came from (only the standard
// trophy race is a podium-ladder target; the rest are documented, not gated).
enum
{
	AP_RACE_UNKNOWN = 0,
	AP_RACE_TROPHY  = 1, // standard adventure trophy race (podium-ladder target)
	AP_RACE_BOSS    = 2, // boss race: 2 racers, 1st = win / 2nd = loss only
	AP_RACE_RELIC   = 3, // relic time trial: solo, no standings (rank always 0)
	AP_RACE_CRYSTAL = 4, // crystal challenge: collect-all, no standings
	AP_RACE_TOKEN   = 5, // CTR token challenge (GameMode2 TOKEN_RACE)
	AP_RACE_ARCADE  = 6, // arcade / non-adventure race
};

static const char *AP_RACE_TYPE_NAMES[7] = {
    "unknown", "trophy", "boss", "relic", "crystal", "token", "arcade"};

static const char *AP_RaceTypeName(int t)
{
	return (t >= 0 && t < 7) ? AP_RACE_TYPE_NAMES[t] : "?";
}

// Live placement of the player this frame: driverRank+1 (1 = 1st), or -1 when
// not in a live adventure race. Mirrored into ap-state.json.
static int ap_live_position = -1;

// Last captured final placement (survives the whole session for diff/inspection).
static int ap_last_race_track = -1; // gGT->levelID at the finish moment
static int ap_last_race_place = -1; // driverRank+1 (1 = 1st), -1 = none yet
static int ap_last_race_mode  = AP_RACE_UNKNOWN;

static int AP_ClassifyRace(struct GameTracker *gGT)
{
	int gm1 = gGT->gameMode1;
	int gm2 = gGT->gameMode2;
	if ((gm1 & ADVENTURE_MODE) == 0)
		return AP_RACE_ARCADE;
	if (IS_BOSS_RACE(gm1))
		return AP_RACE_BOSS;
	if (gm1 & RELIC_RACE)
		return AP_RACE_RELIC;
	if (gm1 & CRYSTAL_CHALLENGE)
		return AP_RACE_CRYSTAL;
	if (gm2 & TOKEN_RACE)
		return AP_RACE_TOKEN;
	return AP_RACE_TROPHY;
}

// Fan a trophy-race finish out into podium-ladder location checks (the native
// half of feat/podium-checks; the apworld declares the rung locations + emits
// the podium_checks slot_data block, ap_seedcfg parses it into ctr_cfg.podium).
// The nesting rule "a better result satisfies every rung at or below it" is
// applied HERE from the observed final placement:
//   placement == 1     -> first + podium + any   (a win fires every rung)
//   placement in {2,3}  -> podium + any           (the podium rung, not 1st)
//   placement >= 4      -> any only
// A rung the seed did not create is stored -1 and skipped; sends dedup against
// the client's checked-set so re-winning a track never re-emits. These are AP
// location codes, NOT AdvProgress bits, so they go straight down ap_net_send_
// location -- NEVER through AP_NotifyAdvReward's bit lookup. MUST be called
// only for AP_RACE_TROPHY finishes: relic / token /
// crystal challenges run on the SAME levelIDs as the trophy tracks (only the
// mode word differs), so gating on the levelID range alone would misfire.
static void AP_SendPodiumChecks(int track, int placement)
{
	if (!ctr_cfg_active() || !ctr_cfg.podium_enabled)
		return;
	if (track < 0 || track >= CTR_CFG_PODIUM_TRACK_COUNT)
		return; // only the 16 trophy races carry rungs
	if (placement < 1)
		return; // -1 = placement unknown -> nothing to fan out

	const ctr_podium_rungs *pr = &ctr_cfg.podium[track];

	// Assemble the earned rung set, best-result-fills-lower.
	long codes[3];
	int n = 0;
	if (placement == 1)
		codes[n++] = pr->first;  // a win also clears podium + any below
	if (placement <= 3)
		codes[n++] = pr->podium; // 1st clears it too; 2nd/3rd start here
	codes[n++] = pr->any;        // every finish clears the any-position rung

	for (int i = 0; i < n; i++)
	{
		long code = codes[i];
		if (code < 0)
			continue; // rung absent this seed
		if (ap_net_location_checked(code))
			continue; // already checked (re-win / reload)

		char msg[128];
		snprintf(msg, sizeof msg,
		         "[AP CHECK] podium: track=%d place=%d location %ld\n",
		         track, placement, code);
		AP_AppendLog(msg);
		ap_net_send_location(code); // LocationChecks([code])
	}
}

// Connect-time backstop: any trophy race whose trophy LOCATION is already
// checked was, by the engine's win-only rule (a trophy is awarded ONLY on 1st),
// finished 1st -- so every podium rung for that track is earned. Sweep all 16
// trophy tracks and fan out (deduped) from that durable signal. Idempotent, so
// running it each connect/tick just no-ops once the rungs are checked. Covers
// races won in a prior session and finishes whose live capture was lost.
static void AP_ReconcilePodiumFromTrophies(void)
{
	if (!ctr_cfg_active() || !ctr_cfg.podium_enabled)
		return;
	for (int lid = 0; lid < CTR_CFG_PODIUM_TRACK_COUNT; lid++)
		if (AP_LocationCheckedByBit(lid + ADV_REWARD_FIRST_TROPHY))
			AP_SendPodiumChecks(lid, 1); // trophy won => 1st => all rungs
}

// Called every frame (all game modes) from AP_OnFrame, BEFORE the adventure
// throttle/early-return, so the finish transition is never missed. Self-gates to
// real races via drivers[0] + ACTION_RACE_FINISHED. Captures placement, then
// fans out podium-ladder checks for trophy-race finishes.
static void AP_RaceListenerTick(struct GameTracker *gGT)
{
	// Rising-edge detector on the player's ACTION_RACE_FINISHED bit -- fires the
	// capture exactly once per race and re-arms automatically when the flag
	// clears (next race start). Same idiom as the M-key overlay toggle above; it
	// naturally ignores the multi-frame ceremony / retry menu / reload that all
	// hold the flag set. Persisting across a hub visit is harmless (0->1 only).
	static int ap_finish_prev = 0;

	struct Driver *p = gGT->drivers[0]; // P1 == the local player in adventure

	// Live-position mirror: only meaningful with a valid player driver in an
	// adventure context; -1 otherwise so a diff can't misread a stale hub rank.
	if (p != NULL && (gGT->gameMode1 & ADVENTURE_MODE) != 0)
		ap_live_position = (p->driverRank >= 0) ? p->driverRank + 1 : -1;
	else
		ap_live_position = -1;

	if (p == NULL)
		return;

	int finishedNow = (p->actionsFlagSet & ACTION_RACE_FINISHED) != 0;
	if (finishedNow && !ap_finish_prev)
	{
		// driverRank is frozen once you cross the line (nobody can pass a
		// finished racer), so this is the authoritative final placement.
		ap_last_race_track = (int)gGT->levelID;
		ap_last_race_place = (p->driverRank >= 0) ? p->driverRank + 1 : -1;
		ap_last_race_mode  = AP_ClassifyRace(gGT);

		char m[160];
		snprintf(m, sizeof m,
		         "[AP RACE] track=%d final_place=%d race_type=%s "
		         "(t=%u adv=%d load=%d)\n",
		         ap_last_race_track, ap_last_race_place,
		         AP_RaceTypeName(ap_last_race_mode), (unsigned)gGT->timer,
		         (gGT->gameMode1 & ADVENTURE_MODE) != 0 ? 1 : 0,
		         (int)sdata->Loading.stage);
		AP_AppendLog(m);

		// Native fan-out: only trophy races carry podium rungs (relic/token/
		// crystal share the same levelIDs, so the type gate is load-bearing).
		if (ap_last_race_mode == AP_RACE_TROPHY)
			AP_SendPodiumChecks(ap_last_race_track, ap_last_race_place);
	}
	ap_finish_prev = finishedNow;
}

// ---------------------------------------------------------------------------
// STATE DUMP -- write a JSON snapshot of the live AP <-> game state to
// ap-state.json (overwritten on a throttle) so the game's belief can be diffed
// against the apworld seed / slot_data offline. Debug-only; no gameplay effect.
// Relative path -> lands next to the exe (the play folder, same dir as
// ap-config.txt). Pairs received-item truth + the game's own counters + the
// per-pad resolved requirement & whether it's met + trophy-race checked-state,
// which is exactly the surface where game/apworld misalignment shows up.
// ---------------------------------------------------------------------------
#define AP_STATE_FILE "ap-state.json"

static const char *AP_ITEM_NAMES[AP_ITEM_INDEX_COUNT] = {
    "Trophy", "Sapphire Relic", "Gold Relic", "Platinum Relic",
    "Red CTR Token", "Green CTR Token", "Blue CTR Token", "Yellow CTR Token",
    "Purple CTR Token", "Red Gem", "Green Gem", "Blue Gem", "Yellow Gem",
    "Purple Gem", "Key"};

static const char *AP_REQ_TYPE_NAMES[9] = {
    "none", "trophy", "key", "token", "sapphire", "gem",
    "any_token", "any_relic", "any_gem"};

static const char *AP_BOSS_NAMES[CTR_CFG_BOSS_COUNT] = {
    "ripper_roo", "papu_papu", "komodo_joe", "pinstripe", "oxide"};

static const char *AP_ReqTypeName(int t)
{
	return (t >= 0 && t < 9) ? AP_REQ_TYPE_NAMES[t] : "?";
}

// Tier-aware requirement name for the debug dump. A type-4 relic req carries its
// tier in colour (schema 4: 0=Sapphire, 1=Gold, 2=Platinum; legacy -1=Sapphire),
// so the flat "sapphire" name would misread every tier as sapphire. Widen only
// type 4; every other type is colour-independent and defers to AP_ReqTypeName.
static const char *AP_ReqTypeNameColour(int t, int colour)
{
	if (t == 4)
	{
		switch (colour)
		{
		case 1:  return "gold_relic";
		case 2:  return "platinum_relic";
		default: return "sapphire_relic"; // colour 0 or legacy -1
		}
	}
	return AP_ReqTypeName(t);
}

static void AP_DumpState(struct GameTracker *gGT)
{
	int i, checked = 0;
	FILE *f = fopen(AP_STATE_FILE, "w");
	if (!f)
		return;

	for (i = 0; i < AP_LOCATION_TABLE_LEN; i++)
		if (ap_net_location_checked(AP_LOCATION_TABLE[i].location_code))
			checked++;

	fputs("{\n", f);
	fprintf(f, "  \"schema_active\": %d,\n", ctr_cfg_active());
	fprintf(f, "  \"in_adventure\": %d,\n",
	        (gGT->gameMode1 & ADVENTURE_MODE) != 0 ? 1 : 0);
	fprintf(f, "  \"connected_slot\": %d,\n", ap_net_self_slot());
	fprintf(f, "  \"locations_checked\": %d,\n", checked);
	fprintf(f, "  \"locations_total\": %d,\n", AP_LOCATION_TABLE_LEN);
	// Live race placement (observation-only groundwork for the podium ladder).
	fprintf(f, "  \"live_position\": %d,\n", ap_live_position);
	fprintf(f,
	        "  \"last_race\": {\"track\": %d, \"placement\": %d, "
	        "\"type\": \"%s\"},\n",
	        ap_last_race_track, ap_last_race_place,
	        AP_RaceTypeName(ap_last_race_mode));
	fprintf(f,
	        "  \"options\": {\"goal\": %d, \"oxide_final_unlock\": %d, "
	        "\"warppad_unlock_mode\": %d, \"bossgarage_mode\": %d, "
	        "\"shuffle_warp_pads\": %d},\n",
	        ctr_cfg.goal, ctr_cfg.oxide_final_unlock, ctr_cfg.warppad_unlock_mode,
	        ctr_cfg.bossgarage_mode, ctr_cfg.shuffle_warp_pads);

	// The game's own cached counters (what gates/UI read) -- compare vs AP truth.
	fprintf(f,
	        "  \"game_counters\": {\"trophies\": %d, \"relics\": %d, "
	        "\"keys\": %d, \"tokens\": %d, \"completion_pct\": %d},\n",
	        gGT->currAdvProfile.numTrophies, gGT->currAdvProfile.numRelics,
	        gGT->currAdvProfile.numKeys, gGT->currAdvProfile.numCtrTokens,
	        gGT->currAdvProfile.completionPercent);

	// AP-side truth: received-item counts by type.
	fputs("  \"received_items\": {", f);
	for (i = 0; i < AP_ITEM_INDEX_COUNT; i++)
		fprintf(f, "%s\"%s\": %d", i ? ", " : "", AP_ITEM_NAMES[i],
		        AP_GateCount(i));
	fputs("},\n", f);

	// Per-pad: destination, resolved requirement, met?, trophy-race checked?
	fputs("  \"pads\": [\n", f);
	for (i = 0; i < CTR_CFG_PAD_COUNT; i++)
	{
		const ctr_warp_unlock *u = &ctr_cfg.warp_pad_unlock[i];
		const ctr_req *r = &u->stage1;
		int dest = ctr_cfg_warp_dest(i);
		// trophy_checked only means something for the 16 race-track destinations
		// (LevelID 0..15, the trophy-race bit pool). For non-race dests (trials,
		// arenas, cups) dest+TROPHY_OFFSET collides into a relic bit and would read
		// a false positive -- emit -1 (n/a) instead.
		int trophyChecked =
		    (dest >= 0 && dest < 16)
		        ? AP_LocationCheckedByBit(dest + ADV_REWARD_FIRST_TROPHY)
		        : -1;
		fprintf(f,
		        "    {\"pad\": %d, \"dest\": %d, \"req_type\": \"%s\", "
		        "\"count\": %d, \"colour\": %d, \"unlocked\": %d, "
		        "\"stage2_req_type\": \"%s\", \"stage2_count\": %d, "
		        "\"stage2_colour\": %d, \"stage2_unlocked\": %d, "
		        "\"trophy_checked\": %d}%s\n",
		        i, dest, AP_ReqTypeNameColour(r->type, r->colour), r->count, r->colour,
		        ctr_cfg_warp_unlocked(i),
		        AP_ReqTypeNameColour(u->stage2.type, u->stage2.colour),
		        u->stage2.count, u->stage2.colour,
		        ctr_cfg_warp_stage2_unlocked(i), trophyChecked,
		        (i + 1 < CTR_CFG_PAD_COUNT) ? "," : "");
	}
	fputs("  ],\n", f);

	// Cup PHYSICAL pads (100..104): map + two-stage req + met, so contract §6.3
	// offline diffing (ap-state.json vs slot_data) covers destination-shuffled cups
	// the same way the dense "pads" array covers 0..27. dest = gem_cup_map (a race
	// destination here means the cup pad hosts a trophy race); trophy_checked follows
	// the DESTINATION when that destination is a race track, else -1 (n/a).
	fputs("  \"cup_pads\": [\n", f);
	for (i = 0; i < 5; i++)
	{
		const ctr_warp_unlock *u = &ctr_cfg.gem_cup_unlock[i];
		const ctr_req *r = &u->stage1;
		int phys = 100 + i;
		int dest = ctr_cfg_warp_dest(phys);
		int trophyChecked =
		    (dest >= 0 && dest < 16)
		        ? AP_LocationCheckedByBit(dest + ADV_REWARD_FIRST_TROPHY)
		        : -1;
		fprintf(f,
		        "    {\"pad\": %d, \"dest\": %d, \"req_type\": \"%s\", "
		        "\"count\": %d, \"colour\": %d, \"unlocked\": %d, "
		        "\"stage2_req_type\": \"%s\", \"stage2_count\": %d, "
		        "\"stage2_colour\": %d, \"stage2_unlocked\": %d, "
		        "\"trophy_checked\": %d}%s\n",
		        phys, dest, AP_ReqTypeNameColour(r->type, r->colour), r->count, r->colour,
		        ctr_cfg_warp_unlocked(phys),
		        AP_ReqTypeNameColour(u->stage2.type, u->stage2.colour),
		        u->stage2.count, u->stage2.colour,
		        ctr_cfg_warp_stage2_unlocked(phys), trophyChecked,
		        (i + 1 < 5) ? "," : "");
	}
	fputs("  ],\n", f);

	// Boss garages: resolved requirement + met?
	fputs("  \"bosses\": [\n", f);
	for (i = 0; i < CTR_CFG_BOSS_COUNT; i++)
	{
		const ctr_req *r = &ctr_cfg.boss_req[i];
		fprintf(f,
		        "    {\"boss\": \"%s\", \"req_type\": \"%s\", \"count\": %d, "
		        "\"met\": %d}%s\n",
		        AP_BOSS_NAMES[i], AP_ReqTypeNameColour(r->type, r->colour), r->count,
		        AP_BossReqMet(r), (i + 1 < CTR_CFG_BOSS_COUNT) ? "," : "");
	}
	fputs("  ]\n", f);

	fputs("}\n", f);
	fclose(f);
}

void AP_OnFrame(struct GameTracker *gGT)
{
	// M-key: RESERVED for the future hub-map ZOOM (Warp-Pad State Model v2). The
	// old debug map-overlay colour toggle it used to drive is retired -- map
	// colours are now always on (AP_PadState). The zoom itself is deferred: it
	// needs to scale UI_Map_GetIconPos + UI_Map_DrawMap (ASM-verified vanilla,
	// shared by the hub map AND the track-select / in-race minimaps) around the
	// map centre, which is fragile shared-render work to be done + verified in
	// game, not inferred here. AP_MAP_OVERLAY_SCANCODE / Platform_InputRawKeyDown
	// are left declared as the input plumbing that rework will reuse. No handler
	// runs today, so M is an inert no-op.

	// Diagnostic breadcrumbs (crash investigation): a one-time run-start marker so
	// the appended log has a clear per-process boundary, plus a line on every
	// levelID (hub/track) transition. The log flushes per line, so if a crash
	// happens during a transition (e.g. an Autopelago item lands mid hub-load) the
	// tail shows exactly where. Logging only -- no gameplay effect.
	{
		static int ap_booted = 0;
		static int ap_prev_level = -999;
		if (!ap_booted)
		{
			ap_booted = 1;
			AP_AppendLog("[AP BOOT] ===== client run start =====\n");
		}
		if ((int)gGT->levelID != ap_prev_level)
		{
			char m[96];
			snprintf(m, sizeof m, "[AP HUB] levelID %d -> %d (t=%u load=%d)\n",
			         ap_prev_level, (int)gGT->levelID, (unsigned)gGT->timer,
			         (int)sdata->Loading.stage);
			AP_AppendLog(m);
			ap_prev_level = (int)gGT->levelID;
		}
	}

	// Race-placement listener: runs every frame in all modes so the finish
	// transition is never missed by the throttle below. Observation-only.
	AP_RaceListenerTick(gGT);

	// Network: connect once + pump every frame, in all game modes.
	AP_NetTick(gGT);

	// Trap framework: advance prime->fire->clear lifecycle + apply the FP camera,
	// and poll the Shortcutless debug keys. Runs every frame / all modes (the tick
	// gates its own race-only logic). Physics effects apply at their engine sites.
	AP_TrapTick(gGT);
	AP_ShortcutKeys();
	AP_ShortcutSkipTick(gGT); // layer-2 checkpoint-% gap-skip detector (Shortcutless)

	// State snapshot (~every 60 frames) -- runs in ALL game modes so it's available
	// at the title screen right after connect. AP-side fields (options, received
	// items, checked locations, per-pad reqs) are valid once slot_data is parsed;
	// game_counters read live only in adventure mode (see "in_adventure" in JSON).
	static int dumpTick = 0;
	if ((++dumpTick % 60) == 0)
		AP_DumpState(gGT);

	// Adventure-only item/goal/debug poll below.
	if ((gGT->gameMode1 & ADVENTURE_MODE) == 0 ||
	    sdata->Loading.stage != LOAD_IDLE)
	{
		ap_dbg_armed = 0;
		return;
	}

	// Throttle: act ~twice a second, not every frame.
	static int tick = 0;
	if ((++tick % 30) != 0)
		return;

	AP_ApplyItems(&sdata->advProgress); // grant received items into game state
	AP_EvaluateGoal(); // item-based goals (all-bosses / all-gem-cups / 101%) fire here
	AP_PollDebug(&sdata->advProgress);
}

#endif // CTR_AP
