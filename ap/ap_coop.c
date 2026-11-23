#ifdef CTR_AP
#include <common.h>
#include "ap_coop.h"
#include "ap_coop_logic.h"
#include "ap_capability.h"
#include "ap_charswap.h"
#include "ap_hooks.h"
#include "ap_hit_encounter.h"
#include "ap_net.h"
#include "ap_tracker.h"
#include <platform/native_controls.h>

static const char *ap_coop_help = "Change in the Adventure hub.";
static unsigned ap_coop_restored;
static int ap_coop_restore_pending;
static u64 ap_coop_joined_device;
static u64 ap_coop_p2_device;
static int ap_coop_hard_race;
static int ap_coop_warp_rules = -1;
static int ap_coop_entry_rules = -1;
static int ap_coop_retry_level = -1;
static int ap_coop_retry_hub;
static int ap_coop_retry_capacity;
static int ap_coop_reload_hub = -1;

enum AP_CoopMenu { COOP_MENU_CLOSED, COOP_MENU_ACTIONS, COOP_MENU_RACER };
enum AP_CoopAction { COOP_CHANGE_RACER, COOP_DROP_OUT, COOP_BACK, COOP_ACTION_COUNT };
static enum AP_CoopMenu ap_coop_menu;
static u64 ap_coop_picker_device;
static int ap_coop_selection;
static int ap_coop_hint_hub = -1;
static int ap_coop_notice_frames;
static char ap_coop_notice[64];
static struct { u64 device; u16 held; } ap_coop_buttons[NATIVE_CONTROLS_MAX_DEVICES];

static void AP_CoopLog(const char *event, const char *reason, int level, int players)
{
	char line[160];
	snprintf(line, sizeof line, "[AP COOP] %s reason=%s level=%d players=%d\n",
	         event, reason, level, players);
	AP_LogLine(line);
}

static void AP_CoopReloadHub(struct GameTracker *gt, const char *reason)
{
	AP_CoopLog("hub reload", reason, gt->levelID, g_config.coopAdventure ? 2 : 1);
	AP_HubReloadInPlace(gt);
	ap_coop_reload_hub = gt->levelID;
	RaceFlag_SetFullyOnScreen();
}

void AP_CoopFinishHubReload(const struct GameTracker *gt)
{
	if (gt->levelID == ap_coop_reload_hub && (gt->gameMode1 & ADVENTURE_ARENA))
		sdata->RaceFlag_TransitionSpeed = 512;
	ap_coop_reload_hub = -1;
}

void AP_CoopConnectReset(void)
{
	/* A preference never crosses a room boundary through global config.ini. */
	if (!sdata || !sdata->gGT || (sdata->gGT->gameMode1 & MAIN_MENU) ||
	    !(sdata->gGT->gameMode1 & ADVENTURE_MODE))
	{
		ap_coop_joined_device = 0;
		g_config.coopAdventure = 0;
		NativeControls_ClearAdventureSeats();
		g_config.coopRacer = -1;
		g_config.coopRules = 0;
	}
	ap_coop_restored = ap_net_coop_revision();
	ap_coop_restore_pending = 1;
	// A socket reconnect can happen during a race or a requested retry.
	// The device and return hub belong to the loaded scene.
}

static int AP_CoopPersist(int racer, int hard)
{
	/* Bit 4 was the persisted join state. Seats are now assigned in the hub. */
	int packed = (racer < 0 ? 64 : racer) | (hard ? 32 : 0);
	if (ap_coop_restore_pending || !ap_net_coop_set(packed))
	{
		ap_coop_help = "Reconnect to save.";
		return 0;
	}
	g_config.coopRacer = racer;
	g_config.coopRules = hard;
	return 1;
}

static void AP_CoopNotice(const char *verb, const char *racer)
{
	snprintf(ap_coop_notice, sizeof ap_coop_notice, "%s%s", verb, racer);
	ap_coop_notice_frames = 120;
}

static int AP_CoopControllers(void)
{
	if (!sdata || !sdata->gGamepads) return 0;
	/* numGamepadsConnected is the highest occupied slot, not a population
	 * count. A controller in slot 3 cannot substitute for missing slot 2. */
	u64 partner = NativeControls_GetPlayerDeviceIdentity(1);
	return partner && partner == ap_coop_joined_device &&
	       (sdata->gGamepads->gamepadsConnectedByFlag & 3u) == 3u ? 2 : 1;
}

