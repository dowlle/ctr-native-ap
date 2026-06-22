#ifdef CTR_AP

#include <common.h>
#include <stdio.h>

#include "ap_hooks.h"

// === AP SPIKE (temporary): UNLOCK_ADV_BIT write-path test ===
// One-shot grant of the Crash Cove Trophy (advProgress.rewards[0] bit 0x200
// == bitIndex 9) the first frame an adventure save is active. Proves the
// recompiled native binary can drive adventure progression through the game's
// own primitive, before any AP networking exists.
// TODO(ap): replace with the real item-receive path once apclientpp lands.

void AP_OnFrame(struct GameTracker *gGT)
{
	static int ap_spike_granted = 0;

	if (!ap_spike_granted && (gGT->gameMode1 & ADVENTURE_MODE) != 0)
	{
		struct AdvProgress *adv = &sdata->advProgress;
		UNLOCK_ADV_BIT(adv->rewards, 9); // Crash Cove Trophy
		ap_spike_granted = 1;

		fprintf(stderr,
		        "[AP SPIKE] granted Crash Cove Trophy via UNLOCK_ADV_BIT; "
		        "advProgress.rewards[0]=0x%08x\n",
		        adv->rewards[0]);

		// Also log to a fixed file so capture is launch-agnostic
		// (Steam launch can't redirect stderr). Temporary.
		FILE *ap_log = fopen("D:\\pythonProjects\\ctr-archipelago\\reference"
		                     "\\ctr-native\\build-ap\\ap-spike.log", "w");
		if (ap_log)
		{
			fprintf(ap_log,
			        "[AP SPIKE] granted Crash Cove Trophy via UNLOCK_ADV_BIT; "
			        "advProgress.rewards[0]=0x%08x\n",
			        adv->rewards[0]);
			fclose(ap_log);
		}
	}
}

#endif // CTR_AP
