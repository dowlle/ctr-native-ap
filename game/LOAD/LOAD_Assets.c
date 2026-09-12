#include <common.h>

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
#include <platform/native_checkpoint.h>
#endif

#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h>
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800326b4-0x80032700.
void LOAD_RunPtrMap(char *origin, int *patchArr, int numPtrs)
{
	int *ptrCurrOffset = patchArr;

	for (ptrCurrOffset = &patchArr[0]; ptrCurrOffset < &patchArr[numPtrs]; ptrCurrOffset++)
	{
		int offset = (*ptrCurrOffset >> 2) << 2;
		*(int *)&origin[offset] = *(int *)&origin[offset] + (int)origin;
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
		NativeCheckpoint_RegisterPointerSlot(&origin[offset]);
#endif
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80032700-0x800327dc.
void LOAD_Robots2P(struct BigHeader *bigfile, int p1, int p2, void (*callback)(struct LoadQueueSlot *))
{
	int i;
	char *robotSet;
	b32 boolFoundRepeat = false;

	// 8 sets, but only check 7 cause
	// the last is Gem Cups pack (4 bosses)
	for (i = 0; i < 7; i++)
	{
		robotSet = &data.characterIDs_2P_AIs[4 * i];

		boolFoundRepeat = false;
		for (int j = 0; j < 4; j++)
		{
			if ((robotSet[j] == p1) || (robotSet[j] == p2))
			{
				boolFoundRepeat = true;
				break;
			}
		}

		if (!boolFoundRepeat)
		{
			break;
		}
	}

	if (i > 6)
	{
		return;
	}

	data.characterIDs[2] = robotSet[0];
	data.characterIDs[3] = robotSet[1];
	data.characterIDs[4] = robotSet[2];
	data.characterIDs[5] = robotSet[3];

	LOAD_AppendQueue(bigfile, LT_GETADDR, BI_2PARCADEPACK + i, NULL, callback);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800327dc-0x8003282c.
void LOAD_Robots1P(int characterID)
{
	int newCharacterID = 0;

	data.characterIDs[0] = characterID;

	for (int i = 1; i < 8; i++, newCharacterID++)
	{
		if (newCharacterID == characterID)
		{
			newCharacterID++;
		}

		data.characterIDs[i] = newCharacterID;
	}
}

static void (*const LOAD_DriverMPK_SetPointer)(struct LoadQueueSlot *) = (void (*)(struct LoadQueueSlot *))-2;

#ifdef CTR_AP
// Hit Character encounters (ticket 06). The engine ids whose HI models were
// sideloaded for this ordinary load, so stage 6 can refuse a required model that
// did not actually load before VehBirth ever dereferences it. Reset per load.
// The production roster array is sized by AP_HIT_FIELD_MAX; the field is P1 plus
// at most seven AI, so this pins the loader to the corrected bound.
CTR_STATIC_ASSERT(AP_HIT_FIELD_MAX == 7);
static ap_hit_extras_state s_hitExtras;

void LOAD_HitEncounterResetExtras(void)
{
	AP_HitExtrasResetPure(&s_hitExtras);
}

// Called from LOAD_TenStages stage 6, after fileBase->model conversion and
// before any birth. A required extra whose model is still NULL is an
// unrecoverable load failure: name it and terminate cleanly rather than let
// VehBirth_GetModelByName return NULL into a blind dereference.
void LOAD_HitEncounterValidateExtras(void)
{
	const void *models[3];
	int missing;
	int i;

	for (i = 0; i < 3; i++)
		models[i] = data.driverModelExtras[i].model;

	missing = AP_HitExtrasFirstMissingPure(&s_hitExtras, models);
	if (missing >= 0)
	{
		char msg[160];
		snprintf(msg, sizeof msg,
		         "Required driver model for character %d failed to load.",
		         s_hitExtras.ids[missing]);
		Platform_Fatal("CTR Native - Hit Character", msg);
	}
}
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003282c-0x80032b50.
int LOAD_DriverMPK(struct BigHeader *bigfile, int levelLOD, void (*callback)(struct LoadQueueSlot *))
{
	int i;
	int gameMode1;

	struct GameTracker *gGT = sdata->gGT;
	gameMode1 = gGT->gameMode1;

	int lastFileIndexMPK;

#ifdef CTR_AP
	// Every load entry clears the previous load's Hit-encounter extras, not just
	// the ordinary branch. A hub/menu/boss load that skips the branch below must
	// not leave stale required-model ids for stage 6 to validate (ticket 06).
	LOAD_HitEncounterResetExtras();
#endif

	// 3P/4P
	if (levelLOD - 3U < 2)
	{
		// disable_split_screen_lod (config.ini, see platform/native_config.c): load
		// the hi-res character model in 3-4P split screen instead of the low LOD.
		int racerModel = g_config.disableSplitScreenLod ? BI_RACERMODELHI : BI_RACERMODELLOW;
		for (i = 0; i < 3; i++)
		{
			// low lod CTR model
			LOAD_AppendQueue(bigfile, LT_GETADDR, racerModel + data.characterIDs[i], &data.driverModelExtras[i].fileBase, LOAD_DriverMPK_SetPointer);
		}

		// load 4P MPK of fourth player
		lastFileIndexMPK = BI_4PARCADEPACK + data.characterIDs[3];
	}

	else if (levelLOD == 1)
	{
		if ((gameMode1 & (TIME_TRIAL | MAIN_MENU)) == TIME_TRIAL)
		{
			goto LoadHighAndPack;
		}

		if (
		    // adv/cutscene mpk when we just need text from MPK
		    ((gameMode1 & (GAME_CUTSCENE | ADVENTURE_ARENA)) != 0) ||

		    // credits
		    ((gGT->gameMode2 & CREDITS) != 0) ||

		    // adventure character select
		    (gGT->levelID == ADVENTURE_GARAGE))
		{
			lastFileIndexMPK = BI_ADVENTUREPACK + data.characterIDs[0];
			goto QueueLastPack;
		}

		if ((gameMode1 & ADVENTURE_BOSS) != 0)
		{
			goto LoadHighAndPack;
		}

		if (
		    // If you are in Adventure cup
		    ((gameMode1 & ADVENTURE_CUP) != 0) &&

		    // purple gem cup
		    (gGT->cup.cupID == 4)

#ifdef CTR_CUSTOM_TRACKS
		    // ... unless this load IS the event race. A displaced cup keeps
		    // cupID 4 because the Gem hangs off that identity, which is the only
		    // reason it would otherwise inherit the four-boss field below. Falling
		    // through to the ordinary 1P arcade branch instead swaps the boss pack
		    // for the player's own arcade pack, and that pack is what makes a
		    // shuffled field possible at all -- see decision 11.
		    //
		    // Same predicate as the subfile override and the arena sizing, so the
		    // roster a race gets, the arena it is given and the bytes it is served
		    // can never disagree about which race this is. All three facts it
		    // reads are committed before the ten-stage loader is armed, and this
		    // function is called from stage 6.
		    && !CustomTrack_ServingLoad((int)gGT->levelID, 1, gGT->cup.cupID)
#endif
		)
		{
			// high lod model
			LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELHI + data.characterIDs[0], &data.driverModelExtras[0].fileBase, LOAD_DriverMPK_SetPointer);

			// pack of four AIs with bosses
			LOAD_AppendQueue(bigfile, LT_GETADDR, BI_2PARCADEPACK + 7, NULL, callback);

			data.characterIDs[1] = RIPPER_ROO;
			data.characterIDs[2] = PAPU_PAPU;
			data.characterIDs[3] = KOMODO_JOE;
			data.characterIDs[4] = PINSTRIPE;

			return sdata->ptrMPK;
		}

		if ((gameMode1 & (TIME_TRIAL | MAIN_MENU)) != MAIN_MENU)
		{
			LOAD_Robots1P(data.characterIDs[0]);

#ifdef CTR_CUSTOM_TRACKS
			// The event race, and only it, races a shuffled field. This PERMUTES
			// what LOAD_Robots1P just wrote rather than choosing from the roster,
			// which is what makes it safe without checking anything: an AI's model
			// is resolved by name against the single pack queued below
			// (VehBirth_GetModelByName), so an id outside that pack is a
			// null-pointer crash, not a wrong model. Every permutation of
			// LOAD_Robots1P's seven ids is still seven distinct ids, still none of
			// them the player's, still all of them in that pack.
			//
			// Placed after the LOAD_Robots1P call rather than inside it because
			// AH_WarpPad.c calls that function every hub frame for the grid order,
			// and a shuffle there would be overwritten by this load anyway.
			if (CustomTrack_ServingLoad((int)gGT->levelID, (gameMode1 & ADVENTURE_CUP) != 0, gGT->cup.cupID))
			{
				int rosterIDs[CTR_CT_ROBOT_SLOTS];
				int rosterDraws[CTR_CT_ROBOT_SLOTS - 1];

				for (i = 0; i < CTR_CT_ROBOT_SLOTS; i++)
					rosterIDs[i] = data.characterIDs[CTR_CT_ROBOT_FIRST_SLOT + i];

				// The adventure stream, which is what AH_WarpPad.c's grid shuffle
				// already draws from, so this consumes an RNG the race path is
				// already known to perturb rather than introducing a new one.
				for (i = 0; i < CTR_CT_ROBOT_SLOTS - 1; i++)
					rosterDraws[i] = RngDeadCoed(&sdata->advRng);

				CustomTrackPolicy_PermuteRoster(rosterIDs, CTR_CT_ROBOT_SLOTS, rosterDraws);

				for (i = 0; i < CTR_CT_ROBOT_SLOTS; i++)
					data.characterIDs[CTR_CT_ROBOT_FIRST_SLOT + i] = (s16)rosterIDs[i];

				CustomTrack_Log("[CustomTracks] event race field: %d %d %d %d (of %d candidates)\n",
				                (int)data.characterIDs[1], (int)data.characterIDs[2], (int)data.characterIDs[3],
				                (int)data.characterIDs[4], CTR_CT_ROBOT_SLOTS);
			}
#endif

#ifdef CTR_AP
			// Hit Character encounters (ticket 06): replace the stock default
			// field with the seed's resolved encounter roster, but ONLY for the
			// slice -- Fake Crash on Crash Cove, single-player ordinary Adventure
			// Trophy. Custom-served loads are excluded (their own permute owns the
			// field), as are boss/cup/trial/arcade/relic/token/crystal and
			// multiplayer loads. The player's arcade pack is built around the
			// stock set LOAD_Robots1P just wrote, so any selected opponent
			// outside it needs a BI_RACERMODELHI extra.
			{
				int hitApply = AP_HitEncounterShouldApply(
				    (gameMode1 & ADVENTURE_MODE) != 0,
				    (int)gGT->levelID,
				    (gameMode1 & ADVENTURE_CUP) != 0,
				    IS_BOSS_RACE(gameMode1),
				    (gameMode1 & ARCADE_MODE) != 0,
				    (gameMode1 & RELIC_RACE) != 0,
				    (gGT->gameMode2 & TOKEN_RACE) != 0,
				    (gameMode1 & CRYSTAL_CHALLENGE) != 0,
				    (int)gGT->numPlyrNextGame);
#ifdef CTR_CUSTOM_TRACKS
				if (CustomTrack_ServingLoad((int)gGT->levelID,
				                            (gameMode1 & ADVENTURE_CUP) != 0,
				                            gGT->cup.cupID))
					hitApply = 0;
#endif
				if (hitApply)
				{
					int hitRoster[AP_HIT_FIELD_MAX];
					int hitExtras[3];
					int hitCount, hitNeed, k;

					hitCount = AP_HitEncounterBuildField((int)gGT->levelID,
					                                     (int)data.characterIDs[0], 7,
					                                     hitRoster);
					for (k = 0; k < hitCount; k++)
						data.characterIDs[1 + k] = (s16)hitRoster[k];

					hitNeed = AP_HitEncounterExtras(hitRoster, hitCount,
					                                (int)data.characterIDs[0],
					                                hitExtras, 3);
					if (hitNeed > 3)
						Platform_Fatal("CTR Native - Hit Character",
						               "Hit encounter needs more driver models than the loader can sideload.");
					// The pack is queued after the extras; LOAD_AppendQueue drops
					// silently past its eight slots, so refuse up front rather
					// than race with a missing opponent model.
					if (!AP_HitQueueFitsPure((int)sdata->queueLength, hitNeed + 1, 8))
						Platform_Fatal("CTR Native - Hit Character",
						               "Hit encounter driver models do not fit the load queue.");
					for (k = 0; k < hitNeed; k++)
					{
						LOAD_AppendQueue(bigfile, LT_GETADDR,
						                 BI_RACERMODELHI + hitExtras[k],
						                 &data.driverModelExtras[k].fileBase,
						                 LOAD_DriverMPK_SetPointer);
					}
					AP_HitExtrasRecordPure(&s_hitExtras, hitExtras, hitNeed);
				}
			}
#endif
		}

		// arcade mpk
		lastFileIndexMPK = BI_1PARCADEPACK + data.characterIDs[0];
	}

	else if ((levelLOD == 8) || ((gameMode1 & TIME_TRIAL) != 0))
	{
	LoadHighAndPack:
		// Do NOT switch the order to optimize Relic,
		// if HI+IDs[1] and PACK+IDs[0] is loaded,
		// then mask-grab breaks for all characters
		// on Hot Air Skyway (except Crash Bandicoot)

		// Load Player 1 [0]
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELHI + data.characterIDs[0], &data.driverModelExtras[0].fileBase, LOAD_DriverMPK_SetPointer);

		// Load boss or ghost [1]
		lastFileIndexMPK = BI_TIMETRIALPACK + data.characterIDs[1];
	}

	// else if (levelLOD == 2)
	else
	{
		// med models
		for (i = 0; i < 2; i++)
		{
			// med lod CTR model
			LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELMED + data.characterIDs[i], &data.driverModelExtras[i].fileBase, LOAD_DriverMPK_SetPointer);
		}

		LOAD_Robots2P(bigfile, data.characterIDs[0], data.characterIDs[1], callback);
		return sdata->ptrMPK;
	}

QueueLastPack:
	LOAD_AppendQueue(bigfile, LT_GETADDR, lastFileIndexMPK, NULL, callback);
	return sdata->ptrMPK;
}