int AP_CoopP2Available(void)
{
	if (!sdata->gGamepads)
		return 0;
	const struct ControllerPacket *packet = sdata->gGamepads->gamepad[1].ptrControllerPacket;
	if (!packet || packet->plugged != PLUGGED)
		return 0;
	const u64 device = NativeControls_GetPlayerDeviceIdentity(1);
	if (!ap_coop_p2_device || ap_coop_p2_device == device)
		return 1;
	// A fresh SDL instance cannot prove it is the same physical controller.
	return 0;
}

static void AP_CoopReloadSolo(struct GameTracker *gt, int capacity)
{
	// An in-place restart retains two-player assets and pools.
	ap_coop_retry_level = gt->levelID;
	ap_coop_retry_hub = gt->prevLEV;
	ap_coop_retry_capacity = capacity;
	if (AP_CoopRace(gt))
	{
		// The solo loader uses all eight slots, including entries a
		// four-bot split grid can leave stale.
		sdata->kartSpawnOrderArray[0] = 7;
		for (int i = 1; i < 8; i++)
			sdata->kartSpawnOrderArray[i] = i - 1;
	}
	MainRaceTrack_RequestLoad(gt->levelID);
}

int AP_CoopReloadOnRetry(struct GameTracker *gt)
{
	if (!AP_CoopDriving(gt))
		return 0;
	// A removal can be queued after the last VBlank and before this commit.
	// Resolve SDL assignments again before keeping the loaded seats.
	Platform_PollInput();
	if (AP_CoopP2Available())
		return 0;
	AP_CoopLog("drop out", "controller disconnected", gt->levelID, 1);
	AP_CoopReloadSolo(gt, 0);
	return 1;
}

int AP_CoopHubAvailable(const struct GameTracker *gt)
{
	return gt && ctr_cfg_active() && (gt->gameMode1 & ADVENTURE_ARENA) &&
	       (gt->gameMode1 & ADVENTURE_MODE) &&
	       !(gt->gameMode1 & (MAIN_MENU | GAME_CUTSCENE));
}

static int AP_CoopRacerUnlocked(int racer)
{
	return racer >= 0 && racer < 16 &&
	       (!ctr_cfg.character_phase_present || AP_CharacterUnlocked(racer));
}

static int AP_CoopNextRacer(int racer, int direction)
{
	for (int i = 1; i <= 16; i++)
	{
		int next = (racer + direction * i + 32) % 16;
		if (AP_CoopRacerUnlocked(next))
			return next;
	}
	return -1;
}

static void AP_CoopRestorePreference(void)
{
	int packed;
	if (ap_net_coop_revision() == ap_coop_restored || !ap_net_coop_known(&packed)) return;
	ap_coop_restored = ap_net_coop_revision();
	ap_coop_restore_pending = 0;
	const int saveLost = ap_net_coop_save_lost();
	if (saveLost)
		AP_CoopNotice("CO-OP SETTINGS NOT SAVED", "");
	g_config.coopRules = (packed & 32) != 0;
	if (!(packed & 64))
		g_config.coopRacer = packed & 15;
	else if (!g_config.coopAdventure)
		g_config.coopRacer = -1;
	else if (saveLost)
		g_config.coopRacer = AP_CoopNextRacer(-1, 1);
	else if (AP_CoopRacerUnlocked(g_config.coopRacer))
		AP_CoopPersist(g_config.coopRacer, g_config.coopRules);
}

void AP_CoopCapturePadRules(void)
{
	AP_CoopRestorePreference();
	ap_coop_warp_rules = ap_coop_restore_pending || ap_net_coop_save_pending() ?
	                    -1 : g_config.coopRules != 0;
}

static int AP_CoopSpecialTrophyRoster(int level)
{
	return AP_HitEncounterShouldApply(1, level, 0, 0, 0, 0, 0, 0, 1);
}

int AP_CoopCrystalSupported(int level)
{
	// The retail-memory AP build cannot admit a second Adventure controller.
#ifdef CTR_CUSTOM_TRACKS
	return level >= NITRO_COURT && level <= LAB_BASEMENT;
#else
	(void)level;
	return 0;
#endif
}

