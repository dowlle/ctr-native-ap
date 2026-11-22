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
static u16 physicalButtons[3];
static u64 assignedPartner;
static int feedOn = 1;
static int hubSafe = 1;
static int transportReady = 1;
static s16 requestedLevel = -1;
static int gridSize = 8;
static int pendingPadRemoval;
int LOAD_CoopGridSize(void) { return gridSize; }
u64 NativeControls_GetPlayerDeviceIdentity(int player) { return player == 1 ? p2Device : 100; }
u64 NativeControls_GetGamepadIdentity(int device) { return device < 3 ? 100 + device : 0; }
u16 NativeInput_AdventureButtons(int device) { return physicalButtons[device]; }
int NativeControls_SetAdventurePartner(u64 id) { assignedPartner = id; return 1; }
void NativeControls_ClearAdventureSeats(void) { assignedPartner = 0; }
int Platform_PollInput(void)
{
	if (pendingPadRemoval) p2Device = 0;
	pendingPadRemoval = 0;
	return 1;
}
void VehStuckProc_Warp_Init(struct Thread *t, struct Driver *d) { (void)t; (void)d; }
int AP_HubSafeForRequest(struct GameTracker *gt) { (void)gt; return hubSafe; }
int AP_CharSwap_PickerOpen(void) { return 0; }
void AP_HubPickerFreeze(struct GameTracker *gt, int on)
{
	if (on)
		gt->gameMode2 |= VEH_FREEZE_DOOR;
	else
		gt->gameMode2 &= ~VEH_FREEZE_DOOR;
}
int AP_HubFeedOn(void) { return feedOn; }
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

static void tapPartner(struct GameTracker *gt, int device, u16 buttons)
{
	physicalButtons[device] = buttons;
	AP_CoopMenuTick(gt);
	physicalButtons[device] = 0;
	AP_CoopMenuTick(gt);
}

