#ifndef AP_COOP_H
#define AP_COOP_H
#include <common.h>
#include "ap_coop_logic.h"
#include "ap_deathlink.h"
static inline int AP_CoopRace(const struct GameTracker *gt)
{
	return gt && gt->numPlyrCurrGame == 2 &&
	       (gt->gameMode1 & ADVENTURE_MODE) &&
	       !(gt->gameMode1 & (ADVENTURE_ARENA | ADVENTURE_CUP | ADVENTURE_BOSS |
	                          RELIC_RACE | CRYSTAL_CHALLENGE | ARCADE_MODE |
	                          TIME_TRIAL | BATTLE_MODE | MAIN_MENU | GAME_CUTSCENE)) &&
	       !(gt->gameMode2 & TOKEN_RACE);
}
static inline int AP_IsSlotHuman(const struct GameTracker *gt, const struct Driver *driver)
{
	if (!gt || !driver) return 0;
	if (driver != gt->drivers[0] && !(AP_CoopRace(gt) && driver == gt->drivers[1])) return 0;
	return !(driver->actionsFlagSet & ACTION_BOT);
}
int AP_CoopHardRace(void);
static inline int AP_CoopRank(const struct GameTracker *gt, int finishedOnly)
{
	int ranks[] = {-1, -1};
	int count = AP_CoopRace(gt) ? 2 : 1;
	if (!gt) return -1;
	for (int i = 0; i < count; i++)
	{
		const struct Driver *d = gt->drivers[i];
		if (d && (!finishedOnly || (d->actionsFlagSet & ACTION_RACE_FINISHED)))
			ranks[i] = d->driverRank;
	}
	if (count == 2 && AP_CoopHardRace())
		return AP_CoopWorseRank(ranks[0], ranks[1]);
	return AP_CoopBetterRank(ranks[0], ranks[1]);
}
static inline int AP_CoopWon(const struct GameTracker *gt)
{
	if (AP_RaceAttemptIsForcedLoss()) return 0;
	if (AP_CoopRace(gt) && AP_CoopHardRace())
		return gt->drivers[0] && gt->drivers[1] &&
		       AP_CoopHardWon(gt->drivers[0]->driverRank, gt->drivers[1]->driverRank);
	return AP_CoopRank(gt, 0) == 0;
}
static inline int AP_CoopTrophyPlace(const struct GameTracker *gt)
{
	return AP_CoopRace(gt) && AP_CoopHardRace() ? 2 : 1;
}
void AP_CoopPrepareLoad(struct GameTracker *gt);
int AP_CoopP2Available(void);
int AP_CoopReloadOnRetry(struct GameTracker *gt);
int AP_CoopHubAvailable(const struct GameTracker *gt);
int AP_CoopPadReady(void);
void AP_CoopPrepareGrid(struct GameTracker *gt);
int LOAD_CoopGridSize(void);
void LOAD_CoopConvertBotModels(void);
void AP_CoopRulesStep(int direction);
const char *AP_CoopHelp(void);
void AP_CoopTick(struct GameTracker *gt);
void AP_CoopMenuTick(struct GameTracker *gt);
int AP_CoopMenuOpen(void);
void AP_CoopDrawMenu(void);
void AP_CoopConnectReset(void);
#endif
