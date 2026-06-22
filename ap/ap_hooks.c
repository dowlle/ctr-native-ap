#ifdef CTR_AP

#include <common.h>
#include <stdio.h>

#include "ap_hooks.h"
#include "ap_locations.h" // generated bit_index -> AP location_code table (99 locs)

// ============================================================================
// Archipelago integration. Three parts:
//
//   WRITE path (item-receive): reconcile the game's AdvProgress against the set
//   of items the player *should* have. Idempotent + self-healing. (Currently a
//   spike: one hardcoded desired item; becomes the apclientpp received list.)
//
//   LOCATION EVENTS (option A -- AUTHORITATIVE): the game's own reward-grant
//   sites call AP_NotifyAdvReward() the instant a player earns a checkable
//   reward (trophy/relic/token/gem/boss), and AP_NotifyGoal() when Oxide is
//   beaten. This is reliable regardless of load/menu state -- unlike watching
//   the persistent bit, which is set during the race-end -> hub-load transition
//   and was therefore missed by the per-frame poll (see board 2026-06-22 17:18).
//
//   DEBUG poll: a per-frame scan that logs *unmapped* AdvProgress bit changes
//   (story/hint flags) for visibility. It no longer drives location checks --
//   the events above do -- so it cannot double-count or miss real checks.
//
// TODO(ap): apclientpp -- feed the WRITE desired-set from received items, and
// send the LOCATION EVENTS as network LocationChecks + the goal as StatusUpdate.
// ============================================================================

#define AP_SPIKE_LOG "D:\\pythonProjects\\ctr-archipelago\\reference" \
                     "\\ctr-native\\build-ap\\ap-spike.log"
#define AP_READ_LOG  "D:\\pythonProjects\\ctr-archipelago\\reference" \
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

// Resolve a global AdvProgress bit to its AP location code via the generated
// table, or -1 if the bit is not a checkable location.
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
// grant sites. Each checked location is logged now and (TODO) queued for the
// network LocationChecks send once apclientpp lands.
// ---------------------------------------------------------------------------

static int ap_goal_reached = 0;
static int ap_goal_logged_first = 0;
static int ap_goal_logged_second = 0;

// Per-session dedup so a grant site that re-enters (e.g. an end-event drawn
// every frame, or a reward path without its own CHECK_ADV_BIT guard) only
// produces one check. 192 bits = the 6 AdvProgress reward words.
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
		// TODO(ap): queue `code` for the network LocationChecks send.
	}
	else
	{
		// A reward bit the player earned that isn't a checkable AP location
		// (e.g. a non-location story/unlock reward routed through a grant site).
		snprintf(msg, sizeof msg,
		         "[AP CHECK] non-location reward granted: bit %d (no AP code)\n",
		         rewardBit);
	}
	AP_AppendLog(msg);
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
	ap_goal_reached = 1;
	snprintf(msg, sizeof msg,
	         "[AP GOAL] Oxide beaten (%s win) -- goal reached\n",
	         oxideSecond ? "second/final" : "first");
	AP_AppendLog(msg);
	// TODO(ap): send StatusUpdate(CLIENT_GOAL) to the server.
}

// ---------------------------------------------------------------------------
// WRITE path (spike): one fixed desired item, the Crash Cove trophy.
//   advProgress.rewards[0] bit 0x200 == global bitIndex 9.
// TODO(ap): replace the hardcoded desired item with the apclientpp item list.
// ---------------------------------------------------------------------------

static void AP_LogReconcile(struct GameTracker *gGT, struct AdvProgress *adv)
{
	char msg[160];
	snprintf(msg, sizeof msg,
	         "[AP SPIKE] reconcile applied Crash Cove trophy; "
	         "rewards[0]=0x%08x, numTrophies=%d\n",
	         adv->rewards[0], gGT->currAdvProfile.numTrophies);
	fputs(msg, stderr);
	FILE *f = fopen(AP_SPIKE_LOG, "a");
	if (f)
	{
		fputs(msg, f);
		fclose(f);
	}
}

// ---------------------------------------------------------------------------
// DEBUG poll: log *unmapped* AdvProgress bit changes (non-location state like
// story/hint flags) for visibility. Mapped locations and goal bits are owned by
// the LOCATION EVENTS above and intentionally skipped here, so there is no
// double-counting. Re-baselines on entering adventure / after loads.
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
				// Skip mapped locations + goal bits -- events own them.
				if (AP_LookupLocationCode(globalBit) >= 0 ||
				    globalBit == AP_GOAL_BIT_OXIDE_FIRST ||
				    globalBit == AP_GOAL_BIT_OXIDE_SECOND)
					continue;
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
	// Only act when it is safe: in adventure mode and not mid-load (the load
	// sequence copies memcard->advProgress over sdata->advProgress and recounts).
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

	struct AdvProgress *adv = &sdata->advProgress;

	// --- WRITE path (spike): reconcile desired items into game state. ---
	if (CHECK_ADV_BIT(adv->rewards, 9) == 0)
	{
		UNLOCK_ADV_BIT(adv->rewards, 9);
		GAMEPROG_AdvPercent(adv);
		AP_LogReconcile(gGT, adv);
	}

	// --- DEBUG poll: log unmapped state changes (locations come from events). ---
	AP_PollDebug(adv);
}

#endif // CTR_AP
