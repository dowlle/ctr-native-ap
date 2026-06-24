#ifdef CTR_AP

#include <common.h>
#include <stdio.h>

#include "ap_hooks.h"
#include "ap_locations.h" // generated bit_index -> AP location_code table (99 locs)
#include "ap_net.h"       // C API into the apclientpp network client (ap_net.cpp)
#include "ap_items.h"     // item-id -> AdvProgress category bit pools

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
// MAP OVERLAY (debug/QoL) -- badge each warp pad on the existing minimap by the
// AP state of its DESTINATION track. Pure colour override on the icon already
// drawn by AH_Map_Warppads; no geometry change, no gameplay effect.
// ---------------------------------------------------------------------------

// 1 if the location at this global AdvProgress bit holds an OWN, progression,
// still-unchecked AP item ("useful reward still here"); 0 otherwise. Mirrors the
// AP_WarpPad* helpers: lookup -> scout -> own-slot + flags bit0 (progression).
static int AP_BitIsUsefulUnchecked(int globalBit)
{
	long code;
	long long item = 0;
	int player = -1;
	unsigned flags = 0;

	code = AP_LookupLocationCode(globalBit);
	if (code < 0)
		return 0;                       // not a checkable location
	if (ap_net_location_checked(code))
		return 0;                       // already collected -> nothing useful here
	if (!ap_net_scout_known(code, &item, &player, &flags))
		return 0;                       // not scouted (not connected / pre-scout)
	if (player != ap_net_self_slot())
		return 0;                       // foreign multiworld item -> not "our" reward
	return (flags & 1u) ? 1 : 0;        // bit0 = progression
}

// Usefulness verdict for a warp pad pointing at destination LevelID `destLevelID`:
//   1  = at least one of the dest track's 5 reward locations holds an own,
//        progression, still-unchecked item (useful reward still here)
//   0  = a race track, but nothing useful is left unchecked there
//  -1  = not a race-track destination (0..15 only carry the trophy-race pool;
//        other dests would collide into unrelated bits)
// The 5 per-track bits: trophy +0x06, sapphire +0x16, gold +0x28, platinum
// +0x3a, token +0x4c (enum AdvRewardBitIndex, namespace_Memcard.h:186-205).
int AP_PadUsefulness(int destLevelID)
{
	if (destLevelID < 0 || destLevelID >= 16)
		return -1;

	if (AP_BitIsUsefulUnchecked(destLevelID + ADV_REWARD_FIRST_TROPHY) ||
	    AP_BitIsUsefulUnchecked(destLevelID + ADV_REWARD_FIRST_SAPPHIRE_RELIC) ||
	    AP_BitIsUsefulUnchecked(destLevelID + ADV_REWARD_FIRST_GOLD_RELIC) ||
	    AP_BitIsUsefulUnchecked(destLevelID + ADV_REWARD_FIRST_PLATINUM_RELIC) ||
	    AP_BitIsUsefulUnchecked(destLevelID + ADV_REWARD_FIRST_CTR_TOKEN))
		return 1;

	return 0;
}

// Map-overlay toggle. Flipped on the rising edge of SDL_SCANCODE_M in AP_OnFrame
// (see below). The game-side AH_Map_Warppads reads it via AP_MapOverlayOn().
static int g_ap_map_overlay = 0;

int AP_MapOverlayOn(void)
{
	return g_ap_map_overlay;
}

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
	if (player != ap_net_self_slot())
		return STATIC_KEY;

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
		return STATIC_KEY; // own Wumpa / unmapped -> marker rather than a wrong model
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
		return 0; // foreign item keeps its marker colour

	switch (AP_ItemCategory(item))
	{
	case AP_CAT_SAPPHIRE:
		return 0x020a5ff0; // blue (vanilla relic colour)
	case AP_CAT_GOLD:
		return 0x0ffc6290; // gold
	case AP_CAT_PLATINUM:
		return 0x0ebebf50; // platinum / pale silver
	default:
		return 0; // not an own relic -> caller default
	}
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
// Mirrors the apworld completion_condition per goal:
//   0 oxide             -> beat N. Oxide (first challenge)
//   1 oxidefinal        -> beat N. Oxide (final challenge)
//   2 everythingplusone -> beat N. Oxide (final) + 18 Gold relics + all 5 gems
//   3 allbosses         -> 16 trophies received
//   4 allgemcups        -> all 5 gems received (i.e. won every gem cup)
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
		case 2: done = ap_oxide_final_beaten
		               && AP_GateCount(AP_IDX_GOLD) >= 18
		               && AP_AllFiveGems(); break;
		case 3: done = AP_GateCount(AP_IDX_TROPHY) >= 16; break;
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
		ap_net_send_location(code); // LocationChecks([code])
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
	AP_EvaluateGoal();
}

// ---------------------------------------------------------------------------
// NETWORK glue: connect once, pump every frame, surface received items.
// ---------------------------------------------------------------------------

static int ap_net_started = 0;

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

// ── Phase 2: per-seed resolved-requirement comparators ──
// These live C-side (not in ap_seedcfg.cpp) because they read the received-item
// counters above (AP_GateCount*) and the per-track numTrophiesToOpen threshold
// from data.metaDataLEV[], both of which are unreachable from the isolated C++
// slot_data parser. ctr_cfg + ctr_cfg_active()/ctr_cfg_warp_dest() come from
// ap_seedcfg.{h,cpp}; the gate sites (AH_*.c) call these by name.