int AP_CoopPadReady(int level, int crystal)
{
	if (!g_config.coopAdventure || AP_CoopControllers() != 2 ||
	    (crystal ? !AP_CoopCrystalSupported(level) : AP_CoopSpecialTrophyRoster(level)))
		return 1;
	if (ap_coop_warp_rules < 0)
		AP_CoopCapturePadRules();
	if (ap_coop_warp_rules >= 0)
		return 1;
	AP_CoopLog("refused", "co-op settings syncing", sdata->gGT->levelID,
	           sdata->gGT->numPlyrCurrGame);
	return 0;
}

void AP_CoopConfirmPad(void)
{
	ap_coop_entry_rules = ap_coop_warp_rules;
}

int AP_CoopHubWarpDriver(const struct GameTracker *gt)
{
	const int players = AP_CoopHub(gt) ? 2 : 1;
	for (int i = 0; i < players; i++)
	{
		const struct Driver *driver = gt->drivers[i];
		if (driver && (driver->kartState == KS_WARP_PAD ||
		    driver->funcPtrs[DRIVER_FUNC_INIT] == VehStuckProc_Warp_Init))
			return i;
	}
	return -1;
}

static int AP_CoopWarpOwnsDriver(const struct GameTracker *gt)
{
	return AP_CoopHubWarpDriver(gt) >= 0;
}

int AP_CoopHubHeldDriver(const struct GameTracker *gt)
{
	if (!AP_CoopHub(gt))
		return -1;
	int warp = AP_CoopHubWarpDriver(gt);
	if (warp >= 0)
		return 1 - warp;
	const struct Driver *primary = gt->drivers[0];
	if (!LOAD_IsOpen_AdvHub() || AP_CoopHubFadingOut(gt) ||
	    (gt->gameMode2 & (GAME_MODE2_VEH_FREEZE_MASK | SPAWN_AT_BOSS)) ||
	    (gt->cameraDC[0].flags & CAMERA_FLAG_TRANSITION_AWAY) || sdata->AkuAkuHintState ||
	    (primary && (primary->kartState == KS_FREEZE ||
	     primary->funcPtrs[DRIVER_FUNC_INIT] == VehPhysProc_FreezeEndEvent_Init)) ||
	    sdata->ptrActiveMenu || AP_TrackerOpen() || AP_WumpaShopPageOpen() || AP_CoopMenuOpen())
		return 1;
	return -1;
}

static void AP_CoopCloseMenu(struct GameTracker *gt)
{
	if (AP_CoopMenuOpen() && (gt->gameMode1 & ADVENTURE_ARENA) &&
	    !(gt->gameMode1 & LOADING) && sdata->Loading.stage == LOAD_IDLE && !sdata->load_inProgress)
	{
		if (AP_CoopWarpOwnsDriver(gt) || (gt->cameraDC[0].flags & CAMERA_FLAG_TRANSITION_AWAY))
			gt->gameMode2 &= ~VEH_FREEZE_DOOR;
		else if (!sdata->AkuAkuHintState)
			AP_HubPickerFreeze(gt, 0);
	}
	ap_coop_menu = COOP_MENU_CLOSED;
	ap_coop_picker_device = 0;
}

int AP_CoopMenuOpen(void) { return ap_coop_menu != COOP_MENU_CLOSED; }

static void AP_CoopOpenPicker(void)
{
	int racer = g_config.coopRacer;
	ap_coop_selection = AP_CoopRacerUnlocked(racer) ? racer : AP_CoopNextRacer(-1, 1);
	ap_coop_menu = COOP_MENU_RACER;
}