static void testHubMenu(struct GameTracker *gt)
{
	gt->gameMode1 = ADVENTURE_MODE | ADVENTURE_ARENA;
	g_config.coopAdventure = 0;
	g_config.coopRacer = -1;
	ap_coop_joined_device = 0;
	p2Device = 101;
	assignedPartner = 0;
	ap_coop_notice_frames = 0;
	feedOn = 0;
	AP_CoopMenuTick(gt);
	assert(!ap_coop_notice_frames);
	feedOn = 1;
	AP_CoopMenuTick(gt);
	assert(!strcmp(ap_coop_notice, "P2: PRESS START TO JOIN"));
	ap_coop_notice_frames = 0;
	AP_CoopMenuTick(gt);
	assert(!ap_coop_notice_frames);
	feedOn = 0;

	tapPartner(gt, 0, RAW_BTN_START);
	assert(ap_coop_menu == COOP_MENU_CLOSED);
	int before = writes;
	tapPartner(gt, 2, RAW_BTN_START);
	assert(ap_coop_menu == COOP_MENU_CLOSED && !(gt->gameMode2 & VEH_FREEZE_DOOR));
	assert(writes == before + 1 && g_config.coopAdventure && assignedPartner == 102 && packedValue == 0);
	assert(strstr(ap_coop_notice, "P2 JOINED:"));
	p2Device = assignedPartner; /* The input assignment has been published. */

	struct Driver *driver = gt->drivers[0];
	driver->funcPtrs[DRIVER_FUNC_INIT] = VehStuckProc_Warp_Init;
	tapPartner(gt, 2, RAW_BTN_START);
	assert(ap_coop_menu == COOP_MENU_CLOSED);
	assert(driver->funcPtrs[DRIVER_FUNC_INIT] == VehStuckProc_Warp_Init);
	driver->funcPtrs[DRIVER_FUNC_INIT] = NULL;
	driver->kartState = KS_WARP_PAD;
	tapPartner(gt, 2, RAW_BTN_START);
	assert(ap_coop_menu == COOP_MENU_CLOSED);
	driver->kartState = KS_NORMAL;

	tapPartner(gt, 1, RAW_BTN_START);
	assert(ap_coop_menu == COOP_MENU_CLOSED);
	tapPartner(gt, 2, RAW_BTN_START);
	assert(ap_coop_menu == COOP_MENU_ACTIONS);
	driver->funcPtrs[DRIVER_FUNC_INIT] = VehStuckProc_Warp_Init;
	AP_CoopMenuTick(gt);
	assert(ap_coop_menu == COOP_MENU_CLOSED && !(gt->gameMode2 & VEH_FREEZE_DOOR));
	assert(driver->funcPtrs[DRIVER_FUNC_INIT] == VehStuckProc_Warp_Init);
	driver->funcPtrs[DRIVER_FUNC_INIT] = NULL;
	tapPartner(gt, 2, RAW_BTN_START);
	tapPartner(gt, 1, RAW_BTN_CROSS);
	assert(ap_coop_menu == COOP_MENU_ACTIONS);
	tapPartner(gt, 2, RAW_BTN_UP);
	tapPartner(gt, 2, RAW_BTN_CROSS);
	assert(ap_coop_menu == COOP_MENU_CLOSED && g_config.coopAdventure);
	tapPartner(gt, 2, RAW_BTN_START);
	tapPartner(gt, 2, RAW_BTN_CROSS);
	assert(ap_coop_menu == COOP_MENU_RACER);
	tapPartner(gt, 2, RAW_BTN_RIGHT);
	tapPartner(gt, 2, RAW_BTN_CROSS);
	assert(g_config.coopAdventure && g_config.coopRacer == 1 && packedValue == 1);
	tapPartner(gt, 2, RAW_BTN_START);
	tapPartner(gt, 2, RAW_BTN_DOWN);
	tapPartner(gt, 2, RAW_BTN_CROSS);
	assert(!g_config.coopAdventure && !assignedPartner && packedValue == 1);
	assert(!strcmp(ap_coop_notice, "P2 DROPPED OUT"));
	tapPartner(gt, 2, RAW_BTN_START);
	assert(g_config.coopAdventure && g_config.coopRacer == 1 && packedValue == 1);
	p2Device = 0; /* Missing pin: a different pad can join without using the picker. */
	tapPartner(gt, 1, RAW_BTN_START);
	assert(assignedPartner == 101 && ap_coop_menu == COOP_MENU_CLOSED);

	before = writes;
	gt->gameMode1 = ADVENTURE_MODE;
	tapPartner(gt, 2, RAW_BTN_START);
	assert(strstr(notice, "IN THE HUB"));
	gt->gameMode1 |= ADVENTURE_CUP;
	tapPartner(gt, 2, RAW_BTN_START);
	assert(strstr(notice, "CUP"));
	gt->gameMode1 = ADVENTURE_MODE | ADVENTURE_ARENA;
	sdata->Loading.stage = LOAD_RESTART;
	tapPartner(gt, 2, RAW_BTN_START);
	assert(strstr(notice, "LOADING") && writes == before && ap_coop_menu == COOP_MENU_CLOSED);
	sdata->Loading.stage = LOAD_IDLE;
}

