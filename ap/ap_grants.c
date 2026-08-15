#ifdef CTR_AP

#include <common.h>
#include <stdio.h>
#include <string.h>
#include "ap_grants.h"
#include "ap_grants_logic.h"
#include "ap_hooks.h"
#include "ap_net.h"

#define AP_GRANT_FILE "ctr-ap-grants.txt"
#define AP_GRANT_ROW_CAP 32
#define AP_WEAPON_NONE 0x0f
#define AP_WEAPON_TURBO 0

static AP_GrantLedger g_turboGrant;
static long long g_turboPersistedFired;
static char g_grantSeed[128];
static char g_grantSlot[64];
static int g_grantCountdownSeen;

static void AP_GrantLoad(void)
{
	FILE *f;
	char line[320];
	g_turboPersistedFired = 0;
	g_grantSeed[0] = '\0';
	g_grantSlot[0] = '\0';
	(void)ap_net_seed_name(g_grantSeed, sizeof g_grantSeed);
	(void)ap_net_slot_name(g_grantSlot, sizeof g_grantSlot);
	f = fopen(AP_GRANT_FILE, "r");
	if (f == 0) return;
	while (fgets(line, sizeof line, f))
	{
		char seed[128], slot[64];
		long long fired;
		if (sscanf(line, "%127[^\t]\t%63[^\t]\t%lld", seed, slot, &fired) == 3 &&
		    !strcmp(seed, g_grantSeed) && !strcmp(slot, g_grantSlot))
		{
			g_turboPersistedFired = fired > 0 ? fired : 0;
			break;
		}
	}
	fclose(f);
}

static void AP_GrantStore(void)
{
	FILE *f;
	char rows[AP_GRANT_ROW_CAP][320];
	int count = 0, i;
	if (!g_grantSeed[0] || !g_grantSlot[0]) return;
	f = fopen(AP_GRANT_FILE, "r");
	if (f)
	{
		/* Reserve one row for the current seed and slot. */
		while (count < AP_GRANT_ROW_CAP - 1 &&
		       fgets(rows[count], sizeof rows[0], f))
		{
			char seed[128], slot[64];
			long long fired;
			if (sscanf(rows[count], "%127[^\t]\t%63[^\t]\t%lld", seed, slot, &fired) != 3 ||
			    strcmp(seed, g_grantSeed) || strcmp(slot, g_grantSlot)) count++;
		}
		fclose(f);
	}
	f = fopen(AP_GRANT_FILE, "w");
	if (!f) return;
	for (i = 0; i < count; i++) fputs(rows[i], f);
	fprintf(f, "%s\t%s\t%lld\n", g_grantSeed, g_grantSlot, g_turboPersistedFired);
	fclose(f);
}

void AP_GrantConnectReset(void)
{
	AP_GrantLoad();
	AP_GrantLedgerBeginReplay(&g_turboGrant);
	g_grantCountdownSeen = 0;
}

void AP_TurboGrantReceive(void)
{
	AP_GrantLedgerReceive(&g_turboGrant, g_turboPersistedFired);
}

static int AP_TurboGrantRaceActive(struct GameTracker *gGT)
{
	int forbidden = START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE |
	                PAUSE_ALL | BATTLE_MODE | CRYSTAL_CHALLENGE;
	return LOAD_IsOpen_RacingOrBattle() && g_grantCountdownSeen &&
	       gGT->trafficLightsTimer < 1 && !(gGT->gameMode1 & forbidden);
}

void AP_TurboGrantTick(struct GameTracker *gGT)
{
	struct Driver *driver;
	int onTrack, slotEmpty, itemsanity, turboOwned;
	if (!gGT) return;
	onTrack = LOAD_IsOpen_RacingOrBattle();
	if (!onTrack || (gGT->gameMode1 & END_OF_RACE)) g_grantCountdownSeen = 0;
	else if (gGT->trafficLightsTimer >= 1) g_grantCountdownSeen = 1;
	driver = gGT->drivers[0];
	if (g_turboGrant.inFlight)
	{
		int held = driver && driver->heldItemID == AP_WEAPON_TURBO;
		if (AP_GrantObserveSlot(&g_turboGrant, held))
			AP_LogLine("[AP GRANT] Turbo lost before fire; requeued\n");
	}
	if (!driver) return;
	slotEmpty = driver->heldItemID == AP_WEAPON_NONE && !driver->noItemTimer &&
	            !driver->itemRollTimer;
	itemsanity = ctr_cfg_active() && ap_net_location_exists(35016000L);
	turboOwned = AP_ItemsanityOwnsWeapon(AP_WEAPON_TURBO);
	if (!AP_GrantCanDeliver(&g_turboGrant, AP_TurboGrantRaceActive(gGT),
	                        slotEmpty, itemsanity, turboOwned, 0)) return;
	if (!AP_GrantMarkDelivered(&g_turboGrant)) return;
	driver->heldItemID = AP_WEAPON_TURBO;
	driver->numHeldItems = 0;
	driver->noItemTimer = 0;
	driver->itemRollTimer = 0;
	OtherFX_Play(0x5e, 0);
	AP_LogLine("[AP GRANT] Turbo delivered to held-item slot\n");
}

void AP_TurboGrantOnWeaponFire(struct Driver *driver, int heldItemID)
{
	struct GameTracker *gGT = sdata->gGT;
	if (!gGT || driver != gGT->drivers[0] || heldItemID != AP_WEAPON_TURBO) return;
	if (!AP_GrantMarkFired(&g_turboGrant)) return;
	g_turboPersistedFired++;
	AP_GrantStore();
	AP_LogLine("[AP GRANT] Turbo fired and consumed\n");
}

#endif
