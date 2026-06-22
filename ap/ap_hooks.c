#ifdef CTR_AP

#include <common.h>
#include <stdio.h>

#include "ap_hooks.h"

// === AP SPIKE (temporary): item-reconcile write-path prototype ===
// Periodically reconciles the game's adventure progression against the set of
// items the player *should* have. Idempotent and self-healing: setting an
// already-set bit is a no-op, and if a save load wipes a granted bit, the next
// reconcile tick re-applies it. This is the shape the real item-receive path
// needs -- items arrive from the multiworld at arbitrary times (menus, loads,
// races), so we cannot grant once at a single moment; we continuously make the
// game state match the authoritative item list.
//
// The spike uses one fixed "desired" item: the Crash Cove trophy
// (advProgress.rewards[0] bit 0x200 == bitIndex 9). The real path will drive the
// desired set from the AP server's received-items list (apclientpp).
// TODO(ap): replace the hardcoded desired item with the apclientpp item list.

static void AP_LogReconcile(struct GameTracker *gGT, struct AdvProgress *adv)
{
	fprintf(stderr,
	        "[AP SPIKE] reconcile applied Crash Cove trophy; "
	        "rewards[0]=0x%08x, numTrophies=%d\n",
	        adv->rewards[0], gGT->currAdvProfile.numTrophies);

	// Append (not overwrite) so repeated re-applies are visible -- each line
	// after the first is the self-healing reconcile recovering from a wipe.
	FILE *ap_log = fopen("D:\\pythonProjects\\ctr-archipelago\\reference"
	                     "\\ctr-native\\build-ap\\ap-spike.log", "a");
	if (ap_log)
	{
		fprintf(ap_log,
		        "[AP SPIKE] reconcile applied Crash Cove trophy; "
		        "rewards[0]=0x%08x, numTrophies=%d\n",
		        adv->rewards[0], gGT->currAdvProfile.numTrophies);
		fclose(ap_log);
	}
}

void AP_OnFrame(struct GameTracker *gGT)
{
	// Only write when it is safe: in adventure mode and not mid-load (the load
	// sequence copies memcard->advProgress over sdata->advProgress and recounts,
	// so writing during it would be clobbered).
	if ((gGT->gameMode1 & ADVENTURE_MODE) == 0 ||
	    sdata->Loading.stage != LOAD_IDLE)
		return;

	// Throttle: reconcile ~twice a second, not every frame.
	static int tick = 0;
	if ((++tick % 30) != 0)
		return;

	struct AdvProgress *adv = &sdata->advProgress;

	// Desired item: Crash Cove trophy (bit 9). If the game state doesn't already
	// reflect it, apply it and refresh the live counters the UI/gates read.
	if (CHECK_ADV_BIT(adv->rewards, 9) == 0)
	{
		UNLOCK_ADV_BIT(adv->rewards, 9);
		GAMEPROG_AdvPercent(adv);
		AP_LogReconcile(gGT, adv);
	}
}

#endif // CTR_AP
