#ifdef CTR_AP
#include <common.h>
#include "ap_coop.h"
#include "ap_coop_logic.h"
#include "ap_capability.h"
#include "ap_charswap.h"
#include "ap_hooks.h"
#include "ap_net.h"

static const char *ap_coop_help = "Hub only; connect two controllers.";
static unsigned ap_coop_restored;

void AP_CoopConnectReset(void)
{
	/* A preference never crosses a room boundary through global config.ini. */
	g_config.coopAdventure = 0;
	g_config.coopRacer = ctr_cfg.starting_character;
	ap_coop_restored = ap_net_coop_revision();
}

static void AP_CoopPersist(void)
{
	ap_net_coop_set(g_config.coopRacer + (g_config.coopAdventure ? 16 : 0));
}

static int AP_CoopControllers(void)
{
	if (!sdata || !sdata->gGamepads) return 0;
	/* numGamepadsConnected is the highest occupied slot, not a population
	 * count. A controller in slot 3 cannot substitute for missing slot 2. */
	return (sdata->gGamepads->gamepadsConnectedByFlag & 3u) == 3u ? 2 : 1;
}

static int AP_CoopRacerUnlocked(int racer)
{
	return racer >= 0 && racer < 16 &&
	       (!ctr_cfg.character_phase_present || AP_CharacterUnlocked(racer));
}

static int AP_CoopCanEdit(int enabling)
{
	struct GameTracker *gt = sdata ? sdata->gGT : 0;
	int refusal = AP_CoopToggleRefusal(
	    gt && ctr_cfg_active() && (gt->gameMode1 & ADVENTURE_ARENA) &&
	        !(gt->gameMode1 & (MAIN_MENU | GAME_CUTSCENE)),
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
	if (enabling && !AP_CoopRacerUnlocked(g_config.coopRacer))
		for (int i = 0; i < 16; i++)
			if (AP_CoopRacerUnlocked(i)) { g_config.coopRacer = i; break; }
	g_config.coopAdventure = enabling;
	AP_CoopPersist();
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
			g_config.coopRacer = racer;
			AP_CoopPersist();
			return;
		}
	}
}

const char *AP_CoopHelp(void) { return ap_coop_help; }

void AP_CoopPrepareLoad(struct GameTracker *gt)
{
	int mode = gt->gameMode1;
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
	int two = AP_CoopSeatsTwo(g_config.coopAdventure == 1, AP_CoopControllers(), 1,
	                         trophy, retail, (mode & ADVENTURE_CUP) != 0) &&
	          AP_CoopRacerUnlocked(g_config.coopRacer);
	gt->numPlyrCurrGame = two ? 2 : 1;
	/* Hub and unsupported races are always solo, without losing the preference. */
	gt->numPlyrNextGame = 1;
	if (g_config.coopAdventure == 1 && !two &&
	    !(mode & (ADVENTURE_ARENA | MAIN_MENU | GAME_CUTSCENE)))
	{
		AP_FeedNoticeLine(AP_CoopControllers() < 2 ? "P2 DISCONNECTED: SOLO RACE" :
		    !AP_CoopRacerUnlocked(g_config.coopRacer) ? "P2 RACER LOCKED: SOLO RACE" :
	    "P2 SITS THIS RACE OUT");
	}
	if (two)
	{
		data.characterIDs[1] = g_config.coopRacer;
		/* Retail's 2P roster has six seats. Both humans start behind the bots. */
		sdata->kartSpawnOrderArray[0] = 5;
		sdata->kartSpawnOrderArray[1] = 4;
		for (int i = 2; i < 6; i++) sdata->kartSpawnOrderArray[i] = i - 2;
	}
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
