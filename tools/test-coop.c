// cc -m32 -Wall -Wextra -DCTR_AP -DCTR_NATIVE -DCTR_CUSTOM_TRACKS -DBUILD=926 -ffunction-sections -fdata-sections -Wl,--gc-sections -I ap -I . -I include
//    -o /tmp/test-coop tools/test-coop.c -lm
// Compile the production option, ownership, load and result code with engine
// and transport stubs. No rendering or network is involved.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <common.h>
#include "platform/native_custom_race_context.h"
#include "ap/ap_seedcfg.h"
#include "platform/native_controls.h"
struct sData sdata_static;
struct Data data;
NativeConfig g_config;
ctr_seed_config ctr_cfg;
static int active = 1, packedValue, known, writes;
static unsigned revision;
static char notice[64];
static int customServing, cortex, haveContext, queriedPlayers;
static struct CustomTrackRaceContext raceContext;
static u64 p2Device = 101;
static int p2Pref = NATIVE_CONTROLS_PREF_AUTO;
static int transportReady = 1;
static s16 requestedLevel = -1;
u64 NativeControls_GetPlayerDeviceIdentity(int player) { return player == 1 ? p2Device : 100; }
int NativeControls_GetPlayerPref(int player) { return player == 1 ? p2Pref : NATIVE_CONTROLS_PREF_AUTO; }
void MainRaceTrack_RequestLoad(s16 level) { requestedLevel = level; }
int CustomTrack_ServingLoad(int level, int cup, int cupID)
{
	(void)level; (void)cup; (void)cupID;
	queriedPlayers = sdata->gGT->numPlyrCurrGame;
	/* The current custom predicate admits only one player. */
	return customServing && queriedPlayers == 1;
}
int AP_CortexTrackActive(void) { return cortex; }
const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int level)
{ (void)level; return haveContext ? &raceContext : NULL; }
int ctr_cfg_active(void) { return active; }
int AP_RaceAttemptIsForcedLoss(void) { return 0; }
void AP_LogLine(const char *line) { (void)line; }
void AP_CharSwap_ApplyStatPackage(struct Driver *driver) { (void)driver; }
void AP_FeedNoticeLine(const char *text) { snprintf(notice, sizeof notice, "%s", text); }
int ap_net_coop_set(int packed)
{
	if (!transportReady) return 0;
	packedValue = packed;
	writes++;
	known = 1;
	return 1;
}
int ap_net_coop_known(int *out) { *out = packedValue; return known; }
unsigned ap_net_coop_revision(void) { return revision; }
#include "../ap/ap_capability.c"
#include "../ap/ap_coop.c"

static void unlockRacer(int racer)
{
	for (int slot = 0; slot < 16; slot++)
		if (AP_CapabilityRosterCharacter(slot) == racer)
		{
			AP_CharacterReceive(slot);
			return;
		}
	assert(0);
}

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
	ctr_cfg.character_unlocks = 1;
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
	unlockRacer(10);
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
	struct ControllerPacket p2Pad = {.plugged = PLUGGED};
	pads.gamepad[1].ptrControllerPacket = &p2Pad;
	assert(AP_CoopP2Available() && !AP_CoopReloadOnRetry(&gt));
	AP_CoopConnectReset(); /* Same-room socket reconnect during the race. */
	p2Pref = NATIVE_CONTROLS_PREF_PINNED;
	p2Device = 102; /* A replug reclaims the pin with a new SDL ID. */
	assert(AP_CoopP2Available() && !AP_CoopReloadOnRetry(&gt));
	p2Pref = NATIVE_CONTROLS_PREF_AUTO;
	p2Device = UINT64_MAX; /* Auto puts the keyboard in the vacated slot. */
	gt.prevLEV = N_SANITY_BEACH;
	assert(!AP_CoopP2Available() && AP_CoopReloadOnRetry(&gt));
	assert(requestedLevel == CRASH_COVE);
	AP_CoopConnectReset(); /* The same reconnect after requesting the full load. */
	gt.prevLEV = CRASH_COVE; /* Full load records the last level. */
	AP_CoopPrepareLoad(&gt);
	assert(gt.numPlyrCurrGame == 1 && gt.prevLEV == N_SANITY_BEACH);
	gt.gameMode1 = ADVENTURE_MODE | ADVENTURE_ARENA;
	packedValue = 16 + 10;
	revision++;
	AP_CoopTick(&gt);
	assert(g_config.coopAdventure && g_config.coopRacer == 10);
	gt.gameMode1 = ADVENTURE_MODE;
	p2Device = 102;
	AP_CoopPrepareLoad(&gt);
	assert(gt.numPlyrCurrGame == 2 && AP_CoopP2Available());
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
	gt.numPlyrCurrGame = 1;
	customServing = 1;
	AP_CoopPrepareLoad(&gt);
	assert(queriedPlayers == 1 && gt.numPlyrCurrGame == 1);
	assert(g_config.coopAdventure && strstr(notice, "SITS THIS RACE OUT"));
	/* Retained custom identity must win even if the one-player serving
	 * predicate is false while returning from a two-human race. */
	gt.numPlyrCurrGame = 2; haveContext = 1; raceContext.retail = 0;
	AP_CoopPrepareLoad(&gt);
	assert(queriedPlayers == 2 && gt.numPlyrCurrGame == 1);
	customServing = 0; raceContext.retail = 1;
	AP_CoopPrepareLoad(&gt);
	assert(gt.numPlyrCurrGame == 2); /* retail content-plan identity */
	haveContext = 0; cortex = 1;
	AP_CoopPrepareLoad(&gt);
	assert(gt.numPlyrCurrGame == 1 && g_config.coopAdventure);
	cortex = 0;
	AP_CoopPrepareLoad(&gt);
	assert(gt.numPlyrCurrGame == 2); /* supported retail return */
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
	unlockRacer(12);
	AP_CoopTick(&gt);
	assert(g_config.coopRacer == 12 && g_config.coopAdventure);
	/* The new P2 picker uses the actual unlock predicate even on legacy
	 * seeds. P1's wider legacy browse UI is deliberately left unchanged. */
	ctr_cfg.character_phase_present = 0;
	data.characterIDs[0] = 0;
	pads.gamepadsConnectedByFlag = 3;
	g_config.coopRacer = 7;
	AP_CoopRacerStep(1);
	assert(g_config.coopRacer == 0);
	data.characterIDs[0] = 10;
	g_config.coopRacer = 7;
	AP_CoopRacerStep(1);
	assert(g_config.coopRacer == 10); /* the currently driven P1 guest is available */
	g_config.coopRacer = 12;
	gt.gameMode1 = ADVENTURE_MODE;
	AP_CoopPrepareLoad(&gt);
	assert(gt.numPlyrCurrGame == 1 && strstr(notice, "RACER LOCKED"));
	/* Modern all-unlocked comfort mode still offers all sixteen. */
	ctr_cfg.character_phase_present = 1;
	ctr_cfg.character_unlocks = 0;
	gt.gameMode1 |= ADVENTURE_ARENA;
	g_config.coopRacer = 7;
	AP_CoopRacerStep(1);
	assert(g_config.coopRacer == 8);
	g_config.coopRacer = 12;
	gt.gameMode1 = ADVENTURE_MODE;
	AP_CoopPrepareLoad(&gt);
	assert(gt.numPlyrCurrGame == 2);
	puts("PASS co-op production: option refusals, unlocked racer, custom identity and solo fallback, ownership, 81 rank pairs, finished-bot result, restore");
	return 0;
}
