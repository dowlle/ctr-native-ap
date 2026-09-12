#include <common.h>

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

	// get hubID of level
	hub = data.metaDataLEV[lev].hubID;

	// If Level ID is Oxide Station
	if (lev == OXIDE_STATION)
	{
		hub = 0;
	}

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

	sdata->bossWeaponCooldown =
	    (RngDeadCoed(&sdata->advRng) & 0x10) + bossMeta->weaponCooldown + 0xc + ((s8)sdata->advProgress.timesLostBossRace[gGT->bossID] * 4);
}

static struct MetaDataBOSS *PickupBots_GetInitialBossMeta(void)
{
	struct GameTracker *gGT = sdata->gGT;

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

static void PickupBots_UpdateBoss(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *boss = gGT->drivers[1];
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
		else if ((boss->heldItemID == PICKUPBOTS_ITEM_POTION) && (weaponFlags == 1) && (gGT->levelID == OXIDE_STATION))
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
		return;
	}

	PickupBots_UpdateBoss();
}
