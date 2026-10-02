#ifdef CTR_AP

// Race-loss DeathLink stakes (native #449, 0.2.4): production glue.
//
// Compiled in the C unity build after ap_hooks.c (game_unity.h), so it reads the
// engine structs and ap_hooks.c's file-static helpers (AP_LookupLocationCode,
// AP_ComposedBossesWon) directly. The decisions themselves are the freestanding
// functions in ap_win_logic.h, which the host harnesses drive.
//
// Flow: AP_WinStakesOnLevelStart fixes the race's stakes when it starts
// (ticket 04); a Gem Cup decides on its first leg after AP_WinStakesOnCupEnter
// and keeps that for the whole cup. The race_loss send path in ap_deathlink.c
// asks AP_WinStakesAllowLossSend before sending; an exempt loss writes one log
// line saying why and sends nothing. Receive behaviour, the other send triggers,
// the Gem Cup leg rule and the forced-loss rule are untouched.

#include <common.h>
#include <stdio.h>

#include "ap_win_logic.h"
#include "ap_hooks.h"   // AP_LogLine, AP_TrialTrackConfigured, AP_CortexTrackActive
#include "ap_net.h"     // ap_net_location_checked
#include "ap_seedcfg.h" // ctr_cfg, ctr_cfg_active
#include "ap_cortex_track.h" // AP_CortexSlotCode, AP_CV_SLOT_*
#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h> // CustomTrack_CupRaceRedirectActive
#endif

static AP_ItemIdTally g_wl_tally;
static AP_WinStakesLatch g_wl_latch;

void AP_WinLogicTallyReset(void)
{
	AP_ItemIdTallyReset(&g_wl_tally);
}

void AP_WinLogicTallyItem(long long itemId, unsigned flags)
{
	AP_ItemIdTallyReceive(&g_wl_tally, itemId, flags);
}

// ── Leaf inputs ──
static int AP_WinEnvItemCount(void *user, long id)
{
	(void)user;
	return AP_ItemIdTallyCount(&g_wl_tally, (long long)id);
}

static int AP_WinEnvBosses(void *user)
{
	(void)user;
	return AP_ComposedBossesWon();
}

static int AP_WinEnvCollected(void *user, long code)
{
	(void)user;
	return ap_net_location_checked((long long)code);
}

static AP_WinLogicEnv AP_WinEnvNow(void)
{
	AP_WinLogicEnv env;
	env.user = 0;
	env.item_count = AP_WinEnvItemCount;
	env.bosses_won = AP_WinEnvBosses;
	env.collected = AP_WinEnvCollected;
	env.boost_mode = ctr_cfg.boost_mode;
	env.starting_character = ctr_cfg.starting_character;
	env.character_unlocks = ctr_cfg.character_unlocks ? 1 : 0;
	env.unreliable = g_wl_tally.overflow;
	return env;
}

static long AP_WinBitCode(int bit)
{
	return AP_LookupLocationCode(bit);
}

// ── The race being played, read from the engine ──
static void AP_WinFactsNow(struct GameTracker *gGT, AP_WinRaceFacts *f)
{
	int L = (int)gGT->levelID;
	int i;

	f->adventure = (gGT->gameMode1 & ADVENTURE_MODE) != 0;
	f->boss = IS_BOSS_RACE(gGT->gameMode1) ? 1 : 0;
	f->bossID = gGT->bossID;
	f->cup = (gGT->gameMode1 & ADVENTURE_CUP) != 0;
	f->cupID = gGT->cup.cupID;
	f->cupCustom = 0;
#ifdef CTR_CUSTOM_TRACKS
	if (f->cup && ctr_cfg_active() && ctr_cfg.custom_tracks_ok &&
	    (gGT->gameMode2 & CUP_ANY_KIND) == 0)
		f->cupCustom = CustomTrack_CupRaceRedirectActive(f->cupID, 1) ? 1 : 0;
#endif
	f->relic = (gGT->gameMode1 & RELIC_RACE) != 0;
	f->token = (gGT->gameMode2 & TOKEN_RACE) != 0;
	f->crystal = (gGT->gameMode1 & CRYSTAL_CHALLENGE) != 0;
	f->levelID = L;
	f->cortex = AP_CortexTrackActive();
	f->trial = AP_TrialTrackConfigured(L);
	f->trialTrophy = f->trial ? AP_TrialTrackLocation(L, CTR_CFG_TRIAL_TROPHY) : -1;
	f->trialCtr = f->trial ? AP_TrialTrackLocation(L, CTR_CFG_TRIAL_CTR) : -1;
	f->cortexCodes[0] = AP_CortexTrackCode(AP_CV_SLOT_TROPHY);
	for (i = 0; i < 3; i++)
		f->cortexCodes[1 + i] = AP_CortexTrackCode(AP_CV_SLOT_RELIC0 + i);
	f->cortexCodes[4] = AP_CortexTrackCode(AP_CV_SLOT_TOKEN);
	f->customTrophy = -1;
	f->customCtr = -1;
	if (ctr_cfg_active() && ctr_cfg.custom_tracks_ok)
	{
		f->customTrophy = ctr_cfg.custom_track.trophy_location > 0
		                      ? ctr_cfg.custom_track.trophy_location : -1;
		f->customCtr = ctr_cfg.custom_ctr_enabled && ctr_cfg.custom_ctr_location > 0
		                   ? ctr_cfg.custom_ctr_location : -1;
	}
}