static void AP_CoopMenuInput(struct GameTracker *gt, u16 tap)
{
	if (tap & (RAW_BTN_TRIANGLE | RAW_BTN_START))
	{
		if (ap_coop_menu == COOP_MENU_RACER && !(tap & RAW_BTN_START))
		{
			ap_coop_menu = COOP_MENU_ACTIONS;
			ap_coop_selection = COOP_CHANGE_RACER;
			return;
		}
		AP_CoopCloseMenu(gt);
		return;
	}
	if (ap_coop_menu == COOP_MENU_ACTIONS)
	{
		if (tap & RAW_BTN_UP)
			ap_coop_selection = (ap_coop_selection + COOP_ACTION_COUNT - 1) % COOP_ACTION_COUNT;
		else if (tap & RAW_BTN_DOWN)
			ap_coop_selection = (ap_coop_selection + 1) % COOP_ACTION_COUNT;
		if (!(tap & RAW_BTN_CROSS))
			return;
		if (ap_coop_selection == COOP_CHANGE_RACER)
		{
			AP_CoopOpenPicker();
			return;
		}
		if (ap_coop_selection == COOP_DROP_OUT)
		{
			NativeControls_SetAdventurePartner(0);
			ap_coop_joined_device = 0;
			g_config.coopAdventure = 0;
			AP_CoopNotice("P2 DROPPED OUT", "");
			AP_CoopLog("drop out", "controller menu", gt->levelID, 1);
			AP_CoopCloseMenu(gt);
			AP_CoopReloadHub(gt, "drop out");
			return;
		}
	}
	else
	{
		if (tap & (RAW_BTN_LEFT | RAW_BTN_RIGHT))
			ap_coop_selection = AP_CoopNextRacer(ap_coop_selection, (tap & RAW_BTN_LEFT) ? -1 : 1);
		if (!(tap & RAW_BTN_CROSS) || !AP_CoopRacerUnlocked(ap_coop_selection))
			return;
		if (!AP_CoopPersist(ap_coop_selection, g_config.coopRules))
			AP_FeedNoticeLine("RECONNECT TO SAVE");
		else if (ap_net_coop_save_pending())
			AP_CoopNotice("CO-OP SETTINGS: SAVING", "");
		if (g_config.coopRacer != data.characterIDs[1] && AP_CoopHub(gt))
		{
			AP_CoopCloseMenu(gt);
			AP_CoopReloadHub(gt, "change racer");
			return;
		}
	}
	AP_CoopCloseMenu(gt);
}

