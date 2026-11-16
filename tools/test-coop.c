// cc -m32 -Wall -Wextra -DCTR_AP -DCTR_NATIVE -DBUILD=926 -I ap -I . -I include
//    -o /tmp/test-coop tools/test-coop.c -lm
// Compile the production option, ownership, load and result code with engine
// and transport stubs. No rendering or network is involved.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <common.h>
#include "ap/ap_seedcfg.h"
struct sData sdata_static;
struct Data data;
NativeConfig g_config;
ctr_seed_config ctr_cfg;
static int active = 1, packedValue, known, writes;
static unsigned revision;
static unsigned unlocked = 1u;
static char notice[64];
int ctr_cfg_active(void) { return active; }
int AP_CharacterUnlocked(int racer) { return (unlocked >> racer) & 1u; }
void AP_CharSwap_ApplyStatPackage(struct Driver *driver) { (void)driver; }
void AP_FeedNoticeLine(const char *text) { snprintf(notice, sizeof notice, "%s", text); }
void ap_net_coop_set(int packed) { packedValue = packed; writes++; known = 1; }
int ap_net_coop_known(int *out) { *out = packedValue; return known; }
unsigned ap_net_coop_revision(void) { return revision; }
#include "../ap/ap_coop.c"

int main(void)
{
	static struct GameTracker gt;
	static struct GamepadSystem pads;
	static struct Driver drivers[3];
	sdata->gGT = &gt;
	sdata->gGamepads = &pads;
	sdata->Loading.stage = LOAD_IDLE;
	gt.drivers[0] = &drivers[0];
	gt.drivers[1] = &drivers[1];
	gt.drivers[2] = &drivers[2];
	ctr_cfg.character_phase_present = 1;
	gt.gameMode1 = ADVENTURE_MODE | ADVENTURE_ARENA;
	pads.numGamepadsConnected = 1;
	AP_CoopStep(1);
	assert(!g_config.coopAdventure && !writes);
	pads.numGamepadsConnected = 2;
	pads.gamepadsConnectedByFlag = 5; /* slots 1 and 3 are insufficient */
	AP_CoopStep(1);
	assert(!g_config.coopAdventure && !writes);
	pads.gamepadsConnectedByFlag = 3;
	AP_CoopStep(1);
	assert(g_config.coopAdventure && writes == 1);
	gt.gameMode1 |= ADVENTURE_CUP;
	AP_CoopStep(-1);
	assert(g_config.coopAdventure && writes == 1);
	gt.gameMode1 = ADVENTURE_MODE;
	AP_CoopStep(-1);
	assert(g_config.coopAdventure && writes == 1);
	gt.gameMode1 |= ADVENTURE_ARENA;
	sdata->Loading.stage = 0;
	AP_CoopStep(-1);
	assert(g_config.coopAdventure && writes == 1);
	sdata->Loading.stage = LOAD_IDLE;
	unlocked |= 1u << 10;
	AP_CoopRacerStep(1);
	assert(g_config.coopRacer == 10);
	AP_CoopRacerStep(-1);
	assert(g_config.coopRacer == 0);
	AP_CoopRacerStep(-1);
	assert(g_config.coopRacer == 10);
	AP_CoopPrepareLoad(&gt);
	assert(gt.numPlyrCurrGame == 1); /* hub remains solo */
	gt.levelID = CRASH_COVE;
	gt.gameMode1 = ADVENTURE_MODE;
	AP_CoopPrepareLoad(&gt);
	assert(gt.numPlyrCurrGame == 2 && gt.numPlyrNextGame == 1);
	assert(data.characterIDs[1] == 10);
	unsigned seats = 0;
	for (int i = 0; i < 6; i++) seats |= 1u << sdata->kartSpawnOrderArray[i];
	assert(seats == 63);
	assert(AP_IsSlotHuman(&gt, &drivers[0]) && AP_IsSlotHuman(&gt, &drivers[1]));
	assert(!AP_IsSlotHuman(&gt, &drivers[2]) && !AP_IsSlotHuman(&gt, NULL));
	assert(!AP_CoopFinishReady(1, 2, 1));
	assert(AP_CoopFinishReady(1, 2, 2));
	assert(AP_CoopFinishReady(0, 2, 1)); /* Versus keeps its existing early finish */
	for (int a = -1; a < 8; a++) for (int b = -1; b < 8; b++)
	{
		drivers[0].driverRank = a; drivers[1].driverRank = b;
		assert(AP_CoopWon(&gt) == (a == 0 || b == 0));
		assert(AP_CoopRank(&gt, 0) == AP_CoopBetterRank(a, b));
	}
	drivers[0].driverRank = 5; drivers[1].driverRank = 0;
	drivers[1].actionsFlagSet = ACTION_RACE_FINISHED | ACTION_BOT;
	assert(AP_CoopRank(&gt, 1) == 0 && AP_CoopWon(&gt));
	assert(!AP_IsSlotHuman(&gt, &drivers[1])); /* finished bot cannot fire checks */
	drivers[1].actionsFlagSet = 0;
	gt.gameMode1 = ADVENTURE_MODE | MAIN_MENU;
	gt.numPlyrCurrGame = 4; gt.numPlyrNextGame = 2;
	AP_CoopPrepareLoad(&gt);
	assert(gt.numPlyrCurrGame == 4 && gt.numPlyrNextGame == 2);
	gt.gameMode1 = ADVENTURE_MODE;
	gt.numPlyrCurrGame = 1; gt.numPlyrNextGame = 1;
	const unsigned unsupported[] = {ADVENTURE_CUP, ADVENTURE_BOSS, RELIC_RACE,
	    CRYSTAL_CHALLENGE, ARCADE_MODE, TIME_TRIAL, BATTLE_MODE, MAIN_MENU, GAME_CUTSCENE};
	for (unsigned i = 0; i < sizeof unsupported / sizeof unsupported[0]; i++)
	{
		gt.gameMode1 = ADVENTURE_MODE | unsupported[i];
		AP_CoopPrepareLoad(&gt);
		assert(gt.numPlyrCurrGame == 1 && !AP_CoopRace(&gt));
	}
	gt.gameMode1 = ADVENTURE_MODE;
	gt.gameMode2 = TOKEN_RACE;
	AP_CoopPrepareLoad(&gt);
	assert(gt.numPlyrCurrGame == 1);
	assert(strstr(notice, "SITS THIS RACE OUT"));
	gt.gameMode2 = 0;
	pads.numGamepadsConnected = 1;
	pads.gamepadsConnectedByFlag = 1;
	AP_CoopPrepareLoad(&gt);
	assert(gt.numPlyrCurrGame == 1);
	assert(strstr(notice, "DISCONNECTED"));
	gt.gameMode1 |= ADVENTURE_ARENA;
	AP_CoopStep(-1); /* solo remains available after P2 disconnects */
	assert(!g_config.coopAdventure);
	AP_CoopConnectReset();
	packedValue = 16 + 12; known = 1; revision++;
	AP_CoopTick(&gt);
	assert(g_config.coopRacer != 12); /* delayed unlock remains pending */
	unlocked |= 1u << 12;
	AP_CoopTick(&gt);
	assert(g_config.coopRacer == 12 && g_config.coopAdventure);
	puts("PASS co-op production: option refusals, unlocked racer, solo fallback, ownership, 81 rank pairs, finished-bot result, restore");
	return 0;
}
