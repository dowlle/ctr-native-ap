#ifndef AP_BOSS_MAP_STAR_H
#define AP_BOSS_MAP_STAR_H

#include "ap_boss_won_flags.h"

// Boss garage star on the hub map (game/232/AH_Map.c), #458 follow-up.
//
// The open garage of an unbeaten boss shows a blinking star with an arrow
// ring: the garage's "accessible" glow. A boss counted as beaten shows no star.
//
// Without a Bosses goal, "beaten" is the checked boss race location: once it
// is checked, racing the boss gives nothing more, whoever checked it.
//
// With a Bosses goal (goal_bosses > 0) that is still short of its count, a
// boss only counts toward the goal when this player beat it (the #458 boss-won
// flags), not when another player's Collect checked its location. So while
// the goal still needs bosses, "beaten" is that player's own win: a
// Collect-marked but unbeaten boss keeps its glow, because racing it still
// moves the goal. Once the goal has its bosses, or before the flags have been
// read from the server, the checked location decides as before.
//
// bossIdx 0..3: Ripper Roo, Papu Papu, Komodo Joe, Pinstripe.
static inline int AP_BossMapBeaten(int bossIdx, int locationChecked,
                                   int goalBosses, int wonKnown,
                                   unsigned wonBits)
{
	if (goalBosses > 0 && wonKnown &&
	    AP_BossWonCount(wonBits) < goalBosses)
		return (wonBits & AP_BOSS_WON_BIT(bossIdx)) != 0;
	return locationChecked != 0;
}

#endif // AP_BOSS_MAP_STAR_H