static AP_WinStakes AP_WinDecideNow(struct GameTracker *gGT, const char *when)
{
	AP_WinRaceFacts f;
	AP_WinLogicEnv env;
	AP_WinStakes s;
	long set[AP_WS_WINSET_MAX];
	int n, mode, i, used;
	char wins[48];
	char msg[256];

	AP_WinFactsNow(gGT, &f);
	n = AP_WinSetResolve(&f, AP_WinBitCode, set, &mode);
	env = AP_WinEnvNow();
	s = AP_WinStakesDecide(&ctr_win_logic, &env, set, n);

	wins[0] = '\0';
	for (i = 0, used = 0; i < n; i++)
		used += snprintf(wins + used, sizeof wins - (size_t)used, "%s%ld", i ? "," : "", set[i]);
	snprintf(msg, sizeof msg, "[AP DEATH] race stakes %s: %s level=%d win=%s -> %s (%s)\n",
	         when, AP_WinModeName(mode), f.levelID, n > 0 ? wins : "none",
	         s.stakes ? "a loss sends" : "exempt", AP_WinStakesWhyText(s.why));
	AP_LogLine(msg);
	return s;
}

void AP_WinStakesOnCupEnter(void)
{
	AP_WinStakesCupEntered(&g_wl_latch);
}

void AP_WinStakesOnLevelStart(struct GameTracker *gGT, int racingLevel)
{
	int isRace, isCup, need;

	if (gGT == 0)
		return;
	isRace = racingLevel && ctr_cfg_active() &&
	         (gGT->gameMode1 & ADVENTURE_MODE) != 0 &&
	         (gGT->gameMode1 & ADVENTURE_ARENA) == 0;
	isCup = isRace && (gGT->gameMode1 & ADVENTURE_CUP) != 0;
	need = AP_WinStakesLevelStart(&g_wl_latch, isRace, isCup);
	if (need == AP_WS_NEED_NONE)
		return;
	AP_WinStakesRecord(&g_wl_latch, need,
	                   AP_WinDecideNow(gGT, need == AP_WS_NEED_CUP ? "at cup start"
	                                                               : "at race start"));
}

int AP_WinStakesAllowLossSend(struct GameTracker *gGT, int inCup, const char *cause)
{
	AP_WinStakes s;
	char msg[224];

	if (gGT == 0)
		return 1;
	if (!AP_WinStakesCurrent(&g_wl_latch, inCup, &s))
	{
		// No decision from the race start (the slot connected mid-race, or the
		// start was not an adventure race this module saw): decide now, once.
		s = AP_WinDecideNow(gGT, "at the loss (none at race start)");
		AP_WinStakesRecord(&g_wl_latch, inCup ? AP_WS_NEED_CUP : AP_WS_NEED_RACE, s);
	}
	if (s.stakes)
		return 1;
	snprintf(msg, sizeof msg, "[AP DEATH] race_loss not sent (%s): race exempt, %s\n",
	         cause ? cause : "?", AP_WinStakesWhyText(s.why));
	AP_LogLine(msg);
	return 0;
}

#endif // CTR_AP
