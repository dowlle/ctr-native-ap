#include <common.h>

#ifdef CTR_AP
#include "../ap/ap_boss_adapter.h"

// J03 (#169 full weapon suite): the boss adapter's per-race state and the
// driver it armed. Reset at every race init, so a restart, an exit to the map
// or a failed load never carries a use or a timer into the next race.
static AP_BossAdapterState s_apBossAdapter;
static struct Driver *s_apBossAdapterDriver;
#endif

#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
static struct MetaDataBOSS s_cortexVortexOxideWeapons[14];
static const struct MetaDataBOSS s_cortexVortexOxideWeaponTemplate[14] = {
	{0x00, 0x02, 0x66, 0x01, 0x02, 0x00},
	{0x10, 0x02, 0x64, 0x01, 0x02, 0x00},
	{0x21, 0x03, 0x03, 0x01, 0x0c, 0x01},
	{0x2f, 0x03, 0x01, 0x01, 0x0c, 0x00},
	{0x3b, 0x02, 0x0f, 0x01, 0x0c, 0x01},
	{0x40, 0x03, 0x01, 0x01, 0x0c, 0x00},
	{0x46, 0x02, 0x04, 0x01, 0x0c, 0x01},
	{0x56, 0x02, 0x0f, 0x01, 0x0c, 0x01},
	{0x5d, 0x02, 0x04, 0x01, 0x0c, 0x01},
	{0x60, 0x02, 0x0f, 0x01, 0x0c, 0x01},
	{0x65, 0x02, 0x04, 0x01, 0x0c, 0x01},
	{0x66, 0x02, 0x0f, 0x01, 0x0c, 0x01},
	{0x7f, 0x02, 0x04, 0x01, 0x0c, 0x01},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

static void PickupBots_InitCortexVortexOxideWeapons(struct GameTracker *gGT)
{
	int i;
	int lastCheckpoint = gGT->level1 != NULL ? gGT->level1->cnt_restart_points - 1 : -1;

	memcpy(s_cortexVortexOxideWeapons, s_cortexVortexOxideWeaponTemplate,
	       sizeof s_cortexVortexOxideWeapons);
	if (lastCheckpoint < 0)
		return;
	for (i = 0; i < 13; i++)
		s_cortexVortexOxideWeapons[i].trackCheckpoint =
		    (u8)AP_OxideWeaponCheckpoint(
		        s_cortexVortexOxideWeaponTemplate[i].trackCheckpoint,
		        lastCheckpoint);
}
#endif

void PickupBots_Init(void)
{
	// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80040850-0x800408b8.
	int hub;
	int lev = sdata->gGT->levelID;

#ifdef CTR_CUSTOM_PACKAGES
	// A custom-page race has no hub and no boss: never read the host slot's
	// level metadata. bossWeaponMeta is only read by boss AI.
	if (MainRaceTrack_OfflineCustomLoad())
		return;
#endif

#ifdef CTR_AP
	AP_BossAdapterReset(&s_apBossAdapter);
	s_apBossAdapterDriver = NULL;
#endif

	// get hubID of level
	hub = data.metaDataLEV[lev].hubID;

	// If Level ID is Oxide Station
	if (lev == OXIDE_STATION)
	{
		hub = 0;
	}

#ifdef CTR_AP
	// #169 boss track shuffle: a garage boss on a drawn venue keeps its own
	// retail attack, mapped onto this track's checkpoints. The venue's own
	// hubID would hand it another boss's table, or none on a trial track.
	// J03: also N. Oxide's composed final record (before the Cortex Vortex
	// table, which it replaces) and an armed Purple Gem Cup gauntlet leg.
	{
		struct MetaDataBOSS *venueMeta = AP_BossVenueAttackSchedule(1);
		if (venueMeta != NULL)
		{
			sdata->bossWeaponMeta = venueMeta;
			return;
		}
	}
#endif

#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
	if (CustomTrack_OxideFinalServing(lev, sdata->gGT->bossID,
	                                  (sdata->gGT->gameMode1 & ADVENTURE_BOSS) != 0))
	{
		PickupBots_InitCortexVortexOxideWeapons(sdata->gGT);
		sdata->bossWeaponMeta = s_cortexVortexOxideWeapons;
		return;
	}
#endif

	if (hub > -1)
	{
		// set pointer to boss weapon meta
		sdata->bossWeaponMeta = data.bossWeaponMetaPtr[hub];
	}
	return;
}

enum
{
	PICKUPBOTS_ITEM_NONE = 0xf,
	PICKUPBOTS_ITEM_INVALID = -1,
	PICKUPBOTS_ITEM_BOMB = 1,
	PICKUPBOTS_ITEM_MISSILE = 2,
	PICKUPBOTS_ITEM_TNT = 3,
	PICKUPBOTS_ITEM_POTION = 4,
	PICKUPBOTS_BOSS_PATH_REQUEST_FRAMES = 0x1e,
	PICKUPBOTS_BOSS_JUICE_COUNTER_MAX = 5,
};

static int PickupBots_IsBotWeaponReady(struct Driver *driver)
{
#if defined(CTR_NATIVE)
	// NOTE(aalhendi): Retail can read PS1 low memory when a boss-race
	// end-of-race rank slot is empty. Native treats that slot as no bot.
	if (driver == NULL)
	{
		return 0;
	}
#endif

	return ((driver->actionsFlagSet & ACTION_BOT) != 0) && ((driver->botData.botFlags & BOT_FLAG_DAMAGE_ACTIVE) == 0) &&
	       ((driver->actionsFlagSet & ACTION_RACE_FINISHED) == 0) && (driver->botData.weaponCooldown == 0) && (driver->instTntRecv == NULL) &&
	       (driver->clockReceive == 0);
}

static int PickupBots_IsCloseToPlayer(struct Driver *player, struct Driver *bot)
{
	int x = player->instSelf->matrix.t[0] - bot->instSelf->matrix.t[0];
	int z = player->instSelf->matrix.t[2] - bot->instSelf->matrix.t[2];

	return (u32)((x * x + z * z) - 0x90001) < 0x13affff;
}

static void PickupBots_SetCooldown(struct Driver *bot)
{
	bot->botData.weaponCooldown = (MixRNG_Scramble() & 0xff) + 0xf0;
}

static void PickupBots_PlayVoice(u32 voiceID, struct Driver *attacker, struct Driver *victim)
{
	Voiceline_RequestPlay(voiceID, data.characterIDs[attacker->driverID], data.characterIDs[victim->driverID]);
}

static void PickupBots_UpdateArcade(void)
{
	struct GameTracker *gGT = sdata->gGT;

	for (int i = 0; i < (u8)gGT->numPlyrCurrGame; i++)
	{
		struct Driver *player = gGT->drivers[i];

		if (player->driverRank != 0)
		{
			struct Driver *bot = gGT->driversInRaceOrder[player->driverRank - 1];

			if (PickupBots_IsBotWeaponReady(bot) && PickupBots_IsCloseToPlayer(player, bot))
			{
				int rng = MixRNG_Scramble() % 200;

				if (rng == 0)
				{
					int weaponID;

					if ((bot->lapIndex != 0) && (MixRNG_Scramble() % 100 < 0x32))
					{
						bot->numWumpas = 10;
					}

					if ((gGT->elapsedEventTime & 1) != 0)
					{
						bot->heldItemID = PICKUPBOTS_ITEM_TNT;

						if ((player->actionsFlagSet & ACTION_BOT) == 0)
						{
							PickupBots_PlayVoice(0xf, bot, player);
						}

						weaponID = PICKUPBOTS_ITEM_TNT;
					}
					else
					{
						bot->heldItemID = PICKUPBOTS_ITEM_POTION;

						if ((player->actionsFlagSet & ACTION_BOT) == 0)
						{
							PickupBots_PlayVoice(0xf, bot, player);
						}

						weaponID = PICKUPBOTS_ITEM_POTION;
					}

					VehPickupItem_ShootNow(bot, weaponID, 0);
					bot->numWumpas = 0;
					PickupBots_SetCooldown(bot);
				}
				else if (rng == 1)
				{
					bot->heldItemID = PICKUPBOTS_ITEM_BOMB;

					if ((player->actionsFlagSet & ACTION_BOT) == 0)
					{
						PickupBots_PlayVoice(10, bot, player);
					}

					VehPickupItem_ShootNow(bot, 2, 0);
					PickupBots_SetCooldown(bot);
				}
				else if (rng == 2)
				{
					bot->heldItemID = PICKUPBOTS_ITEM_MISSILE;

					if ((player->actionsFlagSet & ACTION_BOT) == 0)
					{
						PickupBots_PlayVoice(11, bot, player);
					}

					VehPickupItem_ShootNow(bot, 2, 0);
					PickupBots_SetCooldown(bot);
				}

				bot->heldItemID = PICKUPBOTS_ITEM_NONE;
			}
		}

		if (player->driverRank < 3)
		{
			struct Driver *bot = gGT->driversInRaceOrder[player->driverRank + 1];

			if (PickupBots_IsBotWeaponReady(bot) && (((u32)player->lapIndex < (s8)gGT->numLaps) || (player->distanceToFinish_curr > 16000)) &&
			    PickupBots_IsCloseToPlayer(player, bot))
			{
				int rng = MixRNG_Scramble() % 800;
				int weaponID = PICKUPBOTS_ITEM_NONE;

				if ((rng < 2) && (bot->lapIndex != (u8)((s8)gGT->numLaps - 1)))
				{
					weaponID = PICKUPBOTS_ITEM_MISSILE;
				}
				else if (rng < 4)
				{
					weaponID = PICKUPBOTS_ITEM_BOMB;
				}

				if (weaponID != PICKUPBOTS_ITEM_NONE)
				{
					bot->heldItemID = weaponID;

					if ((player->actionsFlagSet & ACTION_BOT) == 0)
					{
						PickupBots_PlayVoice(11, bot, player);
					}

					VehPickupItem_ShootNow(bot, 2, 0);
					PickupBots_SetCooldown(bot);
				}

				bot->heldItemID = PICKUPBOTS_ITEM_NONE;
			}
		}
	}
}

static void PickupBots_SetBossCooldown(struct MetaDataBOSS *bossMeta)
{
	struct GameTracker *gGT = sdata->gGT;

#ifdef CTR_AP
	// #169 tier 3: boss_loss_easing off keeps a composed kit's pressure the
	// same on every try; the retail per-loss bonus applies everywhere else.
	int lossBonus = AP_BossLossEasingNow() ? ((s8)sdata->advProgress.timesLostBossRace[gGT->bossID] * 4) : 0;
	// J03: a gauntlet leg is no garage race and has no loss count.
	if (AP_BossGauntletAttacker() >= 0)
		lossBonus = 0;
	sdata->bossWeaponCooldown =
	    (RngDeadCoed(&sdata->advRng) & 0x10) + bossMeta->weaponCooldown + 0xc + lossBonus;
#else
	sdata->bossWeaponCooldown =
	    (RngDeadCoed(&sdata->advRng) & 0x10) + bossMeta->weaponCooldown + 0xc + ((s8)sdata->advProgress.timesLostBossRace[gGT->bossID] * 4);
#endif
}

static struct MetaDataBOSS *PickupBots_GetInitialBossMeta(void)
{
	struct GameTracker *gGT = sdata->gGT;

#ifdef CTR_AP
	{
		struct MetaDataBOSS *venueMeta = AP_BossVenueAttackSchedule(0);
		if (venueMeta != NULL)
			return venueMeta;
	}
#endif

#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
	if (CustomTrack_OxideFinalServing((int)gGT->levelID, gGT->bossID,
	                                  (gGT->gameMode1 & ADVENTURE_BOSS) != 0))
		return s_cortexVortexOxideWeapons;
#endif

	if (gGT->levelID == OXIDE_STATION)
	{
		return data.bossWeaponMetaPtr[0];
	}

	return data.bossWeaponMetaPtr[data.metaDataLEV[gGT->levelID].hubID];
}

static void PickupBots_AdvanceBossMeta(struct Driver *boss)
{
	struct GameTracker *gGT = sdata->gGT;
	struct MetaDataBOSS *bossMeta = sdata->bossWeaponMeta;
	struct MetaDataBOSS *nextMeta = &bossMeta[1];

	if (nextMeta->throwFlag == 0)
	{
		int threshold = gGT->level1->ptr_restart_points[bossMeta->trackCheckpoint].distToFinish << 3;

		if (threshold < (int)boss->distanceToFinish_curr)
		{
			int preservedThrow = -1;

			if (((bossMeta->weaponType == BOSS_WEAPON_ENCODED_POTION) || (bossMeta->weaponType == BOSS_WEAPON_ENCODED_TNT)) &&
			    (sdata->bossJuiceCounter == PICKUPBOTS_BOSS_JUICE_COUNTER_MAX))
			{
				preservedThrow = bossMeta->throwFlag;
			}

			bossMeta = PickupBots_GetInitialBossMeta();

			if (preservedThrow != -1)
			{
				bossMeta->throwFlag = preservedThrow;
			}
		}
	}
	else
	{
		int threshold = gGT->level1->ptr_restart_points[nextMeta->trackCheckpoint].distToFinish << 3;

		if ((int)boss->distanceToFinish_curr < threshold)
		{
			int preservedThrow = -1;

			if (((bossMeta->weaponType == BOSS_WEAPON_ENCODED_POTION) || (bossMeta->weaponType == BOSS_WEAPON_ENCODED_TNT)) &&
			    (sdata->bossJuiceCounter == PICKUPBOTS_BOSS_JUICE_COUNTER_MAX))
			{
				preservedThrow = bossMeta->throwFlag;
			}

			bossMeta = nextMeta;

			if (preservedThrow != -1)
			{
				bossMeta->throwFlag = preservedThrow;
			}
		}
	}

	sdata->bossWeaponMeta = bossMeta;
}

static void PickupBots_UpdateBossPathRequest(struct Driver *boss)
{
	if (sdata->bossWeaponMeta->pathChangeDisabled != 0)
	{
		return;
	}

#ifdef CTR_AP
	// J03: the boss lane requests belong to the garage boss race only; a
	// gauntlet attacker keeps its ordinary race lanes.
	if ((sdata->gGT->gameMode1 & ADVENTURE_BOSS) == 0)
	{
		return;
	}
#endif

	if (sdata->bossPathRequestTimer == PICKUPBOTS_BOSS_PATH_REQUEST_FRAMES)
	{
		if ((boss->botData.botFlags & BOT_FLAG_BOSS_PATH_ACTIVE) != 0)
		{
			return;
		}

		if (sdata->bossPathRequestPhase == 0)
		{
			if (boss->botData.botPath == 2)
			{
				boss->botData.desiredPath_BossOnly = 1;
				sdata->bossPathRequestTimer = 0;
				boss->botData.botFlags |= BOT_FLAG_BOSS_PATH_REQUESTED;
			}
			else if (boss->botData.botPath == 1)
			{
				boss->botData.desiredPath_BossOnly = 0;
				sdata->bossPathRequestTimer = 0;
				sdata->bossPathRequestPhase = boss->botData.botPath;
				boss->botData.botFlags |= BOT_FLAG_BOSS_PATH_REQUESTED;
			}
		}
		else
		{
			if (boss->botData.botPath == 0)
			{
				boss->botData.desiredPath_BossOnly = 1;
				sdata->bossPathRequestTimer = 0;
				boss->botData.botFlags |= BOT_FLAG_BOSS_PATH_REQUESTED;
			}
			else if (boss->botData.botPath == 1)
			{
				boss->botData.desiredPath_BossOnly = 2;
				sdata->bossPathRequestTimer = 0;
				sdata->bossPathRequestPhase = 0;
				boss->botData.botFlags |= BOT_FLAG_BOSS_PATH_REQUESTED;
			}
		}
	}
	else if ((boss->botData.botFlags & BOT_FLAG_BOSS_PATH_REQUESTED) == 0)
	{
		sdata->bossPathRequestTimer++;
	}
}

static int PickupBots_GetBossWeaponID(struct MetaDataBOSS *bossMeta)
{
	int weaponID = bossMeta->weaponType;

	if (weaponID == BOSS_WEAPON_ENCODED_TNT)
	{
		weaponID = PICKUPBOTS_ITEM_TNT;
	}
	else if (weaponID == BOSS_WEAPON_ENCODED_BOMB)
	{
		weaponID = PICKUPBOTS_ITEM_BOMB;
	}
	else if (weaponID == BOSS_WEAPON_ENCODED_POTION)
	{
		weaponID = PICKUPBOTS_ITEM_POTION;
	}
	else if (weaponID == BOSS_WEAPON_NONE)
	{
		weaponID = PICKUPBOTS_ITEM_INVALID;
	}

	return weaponID;
}

static int PickupBots_UpdateBossJuice(struct MetaDataBOSS *bossMeta, int weaponID)
{
	u16 juiceFlag = bossMeta->juiceFlag;

	if ((juiceFlag & BOSS_WEAPON_RANDOM_JUICE) == 0)
	{
		sdata->bossJuiceCounter = 0;
		return weaponID;
	}

	if (sdata->bossJuiceCounter < PICKUPBOTS_BOSS_JUICE_COUNTER_MAX)
	{
		sdata->bossJuiceCounter++;
		return weaponID;
	}

	if (bossMeta->weaponType == BOSS_WEAPON_ENCODED_TNT)
	{
		weaponID = PICKUPBOTS_ITEM_TNT;

		if (bossMeta->throwFlag != BOSS_WEAPON_NORMAL)
		{
			bossMeta->throwFlag = BOSS_WEAPON_NORMAL;
			sdata->bossJuiceCounter = PICKUPBOTS_BOSS_JUICE_COUNTER_MAX;
			bossMeta->juiceFlag = juiceFlag | BOSS_WEAPON_JUICED;
			return weaponID;
		}
	}
	else if (bossMeta->weaponType == BOSS_WEAPON_ENCODED_BOMB)
	{
		weaponID = PICKUPBOTS_ITEM_BOMB;

		if ((juiceFlag & BOSS_WEAPON_JUICED) == 0)
		{
			bossMeta->juiceFlag = juiceFlag | BOSS_WEAPON_JUICED;
			sdata->bossJuiceCounter = PICKUPBOTS_BOSS_JUICE_COUNTER_MAX;
			return PICKUPBOTS_ITEM_TNT;
		}

		bossMeta->juiceFlag = juiceFlag & ~BOSS_WEAPON_JUICED;
		sdata->bossJuiceCounter = 0;
		return weaponID;
	}
	else if (bossMeta->weaponType == BOSS_WEAPON_ENCODED_POTION)
	{
		weaponID = PICKUPBOTS_ITEM_POTION;

		if (bossMeta->throwFlag != BOSS_WEAPON_NORMAL)
		{
			bossMeta->throwFlag = BOSS_WEAPON_NORMAL;
			sdata->bossJuiceCounter = PICKUPBOTS_BOSS_JUICE_COUNTER_MAX;
			bossMeta->juiceFlag |= BOSS_WEAPON_JUICED;
			return weaponID;
		}
	}
	else
	{
		return weaponID;
	}

	bossMeta->throwFlag = BOSS_WEAPON_THROW;
	sdata->bossJuiceCounter = 0;
	bossMeta->juiceFlag &= ~BOSS_WEAPON_JUICED;
	return weaponID;
}

#ifdef CTR_AP
// ── J03 boss adapter: the advanced families (decision 6, Wayfinder D2) ──────
// Bots never run VehPhysProc, so the timers it keeps for a human (mask flag,
// invisibility, super engine) are kept here for the one driver the adapter
// armed. The decisions are ap/ap_boss_adapter.h.

// The schedule row the boss is on and the last real row, from the live table.
static void PickupBots_AdapterSegment(struct MetaDataBOSS *bossMeta, int *segment, int *lastSegment)
{
	struct MetaDataBOSS *base = PickupBots_GetInitialBossMeta();
	int i;

	*segment = 0;
	*lastSegment = 0;
	if (base == NULL)
		return;
	for (i = 0; i < 16 && base[i].throwFlag != 0; i++)
		*lastSegment = i;
	if (bossMeta >= base && bossMeta - base < 16)
		*segment = (int)(bossMeta - base);
}

// Is a mask (Aku or Uka) attached to the driver and alive?
static int PickupBots_AdapterMaskAlive(struct Driver *d)
{
	struct Thread *t;

	if (d->instSelf == NULL || d->instSelf->thread == NULL)
		return 0;
	for (t = d->instSelf->thread->childThread; t != NULL; t = t->siblingThread)
		if (((u32)(t->modelIndex - STATIC_AKUAKU) < 2) && (t->flags & THREAD_FLAG_DEAD) == 0)
			return 1;
	return 0;
}

static struct MaskHeadWeapon *PickupBots_AdapterMaskObject(struct Driver *d)
{
	struct Thread *t;

	if (d->instSelf == NULL || d->instSelf->thread == NULL)
		return NULL;
	for (t = d->instSelf->thread->childThread; t != NULL; t = t->siblingThread)
		if (((u32)(t->modelIndex - STATIC_AKUAKU) < 2) && (t->flags & THREAD_FLAG_DEAD) == 0)
			return (struct MaskHeadWeapon *)t->object;
	return NULL;
}

static void PickupBots_AdapterEndInvisibility(struct Driver *d)
{
	if (d->invisibleTimer == 0)
		return;
	d->invisibleTimer = 0;
	d->instSelf->flags = d->instFlagsBackup;
	d->instSelf->alphaScale = 0;
	OtherFX_Play(0x62, 1);
}

// End every self-state of the armed driver: finish, race over, race exit.
// The shield needs nothing: its own thread pops it when the driver finishes
// (RB_ShieldDark_ThTick_Grow), and a reload frees it with the level.
static void PickupBots_AdapterEndAll(struct Driver *d)
{
	struct MaskHeadWeapon *mask;

	PickupBots_AdapterEndInvisibility(d);
	d->superEngineTimer = 0;
	s_apBossAdapter.superMs = 0;
	if (s_apBossAdapter.maskOn)
	{
		mask = PickupBots_AdapterMaskObject(d);
		if (mask != NULL)
			mask->duration = 0; // RB_MaskWeapon_ThTick fades it away
		d->actionsFlagSet &= ~ACTION_MASK_WEAPON;
		s_apBossAdapter.maskOn = 0;
	}
	d->invincibleTimer = 0;
}

// Fire one advanced segment (called with the boss's cooldown already reset).
static void PickupBots_FireAdvanced(struct Driver *boss, struct Driver *player, struct MetaDataBOSS *bossMeta)
{
	struct GameTracker *gGT = sdata->gGT;
	AP_BossAdapterFacts f;
	int decision;
	u8 oldWumpa;

	PickupBots_AdapterSegment(bossMeta, &f.segment, &f.lastSegment);
	f.weapon = bossMeta->weaponType;
	f.lap = boss->lapIndex;
	f.numLaps = (s8)gGT->numLaps;
	AP_BossAdapterEnter(&s_apBossAdapter, f.lap, f.segment);
	if (s_apBossAdapter.used)
		return;
	// Only one driver is armed per race.
	if (s_apBossAdapterDriver != NULL && s_apBossAdapterDriver != boss)
		return;
	f.bossRank = boss->driverRank;
	f.playerRank = player->driverRank;
	f.playerClocked = player->clockReceive != 0;
	f.warpOrbInPlay = (gGT->gameMode1 & WARPBALL_HELD) != 0;
	f.shieldHeld = boss->instBubbleHold != NULL;
	f.maskActive = s_apBossAdapter.maskOn && PickupBots_AdapterMaskAlive(boss);
	f.invisible = boss->invisibleTimer != 0;
	f.superEngine = boss->superEngineTimer != 0;
	f.superAllowed = AP_BossFinalComposedNow();
	decision = AP_BossAdapterDecide(&s_apBossAdapter, &f);
	AP_BossAdapterCommit(&s_apBossAdapter, &f, decision);
	if (decision != AP_BA_FIRE)
		return;
	s_apBossAdapterDriver = boss;

	// The juice bit picks the retail juiced duration (invisibility only).
	oldWumpa = boss->numWumpas;
	boss->numWumpas = ((bossMeta->juiceFlag & BOSS_WEAPON_JUICED) != 0) ? 10 : 0;
	boss->heldItemID = (u8)f.weapon;

	switch (f.weapon)
	{
	case AP_BA_MASK:
	{
		// Retail MaskUseWeapon picks Aku or Uka from the racer's side and is
		// silent for a bot; its activation sound is the telegraph here.
		int goodGuy = VehPickupItem_MaskBoolGoodGuy(boss);
		OtherFX_Play((STATIC_UKAUKA - goodGuy) + 0x1A, 1);
		VehPickupItem_ShootNow(boss, f.weapon, 0);
		if (PickupBots_AdapterMaskAlive(boss))
			boss->actionsFlagSet |= ACTION_MASK_WEAPON;
		else
			s_apBossAdapter.maskOn = 0; // pool full: no mask, nothing to keep
		break;
	}
	default:
		// turbo, shield, clock, warp orb, invisibility, super engine: the retail
		// item with its own retail cue (fire, bubble, clock, orb, fade sound);
		// decision 6 asks no extra warning for the clock.
		VehPickupItem_ShootNow(boss, f.weapon, 0);
		break;
	}

	boss->heldItemID = PICKUPBOTS_ITEM_NONE;
	boss->numWumpas = oldWumpa;
}

// Every unpaused race frame: keep and expire the armed driver's self-states.
static void PickupBots_AdapterTick(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d = s_apBossAdapterDriver;
	int ms, rule, timer;
	int live = 0, i;

	if (!s_apBossAdapter.armed || d == NULL)
		return;
	for (i = 0; i < 8; i++)
		if (gGT->drivers[i] == d)
			live = 1;
	if (!live || d->instSelf == NULL)
	{
		// The driver left the race (exit or reload): nothing to restore.
		AP_BossAdapterReset(&s_apBossAdapter);
		s_apBossAdapterDriver = NULL;
		return;
	}
	ms = gGT->elapsedTimeMS;
	rule = AP_BossAdapterEndRule((d->actionsFlagSet & ACTION_RACE_FINISHED) != 0,
	                             (gGT->gameMode1 & END_OF_RACE) != 0,
	                             (d->botData.botFlags & BOT_FLAG_DAMAGE_ACTIVE) != 0);
	if (rule == AP_BA_END_ALL)
	{
		PickupBots_AdapterEndAll(d);
		return;
	}
	if (rule == AP_BA_END_INVISIBILITY)
		PickupBots_AdapterEndInvisibility(d);

	// Invisibility: retail countdown and restore (VehPhysProc for a human).
	if (d->invisibleTimer != 0)
	{
		timer = d->invisibleTimer;
		if (AP_BossAdapterCountdown(&timer, ms))
		{
			d->invisibleTimer = 1;
			PickupBots_AdapterEndInvisibility(d);
		}
		else
			d->invisibleTimer = timer;
	}

	// Super engine: retail 0.12 s reserves top-up while the timer runs.
	if (d->superEngineTimer != 0)
	{
		timer = d->superEngineTimer;
		AP_BossAdapterCountdown(&timer, ms);
		d->superEngineTimer = (s16)timer;
		if (d->superEngineTimer != 0 && d->botData.aiPhysics.speedLinear > 0)
			VehFire_Increment(d, 120, (TURBO_PAD | SUPER_ENGINE), 0x80);
	}

	// Mask: invincible (PickupBots_AdapterProtect), damages on contact
	// (ACTION_MASK_WEAPON, VehPhysCrash_Attack) and faster (single turbo
	// reserves while it lasts). The mask thread owns the retail duration.
	if (s_apBossAdapter.maskOn)
	{
		if (PickupBots_AdapterMaskAlive(d))
		{
			d->actionsFlagSet |= ACTION_MASK_WEAPON;
			if (d->reserves < 0x40)
				VehFire_Increment(d, 0x78, 1, 0x80);
		}
		else
		{
			d->actionsFlagSet &= ~ACTION_MASK_WEAPON;
			s_apBossAdapter.maskOn = 0;
		}
	}

	// Shield pop invincibility (retail VehPickState sets 0x2a0 ms).
	if (d->invincibleTimer != 0)
	{
		timer = d->invincibleTimer;
		AP_BossAdapterCountdown(&timer, ms);
		d->invincibleTimer = timer;
	}
}

// RB_Hazard_HurtDriver, bot branch: retail VehPickState protection for the
// armed driver only. Returns 1 when the hit is absorbed (no damage, no Hit
// check, no DeathLink).
int PickupBots_AdapterProtect(struct Driver *victim)
{
	if (!s_apBossAdapter.armed || victim == NULL || victim != s_apBossAdapterDriver)
		return 0;
	if (victim->kartState == KS_MASK_GRABBED)
		return 0;
	if ((s_apBossAdapter.maskOn && (victim->actionsFlagSet & ACTION_MASK_WEAPON) != 0) || victim->invincibleTimer != 0)
	{
		Voiceline_RequestPlay(2, data.characterIDs[victim->driverID], 0x10);
		return 1;
	}
	if (victim->instBubbleHold != NULL)
	{
		struct Shield *shieldObj = victim->instBubbleHold->thread->object;
		shieldObj->flags |= 1;
		victim->invincibleTimer = 0x2a0;
		victim->instBubbleHold = NULL;
		Voiceline_RequestPlay(2, data.characterIDs[victim->driverID], 0x10);
		return 1;
	}
	return 0;
}

// The armed gauntlet leg's attacker: the AI racing the leg's drawn racer,
// else the first AI.
static struct Driver *PickupBots_GauntletAttacker(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int character = AP_BossGauntletAttacker();
	int i;

	if (character < 0)
		return NULL;
	for (i = 1; i < 8; i++)
	{
		struct Driver *d = gGT->drivers[i];
		if (d != NULL && (d->actionsFlagSet & ACTION_BOT) != 0 && data.characterIDs[d->driverID] == character)
			return d;
	}
	if (gGT->drivers[1] != NULL && (gGT->drivers[1]->actionsFlagSet & ACTION_BOT) != 0)
		return gGT->drivers[1];
	return NULL;
}
#endif

static void PickupBots_UpdateBoss(struct Driver *boss)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *player = gGT->drivers[0];
	struct MetaDataBOSS *bossMeta = sdata->bossWeaponMeta;

	if (((boss->botData.botFlags & BOT_FLAG_DAMAGE_ACTIVE) != 0) || ((boss->actionsFlagSet & ACTION_RACE_FINISHED) != 0) || (boss->instTntRecv != NULL) ||
	    (boss->clockReceive != 0) || (boss->botData.aiPhysics.speedLinear < 0x1f41))
	{
		PickupBots_SetBossCooldown(bossMeta);
		return;
	}

	PickupBots_AdvanceBossMeta(boss);
	bossMeta = sdata->bossWeaponMeta;

	PickupBots_UpdateBossPathRequest(boss);

	if (sdata->bossWeaponCooldown > 0)
	{
		sdata->bossWeaponCooldown--;
		return;
	}

	PickupBots_SetBossCooldown(bossMeta);

#ifdef CTR_AP
	// J03: the advanced families go through the boss adapter, never the
	// projectile branch below. Retail tables never carry these codes.
	if (AP_BossAdapterIsAdvanced(bossMeta->weaponType))
	{
		PickupBots_FireAdvanced(boss, player, bossMeta);
		return;
	}
#endif

	int weaponID = PickupBots_UpdateBossJuice(bossMeta, PickupBots_GetBossWeaponID(bossMeta));
	int throwFlag = bossMeta->throwFlag;
	int weaponFlags = (throwFlag == BOSS_WEAPON_THROW);

	if (weaponID >= 0)
	{
		u8 oldWumpa = boss->numWumpas;
		boss->numWumpas = ((bossMeta->juiceFlag & BOSS_WEAPON_JUICED) != 0) ? 10 : 0;
		boss->heldItemID = weaponID;

		if ((u16)(weaponID - PICKUPBOTS_ITEM_TNT) < 2)
		{
			PickupBots_PlayVoice(0xf, boss, player);
		}
		else
		{
			weaponFlags |= 2;
			PickupBots_PlayVoice(10, boss, player);
		}

		if (boss->heldItemID == PICKUPBOTS_ITEM_BOMB)
		{
			VehPickupItem_ShootNow(boss, 2, (s16)weaponFlags);
		}
#ifdef CTR_AP
		// #169: Oxide's double potion belongs to Oxide, not to a garage boss
		// whose drawn venue is Oxide Station.
		else if ((boss->heldItemID == PICKUPBOTS_ITEM_POTION) && (weaponFlags == 1) &&
		         ((gGT->gameMode1 & ADVENTURE_BOSS) != 0) &&
		         (AP_BossRetailRulesLevel(gGT->levelID) == OXIDE_STATION))
#else
		else if ((boss->heldItemID == PICKUPBOTS_ITEM_POTION) && (weaponFlags == 1) && (gGT->levelID == OXIDE_STATION))
#endif
		{
			VehPickupItem_ShootNow(boss, weaponID, 1);
			VehPickupItem_ShootNow(boss, weaponID, 1);
		}
		else
		{
			VehPickupItem_ShootNow(boss, weaponID, (s16)weaponFlags);

			if ((boss->heldItemID == PICKUPBOTS_ITEM_TNT) && (bossMeta->throwFlag == BOSS_WEAPON_NORMAL) &&
			    (sdata->bossJuiceCounter != PICKUPBOTS_BOSS_JUICE_COUNTER_MAX))
			{
				sdata->bossJuiceCounter = PICKUPBOTS_BOSS_JUICE_COUNTER_MAX;
			}
		}

		boss->heldItemID = PICKUPBOTS_ITEM_NONE;
		boss->numWumpas = oldWumpa;
	}
}