void AP_CoopMenuTick(struct GameTracker *gt)
{
	const u64 primary = NativeControls_GetPlayerDeviceIdentity(0);
	const u64 partner = NativeControls_GetPlayerDeviceIdentity(1);
	if (ap_coop_notice_frames > 0)
		ap_coop_notice_frames--;
	u64 start = 0;
	u16 pickerTap = 0;
	int second = 0, pickerConnected = 0;
	for (int device = 0; device < NATIVE_CONTROLS_MAX_DEVICES; device++)
	{
		u64 id = NativeControls_GetGamepadIdentity(device);
		u16 held = id ? NativeInput_AdventureButtons(device) : 0;
		// SDL maintains enumeration order until a hotplug. Treat a device
		// which moved into this index as already held for the first frame.
		u16 tap = ap_coop_buttons[device].device == id ? held & ~ap_coop_buttons[device].held : 0;
		ap_coop_buttons[device].device = id;
		ap_coop_buttons[device].held = held;
		if (!id || id == primary)
			continue;
		second = 1;
		if (id == ap_coop_picker_device)
		{
			pickerConnected = 1;
			pickerTap = tap;
		}
		if (!start && (tap & RAW_BTN_START) &&
		    (!ap_coop_joined_device || partner != ap_coop_joined_device || partner == id))
			start = id;
	}

	if (!gt || !ctr_cfg_active() || !(gt->gameMode1 & ADVENTURE_MODE) ||
	    (gt->gameMode1 & MAIN_MENU))
	{
		if (gt) AP_CoopCloseMenu(gt);
		ap_coop_hint_hub = -1;
		if (gt && (gt->gameMode1 & MAIN_MENU))
		{
			NativeControls_ClearAdventureSeats();
			ap_coop_joined_device = 0;
			g_config.coopAdventure = 0;
		}
		ap_coop_notice_frames = 0;
		return;
	}

	struct GamepadSystem *pads = sdata->gGamepads;
	for (int slot = 1; slot < PLATFORM_INPUT_PAD_COUNT; slot++)
		pads->gamepad[slot].buttonsTapped &= ~BTN_START;
	pads->anyoneTapped = (pads->anyoneTapped & ~BTN_START) | (pads->gamepad[0].buttonsTapped & BTN_START);

	int hub = AP_CoopHubAvailable(gt) && !(gt->gameMode1 & ADVENTURE_CUP);
	int idle = sdata->Loading.stage == LOAD_IDLE && !sdata->load_inProgress &&
	           !(gt->gameMode1 & LOADING);
	if (hub && idle)
		AP_CoopRestorePreference();
	int warp = hub && AP_CoopWarpOwnsDriver(gt);
	if (!hub)
		ap_coop_hint_hub = -1;
	if (ap_coop_menu != COOP_MENU_CLOSED)
	{
		if (!hub || !idle || warp || !pickerConnected || sdata->ptrActiveMenu ||
		    AP_CoopHubFadingOut(gt) ||
		    (gt->cameraDC[0].flags & CAMERA_FLAG_TRANSITION_AWAY) || sdata->AkuAkuHintState ||
		    (gt->gameMode1 & PAUSE_ALL) || (pads->gamepad[0].buttonsTapped & BTN_START))
			AP_CoopCloseMenu(gt);
		else
		{
			AP_HubPickerFreeze(gt, 1);
			AP_CoopMenuInput(gt, pickerTap);
		}
		return;
	}
	if (!hub || !idle)
	{
		if (start)
		{
			AP_CoopLog("refused", !idle ? "loading" :
			           (gt->gameMode1 & ADVENTURE_CUP) ? "cup" : "outside hub",
			           gt->levelID, gt->numPlyrCurrGame);
			AP_FeedNoticeLine(!idle ? "P2: WAIT FOR LOADING" :
			    (gt->gameMode1 & ADVENTURE_CUP) ? "P2: FINISH OR LEAVE THE CUP" : "P2: JOIN OR LEAVE IN THE HUB");
		}
		return;
	}
	if (warp || !AP_HubSafeForRequest(gt) || AP_CharSwap_PickerOpen() ||
	    (pads->gamepad[0].buttonsTapped & BTN_START))
	{
		if (start)
			AP_CoopLog("refused", warp ? "warp" : "hub busy", gt->levelID, gt->numPlyrCurrGame);
		return;
	}
#ifndef CTR_CUSTOM_TRACKS
	if (start)
	{
		AP_CoopLog("refused", "retail memory budget", gt->levelID, 1);
		AP_CoopNotice("SPLIT SCREEN NEEDS THE FULL MEMORY BUILD", "");
	}
	return;
#endif
	if (!start && AP_CoopHub(gt) && AP_CoopRacerUnlocked(g_config.coopRacer) &&
	    g_config.coopRacer != data.characterIDs[1])
	{
		AP_CoopReloadHub(gt, "restored racer");
		return;
	}
	if (!start && ap_coop_hint_hub != gt->levelID && second &&
	    !ap_coop_joined_device && AP_HubFeedOn())
	{
		AP_CoopNotice("P2: PRESS START TO JOIN", "");
		ap_coop_hint_hub = gt->levelID;
	}
	if (start)
	{
		if (ap_coop_joined_device == start && partner == start)
		{
			ap_coop_picker_device = start;
			AP_HubPickerFreeze(gt, 1);
			ap_coop_selection = COOP_CHANGE_RACER;
			ap_coop_menu = COOP_MENU_ACTIONS;
			return;
		}
		int racer = g_config.coopRacer;
		if (!AP_CoopRacerUnlocked(racer))
			racer = AP_CoopNextRacer(-1, 1);
		if (racer < 0 || !NativeControls_SetAdventurePartner(start))
		{
			AP_CoopLog("refused", racer < 0 ? "no unlocked racer" : "controller unavailable",
			           gt->levelID, gt->numPlyrCurrGame);
			return;
		}
		ap_coop_joined_device = start;
		if (!ap_coop_restore_pending && racer != g_config.coopRacer)
			AP_CoopPersist(racer, g_config.coopRules);
		g_config.coopAdventure = 1;
		g_config.coopRacer = racer;
		AP_CoopNotice("P2 JOINED: ", data.MetaDataCharacters[racer].name_Debug);
		AP_CoopLog("join", "controller Start", gt->levelID, 2);
		AP_CoopReloadHub(gt, "join");
	}
}