static void testDelayedPreference(struct GameTracker *gt)
{
	gt->gameMode1 = MAIN_MENU;
	AP_CoopConnectReset();
	gt->gameMode1 = ADVENTURE_MODE | ADVENTURE_ARENA;
	int before = writes;
	tapPartner(gt, 2, RAW_BTN_START);
	assert(g_config.coopAdventure && assignedPartner == 102 && writes == before);
	AP_CoopRulesStep(0);
	assert(!g_config.coopRules && writes == before);
	/* The AP poll delivered the saved value after the frame's co-op tick. */
	packedValue = 32 | 10;
	known = 1;
	revision++;
	AP_CoopRulesStep(0);
	assert(g_config.coopRacer == 10 && !g_config.coopRules && packedValue == 10);
	assert(writes == before + 1);
	g_config.coopAdventure = 0;
	AP_CoopConnectReset();
	assert(g_config.coopRacer == 10); /* Same-room reconnect while P2 is out. */

	gt->gameMode1 = MAIN_MENU;
	AP_CoopConnectReset();
	gt->gameMode1 = ADVENTURE_MODE | ADVENTURE_ARENA;
	before = writes;
	tapPartner(gt, 2, RAW_BTN_START);
	int racer = g_config.coopRacer;
	assert(g_config.coopAdventure && racer >= 0 && writes == before);
	packedValue = 64;
	revision++;
	AP_CoopMenuTick(gt);
	assert(g_config.coopRacer == racer && packedValue == racer && writes == before + 1);
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
	pads.gamepadsConnectedByFlag = 3;
	unlockRacer(10);
	AP_CoopMenuTick(&gt);
	tapPartner(&gt, 1, RAW_BTN_START);
	tapPartner(&gt, 1, RAW_BTN_START);
	tapPartner(&gt, 1, RAW_BTN_CROSS);
	tapPartner(&gt, 1, RAW_BTN_RIGHT);
	tapPartner(&gt, 1, RAW_BTN_CROSS);
	assert(g_config.coopAdventure && g_config.coopRacer == 10);
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
	assert(g_config.coopRacer == 10 && g_config.coopAdventure);
	sdata->Loading.stage = LOAD_RESTART; /* P2 drops after the in-place restart was selected. */
	p2Device = 102; /* A new instance cannot claim a loaded seat. */
	assert(!AP_CoopP2Available());
	p2Device = UINT64_MAX; /* Auto puts the keyboard in the vacated slot. */
	assert(!AP_CoopP2Available());
	p2Device = 101;
	pendingPadRemoval = 1; /* SDL has a removal that the final VBlank did not publish. */
	gt.prevLEV = N_SANITY_BEACH;
	assert(AP_CoopP2Available() && AP_CoopReloadOnRetry(&gt) && !AP_CoopP2Available());
	assert(requestedLevel == CRASH_COVE);
	AP_CoopConnectReset(); /* The same reconnect after requesting the full load. */
	gt.prevLEV = CRASH_COVE; /* Full load records the last level. */
	AP_CoopPrepareLoad(&gt);
	assert(gt.numPlyrCurrGame == 1 && gt.prevLEV == N_SANITY_BEACH);
	sdata->Loading.stage = LOAD_IDLE;
	gt.gameMode1 = ADVENTURE_MODE | ADVENTURE_ARENA;
	packedValue = 16 + 10;
	revision++;
	AP_CoopTick(&gt);
	assert(g_config.coopAdventure && g_config.coopRacer == 10);
	gt.gameMode1 = ADVENTURE_MODE;
	p2Device = 101;
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
	ap_coop_hard_race = 1;
	for (int a = -1; a < 8; a++) for (int b = -1; b < 8; b++)
	{
		drivers[0].driverRank = a; drivers[1].driverRank = b;
		assert(AP_CoopWon(&gt) == ((a == 0 && b == 1) || (a == 1 && b == 0)));
		assert(AP_CoopRank(&gt, 0) == (a < 0 || b < 0 ? -1 : (a > b ? a : b)));
	}
	drivers[0].actionsFlagSet = ACTION_RACE_FINISHED;
	assert(AP_CoopRank(&gt, 1) == -1);
	drivers[0].actionsFlagSet = 0;
	AP_CoopPrepareGrid(&gt);
	assert(sdata->kartSpawnOrderArray[0] == 7 && sdata->kartSpawnOrderArray[1] == 6 &&
	       sdata->kartSpawnOrderArray[7] == 5);
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
	AP_CoopConnectReset();
	packedValue = 32 + 12; known = 1; revision++;
	AP_CoopTick(&gt);
	assert(g_config.coopRacer == 12 && g_config.coopRules && !AP_CoopRacerUnlocked(12));
	unlockRacer(12);
	assert(AP_CoopRacerUnlocked(12));
	/* Legacy seeds offer the same full portrait roster as P1. */
	ctr_cfg.character_phase_present = 0;
	assert(AP_CoopNextRacer(7, 1) == 8 && AP_CoopNextRacer(15, 1) == 0);
	ctr_cfg.character_phase_present = 1;
	ctr_cfg.character_unlocks = 0;
	assert(AP_CoopNextRacer(7, 1) == 8);
	g_config.coopRules = 0;
	testHubMenu(&gt);
	testDelayedPreference(&gt);
	puts("PASS co-op production: seating, 162 rank pairs, Hard podium, Retry, storage and grid");
	puts("PASS P2 hub controls: notice, owner, immediate join, Back, racer change, drop out, race/cup/load refusal");
	return 0;
}
