#ifdef CTR_AP

#include <common.h>
#include <stdio.h>

#include "ap_hooks.h"
#include "ap_locations.h" // generated bit_index -> AP location_code table (99 locs)

// ============================================================================
// Archipelago integration -- per-frame hook.
//
// Two halves run each (throttled) tick while in Adventure Mode and not mid-load:
//
//   WRITE path (item-receive): reconcile the game's AdvProgress against the set
//   of items the player *should* have. Idempotent + self-healing -- setting an
//   already-set bit is a no-op, and if a save load wipes a granted bit the next
//   tick re-applies it. (Currently a spike: one hardcoded desired item.)
//
//   READ path (location-check): snapshot AdvProgress and detect 0->1 bit
//   transitions, i.e. locations the player just checked (won a trophy/relic/etc).
//   Currently logs them; the real path will send LocationChecks to the AP server.
//
// Both halves are still local prototypes. The next milestone (apclientpp) makes
// the WRITE desired-set come from the server's received-items list, and the READ
// detections turn into network LocationChecks. See TODO(ap) markers.
// ============================================================================

#define AP_SPIKE_LOG "D:\\pythonProjects\\ctr-archipelago\\reference" \
                     "\\ctr-native\\build-ap\\ap-spike.log"
#define AP_READ_LOG  "D:\\pythonProjects\\ctr-archipelago\\reference" \
                     "\\ctr-native\\build-ap\\ap-read.log"

// ---------------------------------------------------------------------------
// WRITE path (spike): one fixed desired item, the Crash Cove trophy.
//   advProgress.rewards[0] bit 0x200 == global bitIndex 9.
// TODO(ap): replace the hardcoded desired item with the apclientpp item list.
// ---------------------------------------------------------------------------

static void AP_LogReconcile(struct GameTracker *gGT, struct AdvProgress *adv)
{
	fprintf(stderr,
	        "[AP SPIKE] reconcile applied Crash Cove trophy; "
	        "rewards[0]=0x%08x, numTrophies=%d\n",
	        adv->rewards[0], gGT->currAdvProfile.numTrophies);

	// Append (not overwrite) so repeated re-applies are visible -- each line
	// after the first is the self-healing reconcile recovering from a wipe.
	FILE *ap_log = fopen(AP_SPIKE_LOG, "a");
	if (ap_log)
	{
		fprintf(ap_log,
		        "[AP SPIKE] reconcile applied Crash Cove trophy; "
		        "rewards[0]=0x%08x, numTrophies=%d\n",
		        adv->rewards[0], gGT->currAdvProfile.numTrophies);
		fclose(ap_log);
	}
}

// ---------------------------------------------------------------------------
// READ path: detect newly-set AdvProgress bits (location checks).
//
// `ap_loc_snapshot` caches the last-seen rewards words. On the first armed tick
// it is seeded from the current state (AFTER the write reconcile runs, so the
// spike's forced Crash Cove bit is part of the baseline, not reported). On every
// later tick, a 0->1 transition in any of the 6 reward words is a location the
// player just checked. Each globalBit (= word*32 + bit) is resolved to its AP
// location code via the generated AP_LOCATION_TABLE (ap_locations.h); the two
// Oxide goal bits are reported separately. The 16 track trophies also get a
// friendly name for easy manual verification.
//
// `ap_read_armed` is cleared whenever we leave Adventure Mode or a load is in
// progress, so re-entering / loading a save re-seeds the baseline from the
// loaded state instead of reporting every earned bit as a fresh check.
// ---------------------------------------------------------------------------

static u32 ap_loc_snapshot[6];
static int ap_read_armed = 0;

// Bits the WRITE reconcile set from received items. On native, item-grants and
// location-checks share the same AdvProgress bits (there is no rando ROM patch to
// redirect progression gates onto separate SaveSlot-4 counters as Icebound's
// BizHawk client relies on -- vanilla gates count these bits via
// GAMEPROG_AdvPercent). So a genuine location check is a bit that went 0->1 AND is
// NOT in this mask. Limitation: if an item pre-sets a track's bit, a later real
// win of that track produces no 0->1 and is missed (the "missed-check" case); that
// can only be fully solved by event-based detection (hook the race-win/reward
// grant), an open architecture decision -- see board 2026-06-22 15:50 thread.
static u32 ap_client_set_mask[6] = {0};