void AP_CoopDrawMenu(void)
{
	struct GameTracker *gt = sdata->gGT;
	if (ap_coop_notice_frames > 0 && AP_CoopHubAvailable(gt) &&
	    !(gt->gameMode1 & PAUSE_ALL) && !sdata->ptrActiveMenu)
		DecalFont_DrawLine(ap_coop_notice, 256, 29, FONT_SMALL, JUSTIFY_CENTER | WHITE);
	if (ap_coop_menu == COOP_MENU_CLOSED)
		return;
	const int actions = ap_coop_menu == COOP_MENU_ACTIONS;
	DecalFont_DrawLine(actions ? "PLAYER 2" : "PLAYER 2 RACER",
	                   256, 63, FONT_SMALL, JUSTIFY_CENTER | WHITE);
	if (actions)
	{
		static char *labels[] = {"CHANGE RACER", "DROP OUT", "BACK"};
		for (int row = 0; row < COOP_ACTION_COUNT; row++)
			DecalFont_DrawLine(labels[row], 256, 83 + row * 17, FONT_SMALL,
			                   JUSTIFY_CENTER | (ap_coop_selection == row ? ORANGE : WHITE));
	}
	else if (AP_CoopRacerUnlocked(ap_coop_selection))
	{
		AP_CharSwap_DrawPortraitAt(ap_coop_selection, 240, 76, 0, 0);
		DecalFont_DrawLine((char *)data.MetaDataCharacters[ap_coop_selection].name_Debug,
		                   256, 110, FONT_SMALL, JUSTIFY_CENTER | WHITE);
		DecalFont_DrawLine("LEFT / RIGHT TO BROWSE", 256, 125, FONT_SMALL, JUSTIFY_CENTER | ORANGE);
	}
	DecalFont_DrawLine("CROSS: CONFIRM", 256, 143, FONT_SMALL, JUSTIFY_CENTER | WHITE);
	DecalFont_DrawLine("TRIANGLE: BACK", 256, 156, FONT_SMALL, JUSTIFY_CENTER | WHITE);
	RECT panel = {101, 51, 310, 121};
	RECTMENU_DrawInnerRect(&panel, 0, gt->pushBuffer_UI.ptrOT);
}

void AP_CoopDrawHubCounters(const struct GameTracker *gt)
{
	char line[96];
	snprintf(line, sizeof line, "TROPHIES %d   KEYS %d   RELICS %d",
	         AP_GateCount(AP_IDX_TROPHY), AP_GateCount(AP_IDX_KEY), AP_GateCountRelicSum());
	for (int i = 0; i < 2; i++)
	{
		const RECT *view = &gt->pushBuffer[i].rect;
		DecalFont_DrawLine(line, view->x + 12, view->y + 10, FONT_SMALL, ORANGE);
	}
}

void AP_CoopRulesStep(int direction)
{
	struct GameTracker *gt = sdata ? sdata->gGT : 0;
	if (gt && (gt->gameMode1 & ADVENTURE_CUP))
		ap_coop_help = "Finish or leave the cup first.";
	else if (!AP_CoopHubAvailable(gt) || (gt->gameMode1 & LOADING) ||
	         sdata->Loading.stage != LOAD_IDLE || sdata->load_inProgress)
		ap_coop_help = "Change in the Adventure hub.";
	else
	{
		AP_CoopRestorePreference();
		int hard = direction ? direction > 0 : !g_config.coopRules;
		ap_coop_help = hard ? "Hard: 1st and 2nd. 60% crystal time."
		                    : "Lenient: either can win. Full crystal time.";
		if (hard != g_config.coopRules)
			AP_CoopPersist(g_config.coopRacer, hard);
	}
}

const char *AP_CoopHelp(void)
{
	if (ap_net_coop_save_pending())
		return "Waiting for co-op save.";
	if (ap_net_coop_save_lost())
		return "Co-op edit not saved.";
	return ap_coop_help;
}
int AP_CoopHardRace(void) { return ap_coop_hard_race; }

int AP_CoopCrystalTimeLimit(const struct GameTracker *gt)
{
	// Expanded multiplies by 0.6f and truncates. Keep the original for a solo reload.
	return AP_CoopCrystal(gt) && AP_CoopHardRace()
	    ? (int)(gt->originalEventTime * 0.6f) : gt->originalEventTime;
}

