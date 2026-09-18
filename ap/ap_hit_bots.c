#ifdef CTR_AP

// Hit Character encounters (ticket 06): the BOTS-side gather.
//
// BOTS_ChangeState alone can see the victim/attacker driver facts and the live
// race mode. This function turns them into the flags ap/ap_hit_encounter.c's
// decision consumes. Extracted from game/BOTS.c so a host harness can link the
// REAL gather with stubbed globals (the alternative -- compiling the whole BOTS
// overlay -- is not linkable off-engine).
//
// The Adventure test MUST use ADVENTURE_MODE, the persistent adventure flag.
// ADVENTURE_ARENA is the HUB flag and is cleared by every pad load
// (AH_WarpPad.c WarpPad_RequestLoad), so it reads 0 during the race itself.

#include <common.h>
#include "ap_hit_encounter.h"
#include "ap_hit_policy.h"

// `wasDamageActive` is the pre-call damage state, so type 1 while already
// damage-active (which applies nothing) is rejected while type 4 burn is kept.
void AP_HitBotsVictim(struct Driver *victim, int damageType,
                      struct Driver *attacker, int wasDamageActive)
{
	struct GameTracker *gGT = sdata->gGT;
	int victimEngine;
	unsigned flags = 0;
	int live = 0;
	int liveInstance = 0;

	if (gGT == NULL || victim == NULL)
		return;

	// A valid slot index is required before indexing characterIDs (driverID is
	// an unsigned byte, so only the upper bound needs checking).
	if (victim->driverID >= 8)
		return;

	for (int i = 0; i < 8; i++)
		if (gGT->drivers[i] == victim)
			live = 1;

	if (victim->instSelf != NULL && victim->instSelf->thread != NULL &&
	    (victim->instSelf->thread->flags & THREAD_FLAG_DEAD) == 0)
		liveInstance = 1;

	if ((victim->actionsFlagSet & ACTION_BOT) != 0)
		flags |= 1u;
	if (live)
		flags |= 2u;
	if (victim->instSelf != NULL && victim->instSelf->thread != NULL &&
	    victim->instSelf->thread->modelIndex == DYNAMIC_GHOST)
		flags |= 4u;
	if (victim == gGT->drivers[0])
		flags |= 8u;
	if (attacker != NULL)
		flags |= 16u;
	if (attacker != NULL && attacker == gGT->drivers[0])
		flags |= 32u;
	if (attacker != NULL && (attacker->actionsFlagSet & ACTION_BOT) != 0)
		flags |= 64u;

	// Supported race: an Adventure race that seats a roster -- an ordinary
	// Trophy race, a CTR Challenge (TOKEN_RACE), a boss race or a Gem Cup leg.
	// Time trial, arcade, battle, relic and crystal are rejected. A live
	// instance is also required.
	if (liveInstance &&
	    AP_HitRaceSupportedPure((gGT->gameMode1 & ADVENTURE_MODE) != 0,
	                            IS_BOSS_RACE(gGT->gameMode1),
	                            (gGT->gameMode1 & ADVENTURE_CUP) != 0,
	                            (gGT->gameMode1 & TIME_TRIAL) != 0,
	                            (gGT->gameMode1 & ARCADE_MODE) != 0,
	                            (gGT->gameMode1 & BATTLE_MODE) != 0,
	                            (gGT->gameMode1 & RELIC_RACE) != 0,
	                            (gGT->gameMode2 & TOKEN_RACE) != 0,
	                            (gGT->gameMode1 & CRYSTAL_CHALLENGE) != 0))
		flags |= 128u;
	if (wasDamageActive)
		flags |= 256u;

	victimEngine = (int)data.characterIDs[victim->driverID];
	AP_HitEncounterOnDamage(victimEngine, damageType, flags);
}

#endif // CTR_AP