// Friendly names for the 16 track trophies (AdvProgress word 0, bits 6..21).
// Everything else logs generically; the full location map is the apworld ID
// reconciliation (a later step), not needed for the read-path prototype.
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

static void AP_LogLocationCheck(int word, int bit, int globalBit, u32 wordValue)
{
	char msg[192];
	const char *name = AP_TrophyName(globalBit); // friendly hint for the 16 trophies
	long code = AP_LookupLocationCode(globalBit);

	if (globalBit == AP_GOAL_BIT_OXIDE_FIRST || globalBit == AP_GOAL_BIT_OXIDE_SECOND)
	{
		snprintf(msg, sizeof msg,
		         "[AP READ] GOAL bit set: globalBit %d (Oxide %s win)\n",
		         globalBit,
		         globalBit == AP_GOAL_BIT_OXIDE_FIRST ? "first" : "second");
		// TODO(ap): send goal completion to the server.
	}
	else if (code >= 0)
	{
		snprintf(msg, sizeof msg,
		         "[AP READ] location checked: AP code %ld (globalBit %d)%s%s\n",
		         code, globalBit, name ? " -- " : "", name ? name : "");
		// TODO(ap): send LocationChecks([code]) to the server.
	}
	else
	{
		snprintf(msg, sizeof msg,
		         "[AP READ] unmapped bit set: rewards[%d] bit %d "
		         "(globalBit %d, mask 0x%08x)\n",
		         word, bit, globalBit, (u32)(1u << bit));
	}

	fputs(msg, stderr);
	FILE *ap_log = fopen(AP_READ_LOG, "a");
	if (ap_log)
	{
		fputs(msg, ap_log);
		fclose(ap_log);
	}
}

static void AP_PollLocations(struct AdvProgress *adv)
{
	int w, b;

	if (!ap_read_armed)
	{
		// Seed the baseline from current state; do not report on this tick.
		for (w = 0; w < 6; w++)
			ap_loc_snapshot[w] = adv->rewards[w];
		ap_read_armed = 1;
		return;
	}

	for (w = 0; w < 6; w++)
	{
		u32 cur = adv->rewards[w];
		u32 newly = cur & ~ap_loc_snapshot[w];       // bits that went 0 -> 1
		u32 player = newly & ~ap_client_set_mask[w]; // drop item/client-set bits
		if (player)
		{
			for (b = 0; b < 32; b++)
			{
				if (player & (1u << b))
					AP_LogLocationCheck(w, b, (w * 32) + b, cur);
			}
		}
		ap_loc_snapshot[w] = cur;
	}
}

// ---------------------------------------------------------------------------

void AP_OnFrame(struct GameTracker *gGT)
{
	// Only act when it is safe: in adventure mode and not mid-load (the load
	// sequence copies memcard->advProgress over sdata->advProgress and recounts,
	// so writing during it would be clobbered and reading would report the whole
	// loaded save as fresh checks). Leaving this state disarms the read baseline.
	if ((gGT->gameMode1 & ADVENTURE_MODE) == 0 ||
	    sdata->Loading.stage != LOAD_IDLE)
	{
		ap_read_armed = 0;
		return;
	}

	// Throttle: act ~twice a second, not every frame.
	static int tick = 0;
	if ((++tick % 30) != 0)
		return;

	struct AdvProgress *adv = &sdata->advProgress;

	// --- WRITE path (spike): reconcile desired items into game state. ---
	// Mark the desired item's bit as client-set every tick (idempotent) so the
	// read path below never reports it as a player location check.
	ap_client_set_mask[0] |= (1u << 9); // Crash Cove trophy (bit 9)
	if (CHECK_ADV_BIT(adv->rewards, 9) == 0)
	{
		UNLOCK_ADV_BIT(adv->rewards, 9);
		GAMEPROG_AdvPercent(adv);
		AP_LogReconcile(gGT, adv);
	}

	// --- READ path: detect location checks (runs after the write so the spike's
	//     forced bit is baselined, not reported as a player check). ---
	AP_PollLocations(adv);
}

#endif // CTR_AP