int AP_CoopCrystalFallback(struct GameTracker *gt)
{
	if (!AP_CoopCrystal(gt))
		return 0;
	AP_CoopLog("crystal fallback", "arena allocation", gt->levelID, 2);
	AP_CoopNotice("CRYSTAL ARENA RELOADING SOLO", "");
	AP_CoopReloadSolo(gt, 1);
	return 1;
}

void AP_CoopCrystalLogResult(const struct GameTracker *gt)
{
	if (!AP_CoopCrystal(gt))
		return;
	char line[160];
	int count = AP_CrystalCount(gt, gt->drivers[0]);
	const char *result = AP_RaceAttemptIsForcedLoss() ? "forced loss" :
	    count >= gt->numCrystalsInLEV ? "complete" : "timeout";
	snprintf(line, sizeof line, "[AP COOP] crystal result=%s level=%d collected=%d total=%d limit=%d\n",
	         result, gt->levelID, count, gt->numCrystalsInLEV, AP_CoopCrystalTimeLimit(gt));
	AP_LogLine(line);
}

void AP_CoopPrepareLoad(struct GameTracker *gt)
{
	int mode = gt->gameMode1;
	const int entryRules = ap_coop_entry_rules;
	ap_coop_entry_rules = ap_coop_warp_rules = -1;
	int soloRetry = ap_coop_retry_level >= 0 && ap_coop_retry_level == gt->levelID;
	int capacityRetry = soloRetry && ap_coop_retry_capacity;
	ap_coop_retry_level = -1;
	if (soloRetry)
		gt->prevLEV = ap_coop_retry_hub;
	/* The screen/garage loader owns its temporary menu player counts. An
	 * Adventure flag can still be present while returning to those menus. */
	if (mode & (MAIN_MENU | GAME_CUTSCENE)) return;
	int retail = gt->levelID >= 0 && gt->levelID <= TURBO_TRACK;
#ifdef CTR_CUSTOM_TRACKS
	if (CustomTrack_ServingLoad((int)gt->levelID, (mode & ADVENTURE_CUP) != 0, gt->cup.cupID) ||
	    AP_CortexTrackActive()) retail = 0;
	/* Ask about the serving identity before changing the 1P load predicate. */
	const struct CustomTrackRaceContext *context = CustomTrack_StandaloneContext(gt->levelID);
	if (context)
		retail = context->retail;
#endif
	int trophy = !(mode & (ADVENTURE_ARENA | ADVENTURE_BOSS | RELIC_RACE |
	    CRYSTAL_CHALLENGE | ARCADE_MODE | TIME_TRIAL | BATTLE_MODE | MAIN_MENU |
	    GAME_CUTSCENE)) && !(gt->gameMode2 & TOKEN_RACE);
	if (!ctr_cfg_active() || !(mode & ADVENTURE_MODE)) return;
	int crystal = (mode & CRYSTAL_CHALLENGE) &&
	              !(mode & (ADVENTURE_CUP | ADVENTURE_ARENA | ADVENTURE_BOSS | RELIC_RACE |
	                        ARCADE_MODE | TIME_TRIAL | BATTLE_MODE)) && !(gt->gameMode2 & TOKEN_RACE);
	int splitCrystal = crystal && AP_CoopCrystalSupported(gt->levelID);
#ifdef CTR_CUSTOM_TRACKS
	if (context && (!context->retail || !context->crystal))
		splitCrystal = 0;
#endif
	if ((trophy || splitCrystal) && g_config.coopAdventure)
		AP_CoopRestorePreference();
	int controllers = AP_CoopControllers();
	int hub = (mode & ADVENTURE_ARENA) != 0;
	int racer = g_config.coopRacer;
	int specialRoster = trophy && retail && !(mode & ADVENTURE_CUP) &&
	                    AP_CoopSpecialTrophyRoster(gt->levelID);
	int two = !soloRetry && AP_CoopSeatsTwo(g_config.coopAdventure == 1, controllers, 1,
	                                      trophy || splitCrystal, splitCrystal || (retail && !specialRoster),
	                                      (mode & ADVENTURE_CUP) != 0);
#ifdef CTR_CUSTOM_TRACKS
	if (hub && g_config.coopAdventure && controllers == 2)
		two = 1;
#endif
	if (two && !AP_CoopRacerUnlocked(racer))
		racer = AP_CoopNextRacer(-1, 1);
	two = two && AP_CoopRacerUnlocked(racer);
	gt->numPlyrCurrGame = two ? 2 : 1;
	// Start a pad race with the confirmed rules that were accepted by the hub.
	// Other load paths must never award a provisional Lenient result.
	ap_coop_hard_race = two && !hub && (entryRules >= 0 ? entryRules :
	    (g_config.coopRules || ap_coop_restore_pending || ap_net_coop_save_pending()));
	gt->numPlyrNextGame = 1;
	if ((soloRetry || g_config.coopAdventure == 1) && !hub)
	{
		const char *reason = two ? (ap_coop_hard_race ? "Hard" : "Lenient") :
		    capacityRetry ? "arena allocation" :
		    soloRetry || controllers < 2 ? "controller unavailable" :
		    specialRoster ? "special opponents" :
		    !AP_CoopRacerUnlocked(racer) ? "racer locked" : "solo event";
		AP_CoopLog(crystal ? "crystal seats" : "race seats", reason, gt->levelID, gt->numPlyrCurrGame);
		if (!two)
			AP_FeedNoticeLine(capacityRetry ? "CRYSTAL ARENA RELOADING SOLO" :
			    soloRetry || controllers < 2 ? "P2 DISCONNECTED: SOLO RACE" :
			    specialRoster ? "SPECIAL OPPONENTS: SOLO RACE" :
			    !AP_CoopRacerUnlocked(racer) ? "P2 RACER LOCKED: SOLO RACE" : "P2 SITS THIS RACE OUT");
	}
	ap_coop_p2_device = two ? NativeControls_GetPlayerDeviceIdentity(1) : 0;
	if (two)
	{
		NativeControls_SetAdventurePartner(ap_coop_p2_device);
		data.characterIDs[1] = racer;
	}
}

