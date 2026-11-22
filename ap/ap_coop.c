#ifdef CTR_AP
#include <common.h>
#include "ap_coop.h"
#include "ap_coop_logic.h"
#include "ap_capability.h"
#include "ap_charswap.h"
#include "ap_hooks.h"
#include "ap_net.h"
#include <platform/native_controls.h>

static const char *ap_coop_help = "Hub only; connect two controllers.";
static unsigned ap_coop_restored;
static u64 ap_coop_p2_device;
static int ap_coop_retry_level = -1;
static int ap_coop_retry_hub;

void AP_CoopConnectReset(void)
{
	/* A preference never crosses a room boundary through global config.ini. */
	g_config.coopAdventure = 0;
	g_config.coopRacer = ctr_cfg.starting_character;
	ap_coop_restored = ap_net_coop_revision();
	// A socket reconnect can happen during a race or a requested retry.
	// The device and return hub belong to the loaded scene.
}

static void AP_CoopPersist(int enabling, int racer)
{
	if (!ap_net_coop_set(racer + (enabling ? 16 : 0)))
	{
		ap_coop_help = "Could not save co-op. Reconnect and try again.";
		return;
	}
	g_config.coopAdventure = enabling;
	g_config.coopRacer = racer;
}

static int AP_CoopControllers(void)
{
	if (!sdata || !sdata->gGamepads) return 0;
	/* numGamepadsConnected is the highest occupied slot, not a population
	 * count. A controller in slot 3 cannot substitute for missing slot 2. */
	return (sdata->gGamepads->gamepadsConnectedByFlag & 3u) == 3u ? 2 : 1;
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
	// A replugged gamepad gets a new SDL ID. A pin reclaims its configured
	// controller, while Auto can put an unrelated device in the vacant seat.
	return device != 0 && device != UINT64_MAX && ap_coop_p2_device != UINT64_MAX &&
	       NativeControls_GetPlayerPref(1) == NATIVE_CONTROLS_PREF_PINNED;
}

int AP_CoopReloadOnRetry(struct GameTracker *gt)
{
	if (!AP_CoopRace(gt) || AP_CoopP2Available())
		return 0;
	// The in-place restart retains the two-player files and pools.
	ap_coop_retry_level = gt->levelID;
	ap_coop_retry_hub = gt->prevLEV;
	MainRaceTrack_RequestLoad(gt->levelID);
	return 1;
}

int AP_CoopHubAvailable(const struct GameTracker *gt)
{
	return gt && ctr_cfg_active() && (gt->gameMode1 & ADVENTURE_ARENA) &&
	       !(gt->gameMode1 & (MAIN_MENU | GAME_CUTSCENE));
}

static int AP_CoopRacerUnlocked(int racer)
{
	return racer >= 0 && racer < 16 &&
	       AP_CharacterUnlocked(racer);
}

static int AP_CoopCanEdit(int enabling)
{
	struct GameTracker *gt = sdata ? sdata->gGT : 0;
	int refusal = AP_CoopToggleRefusal(
	    AP_CoopHubAvailable(gt),
	    gt && (gt->gameMode1 & ADVENTURE_CUP),
	    sdata && sdata->Loading.stage == LOAD_IDLE,
	    AP_CoopControllers(), enabling);
	static const char *messages[] = {"Lenient: either human wins.",
	    "Change players in the hub only.", "Finish or leave the cup first.",
	    "Connect a second controller first."};
	ap_coop_help = messages[refusal];
	return refusal == AP_COOP_OK;
}

void AP_CoopStep(int direction)
{
	int enabling = direction > 0;
	if (!AP_CoopCanEdit(enabling)) return;
	int racer = g_config.coopRacer;
	if (enabling && !AP_CoopRacerUnlocked(racer))
		for (int i = 0; i < 16; i++)
			if (AP_CoopRacerUnlocked(i)) { racer = i; break; }
	AP_CoopPersist(enabling, racer);
}

