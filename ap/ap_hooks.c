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

// ---------------------------------------------------------------------------
// LOCATION EVENTS (option A) -- authoritative. Called from the game's reward
// grant sites; logs the check and sends it to the server.
// ---------------------------------------------------------------------------

static int ap_goal_logged_first = 0;
static int ap_goal_logged_second = 0;

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
	char msg[128];
	// Idempotent: grant sites may re-enter while the end-event is on screen.
	if (oxideSecond)
	{
		if (ap_goal_logged_second)
			return;
		ap_goal_logged_second = 1;
	}
	else
	{
		if (ap_goal_logged_first)
			return;
		ap_goal_logged_first = 1;
	}
	snprintf(msg, sizeof msg,
	         "[AP GOAL] Oxide beaten (%s win) -- goal reached\n",
	         oxideSecond ? "second/final" : "first");
	AP_AppendLog(msg);
	ap_net_send_goal(); // StatusUpdate(GOAL)
}

// ---------------------------------------------------------------------------
// NETWORK glue: connect once, pump every frame, surface received items.
// ---------------------------------------------------------------------------

static int ap_net_started = 0;

// Count of received items per bit-pool category (index by AP_ItemCat 0..COUNT-1).
static int ap_item_count[AP_CAT_COUNT] = {0};

// Reconcile received-item counts into AdvProgress bits: ensure `count` bits of
// each category pool are set (filled from the HIGH end -- see ap_items.h
// collision note). Idempotent + self-healing across loads, and refreshes the
// live counters the gates read via GAMEPROG_AdvPercent.
static void AP_ApplyItems(struct AdvProgress *adv)
{
	int changed = 0;
	int c, k;
	for (c = 0; c < AP_CAT_COUNT; c++)
	{
		const AP_CatPool *p = &AP_CATEGORY_POOLS[c];
		int want = ap_item_count[c];
		if (want > p->size)
			want = p->size;
		for (k = 0; k < want; k++)
		{
			int bit = p->bits[p->size - 1 - k]; // high-end fill
			if (CHECK_ADV_BIT(adv->rewards, bit) == 0)
			{
				UNLOCK_ADV_BIT(adv->rewards, bit);
				changed = 1;
			}
		}
	}
	if (changed)
	{
		GAMEPROG_AdvPercent(adv);
		AP_AppendLog("[AP ITEM] applied received items to AdvProgress\n");
	}
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

void AP_OnFrame(struct GameTracker *gGT)
{
	// Network: connect once + pump every frame, in all game modes.
	AP_NetTick();

	// Adventure-only debug poll below.
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
	AP_PollDebug(&sdata->advProgress);
}

#endif // CTR_AP