void AP_CoopPrepareGrid(struct GameTracker *gt)
{
	if (!AP_CoopRace(gt)) return;
	int seats = LOAD_CoopGridSize();
	sdata->kartSpawnOrderArray[0] = seats - 1;
	sdata->kartSpawnOrderArray[1] = seats - 2;
	for (int i = 2; i < seats; i++)
		sdata->kartSpawnOrderArray[i] = i - 2;
}

void AP_CoopTick(struct GameTracker *gt)
{
	if (sdata && sdata->Loading.stage == LOAD_IDLE && (AP_CoopDriving(gt) || AP_CoopHub(gt)) && gt->drivers[1])
		AP_CharSwap_ApplyStatPackage(gt->drivers[1]);
	if (AP_CoopCrystal(gt) && sdata->Loading.stage == LOAD_IDLE &&
	    !(gt->gameMode1 & (LOADING | END_OF_RACE)) && gt->drivers[0] && gt->drivers[1])
	{
		if (gt->trafficLightsTimer < 1 &&
		    !(gt->gameMode1 & (PAUSE_ALL | START_OF_RACE)) && gt->numCrystalsInLEV > 0 &&
		    (AP_CrystalCount(gt, gt->drivers[0]) >= gt->numCrystalsInLEV ||
		     gt->elapsedEventTime > AP_CoopCrystalTimeLimit(gt)))
		{
			for (int player = 0; player < 2; player++)
			{
				gt->drivers[player]->funcPtrs[DRIVER_FUNC_INIT] = VehPhysProc_FreezeEndEvent_Init;
				gt->drivers[player]->actionsFlagSet |= ACTION_RACE_FINISHED;
			}
			MainGameEnd_Initialize();
		}
		else if (!AP_CoopP2Available())
			AP_CoopReloadOnRetry(gt);
	}
	if (!gt || !(gt->gameMode1 & ADVENTURE_ARENA) ||
	    (gt->gameMode1 & (ADVENTURE_CUP | MAIN_MENU | GAME_CUTSCENE)) ||
	    sdata->Loading.stage != LOAD_IDLE) return;
	AP_CoopRestorePreference();
}
#endif