void AP_CoopRacerStep(int direction)
{
	if (!AP_CoopCanEdit(1)) return;
	direction = direction < 0 ? -1 : 1;
	for (int i = 1; i <= 16; i++)
	{
		int base = (unsigned)g_config.coopRacer < 16 ? g_config.coopRacer : 0;
		int racer = (base + direction * i + 32) % 16;
		if (AP_CoopRacerUnlocked(racer))
		{
			AP_CoopPersist(g_config.coopAdventure, racer);
			return;
		}
	}
}

const char *AP_CoopHelp(void) { return ap_coop_help; }

void AP_CoopPrepareLoad(struct GameTracker *gt)
{
	int mode = gt->gameMode1;
	int soloRetry = ap_coop_retry_level >= 0 && ap_coop_retry_level == gt->levelID;
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
	if (CustomTrack_StandaloneContext((int)gt->levelID))
	{
		const struct CustomTrackRaceContext *context = CustomTrack_StandaloneContext((int)gt->levelID);
		retail = context->retail;
	}
#endif
	int trophy = !(mode & (ADVENTURE_ARENA | ADVENTURE_BOSS | RELIC_RACE |
	    CRYSTAL_CHALLENGE | ARCADE_MODE | TIME_TRIAL | BATTLE_MODE | MAIN_MENU |
	    GAME_CUTSCENE)) && !(gt->gameMode2 & TOKEN_RACE);
	if (!ctr_cfg_active() || !(mode & ADVENTURE_MODE)) return;
	int two = !soloRetry && AP_CoopSeatsTwo(g_config.coopAdventure == 1, AP_CoopControllers(), 1,
	                         trophy, retail, (mode & ADVENTURE_CUP) != 0) &&
	          AP_CoopRacerUnlocked(g_config.coopRacer);
	gt->numPlyrCurrGame = two ? 2 : 1;
	/* Hub and unsupported races are always solo, without losing the preference. */
	gt->numPlyrNextGame = 1;
	if ((soloRetry || g_config.coopAdventure == 1) && !two &&
	    !(mode & (ADVENTURE_ARENA | MAIN_MENU | GAME_CUTSCENE)))
	{
		AP_FeedNoticeLine(soloRetry || AP_CoopControllers() < 2 ? "P2 DISCONNECTED: SOLO RACE" :
		    !AP_CoopRacerUnlocked(g_config.coopRacer) ? "P2 RACER LOCKED: SOLO RACE" :
	    "P2 SITS THIS RACE OUT");
	}
	ap_coop_p2_device = two ? NativeControls_GetPlayerDeviceIdentity(1) : 0;
	if (two)
	{
		data.characterIDs[1] = g_config.coopRacer;
	}
}

void AP_CoopPrepareGrid(struct GameTracker *gt)
{
	if (!AP_CoopRace(gt)) return;
	/* Run after stock difficulty setup copies its 2P grid, before paths,
	 * launch order and driver birth consume it. Retail 2P has six seats. */
	sdata->kartSpawnOrderArray[0] = 5;
	sdata->kartSpawnOrderArray[1] = 4;
	for (int i = 2; i < 6; i++) sdata->kartSpawnOrderArray[i] = i - 2;
}

void AP_CoopTick(struct GameTracker *gt)
{
	int packed;
	if (sdata && sdata->Loading.stage == LOAD_IDLE && AP_CoopRace(gt) && gt->drivers[1])
		AP_CharSwap_ApplyStatPackage(gt->drivers[1]);
	if (!gt || !(gt->gameMode1 & ADVENTURE_ARENA) ||
	    (gt->gameMode1 & (ADVENTURE_CUP | MAIN_MENU | GAME_CUTSCENE)) ||
	    sdata->Loading.stage != LOAD_IDLE) return;
	if (ap_net_coop_revision() == ap_coop_restored || !ap_net_coop_known(&packed)) return;
	ap_coop_restored = ap_net_coop_revision();
	/* Unlock catch-up can arrive after the preference. Retain the pending
	 * choice until its racer is available, rather than discarding it. */
	if (!AP_CoopRacerUnlocked(packed % 16)) { ap_coop_restored--; return; }
	g_config.coopRacer = packed % 16;
	g_config.coopAdventure = packed >= 16;
}
#endif