void PickupBots_Update(void)
{
	// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800408b8-0x800414f4.
	struct GameTracker *gGT = sdata->gGT;

#ifdef CTR_AP
	// J03: the boss adapter's self-state timers run every unpaused race frame,
	// before any early return, so expiry and cleanup never wait on a weapon.
	PickupBots_AdapterTick();
#endif

	if (((u8)gGT->numBotsNextGame == 0) || (gGT->elapsedEventTime < 0x4b00))
	{
		if (gGT->gameMode1 >= 0)
		{
			return;
		}

		if (gGT->elapsedEventTime < 0x12c0)
		{
			return;
		}
	}

	if ((gGT->gameMode1 & (ADVENTURE_BOSS | END_OF_RACE)) != ADVENTURE_BOSS)
	{
		if ((u8)gGT->numPlyrCurrGame == 0)
		{
			return;
		}

		PickupBots_UpdateArcade();
#ifdef CTR_AP
		// J03: an armed Purple Gem Cup gauntlet leg adds one boss-style
		// attacker on top of the ordinary AI weapons.
		if ((gGT->gameMode1 & END_OF_RACE) == 0)
		{
			struct Driver *attacker = PickupBots_GauntletAttacker();
			if (attacker != NULL)
				PickupBots_UpdateBoss(attacker);
		}
#endif
		return;
	}

	PickupBots_UpdateBoss(gGT->drivers[1]);
}