struct LngFile
{
	int numStrings;
	int offsetToPtrArr;
	char strings[1];
};

// param_1 - Pointer to "cd position of bigfile"
// param_2 - language index - 0 ja, 1 en, 2 en2, 3 fr, 4 de, 5 it, 6 es, 7 ne
// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80032b50-0x80032c24
void LOAD_LangFile(int bigfilePtr, int lang)
{
	struct LngFile *lngFile;
	u32 size;

	int i;
	int numStrings;
	char **strArray;

#if BUILD == EurRetail
	// This is to turn the screen black for a bit (optional)
	CTR_ErrorScreen(0, 0, 0);
	VSync(0);
#endif

	if (sdata->lngFile == 0)
	{
		sdata->lngFile = MEMPACK_AllocMem(sdata->langBufferSize /* "lang buffer" */);
	}

	lngFile = sdata->lngFile;

	lngFile = LOAD_ReadFile_ex((struct BigHeader *)bigfilePtr, LT_SETADDR, BI_LANGUAGEFILE + lang, lngFile, &size, NULL);
	if (lngFile == NULL)
	{
		return;
	}

	numStrings = lngFile->numStrings;
	strArray = (char **)((u32)lngFile + lngFile->offsetToPtrArr);

	sdata->numLngStrings = numStrings;
	sdata->lngStrings = strArray;

	for (i = 0; i < numStrings; i++)
	{
		strArray[i] = (char *)((u32)strArray[i] + (u32)lngFile);
	}
#if BUILD == EurRetail
	// set voicelines to new lang
	CDSYS_SetXAToLang(lang);
#endif
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80032c24-0x80032d30.
int LOAD_GetBigfileIndex(u32 levelID, int lod, int fileIndexInGroup)
{
	if (levelID < NITRO_COURT)
	{
		return BI_ARCADETRACKS + levelID * 8 + sdata->levBigLodIndex[lod - 1] + fileIndexInGroup;
	}

	if ((u32)(levelID - NITRO_COURT) < 7)
	{
		return BI_BATTLETRACKS + (levelID - NITRO_COURT) * 8 + sdata->levBigLodIndex[lod - 1] + fileIndexInGroup;
	}

	if ((u32)(levelID - INTRO_RACE_TODAY) < 9)
	{
		return BI_CUTSCENES_INTRO + (levelID - INTRO_RACE_TODAY) * 3 + fileIndexInGroup;
	}

	if ((u32)(levelID - OXIDE_ENDING) < 2)
	{
		return BI_CUTSCENES_OUTRO + (levelID - OXIDE_ENDING) * 2 + fileIndexInGroup;
	}

	if (levelID == ADVENTURE_GARAGE)
	{
		return BI_MAINMENUFILE + 2 + fileIndexInGroup;
	}

	if (levelID == NAUGHTY_DOG_CRATE)
	{
		return BI_NDBOX + fileIndexInGroup;
	}

	if ((u32)(levelID - CREDITS_CRASH) < 20)
	{
		return BI_CREDITS + (levelID - CREDITS_CRASH) * 3 + fileIndexInGroup;
	}

	if (levelID == MAIN_MENU_LEVEL)
	{
		return BI_MAINMENUFILE + fileIndexInGroup;
	}

	if (levelID == SCRAPBOOK)
	{
		return BI_SCRAPBOOK + fileIndexInGroup;
	}

	return BI_ADVENTUREHUB + (levelID - GEM_STONE_VALLEY) * 3 + fileIndexInGroup;
}