// Returns owned >= count for a resolved requirement. colour-aware for tokens
// (type 3) and gems (type 5): colour 0..4 selects one colour, -1 sums the
// vanilla "all colours" interpretation native-side -- but the apworld emits a
// concrete colour for token/gem reqs, so the -1 branch only fires defensively.
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
	case 3: // tokens (colour 0..4, or -1 = any/Red baseline)
		return ((r->colour >= 0) ? AP_GateCountTokenColour(r->colour)
		                         : AP_GateCount(AP_IDX_TOKEN_RED)) >= r->count;
	case 4: // sapphire
		return AP_GateCount(AP_IDX_SAPPHIRE) >= r->count;
	case 5: // gems (colour 0..4, or -1 = any/Red baseline)
		return ((r->colour >= 0) ? AP_GateCountGemColour(r->colour)
		                         : AP_GateCountGemColour(0)) >= r->count;
	default:
		return 1;
	}
}

// Trophy-track warp pad LOAD gate. When a per-seed requirement applies use it;
// otherwise fall back verbatim to the Phase-1 rule (received trophies vs the
// per-track numTrophiesToOpen). levelID is the physical pad LevelID (retail
// adventure numbering), valid for the 28-wide ctr_cfg arrays for trophy tracks.
int ctr_cfg_warp_unlocked(int levelID)
{
	if (ctr_cfg_active() && levelID >= 0 && levelID < CTR_CFG_PAD_COUNT &&
	    ctr_cfg.warp_pad_unlock[levelID].type != 0)
		return AP_BossReqMet(&ctr_cfg.warp_pad_unlock[levelID]);

	// Phase-1 fallback: received trophies >= per-track threshold.
	return AP_GateCount(AP_IDX_TROPHY) >= data.metaDataLEV[levelID].numTrophiesToOpen;
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
	}
	fclose(f);
}

static void AP_NetTick(void)
{
	if (!ap_net_started)
	{
		char uri[128] = "ws://localhost:38281";
		char slot[64] = "CtrSmokeTest";
		char pass[64] = "";
		char msg[256];
		AP_ReadConfig(uri, sizeof uri, slot, sizeof slot, pass, sizeof pass);
		snprintf(msg, sizeof msg, "[AP NET] init uri=%s slot=%s\n", uri, slot);
		AP_AppendLog(msg);
		if (ap_net_init("ctr-native", "Crash Team Racing", uri) == 0)
			ap_net_connect_slot(slot, pass);
		ap_net_started = 1; // attempt once
	}

	ap_net_poll();

	// Received items: tally by category. Applied to AdvProgress bits in
	// AP_ApplyItems() (adventure + save-safe only). Single game session counts
	// each item once; a reconnect re-sends the full list (dedup TODO).
	long long items[32];
	int n = ap_net_drain_items(items, 32);
	int i;
	for (i = 0; i < n; i++)
	{
		// Authoritative gate counter: tally by raw item TYPE index 0..14.
		long long idx = items[i] - AP_ITEM_BASE;
		if (idx >= 0 && idx < AP_ITEM_INDEX_COUNT)
			ap_recv_count[idx]++;

		// Coarse category tally: kept only to drive the cosmetic AdvProgress
		// bits (progress %, podium) in AP_ApplyItems -- gates ignore these.
		AP_ItemCat c = AP_ItemCategory(items[i]);
		char msg[128];
		if (c >= 0 && c < AP_CAT_COUNT)
		{
			ap_item_count[c]++;
			snprintf(msg, sizeof msg, "[AP ITEM] received %s (id %lld) -> have %d\n",
			         AP_CATEGORY_POOLS[c].name, items[i], ap_item_count[c]);
		}
		else
		{
			snprintf(msg, sizeof msg,
			         "[AP ITEM] received item id %lld (filler/unmapped)\n", items[i]);
		}
		AP_AppendLog(msg);
	}
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
				if (AP_LookupLocationCode(globalBit) >= 0 ||
				    globalBit == AP_GOAL_BIT_OXIDE_FIRST ||
				    globalBit == AP_GOAL_BIT_OXIDE_SECOND)
					continue; // events own these
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

static const char *AP_REQ_TYPE_NAMES[6] = {
    "none", "trophy", "key", "token", "sapphire", "gem"};

static const char *AP_BOSS_NAMES[CTR_CFG_BOSS_COUNT] = {
    "ripper_roo", "papu_papu", "komodo_joe", "pinstripe", "oxide"};

static const char *AP_ReqTypeName(int t)
{
	return (t >= 0 && t < 6) ? AP_REQ_TYPE_NAMES[t] : "?";
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
		const ctr_req *r = &ctr_cfg.warp_pad_unlock[i];
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
		        "\"trophy_checked\": %d}%s\n",
		        i, dest, AP_ReqTypeName(r->type), r->count, r->colour,
		        ctr_cfg_warp_unlocked(i), trophyChecked,
		        (i + 1 < CTR_CFG_PAD_COUNT) ? "," : "");
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
		        AP_BOSS_NAMES[i], AP_ReqTypeName(r->type), r->count,
		        AP_BossReqMet(r), (i + 1 < CTR_CFG_BOSS_COUNT) ? "," : "");
	}
	fputs("  ]\n", f);

	fputs("}\n", f);
	fclose(f);
}

void AP_OnFrame(struct GameTracker *gGT)
{
	// Map-overlay toggle: flip g_ap_map_overlay on the RISING edge of the hotkey.
	// Runs every frame and in all game modes so the toggle works anywhere. Pure
	// HUD state -- no pad-bus interaction, no gameplay effect.
	{
		static int ap_overlay_key_prev = 0;
		int keyNow = Platform_InputRawKeyDown(AP_MAP_OVERLAY_SCANCODE);
		if (keyNow && !ap_overlay_key_prev)
			g_ap_map_overlay = !g_ap_map_overlay;
		ap_overlay_key_prev = keyNow;
	}

	// Network: connect once + pump every frame, in all game modes.
	AP_NetTick();

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
