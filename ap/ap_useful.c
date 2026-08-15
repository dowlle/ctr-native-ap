#ifdef CTR_AP

#include <common.h>
#include <stdio.h>
#include <string.h>

#include "ap_hooks.h"
#include "ap_net.h"
#include "ap_useful.h"
#include "ap_useful_logic.h"

#define AP_USEFUL_FILE "ctr-ap-useful.txt"
#define AP_USEFUL_ROW_CAP 32

static unsigned g_usefulReceived;
static unsigned g_usefulApplied;
static char g_usefulSeed[128];
static char g_usefulSlot[64];
static int g_usefulCountdownSeen;

static void AP_UsefulLoad(void)
{
	FILE *f;
	char line[320];
	g_usefulReceived = 0;
	g_usefulApplied = 0;
	g_usefulSeed[0] = '\0';
	g_usefulSlot[0] = '\0';
	(void)ap_net_seed_name(g_usefulSeed, sizeof g_usefulSeed);
	(void)ap_net_slot_name(g_usefulSlot, sizeof g_usefulSlot);
	f = fopen(AP_USEFUL_FILE, "r");
	if (f == 0) return;
	while (fgets(line, sizeof line, f))
	{
		char seed[128], slot[64];
		unsigned applied;
		if (sscanf(line, "%127[^\t]\t%63[^\t]\t%u", seed, slot, &applied) == 3 &&
		    !strcmp(seed, g_usefulSeed) && !strcmp(slot, g_usefulSlot))
		{
			g_usefulApplied = applied & 7u;
			break;
		}
	}
	fclose(f);
}

static void AP_UsefulStore(void)
{
	FILE *f;
	char rows[AP_USEFUL_ROW_CAP][320];
	int count = 0, i;
	if (!g_usefulSeed[0] || !g_usefulSlot[0]) return;
	f = fopen(AP_USEFUL_FILE, "r");
	if (f)
	{
		while (count < AP_USEFUL_ROW_CAP - 1 &&
		       fgets(rows[count], sizeof rows[0], f))
		{
			char seed[128], slot[64];
			unsigned applied;
			if (sscanf(rows[count], "%127[^\t]\t%63[^\t]\t%u", seed, slot, &applied) != 3 ||
			    strcmp(seed, g_usefulSeed) || strcmp(slot, g_usefulSlot)) count++;
		}
		fclose(f);
	}
	f = fopen(AP_USEFUL_FILE, "w");
	if (!f) return;
	for (i = 0; i < count; i++) fputs(rows[i], f);
	fprintf(f, "%s\t%s\t%u\n", g_usefulSeed, g_usefulSlot, g_usefulApplied);
	fclose(f);
}

void AP_UsefulConnectReset(void)
{
	AP_UsefulLoad();
	g_usefulCountdownSeen = 0;
}

void AP_UsefulReceive(int effect)
{
	g_usefulReceived = AP_UsefulReceiveBit(g_usefulReceived, effect);
}

static int AP_UsefulRaceActive(struct GameTracker *gGT)
{
	int forbidden = START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE |
	                PAUSE_ALL;
	return LOAD_IsOpen_RacingOrBattle() && g_usefulCountdownSeen &&
	       gGT->trafficLightsTimer < 1 && !(gGT->gameMode1 & forbidden);
}

void AP_UsefulTick(struct GameTracker *gGT)
{
	static const int weaponIDs[AP_USEFUL_ITEM_COUNT] = {6, 7, 0xc};
	static const char *names[AP_USEFUL_ITEM_COUNT] = {
		"Passive Shield", "Invincibility Mask", "Invisibility"
	};
	struct Driver *driver;
	unsigned pending;
	int effect;
	char msg[96];
	if (!gGT) return;
	if (!LOAD_IsOpen_RacingOrBattle() || (gGT->gameMode1 & END_OF_RACE))
		g_usefulCountdownSeen = 0;
	else if (gGT->trafficLightsTimer >= 1)
		g_usefulCountdownSeen = 1;
	if (!AP_UsefulRaceActive(gGT)) return;
	driver = gGT->drivers[0];
	if (!driver) return;
	pending = AP_UsefulPendingMask(g_usefulReceived, g_usefulApplied);
	for (effect = 0; effect < AP_USEFUL_ITEM_COUNT; effect++)
	{
		unsigned bit = 1u << effect;
		if (!(pending & bit)) continue;
		VehPickupItem_ShootNow(driver, weaponIDs[effect], 0);
		g_usefulApplied |= bit;
		AP_UsefulStore();
		snprintf(msg, sizeof msg, "[AP USEFUL] applied %s\n", names[effect]);
		AP_LogLine(msg);
	}
}

#endif
