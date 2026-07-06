#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800abafc-0x800abbdc.
s16 *AH_WarpPad_GetSpawnPosRot(s16 *posData)
{
	struct Thread *t;
	struct GameTracker *gGT;
	struct Instance *inst;
	struct InstDef *instDef;

	gGT = sdata->gGT;
	t = gGT->threadBuckets[WARPPAD].thread;

	// check all warppads
	while (1)
	{
		// if can't find a warppad
		if (t == 0)
		{
			// quit
			return 0;
		}

		// if warppad found that matches level exited
		if (((struct WarpPad *)t->object)->levelID == gGT->prevLEV)
		{
			// end loop
			break;
		}

		t = t->siblingThread;
	}

	inst = t->inst;
	instDef = inst->instDef;

	posData[0] = inst->matrix.t[0] + ((MATH_Cos(instDef->rot.y) << 0xA) >> 0xC);

	posData[1] = inst->matrix.t[1];

	posData[2] = inst->matrix.t[2] + ((MATH_Sin(instDef->rot.y) * -0x400) >> 0xC);

	return &instDef->rot.x;
}

CTR_STATIC_ASSERT(sizeof(struct WarpPad) == 0x78);
CTR_STATIC_ASSERT(offsetof(struct WarpPad, lightDirGem) == 0x50);
CTR_STATIC_ASSERT(offsetof(struct WarpPad, digit10s) == 0x68);
CTR_STATIC_ASSERT(offsetof(struct WarpPad, levelID) == 0x6c);

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800abbdc-0x800abd80.
void AH_WarpPad_AllWarppadNum()
{
	struct WarpPad *wp;
	struct ModelHeader *mh;
	struct Instance *inst;

	struct Thread *t = sdata->gGT->threadBuckets[WARPPAD].thread;

	for (; t != 0; t = t->siblingThread)
	{
		wp = t->object;

		// DCxDemo says:
		// 1 to 8 is taken from mpk i guess, 0, 9 and X are seprate models

		if ((wp->inst[2] != 0) && (wp->digit1s != 0) && (wp->digit1s != 9))
		{
			inst = wp->inst[2];
			mh = &inst->model->headers[0];
			AH_WarpPad_SetNumModelData(inst, &mh[wp->digit1s - 1]);
		}

		if ((wp->inst[3] != 0) && (wp->digit10s != 0))
		{
			inst = wp->inst[3];
			mh = &inst->model->headers[0];
			AH_WarpPad_SetNumModelData(inst, mh);
		}
	}
}

void AH_WarpPad_SetNumModelData(struct Instance *inst, struct ModelHeader *mh)
{
	struct InstDrawPerPlayer *idpp = INST_GETIDPP(inst);

	idpp[0].ptrCommandList = mh->ptrCommandList;
	idpp[0].ptrColorLayout = (u32)mh->ptrColors;
	idpp[0].ptrTexLayout = mh->ptrTexLayout;
	idpp[0].ptrCurrFrame = mh->ptrFrameData;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800abd80-0x800abdfc.
void AH_WarpPad_MenuProc(struct RectMenu *menu)
{
	struct GameTracker *gGT = sdata->gGT;

	RECTMENU_Hide(menu);

	if (menu->rowSelected == 0)
	{
		gGT->gameMode2 |= TOKEN_RACE;
	}

	else if (menu->rowSelected == 1)
	{
		gGT->gameMode1 |= RELIC_RACE;
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800abdfc-0x800abf48.
void AH_WarpPad_SpinRewards(struct Instance *prizeInst, struct WarpPad *warppadObj, int index, int x, int y, int z)
{
	SVec3 *lightDir;
	u32 modelID;
	u32 trig;
	u32 thirds;

	ConvertRotToMatrix(&prizeInst->matrix, &warppadObj->spinRot_Prize);

	modelID = prizeInst->model->id;

	if (modelID != STATIC_TROPHY) // if not trophy (no lightDir on trophy)
	{
		if (modelID == STATIC_GEM) // gem
		{
			lightDir = &warppadObj->lightDirGem;
		}
		else
		{
			if (modelID == STATIC_RELIC) // relic
			{
				lightDir = &warppadObj->lightDirRelic;
			}
			else
			{
				if (modelID == STATIC_TOKEN) // token
				{
					lightDir = &warppadObj->lightDirToken;
				}
				else
				{
					goto SpinReward;
				}
			}
		}
		Vector_SpecLightSpin3D(prizeInst, &warppadObj->spinRot_Prize, lightDir);
	}

SpinReward:

	// initialized as 0x555*index, but not const
	thirds = warppadObj->thirds[index];

	trig = MATH_Sin(thirds);
	prizeInst->matrix.t[1] = y + ((trig << 6) >> 0xc) + 0x100;

	// do not use original "thirds",
	// set new value without "+="
	thirds = 0x555 * index + warppadObj->spinRot_Rewards.y;

	trig = MATH_Sin(thirds);
	prizeInst->matrix.t[0] = x + (trig * 0xA0 >> 0xc);

	trig = MATH_Cos(thirds);
	prizeInst->matrix.t[2] = z + (trig * 0xA0 >> 0xc);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800abf48-0x800ad2c8.
void AH_WarpPad_ThTick(struct Thread *t)
{
	int i;
	b32 boolOpen;
	struct GameTracker *gGT;
	struct WarpPad *warppadObj;
	struct Instance *warppadInst;
	struct Instance **visInstSrc;
	struct Instance **instArr;

	struct Driver *driver;
	struct Instance *driverInst;

	int modelID;
	int levelID;
	int x, y, z, dist;
	char *warppadLNG;

	int angleCamToWarppad;
	int angleSin, angleCos;
	MATRIX *warppadMatrix;

	int wispMaxHeight;
	int wispRiseRate;
	int rng1;
	int rng2;

	int rewardScale;
	int rewardScale2;

	int champID;
	int champSlot;

	char randKartSpawn[8];

	// NOTE(aalhendi): WarpPad level IDs come from "warppad#NN" instance names
	// and use retail adventure numbering, not the native LevelID enum.
	enum
	{
		AH_WP_SLIDE_COLISEUM = 16,
		AH_WP_TURBO_TRACK = 17,
		AH_WP_NITRO_COURT = 18,
		AH_WP_GEM_STONE_VALLEY = 25,
		AH_WP_ADV_CUP = 100,
	};

	boolOpen = false;
	gGT = sdata->gGT;
	warppadObj = t->object;
	warppadInst = t->inst;
	visInstSrc = gGT->cameraDC[0].visInstSrc;

#if defined(CTR_NATIVE)
	// NOTE(aalhendi): Retail can read PS1 low RAM when the hub-swap frame
	// leaves this list null; native treats that as an empty visible-instance list.
	if (visInstSrc != NULL)
#endif
	{
		while (visInstSrc[0] != 0)
		{
			if (visInstSrc[0] == warppadInst)
			{
				boolOpen = true;
				break;
			}

			visInstSrc++;
		}
	}

	// array of instances in warppad object
	instArr = &warppadObj->inst[0];
	warppadMatrix = &warppadInst->matrix;

	// make instances visible
	if (boolOpen)
	{
		for (i = 0; i < WPIS_NUM_INSTANCES; i++)
		{
			if (instArr[i] != 0)
			{
				instArr[i]->flags &= ~(0x80);
			}
		}
	}

	// make instances invisible
	else
	{
		for (i = 0; i < WPIS_NUM_INSTANCES; i++)
		{
			if (instArr[i] != 0)
			{
				instArr[i]->flags |= 0x80;
			}
		}
	}

	warppadInst->flags |= HIDE_MODEL;

	driver = gGT->drivers[0];
	driverInst = driver->instSelf;

	x = warppadMatrix->t[0] - driverInst->matrix.t[0];
	y = warppadMatrix->t[1] - driverInst->matrix.t[1];
	z = warppadMatrix->t[2] - driverInst->matrix.t[2];
	dist = x * x + y * y + z * z;

	levelID = warppadObj->levelID;

	// if near a portal
	if (
	    // Trophy tracks (-16)
	    ((levelID < AH_WP_SLIDE_COLISEUM) && (dist < 0x144000)) ||

	    // Slide Col + Turbo Track (-16)
	    ((((u16)(levelID - AH_WP_SLIDE_COLISEUM)) < 2) && (dist < 0x90000)) ||

	    // Battle tracks (-18)
	    ((((u16)(levelID - AH_WP_NITRO_COURT)) < 7) && (dist < 0x144000)) ||

	    // Gem cups
	    ((levelID >= AH_WP_ADV_CUP) && (dist < 0x90000)))
	{
		// if you are near a new warppad, or if you already were
		// determined as near the same warppad in the last frame,
		// then use this warppad as the "closest". Otherwise the
		// game could run this for two warppads right next to each other
		if ((D232.levelID == -1) || (D232.levelID == levelID))
		{
			// saved as nearest warppad
			D232.levelID = levelID;


			// if not giving Aku Hint
			if (sdata->AkuAkuHintState == 0)
			{
				// default
				if (levelID < AH_WP_ADV_CUP)
				{
					warppadLNG = sdata->lngStrings[data.metaDataLEV[levelID].name_LNG];
				}
				// gem cups
				else
				{
					warppadLNG = sdata->lngStrings[data.AdvCups[levelID - AH_WP_ADV_CUP].lngIndex_CupName];
				}

				// midpoing X,
				// 30 pixels above botttom Y
				DecalFont_DrawLine(warppadLNG, gGT->pushBuffer[0].rect.x + gGT->pushBuffer[0].rect.w / 2,
				                   gGT->pushBuffer[0].rect.y + gGT->pushBuffer[0].rect.h - 30, FONT_BIG, (JUSTIFY_CENTER | ORANGE));
			}

			// if track is unlocked, ignore all other ELSE-IFs
			if (instArr[WPIS_CLOSED_1S] == 0)
			{
			}

			else if (

			    // gem cup
			    (levelID >= AH_WP_ADV_CUP) &&

			    // Dont have hint "you must have 4 tokens for a gem"
			    (CHECK_ADV_BIT(sdata->advProgress.rewards, ADV_REWARD_HINT_GEM_CUPS_CHALLENGE) == 0)

			)
			{
				// give hint "you must have 4 tokens for a gem"
				MainFrame_RequestMaskHint(ADV_MASK_HINT_ID_GEM_CUPS_CHALLENGE, 0);
			}

			else if (

			    // Trophy track
			    (levelID < AH_WP_SLIDE_COLISEUM) &&

			    // Dont have hint "you must have more trophies"
			    (CHECK_ADV_BIT(sdata->advProgress.rewards, ADV_REWARD_HINT_NEED_MORE_TROPHIES) == 0) &&

			    // required item is not KEY
			    (instArr[WPIS_CLOSED_ITEM]->model->id != STATIC_KEY))
			{
				// give hint for "need more trophies"
				MainFrame_RequestMaskHint(ADV_MASK_HINT_ID_NEED_MORE_TROPHIES, 0);
			}

			else if (

			    // Slide Col
			    (levelID == AH_WP_SLIDE_COLISEUM) &&

			    // Dont have hint "you must have 10 relics"
			    (CHECK_ADV_BIT(sdata->advProgress.rewards, ADV_REWARD_HINT_MUST_GET_10_RELICS) == 0))
			{
				// give hint for "need more trophies"
				MainFrame_RequestMaskHint(ADV_MASK_HINT_ID_MUST_GET_10_RELICS, 0);
			}
		}
	}

	// not near portal
	else
	{
		D232.levelID = -1;
	}

	// if warppad is locked
	if (instArr[WPIS_CLOSED_1S] != 0)
	{
		angleCamToWarppad = ratan2(warppadMatrix->t[0] - gGT->pushBuffer[0].pos.x, warppadMatrix->t[2] - gGT->pushBuffer[0].pos.z);

		angleCamToWarppad = -angleCamToWarppad;

		angleSin = MATH_Sin(angleCamToWarppad);
		angleCos = MATH_Cos(angleCamToWarppad);

		// no 10s digit
		if (instArr[WPIS_CLOSED_10S] == 0)
		{
			instArr[WPIS_CLOSED_1S]->matrix.t[0] = warppadMatrix->t[0] + (angleCos * -0x80 >> 0xC);
			instArr[WPIS_CLOSED_1S]->matrix.t[2] = warppadMatrix->t[2] + (angleSin * -0x80 >> 0xC);

			instArr[WPIS_CLOSED_ITEM]->matrix.t[0] = warppadMatrix->t[0] + ((angleCos << 7) >> 0xC);
			instArr[WPIS_CLOSED_ITEM]->matrix.t[2] = warppadMatrix->t[2] + ((angleSin << 7) >> 0xC);
		}

		// 10s digit
		else
		{
			instArr[WPIS_CLOSED_ITEM]->matrix.t[0] = warppadMatrix->t[0] + (angleCos * 0xC0 >> 0xC);
			instArr[WPIS_CLOSED_ITEM]->matrix.t[2] = warppadMatrix->t[2] + (angleSin * 0xC0 >> 0xC);

			instArr[WPIS_CLOSED_X]->matrix.t[0] = warppadMatrix->t[0] + ((angleCos << 6) >> 0xC);
			instArr[WPIS_CLOSED_X]->matrix.t[2] = warppadMatrix->t[2] + ((angleSin << 6) >> 0xC);

			instArr[WPIS_CLOSED_10S]->matrix.t[0] = warppadMatrix->t[0] + (angleCos * -0x40 >> 0xC);
			instArr[WPIS_CLOSED_10S]->matrix.t[2] = warppadMatrix->t[2] + (angleSin * -0x40 >> 0xC);

			instArr[WPIS_CLOSED_1S]->matrix.t[0] = warppadMatrix->t[0] + (angleCos * -0xa0 >> 0xC);
			instArr[WPIS_CLOSED_1S]->matrix.t[2] = warppadMatrix->t[2] + (angleSin * -0xa0 >> 0xC);
		}

		warppadObj->spinRot_Prize.x = 0;
		warppadObj->spinRot_Prize.z = 0;

		warppadObj->spinRot_Prize.y += 0x40;

		// reuse variable,
		// end of function anyway
		warppadInst = instArr[WPIS_CLOSED_ITEM];
#define InstArr0 warppadInst

		// converted to TEST in rebuildPS1
		ConvertRotToMatrix(&InstArr0->matrix, &warppadObj->spinRot_Prize);

		modelID = InstArr0->model->id;

		// Trophy has no specular light
		if (modelID == STATIC_TROPHY)
		{
			return;
		}

		// NOTE(aalhendi): Retail passes the per-WarpPad spec-light arrays at
		// offsets 0x50/0x58/0x60.

		// Relic
		if (modelID == STATIC_RELIC)
		{
#ifdef CTR_AP
			// AP: a relic requirement (especially "any relic", the any_of default) is
			// satisfied by ANY tier, but the icon is a single Sapphire-blue relic,
			// which reads as "sapphires only". Cycle the tint through the 3 tiers every
			// 2s (sapphire -> gold -> platinum) so it reads as "any relic tier" --
			// mirrors the gem-requirement colour cycle below. Tints match
			// AP_WarpPadRewardTint's per-tier values.
			if (ctr_cfg_active())
			{
				static const u32 s_relicTierTint[3] = {0x020a5ff0, 0x0ffc6290, 0x0ebebf50};
				InstArr0->colorRGBA = s_relicTierTint[(gGT->timer / 0x3C) % 3];
			}
#endif
			Vector_SpecLightSpin3D(InstArr0, &warppadObj->spinRot_Prize, &warppadObj->lightDirRelic);
			return;
		}

		// Token
		if (modelID == STATIC_TOKEN)
		{
			Vector_SpecLightSpin3D(InstArr0, &warppadObj->spinRot_Prize, &warppadObj->lightDirToken);
			return;
		}

		// If Gem, change colors every 2 seconds
		if (modelID == STATIC_GEM)
		{
			i = (gGT->timer / 0x3C) % 5;

			InstArr0->colorRGBA = ((u32)data.AdvCups[i].color[0] << 0x14) | ((u32)data.AdvCups[i].color[1] << 0xc) | ((u32)data.AdvCups[i].color[2] << 0x4);
		}

		// for Key or Gem
		Vector_SpecLightSpin3D(InstArr0, &warppadObj->spinRot_Prize, &warppadObj->lightDirGem);
		return;
	}

	// === Assume Unlocked ===

#ifdef CTR_AP
	// DONE (Warp-Pad State Model v2, state 5): hard-locked, always. Every check at
	// this pad's destination is done, so entry is blocked purely as UX (design #1)
	// -- it can never gate progression. Bail before the entire warp/load/glow path
	// below; the pad shows only its gray X (born in LInB) and never warps. A done
	// pad has CLOSED_1S == 0, so it fell through the locked-render path above to
	// reach here. Keyed off AP_PadState so it also hard-locks a pad that BECOMES
	// done live (its look updates on the next re-birth; the gate is honest now).
	if (ctr_cfg_active() &&
	    AP_PadState(ctr_cfg_warp_phys(warppadObj->levelID), warppadObj->levelID) == 5)
	{
		return;
	}
#endif

	if ((dist > 0x8fff) && (warppadObj->boolEnteredWarppad == 0))
	{
		goto WarpPad_AnimateOpen;
	}

	// Retail repeats this setup every close/warping frame before the
	// transition/load gate.
	LOAD_Robots1P(data.characterIDs[0]);

	// variable reuse, get track speed champion
	champID = data.metaDataLEV[levelID].characterID_Champion;

	// default
	champSlot = 0;

	// If Speed Champion is on the track (Crash-Pura)
	// and is not the same characterID as this driver
	if ((champID < 8) && (champID != data.characterIDs[driver->driverID]))
	{
		// set everyone to spawn in order
		for (i = 1; i < 8; i++)
		{
			if (champID == data.characterIDs[i])
			{
				sdata->kartSpawnOrderArray[i] = 0;
				champSlot = i;
			}

			else if (i == 7)
			{
				sdata->kartSpawnOrderArray[7] = champSlot;
			}

			else
			{
				sdata->kartSpawnOrderArray[i] = i;
			}
		}
	}

	// Speed Champion is invalid
	else
	{
		for (i = 1; i < 8; i++)
		{
			randKartSpawn[i] = i;
		}

		for (i = 0; i < 7; i++)
		{
			rng1 = RngDeadCoed(&sdata->advRng);

			rng2 = 7 - i;

			rng2 = (rng1 & 0xfff) % rng2 + 1;
			rng2 = (s16)rng2;

			sdata->kartSpawnOrderArray[(s32)randKartSpawn[rng2]] = (char)i;

			while (rng2 < 7)
			{
				randKartSpawn[rng2] = randKartSpawn[rng2 + 1];
				rng2++;
			}
		}
	}

	// spawn P1 in the back
	sdata->kartSpawnOrderArray[0] = 7;

	// if flag is on-screen, loading has already been finalized
	if (RaceFlag_IsTransitioning() != 0)
	{
		goto WarpPad_AnimateOpen;
	}

	levelID = warppadObj->levelID;

#ifdef CTR_AP
	// AP Phase 2 STRETCH: under warp-pad DESTINATION shuffle, warppadObj->levelID
	// is the REMAPPED track this pad LOADS, but the per-pad unlock REQUIREMENT was
	// keyed by the PHYSICAL pad LevelID (the apworld keys warp_pad_unlock by
	// physical pad). Recover the physical pad id for the unlock-requirement gate
	// below while keeping local `levelID` (= destination) for the actual track
	// load. ctr_cfg_warp_phys is identity-safe: returns its input unchanged when
	// slot_data is inactive or the map is identity, so this is a no-op pre-shuffle.
	int physLevelID = ctr_cfg_warp_phys(levelID);
#endif

	// gem cups
	if (levelID >= AH_WP_ADV_CUP)
	{
		warppadObj->boolEnteredWarppad = 1;
		warppadObj->framesWarping++;
		gGT->drivers[0]->funcPtrs[DRIVER_FUNC_INIT] = VehStuckProc_Warp_Init;
		if (warppadObj->framesWarping < 61)
		{
			goto WarpPad_AnimateOpen;
		}

		sdata->Loading.OnBegin.AddBitsConfig0 |= ADVENTURE_CUP;

		gGT->cup.cupID = levelID - AH_WP_ADV_CUP;
		gGT->cup.trackIndex = 0;
		for (i = 0; i < 8; i++)
		{
			gGT->cup.points[i] = 0;
		}

		levelID = data.advCupTrackIDs[4 * gGT->cup.cupID];
		goto WarpPad_RequestLoad;
	}

	// Slide Col or Turbo Track
	if (((u16)(levelID - AH_WP_SLIDE_COLISEUM)) < 2)
	{
		warppadObj->boolEnteredWarppad = 1;
		warppadObj->framesWarping++;
		gGT->drivers[0]->funcPtrs[DRIVER_FUNC_INIT] = VehStuckProc_Warp_Init;
		if (warppadObj->framesWarping < 61)
		{
			goto WarpPad_AnimateOpen;
		}

		sdata->Loading.OnBegin.AddBitsConfig0 |= RELIC_RACE;
		goto WarpPad_RequestLoad;
	}

	// Battle Tracks
	if ((((u16)(levelID - AH_WP_NITRO_COURT)) < 2) || (levelID == 21) || (levelID == 23))
	{
		warppadObj->boolEnteredWarppad = 1;
		warppadObj->framesWarping++;
		gGT->drivers[0]->funcPtrs[DRIVER_FUNC_INIT] = VehStuckProc_Warp_Init;
		if (warppadObj->framesWarping < 61)
		{
			goto WarpPad_AnimateOpen;
		}

		sdata->Loading.OnBegin.AddBitsConfig0 |= CRYSTAL_CHALLENGE;

		// Dont have hint "collect every crystal"
		if (CHECK_ADV_BIT(sdata->advProgress.rewards, ADV_REWARD_HINT_COLLECT_EVERY_CRYSTAL) == 0)
		{
			MainFrame_RequestMaskHint(ADV_MASK_HINT_ID_COLLECT_EVERY_CRYSTAL, 1);
		}

		// if can't spawn aku cause he's already here,
		// quit function, wait till he's done to start race
		i = AH_MaskHint_boolCanSpawn();
		if ((i & 0xffff) == 0)
		{
			goto WarpPad_AnimateOpen;
		}

		gGT->originalEventTime = D232.timeCrystalChallenge[levelID - AH_WP_NITRO_COURT];
		goto WarpPad_RequestLoad;
	}

	if (levelID < AH_WP_SLIDE_COLISEUM)
	{
#ifdef CTR_AP
		// "Trophy race beaten?" decides first-pass (load the trophy race) vs
		// already-won (open the relic-race / CTR-token-challenge menu). This MUST
		// read AP checked-state, NOT the AdvProgress trophy bit: AP_ApplyItems
		// clears any trophy bit not backed by a *received* Trophy item every frame,
		// so a local win is wiped and the bit never means "raced this track" --
		// which left the relic races + token challenges permanently unreachable
		// (softlock: the seed's progression routes through those locations).
		// Keyed by levelID = the DESTINATION track actually loaded/raced under
		// destination shuffle (whose Trophy Race location the win checks). The
		// unlock REQUIREMENT below stays keyed by physLevelID (a pad property).
		if (AP_LocationCheckedByBit(levelID + ADV_REWARD_FIRST_TROPHY))
#else
		if (CHECK_ADV_BIT(sdata->advProgress.rewards, levelID + ADV_REWARD_FIRST_TROPHY) != 0)
#endif
		{
#ifdef CTR_AP
			// AP Phase 2: per-seed requirement when slot_data active, else the
			// Phase-1 received-trophy rule. Keeps spawn visual + load gate in
			// lockstep (both read ctr_cfg_warp_unlocked / the same requirement).
			// Keyed by PHYSICAL pad (physLevelID) so the requirement matches the
			// one LInB selected and rendered; under destination shuffle this
			// differs from `levelID` (= the remapped track being loaded).
			if (ctr_cfg_warp_unlocked(physLevelID))
#else
			if (gGT->currAdvProfile.numTrophies >= data.metaDataLEV[levelID].numTrophiesToOpen)
#endif
			{
#ifdef CTR_AP
				// AP Phase 2 (open-rando two-stage): STAGE 2 gates the relic Time
				// Trials + CTR Token Challenge menu independently of stage 1 (which
				// opened the trophy race above). Until stage 2 is met the trophy race
				// is raceable but the tier-2 menu/load stays closed -- the pad just
				// spins (TrophyAnimateOnly), the same idle visual as a not-yet-unlocked
				// stage 1. Keyed by PHYSICAL pad (physLevelID), the same key stage 1
				// uses. type 0 / collapsed / flat-v1 -> ctr_cfg_warp_stage2_unlocked
				// returns 1, so non-two-stage seeds reach the menu exactly as before.
				if (!ctr_cfg_warp_stage2_unlocked(physLevelID))
					goto WarpPad_TrophyAnimateOnly;
#endif

				if (warppadObj->framesWarping < 61)
				{
					goto WarpPad_TrophyAnimateOnly;
				}

				// if never opened
				if (sdata->boolOpenTokenRelicMenu == 0)
				{
					if ((gGT->gameMode1 & ADVENTURE_ARENA) != 0)
					{
#ifdef CTR_AP
						// AP: the tier-2 choice menu hides an already-CHECKED race
						// type, and when only ONE type still has an unchecked
						// location it skips the menu and enters that mode directly.
						// Which types remain is read from AP_PadUncollectedBits
						// (keyed by DESTINATION track = levelID here), which skips
						// tiers this seed never placed -- so a checked/absent option
						// is never offered. (The both-checked case is state 5 Done,
						// hard-locked upstream, so it never reaches this menu.)
						if (ctr_cfg_active())
						{
							int apUncMenu[5];
							int apUncMenuN = AP_PadUncollectedBits(levelID, apUncMenu, 5);
							int apTokenLeft = 0, apRelicLeft = 0, apk;
							for (apk = 0; apk < apUncMenuN; apk++)
							{
								int off = apUncMenu[apk] - levelID;
								if (off == ADV_REWARD_FIRST_CTR_TOKEN)
									apTokenLeft = 1;
								else if (off == ADV_REWARD_FIRST_SAPPHIRE_RELIC ||
								         off == ADV_REWARD_FIRST_GOLD_RELIC ||
								         off == ADV_REWARD_FIRST_PLATINUM_RELIC)
									apRelicLeft = 1;
							}

							if (apTokenLeft && !apRelicLeft)
							{
								// only CTR Token challenge left -> enter directly
								// (mirrors AH_WarpPad_MenuProc row 0); no menu shown.
								gGT->gameMode2 |= TOKEN_RACE;
								RECTMENU_Hide(&D232.menuTokenRelic);
							}
							else if (apRelicLeft && !apTokenLeft)
							{
								// only Relic Race left -> enter directly (row 1).
								gGT->gameMode1 |= RELIC_RACE;
								RECTMENU_Hide(&D232.menuTokenRelic);
							}
							else
							{
								// both types have an unchecked location -> show the
								// menu, cursor on an available row.
								D232.menuTokenRelic.rowSelected = apTokenLeft ? 0 : 1;
								RECTMENU_Show(&D232.menuTokenRelic);
							}
						}
						else
#endif
						{
							D232.menuTokenRelic.rowSelected = (CHECK_ADV_BIT(sdata->advProgress.rewards, levelID + ADV_REWARD_FIRST_CTR_TOKEN) != 0);

							RECTMENU_Show(&D232.menuTokenRelic);
						}

						// now opened
						sdata->boolOpenTokenRelicMenu = 1;
					}
				}

				// if opened, but not closed yet
				if ((RECTMENU_BoolHidden(&D232.menuTokenRelic) & 0xffff) == 0)
				{
					goto WarpPad_TrophyAnimateOnly;
				}

				// Relic Hint
				i = ADV_MASK_HINT_ID_RELIC_CHALLENGE;

				// CTR Token Hint
				if ((gGT->gameMode2 & 8) != 0)
				{
					i = ADV_MASK_HINT_ID_CTR_TOKEN_CHALLENGE;
				}

				// if hint is locked
				if (CHECK_ADV_BIT(sdata->advProgress.rewards, ADV_REWARD_FIRST_HINT + i) == 0)
				{
					MainFrame_RequestMaskHint(i, 1);
				}

				// if can't spawn aku cause he's already here,
				// quit function, wait till he's done to start race
				i = AH_MaskHint_boolCanSpawn();
				if ((i & 0xffff) == 0)
				{
					goto WarpPad_TrophyAnimateOnly;
				}

				// reset for future gameplay
				sdata->boolOpenTokenRelicMenu = 0;
				warppadObj->boolEnteredWarppad = 0;

				// Rem Adventure Arena
				sdata->Loading.OnBegin.RemBitsConfig0 |= ADVENTURE_ARENA;

				MainRaceTrack_RequestLoad(levelID);
				goto WarpPad_TrophyAnimateOnly;
			}
		}
	}

#ifdef CTR_AP
	// AP: "trophy already won -> re-race needs the hub boss key" must read AP
	// checked-state (keyed by the DESTINATION track actually raced), NOT the
	// cosmetic AdvProgress trophy bit. AP_ApplyItems fills received-trophy bits
	// from the HIGH END (ap_hooks.c reconcile), so with N received trophies the top
	// N trophy bits are set regardless of which tracks were actually won. Under
	// destination shuffle a pad whose destination is one of those top levelIDs
	// (e.g. Roo's Tubes pad -> Tiny Arena = 15, with >=1 received trophy) would
	// otherwise read "won" here, hit the hub-boss-key gate below with no key, and
	// `goto WarpPad_AnimateOpen` -- the pad animates but never warps -> the player
	// drives straight through a green pad. Mirrors the ThTick:601 f9fbfa7a0 fix.
	if (AP_LocationCheckedByBit(levelID + ADV_REWARD_FIRST_TROPHY))
#else
	if (CHECK_ADV_BIT(sdata->advProgress.rewards, levelID + ADV_REWARD_FIRST_TROPHY) != 0)
#endif
	{
		i = data.metaDataLEV[levelID].hubID + ADV_REWARD_BOSS_KEY_HUB_ID_BASE;

		if (CHECK_ADV_BIT(sdata->advProgress.rewards, i) == 0)
		{
			goto WarpPad_AnimateOpen;
		}
	}

	warppadObj->boolEnteredWarppad = 1;
	warppadObj->framesWarping++;
	gGT->drivers[0]->funcPtrs[DRIVER_FUNC_INIT] = VehStuckProc_Warp_Init;
	if (warppadObj->framesWarping < 61)
	{
		goto WarpPad_AnimateOpen;
	}

WarpPad_RequestLoad:

	// Rem Adventure Arena
	sdata->Loading.OnBegin.RemBitsConfig0 |= ADVENTURE_ARENA;

	MainRaceTrack_RequestLoad(levelID);
	goto WarpPad_AnimateOpen;

WarpPad_TrophyAnimateOnly:

	if (warppadObj->framesWarping < 0x400)
	{
		warppadObj->framesWarping++;
	}

	warppadObj->boolEnteredWarppad = 1;

	gGT->drivers[0]->funcPtrs[DRIVER_FUNC_INIT] = VehStuckProc_Warp_Init;

WarpPad_AnimateOpen:

	if ((instArr[WPIS_OPEN_BEAM] != 0) && ((gGT->timer & 1) != 0))
	{
		warppadObj->spinRot_Beam.x = 0;
		warppadObj->spinRot_Beam.z = 0;

		// what on earth was this RNG?
		// how'd they come up with something so random, that looks so good?
		i = MixRNG_Scramble();
		warppadObj->spinRot_Beam.y += ((s16)(i >> 3) + (s16)((i >> 3) / 6) * -6 + 1) * 0x200;

		// converted to TEST in rebuildPS1
		ConvertRotToMatrix(&instArr[WPIS_OPEN_BEAM]->matrix, &warppadObj->spinRot_Beam);
	}

	wispRiseRate = 0x20;

	wispMaxHeight = 0x600;

	// if close to this warppad
	if (D232.levelID != -1)
	{
		wispMaxHeight = 0x400;
	}

	for (i = 0; i < 2; i++)
	{
		if (instArr[WPIS_OPEN_RING1 + i] != 0)
		{
			warppadObj->spinRot_Wisp[i].x = 0;
			warppadObj->spinRot_Wisp[i].z = 0;

			warppadObj->spinRot_Wisp[i].y += 0x100;

			// converted to TEST in rebuildPS1
			ConvertRotToMatrix(&instArr[WPIS_OPEN_RING1 + i]->matrix, &warppadObj->spinRot_Wisp[i]);

			// if height hasn't reached max height
			if (instArr[WPIS_OPEN_RING1 + i]->matrix.t[1] < (warppadInst->matrix.t[1] + wispMaxHeight))
			{
				instArr[WPIS_OPEN_RING1 + i]->matrix.t[1] += wispRiseRate;

				// if height hasn't reached 4x RiseRate,
				// first 4 frames of rising
				if (instArr[WPIS_OPEN_RING1 + i]->matrix.t[1] < (warppadInst->matrix.t[1] + wispRiseRate * 4))
				{
					// reduce transparency
					instArr[WPIS_OPEN_RING1 + i]->alphaScale -= 0x380;
				}

				// after first 4 frames
				else
				{
					// add transparency as the wisp spirals upward (~0x60  per frame)
					instArr[WPIS_OPEN_RING1 + i]->alphaScale += 0xc00 / (wispMaxHeight / wispRiseRate);
				}
			}

			// eached max height
			else
			{
				// reset height
				instArr[WPIS_OPEN_RING1 + i]->matrix.t[1] = warppadInst->matrix.t[1];

				// full transparency
				instArr[WPIS_OPEN_RING1 + i]->alphaScale = 0x1000;

				rng1 = MixRNG_Scramble() >> 3;

				rng2 = rng1;
				if (rng1 < 0)
				{
					rng2 = rng1 + 0xfff;
				}

				warppadObj->spinRot_Wisp[i].y = (s16)rng1 + (s16)(rng2 >> 0xc) * -0x1000;
			}
		}

		wispRiseRate += 0x10;
	}

	warppadObj->spinRot_Prize.y += 0x80;

	rewardScale = 0x100;

	if (dist > 0x900000 * 2)
	{
		rewardScale = 0;
	}

	else if (dist > 0x900000)
	{
		// range [90, 90*2] to [0%, 100%]
		rewardScale = ((((0x900000 * 2) - dist) * 0x100) / 0x900000);
	}

#ifdef CTR_AP
	// ── AP GLOW PASS (checked-state driven, decoupled from modelIndex) ──
	// Each frame, decide WHICH AP reward each of the 3 prize slots advertises,
	// purely from the DESTINATION track's still-UNCHECKED locations on the server.
	// This is the SOLE authority for AP-glow content: it reads neither modelIndex
	// nor CHECK_ADV_BIT. LInB always births 3 prize slots for a race track, so the
	// instances exist here regardless of vanilla pad state.
	//   - n uncollected, n>3 : cycle a 3-wide window every 2s (0x3C frames) so the
	//                          slots rotate through every uncollected reward.
	//   - n in 1..3          : slot i shows uncollected[i]; extra slots hidden.
	//   - n == 0             : all three slots hidden (everything checked).
	// A slot turned off here is force-hidden (HIDE_MODEL) AND skipped by the spin
	// loop below, so a hidden slot neither renders nor mis-scales.
	int apUncBits[5];
	int apUncN = -1;     // -1 = "not an AP race track" -> leave vanilla glow alone
	int apSlotBit[3] = {-1, -1, -1}; // per-slot advertised bit, or -1 = hide slot
	if (levelID < AH_WP_SLIDE_COLISEUM)
	{
		apUncN = AP_WarpPadUncollectedBits(warppadObj->levelID, apUncBits,
		                                   (int)(sizeof apUncBits / sizeof apUncBits[0]));
		if (apUncN > 0)
		{
			int base = (apUncN > 3) ? (int)((gGT->timer / 0x3C) * 3) : 0;
			for (i = 0; i < 3; i++)
				apSlotBit[i] = (i < apUncN) ? apUncBits[(base + i) % apUncN] : -1;
		}
		// apUncN == 0 -> all apSlotBit stay -1 (hide everything).
	}
	// ── AP single-reward pads: BATTLE ARENAS + GEM CUPS ──
	// Unlike a race track (3 prize slots), these pads carry exactly ONE checkable
	// location and (under CTR_AP) always-birth ONE prize slot (WPIS_OPEN_PRIZE1 --
	// see LInB). Drive slot 0 off that single location bit and reuse the slot-
	// override loop below verbatim: advertise the scouted reward model while the
	// location is UNCHECKED on the server, force-hide it the instant it is CHECKED
	// (apSlotBit[i] < 0 -> HIDE_MODEL + skip). apUncN is set >= 0 purely to arm the
	// override path; only apSlotBit[] decides per-slot content. Slots 1/2 hold no
	// instance (LInB births one), so the existing null-check skips them.
	else if ((((u16)(levelID - AH_WP_NITRO_COURT)) < 2) || (levelID == 21) || (levelID == 23))
	{
		// Battle arena crystal location (Crystal Bonus Round, AP bits 111..114).
		// battleTrackArr maps the pad LevelID offset to its crystal reward slot.
		int apCrystalBit = R232.battleTrackArr[levelID - AH_WP_NITRO_COURT] + ADV_REWARD_FIRST_PURPLE_TOKEN;
		apUncN = 1; // single-slot AP pad -> arm the override loop below
		apSlotBit[0] = AP_LocationCheckedByBit(apCrystalBit) ? -1 : apCrystalBit;
	}
	else if (((u16)(levelID - AH_WP_ADV_CUP)) < 5)
	{
		// Gem cup location (Gem, AP bits 106..110), keyed by cup colour 0..4.
		int apGemBit = (levelID - AH_WP_ADV_CUP) + ADV_REWARD_FIRST_GEM;
		apUncN = 1;
		apSlotBit[0] = AP_LocationCheckedByBit(apGemBit) ? -1 : apGemBit;
	}
#endif

	for (i = 0; i < 3; i++)
	{
		warppadObj->spinRot_Prize.z = 0x155;

		if (instArr[WPIS_OPEN_PRIZE1 + i] != 0)
		{
#ifdef CTR_AP
			// Apply the AP slot decision before spinning/scaling this instance.
			if (apUncN >= 0)
			{
				struct Instance *apPrize = instArr[WPIS_OPEN_PRIZE1 + i];
				if (apSlotBit[i] < 0)
				{
					// Nothing to advertise in this slot -> force-hide and skip the
					// spin/scale entirely so it neither renders nor mutates.
					apPrize->flags |= HIDE_MODEL;
					warppadObj->thirds[i] += 0x20;
					warppadObj->spinRot_Rewards.y += 0x4;
					continue;
				}

				// Swap this slot to the advertised reward's model + tint. Model-
				// pointer reassignment is a native pattern; SpinRewards/scale below
				// read the new model->id. Foreign / unscouted -> AP_WarpPadRewardModel
				// returns STATIC_KEY / -1; we keep the existing model on -1.
				{
					int apModel = AP_WarpPadRewardModel(apSlotBit[i]);
					int apTint;
					if (apModel >= 0 && gGT->modelPtr[apModel] != 0)
						apPrize->model = gGT->modelPtr[apModel];

					// Re-derive colour + flags from the (possibly reassigned) model id.
					// The 3 slots are BORN as trophy/relic/token placeholders
					// (s_warpPadRewardModelIDs): the relic slot pre-tinted blue, the
					// token slot pre-tinted its group colour, the trophy slot untinted
					// (colorRGBA 0). When _ThTick swaps a slot to a different reward
					// model, that stale placeholder colour would BLEED -- e.g. a trophy
					// landing in the relic slot renders BLUE. Reset colour per FINAL
					// model so OWN items always show their natural colour; ONLY a
					// FOREIGN multiworld item is recoloured (to the white gem marker).
					apPrize->flags &= ~DRAW_TRANSPARENT;
					switch (apPrize->model->id)
					{
					case STATIC_RELIC:
						apPrize->colorRGBA = 0x20a5ff0; // vanilla relic blue
						apTint = AP_WarpPadRewardTint(apSlotBit[i]);
						if (apTint)
							apPrize->colorRGBA = apTint; // own relic TIER tint (sapph/gold/plat)
						apPrize->flags |= USE_SPECULAR_LIGHT;
						break;
					case STATIC_TOKEN:
					{
						// Token model needs colour modulation to read as a token; use
						// the destination track's token-group colour (matches the born
						// token placeholder / the vanilla token you'd win there).
						int tg = data.metaDataLEV[warppadObj->levelID].ctrTokenGroupID;
						apPrize->colorRGBA =
						    ((u32)data.AdvCups[tg].color[0] << 0x14) |
						    ((u32)data.AdvCups[tg].color[1] << 0xc) |
						    ((u32)data.AdvCups[tg].color[2] << 0x4);
						apPrize->flags |= (DRAW_TRANSPARENT | USE_SPECULAR_LIGHT);
						break;
					}
					case STATIC_GEM:
						// FOREIGN multiworld item -> white gem marker; OWN gem -> 0
						// (natural gem model colour). AP_WarpPadRewardTint returns pure
						// white only for a foreign item, 0 otherwise.
						apPrize->colorRGBA = AP_WarpPadRewardTint(apSlotBit[i]);
						apPrize->flags |= USE_SPECULAR_LIGHT;
						break;
					case STATIC_KEY:
						// Own boss Key. The key model has NO useful unmodulated colour --
						// at colorRGBA 0 it renders solid BLACK (unlike the trophy, whose
						// texture shows through untinted). Vanilla always modulates it:
						// the hub-door open sequence draws it gold with specular
						// (AH_Door.c: 0xdca6000 + USE_SPECULAR_LIGHT). Mirror that here so
						// the glow key reads as the golden boss key, not a black blob.
						apPrize->colorRGBA = 0xdca6000; // vanilla golden key
						apPrize->flags |= USE_SPECULAR_LIGHT;
						break;
					default:
						// STATIC_TROPHY / any other own reward -> natural, untinted
						// colour (colorRGBA 0 = the INSTANCE_Birth3D default the trophy
						// placeholder already uses -> shows the model's own texture).
						apPrize->colorRGBA = 0;
						break;
					}
				}
			}
#endif
			AH_WarpPad_SpinRewards(instArr[WPIS_OPEN_PRIZE1 + i], warppadObj, i, warppadInst->matrix.t[0], warppadInst->matrix.t[1], warppadInst->matrix.t[2]);

			modelID = instArr[WPIS_OPEN_PRIZE1 + i]->model->id;

			if (rewardScale == 0)
			{
				// invisible
				instArr[WPIS_OPEN_PRIZE1 + i]->flags |= 0x80;
			}

			else
			{
				// visible
				instArr[WPIS_OPEN_PRIZE1 + i]->flags &= ~(0x80);

				// token
				rewardScale2 = 0x2000;

				// not token
				if (modelID != STATIC_TOKEN)
				{
					// trophy
					rewardScale2 = 0x2800;

					// relic
					if (modelID == STATIC_RELIC)
					{
						rewardScale2 = 0x1800;
					}
				}

				rewardScale2 = (u32)(rewardScale2 * rewardScale) >> 8;
				instArr[WPIS_OPEN_PRIZE1 + i]->scale.x = (s16)rewardScale2;
				instArr[WPIS_OPEN_PRIZE1 + i]->scale.y = (s16)rewardScale2;
				instArr[WPIS_OPEN_PRIZE1 + i]->scale.z = (s16)rewardScale2;
			}
		}

		warppadObj->thirds[i] += 0x20;
		warppadObj->spinRot_Rewards.y += 0x4;
	}

	if (instArr[WPIS_CLOSED_1S] != 0)
	{
		INSTANCE_Death(instArr[WPIS_CLOSED_1S]);
		INSTANCE_Death(instArr[WPIS_CLOSED_10S]);
		INSTANCE_Death(instArr[WPIS_CLOSED_X]);
		INSTANCE_Death(instArr[WPIS_CLOSED_ITEM]);
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800ad2c8-0x800ad3ec.
void AH_WarpPad_ThDestroy(struct Thread *t)
{
	int i;
	struct Instance **instArr;
	struct WarpPad *warppadObj;

	warppadObj = t->object;

	// array of instances in warppad object
	instArr = &warppadObj->inst[0];

	if (instArr[WPIS_CLOSED_1S] != 0)
	{
		INSTANCE_Death(instArr[WPIS_CLOSED_1S]);
		instArr[WPIS_CLOSED_1S] = 0;
	}

	if (instArr[WPIS_CLOSED_10S] != 0)
	{
		INSTANCE_Death(instArr[WPIS_CLOSED_10S]);
		instArr[WPIS_CLOSED_10S] = 0;
	}

	if (instArr[WPIS_CLOSED_X] != 0)
	{
		INSTANCE_Death(instArr[WPIS_CLOSED_X]);
		instArr[WPIS_CLOSED_X] = 0;
	}

	if (instArr[WPIS_CLOSED_ITEM] != 0)
	{
		INSTANCE_Death(instArr[WPIS_CLOSED_ITEM]);
		instArr[WPIS_CLOSED_ITEM] = 0;
	}

	if (instArr[WPIS_OPEN_BEAM] != 0)
	{
		INSTANCE_Death(instArr[WPIS_OPEN_BEAM]);
		instArr[WPIS_OPEN_BEAM] = 0;
	}

	for (i = WPIS_OPEN_RING1; i < WPIS_OPEN_PRIZE1; i++)
	{
		if (instArr[i] != 0)
		{
			INSTANCE_Death(instArr[i]);
			instArr[i] = 0;
		}
	}

	for (i = WPIS_OPEN_PRIZE1; i < WPIS_NUM_INSTANCES; i++)
	{
		if (instArr[i] != 0)
		{
			INSTANCE_Death(instArr[i]);
			instArr[i] = 0;
		}
	}
}

static const s16 s_warpPadRewardModelIDs[3] = {STATIC_TROPHY, STATIC_RELIC, STATIC_TOKEN};

// NOTE(aalhendi): Source-backed for NTSC-U 926 0x800ad3ec-0x800ae870.
#ifdef CTR_AP
// Resolve a per-seed unlock requirement (ctr_req) into the LInB display/gate
// triple (model id + owned count + needed count), colour-aware. Mirrors the
// inline switch the trophy pads / gem cups use; factored out for the trial pads
// (Slide Coliseum / Turbo Track), which the open two-stage rework randomizes.
static void AP_ReqToUnlock(const ctr_req *r, int *modelID, int *numOwned, int *numNeeded)
{
	switch (r->type)
	{
	case 1: *modelID = STATIC_TROPHY; *numOwned = AP_GateCount(AP_IDX_TROPHY); break;
	case 2: *modelID = STATIC_KEY;    *numOwned = AP_GateCount(AP_IDX_KEY);    break;
	case 3:
		*modelID = STATIC_TOKEN;
		*numOwned = (r->colour >= 0) ? AP_GateCountTokenColour(r->colour) : AP_GateCount(AP_IDX_TOKEN_RED);
		break;
	case 4: *modelID = STATIC_RELIC;  *numOwned = AP_GateCount(AP_IDX_SAPPHIRE); break;
	case 5: *modelID = STATIC_GEM;    *numOwned = (r->colour >= 0) ? AP_GateCountGemColour(r->colour) : 0; break;
	// any-of aggregates (colour -1): show the type's model + the summed owned count
	// so the displayed X/N matches the gate eval (AP_BossReqMet types 6/7/8).
	case 6: *modelID = STATIC_TOKEN;  *numOwned = AP_GateCountTokenSum(); break; // AnyToken
	case 7: *modelID = STATIC_RELIC;  *numOwned = AP_GateCountRelicSum(); break; // AnyRelic
	case 8: *modelID = STATIC_GEM;    *numOwned = AP_GateCountGemSum();   break; // AnyGem
	default: *modelID = STATIC_TROPHY; *numOwned = AP_GateCount(AP_IDX_TROPHY); break;
	}
	*numNeeded = r->count;
}
#endif

void AH_WarpPad_LInB(struct Instance *inst)
{
	int i;
	int levelID;
	struct Thread *t;
	struct WarpPad *warppadObj;

	struct GameTracker *gGT;

	int unlockItem_numOwned;
	int unlockItem_numNeeded;
	int unlockItem_modelID;
	int rewardModelID;
	int rewardAngle;
	int tokenGroupID;

	int *arrTokenCount;
	struct Instance *newInst;

	// NOTE(aalhendi): WarpPad level IDs come from "warppad#NN" instance names
	// and use retail adventure numbering, not the native LevelID enum.
	enum
	{
		AH_WP_SLIDE_COLISEUM = 16,
		AH_WP_TURBO_TRACK = 17,
		AH_WP_NITRO_COURT = 18,
		AH_WP_ADV_CUP = 100,
	};

	gGT = sdata->gGT;

	if (inst->thread != NULL)
	{
		return;
	}

	t = PROC_BirthWithObject(SIZE_RELATIVE_POOL_BUCKET(sizeof(struct WarpPad), NONE, MEDIUM, WARPPAD),

	                         AH_WarpPad_ThTick, // behavior
	                         "warppad",         // debug name
	                         0                  // thread relative
	);

	if (t == 0)
	{
		return;
	}
	inst->thread = t;
	t->inst = inst;

	t->funcThDestroy = AH_WarpPad_ThDestroy;

	// 0 - locked
	// 1 - open for trophy
	// 2 - unlocked all
	// 3 - open for relic/token
	// 4 - purple token or SlideCol/TurboTrack

	// locked
	t->modelIndex = 0;

	// make invisible
	// this is the red triangle
	// instance from DCxDemo's LEV Viewer
	inst->flags |= HIDE_MODEL;

	warppadObj = t->object;
	warppadObj->levelID = 0; // this is dingo canyon
	warppadObj->boolEnteredWarppad = 0;
	warppadObj->framesWarping = 0;

	for (i = 0; i < WPIS_NUM_INSTANCES; i++)
	{
		warppadObj->inst[i] = 0;
	}

	// each warppad has a name "warppad#xxx"
	// "warppad#0" is dingo canyon, level ID 0
	// "warppad#16" is slide col, level ID 16
	// "warppad#102" is gem cup 2
	// "warppad#104" is gem cup 4
	// etc

	levelID = 0;
	for (i = 8; inst->name[i] != 0; i++)
	{
		levelID = levelID * 10 + inst->name[i] - '0';
	}

	warppadObj->levelID = levelID;

#ifdef CTR_AP
	// AP Phase 2 STRETCH: warp-pad destination shuffle. Override only the LOAD
	// target (warppadObj->levelID, re-read by ThTick at the load gate); the local
	// `levelID` stays the PHYSICAL pad ID so the unlock-requirement selection
	// below still keys off the physical pad. ctr_cfg_warp_dest is identity-safe:
	// returns the input unchanged when slot_data is inactive or warp_pad_map is
	// identity (not shuffled), so this is a no-op for the non-shuffle MVP path.
	warppadObj->levelID = ctr_cfg_warp_dest(levelID);

	// Confirmation log: emit one line per pad whose destination was actually
	// remapped (physical != loaded). Proves the warp_pad_map round-tripped and
	// the native LInB remap is live; silent when the map is identity.
	if (warppadObj->levelID != levelID)
	{
		char apwpmsg[96];
		snprintf(apwpmsg, sizeof apwpmsg,
		         "[AP WARP] pad phys=%d remapped -> loads track=%d\n",
		         levelID, warppadObj->levelID);
		AP_LogLine(apwpmsg);
	}
#endif

	unlockItem_modelID = 0;
	unlockItem_numOwned = 0;
	unlockItem_numNeeded = -1;

#ifdef CTR_AP
	// DONE (Warp-Pad State Model v2, state 5): every destination location for this
	// pad is checked. Render a CLOSED look distinct from Locked/Re-locked -- the
	// gray X ALONE: no requirement item, no digits, no beam/rings/prize glow. Born
	// with CLOSED_1S / CLOSED_ITEM left NULL so AH_WarpPad_ThTick's Done guard keeps
	// the pad inert (it never enters the locked-render path -- which would deref
	// CLOSED_ITEM -- nor the warp path). Hard-locked, always: a done pad has nothing
	// left, so blocking entry is pure UX and cannot gate progression (design #1).
	// levelID = physical pad; warppadObj->levelID = destination (location key).
	if (ctr_cfg_active() && AP_PadState(levelID, warppadObj->levelID) == 5)
	{
		warppadObj->digit1s = 0;
		warppadObj->digit10s = 0;

		// WPIS_CLOSED_X -- the only instance a done pad births.
		newInst = INSTANCE_Birth3D(gGT->modelPtr[STATIC_BIGX], "x", t);

		// copy matrix (identity rotation, mirrors the vanilla X birth below)
		*(int *)((int)&newInst->matrix + 0x0) = 0x1000;
		*(int *)((int)&newInst->matrix + 0x4) = 0;
		*(int *)((int)&newInst->matrix + 0x8) = 0x1000;
		*(int *)((int)&newInst->matrix + 0xC) = 0;
		*(s16 *)((int)&newInst->matrix + 0x10) = 0x1000;
		newInst->matrix.t[0] = inst->matrix.t[0];
		newInst->matrix.t[1] = inst->matrix.t[1] + 0x100;
		newInst->matrix.t[2] = inst->matrix.t[2];

		newInst->scale.x = 0x2000;
		newInst->scale.y = 0x2000;
		newInst->scale.z = 0x2000;

		// gray tint (packed (R<<0x14)|(G<<0xc)|(B<<0x4), 0x80 grey) so the done pad
		// reads distinct from an active red/gold marker. If STATIC_BIGX ignores the
		// vertex colour the X still stands ALONE (no item/digits), which is the
		// primary distinction.
		newInst->colorRGBA = 0x08080800;

		// always face camera
		newInst->model->headers[0].flags |= 1;

		warppadObj->inst[WPIS_CLOSED_X] = newInst;
		return;
	}
#endif

	// Trophy Track
	if (levelID < AH_WP_SLIDE_COLISEUM)
	{
		// optimization idea:
		// instead of data.metaDataLEV[levelID].hubID
		// can we just do gGT->levelID-0x19?

#ifdef CTR_AP
		// AP two-stage RE-LOCK (open-rando): the trophy race is already WON
		// (AP checked-state, keyed by the DESTINATION track) but this pad's
		// STAGE 2 is not yet satisfied. Render the pad CLOSED, advertising the
		// stage-2 requirement, so the player isn't pulled into a dead warp that
		// ThTick blocks at the load gate (ctr_cfg_warp_stage2_unlocked). This is
		// the missing visual half of the stage-2 gate: without it LInB would
		// treat a stage1-met pad as fully unlocked. stage2 keyed by PHYSICAL pad
		// (levelID), the same key ThTick's stage-2 gate uses; checked-state keyed
		// by warppadObj->levelID (= destination track) to match ThTick:601.
		// type 0 stage2 (no second gate / collapsed / flat-v1) -> _stage2_unlocked
		// returns 1 -> this branch is skipped -> pre-two-stage behaviour.
		if (ctr_cfg_active() &&
		    AP_LocationCheckedByBit(warppadObj->levelID + ADV_REWARD_FIRST_TROPHY) &&
		    !ctr_cfg_warp_stage2_unlocked(levelID))
		{
			const ctr_req *r = &ctr_cfg.warp_pad_unlock[levelID].stage2;
			switch (r->type)
			{
			case 1: // trophies
				unlockItem_modelID = STATIC_TROPHY;
				unlockItem_numOwned = AP_GateCount(AP_IDX_TROPHY);
				break;
			case 2: // keys
				unlockItem_modelID = STATIC_KEY;
				unlockItem_numOwned = AP_GateCount(AP_IDX_KEY);
				break;
			case 3: // tokens (colour-aware)
				unlockItem_modelID = STATIC_TOKEN;
				unlockItem_numOwned = (r->colour >= 0)
				    ? AP_GateCountTokenColour(r->colour)
				    : AP_GateCount(AP_IDX_TOKEN_RED);
				break;
			case 4: // sapphire relic
				unlockItem_modelID = STATIC_RELIC;
				unlockItem_numOwned = AP_GateCount(AP_IDX_SAPPHIRE);
				break;
			case 5: // gems (colour-aware)
				unlockItem_modelID = STATIC_GEM;
				unlockItem_numOwned =
				    (r->colour >= 0) ? AP_GateCountGemColour(r->colour) : 0;
				break;
			case 6: // any token (all colours, summed)
				unlockItem_modelID = STATIC_TOKEN;
				unlockItem_numOwned = AP_GateCountTokenSum();
				break;
			case 7: // any relic (all tiers, summed)
				unlockItem_modelID = STATIC_RELIC;
				unlockItem_numOwned = AP_GateCountRelicSum();
				break;
			case 8: // any gem (all colours, summed)
				unlockItem_modelID = STATIC_GEM;
				unlockItem_numOwned = AP_GateCountGemSum();
				break;
			default:
				unlockItem_modelID = STATIC_TROPHY;
				unlockItem_numOwned = AP_GateCount(AP_IDX_TROPHY);
				break;
			}
			unlockItem_numNeeded = r->count;
		}
		else
#endif
#ifdef CTR_AP
		// AP: "trophy owned -> re-race needs hub keys" must read AP checked-state
		// (the DESTINATION track's Trophy Race LOCATION), NOT the cosmetic AdvProgress
		// trophy bit. AP_ApplyItems high-end-fills received-trophy bits, so a NOT-won pad
		// whose levelID is among the top received-trophy bits would otherwise read "won"
		// here and get routed into the vanilla hub-key re-race gate (GetKeysRequirement),
		// silently REPLACING the seed's randomized stage-1 requirement (opening on the
		// wrong item/count -> out-of-logic, or softlock in the harder direction). This is
		// the LInB twin of the ThTick:601/682 f9fbfa7a0 fix; keyed by warppadObj->levelID
		// (destination) for shuffle consistency.
		if (AP_LocationCheckedByBit(warppadObj->levelID + ADV_REWARD_FIRST_TROPHY))
#else
		// if trophy owned
		if (CHECK_ADV_BIT(sdata->advProgress.rewards, levelID + ADV_REWARD_FIRST_TROPHY) != 0)
#endif
		{
#ifdef CTR_AP
			// BUG A fix (phantom hub-key on a satisfied stage-2 pad): the trophy race
			// is WON and this pad's stage-2 is SATISFIED -- ctr_cfg_warp_stage2_unlocked
			// returns 1 for BOTH a type-0 "free" stage-2 AND a met non-free stage-2, so
			// ThTick's load gate already lets the tier-2 menu open. Render the pad OPEN
			// (0 >= 0 -> the "if unlocked" path below shows the stage-2 glow rewards)
			// instead of falling through to the vanilla re-race GetKeysRequirement gate,
			// which rendered a phantom "Key xN" lock the player reads as a softlock. The
			// stage-2-NOT-satisfied case is caught by the re-lock branch above (renders
			// the stage-2 requirement); vanilla / no-slot_data (ctr_cfg_active()==0)
			// still uses the hub-key gate, unchanged.
			if (ctr_cfg_active() && ctr_cfg_warp_stage2_unlocked(levelID))
			{
				unlockItem_modelID = 0;
				unlockItem_numOwned = 0;
				unlockItem_numNeeded = 0;
			}
			else
#endif
			{
			GetKeysRequirement:

				// keys needed to unlock track again
				unlockItem_modelID = STATIC_KEY;
#ifdef CTR_AP
				// AP: owned-count from the authoritative received-Key tally, not the cosmetic
				// currAdvProfile.numKeys (rebuilt from bits) -- the same source every other AP
				// key gate uses (hub door + pad reqs + Oxide).
				unlockItem_numOwned = AP_GateCount(AP_IDX_KEY);
#else
				unlockItem_numOwned = gGT->currAdvProfile.numKeys;
#endif
				unlockItem_numNeeded = D232.arrKeysNeeded[data.metaDataLEV[levelID].hubID];
			}
		}

		// if trophy not owned
		else
		{
#ifdef CTR_AP
			// AP Phase 2: a per-seed randomized requirement replaces the Phase-1
			// trophy threshold for this pad when slot_data is active and the pad
			// has a resolved requirement (type != 0). type 0 (fixed pads / vanilla
			// mode / absent slot_data) falls through to the Phase-1 statics below.
			if (ctr_cfg_active() && levelID >= 0 && levelID < CTR_CFG_PAD_COUNT &&
			    ctr_cfg.warp_pad_unlock[levelID].stage1.type != 0)
			{
				// stage1 is the "open the trophy race" requirement shown on the pad
				// here (the trophy-not-owned branch). The stage2 (tier-2) requirement
				// is gated at the load site in AH_WarpPad_ThTick, not advertised here.
				const ctr_req *r = &ctr_cfg.warp_pad_unlock[levelID].stage1;
				switch (r->type)
				{
				case 1: // trophies
					unlockItem_modelID = STATIC_TROPHY;
					unlockItem_numOwned = AP_GateCount(AP_IDX_TROPHY);
					break;
				case 2: // keys
					unlockItem_modelID = STATIC_KEY;
					unlockItem_numOwned = AP_GateCount(AP_IDX_KEY);
					break;
				case 3: // tokens (colour-aware)
					unlockItem_modelID = STATIC_TOKEN;
					unlockItem_numOwned = (r->colour >= 0)
					    ? AP_GateCountTokenColour(r->colour)
					    : AP_GateCount(AP_IDX_TOKEN_RED);
					break;
				case 4: // sapphire
					unlockItem_modelID = STATIC_RELIC;
					unlockItem_numOwned = AP_GateCount(AP_IDX_SAPPHIRE);
					break;
				case 5: // gems (colour-aware)
					unlockItem_modelID = STATIC_GEM;
					unlockItem_numOwned =
					    (r->colour >= 0) ? AP_GateCountGemColour(r->colour) : 0;
					break;
				case 6: // any token (all colours, summed)
					unlockItem_modelID = STATIC_TOKEN;
					unlockItem_numOwned = AP_GateCountTokenSum();
					break;
				case 7: // any relic (all tiers, summed)
					unlockItem_modelID = STATIC_RELIC;
					unlockItem_numOwned = AP_GateCountRelicSum();
					break;
				case 8: // any gem (all colours, summed)
					unlockItem_modelID = STATIC_GEM;
					unlockItem_numOwned = AP_GateCountGemSum();
					break;
				default:
					unlockItem_modelID = STATIC_TROPHY;
					unlockItem_numOwned = AP_GateCount(AP_IDX_TROPHY);
					break;
				}
				unlockItem_numNeeded = r->count;
			}
			else
#endif
			{
				// number trophies needed to open
				unlockItem_modelID = STATIC_TROPHY;
#ifdef CTR_AP
				unlockItem_numOwned = AP_GateCount(AP_IDX_TROPHY);
#else
				unlockItem_numOwned = gGT->currAdvProfile.numTrophies;
#endif
				unlockItem_numNeeded = data.metaDataLEV[levelID].numTrophiesToOpen;
			}
		}
	}

	// Slide Col
	else if (levelID == AH_WP_SLIDE_COLISEUM)
	{
#ifdef CTR_AP
		// AP open two-stage: Slide Coliseum carries a per-seed randomized
		// single-stage requirement (warp_pad_unlock[16], same dense array as the
		// trophy pads). type 0 / inactive falls back to the vanilla 10-Sapphire gate.
		if (ctr_cfg_active() && levelID < CTR_CFG_PAD_COUNT &&
		    ctr_cfg.warp_pad_unlock[levelID].stage1.type != 0)
		{
			AP_ReqToUnlock(&ctr_cfg.warp_pad_unlock[levelID].stage1,
			               &unlockItem_modelID, &unlockItem_numOwned, &unlockItem_numNeeded);
		}
		else
#endif
		{
			// number relics needed to open
			unlockItem_modelID = STATIC_RELIC;
			unlockItem_numNeeded = 10;
#ifdef CTR_AP
			// AP fallback (type 0 / vanilla): 10 received Sapphire Relics.
			unlockItem_numOwned = AP_GateCount(AP_IDX_SAPPHIRE);
#else
			unlockItem_numOwned = gGT->currAdvProfile.numRelics;
#endif
		}
	}

	// Turbo Track
	else if (levelID == AH_WP_TURBO_TRACK)
	{
#ifdef CTR_AP
		// AP open two-stage: Turbo Track carries a per-seed randomized single-stage
		// requirement (warp_pad_unlock[17]). type 0 / inactive falls back to vanilla 5 Gems.
		if (ctr_cfg_active() && levelID < CTR_CFG_PAD_COUNT &&
		    ctr_cfg.warp_pad_unlock[levelID].stage1.type != 0)
		{
			AP_ReqToUnlock(&ctr_cfg.warp_pad_unlock[levelID].stage1,
			               &unlockItem_modelID, &unlockItem_numOwned, &unlockItem_numNeeded);
		}
		else
#endif
		{
			// number gems needed to open
			unlockItem_modelID = STATIC_GEM;
			unlockItem_numNeeded = 5;

			// count number of gems owned
			unlockItem_numOwned = 0;
			for (i = 0; i < 5; i++)
#ifdef CTR_AP
				// AP fallback (type 0 / vanilla): all 5 received Gem colours.
				if (AP_GateCountGemColour(i) >= 1)
					unlockItem_numOwned++;
#else
				if (CHECK_ADV_BIT(sdata->advProgress.rewards, ADV_REWARD_FIRST_GEM + i) != 0)
					unlockItem_numOwned++;
#endif
		}
	}

	// battle maps
	else if ((((u16)(levelID - AH_WP_NITRO_COURT)) < 2) || (levelID == 21) || (levelID == 23))
	{
#ifdef CTR_AP
		// AP open randomization: battle-arena pads carry a per-seed randomized
		// single-stage requirement (warp_pad_unlock[18/19/21/23]); type 0 / inactive
		// falls back to the vanilla hub-key gate (GetKeysRequirement).
		if (ctr_cfg_active() && levelID < CTR_CFG_PAD_COUNT &&
		    ctr_cfg.warp_pad_unlock[levelID].stage1.type != 0)
		{
			AP_ReqToUnlock(&ctr_cfg.warp_pad_unlock[levelID].stage1,
			               &unlockItem_modelID, &unlockItem_numOwned, &unlockItem_numNeeded);
		}
		else
#endif
			goto GetKeysRequirement;
	}

	// gem cups
	else if (((u16)(levelID - AH_WP_ADV_CUP)) < 5)
	{
		// number tokens needed to open
		unlockItem_modelID = STATIC_TOKEN;
		unlockItem_numNeeded = 4;

#ifdef CTR_AP
		// cup index (levelID - AH_WP_ADV_CUP) is 0..4 = R,G,B,Y,P. Gem cups are not
		// in the warp-pad shuffle group, so the physical pad == this LevelID
		// (ctr_cfg_warp_phys is identity here); index gem_cup_unlock by cup colour.
		{
			int cupIdx = levelID - AH_WP_ADV_CUP;
			// AP Phase 2: a per-seed randomized stage-1 requirement replaces the
			// Phase-1 "4 tokens of this colour" rule when slot_data is active and the
			// cup has a resolved requirement (type != 0). type 0 (option off / vanilla
			// / absent slot_data) falls back to the token rule. Uses the SAME colour-
			// aware type switch the trophy pads use (~1224). NOTE: gem cups carry no
			// pre-existing "2-key hub gate" in this branch, so there is nothing to AND
			// the stage-1 requirement with here (see handoff flag).
			if (ctr_cfg_active() && ctr_cfg.gem_cup_unlock[cupIdx].stage1.type != 0)
			{
				const ctr_req *r = &ctr_cfg.gem_cup_unlock[cupIdx].stage1;
				switch (r->type)
				{
				case 1: // trophies
					unlockItem_modelID = STATIC_TROPHY;
					unlockItem_numOwned = AP_GateCount(AP_IDX_TROPHY);
					break;
				case 2: // keys
					unlockItem_modelID = STATIC_KEY;
					unlockItem_numOwned = AP_GateCount(AP_IDX_KEY);
					break;
				case 3: // tokens (colour-aware)
					unlockItem_modelID = STATIC_TOKEN;
					unlockItem_numOwned = (r->colour >= 0)
					    ? AP_GateCountTokenColour(r->colour)
					    : AP_GateCount(AP_IDX_TOKEN_RED);
					break;
				case 4: // sapphire
					unlockItem_modelID = STATIC_RELIC;
					unlockItem_numOwned = AP_GateCount(AP_IDX_SAPPHIRE);
					break;
				case 5: // gems (colour-aware)
					unlockItem_modelID = STATIC_GEM;
					unlockItem_numOwned =
					    (r->colour >= 0) ? AP_GateCountGemColour(r->colour) : 0;
					break;
				case 6: // any token (all colours, summed)
					unlockItem_modelID = STATIC_TOKEN;
					unlockItem_numOwned = AP_GateCountTokenSum();
					break;
				case 7: // any relic (all tiers, summed)
					unlockItem_modelID = STATIC_RELIC;
					unlockItem_numOwned = AP_GateCountRelicSum();
					break;
				case 8: // any gem (all colours, summed)
					unlockItem_modelID = STATIC_GEM;
					unlockItem_numOwned = AP_GateCountGemSum();
					break;
				default:
					unlockItem_modelID = STATIC_TOKEN;
					unlockItem_numOwned = AP_GateCountTokenColour(cupIdx);
					break;
				}
				unlockItem_numNeeded = r->count;
			}
			else
			{
				// AP Option B fallback: gate on 4 received Tokens of its colour.
				unlockItem_modelID = STATIC_TOKEN;
				unlockItem_numNeeded = 4;
				unlockItem_numOwned = AP_GateCountTokenColour(cupIdx);
			}
		}
#else
		arrTokenCount = &gGT->currAdvProfile.numCtrTokens.red;
		unlockItem_numOwned = arrTokenCount[levelID - AH_WP_ADV_CUP];
#endif
	}

	// if unlocked
	if (unlockItem_numOwned >= unlockItem_numNeeded)
	{
		warppadObj->digit1s = 0;
		t->modelIndex = 1;

		// if beam model exists
		if (gGT->modelPtr[STATIC_BEAM] != 0)
		{
			newInst = INSTANCE_Birth3D(gGT->modelPtr[STATIC_BEAM], "beam", t);

			// copy matrix
			*(int *)((int)&newInst->matrix + 0x0) = *(int *)((int)&inst->matrix + 0x0);
			*(int *)((int)&newInst->matrix + 0x4) = *(int *)((int)&inst->matrix + 0x4);
			*(int *)((int)&newInst->matrix + 0x8) = *(int *)((int)&inst->matrix + 0x8);
			*(int *)((int)&newInst->matrix + 0xC) = *(int *)((int)&inst->matrix + 0xC);
			*(s16 *)((int)&newInst->matrix + 0x10) = *(s16 *)((int)&inst->matrix + 0x10);
			newInst->matrix.t[0] = inst->matrix.t[0];
			newInst->matrix.t[1] = inst->matrix.t[1];
			newInst->matrix.t[2] = inst->matrix.t[2];

			newInst->alphaScale = 0xc00;

			warppadObj->inst[WPIS_OPEN_BEAM] = newInst;
		}

		// if spiral ring exists
		if (gGT->modelPtr[STATIC_BOTTOMRING] != 0)
		{
			for (i = 0; i < 2; i++)
			{
				newInst = INSTANCE_Birth3D(gGT->modelPtr[STATIC_BOTTOMRING], "bottomRing", t);

				// copy matrix
				*(int *)((int)&newInst->matrix + 0x0) = *(int *)((int)&inst->matrix + 0x0);
				*(int *)((int)&newInst->matrix + 0x4) = *(int *)((int)&inst->matrix + 0x4);
				*(int *)((int)&newInst->matrix + 0x8) = *(int *)((int)&inst->matrix + 0x8);
				*(int *)((int)&newInst->matrix + 0xC) = *(int *)((int)&inst->matrix + 0xC);
				*(s16 *)((int)&newInst->matrix + 0x10) = *(s16 *)((int)&inst->matrix + 0x10);
				newInst->matrix.t[0] = inst->matrix.t[0];
				newInst->matrix.t[1] = inst->matrix.t[1] + i * 0x400;
				newInst->matrix.t[2] = inst->matrix.t[2];

				newInst->alphaScale = 0x400;

				warppadObj->inst[WPIS_OPEN_RING1 + i] = newInst;
			}
		}

		for (i = 0; i < 3; i++)
		{
			warppadObj->thirds[i] = 0x555 * i;
		}

		warppadObj->spinRot_Prize.x = 0;
		warppadObj->spinRot_Prize.y = 0;
		warppadObj->spinRot_Prize.z = 0;

		warppadObj->spinRot_Beam.x = 0;
		warppadObj->spinRot_Beam.y = 0;
		warppadObj->spinRot_Beam.z = 0;

		for (i = 0; i < 2; i++)
		{
			warppadObj->spinRot_Wisp[i].x = 0;
			warppadObj->spinRot_Wisp[i].y = 0;
			warppadObj->spinRot_Wisp[i].z = 0;
		}

		if (levelID < AH_WP_SLIDE_COLISEUM)
		{
			// unlocked all
			t->modelIndex = 2;

			// if trophy not owned
#ifdef CTR_AP
			// AP GLOW REDESIGN: under CTR_AP a race track's warp pad ALWAYS births
			// all three prize slots (WPIS_OPEN_PRIZE1..3), regardless of the vanilla
			// trophy bit. AH_WarpPad_ThTick then drives WHICH reward each slot shows
			// (and hides/cycles them) purely off AP checked-state -- see the AP glow
			// pass in _ThTick. We force the 3-slot path here because that is the only
			// branch that creates three drivable instances; the modelIndex 2/3 paths
			// below (which key off CHECK_ADV_BIT, cleared every frame by AP_ApplyItems)
			// would otherwise leave us 0 or 1 slot. The models/tints born here are
			// vanilla placeholders; _ThTick overwrites model + colorRGBA every frame.
			// lightDirRelic/Token are seeded once so a slot that _ThTick turns into a
			// relic/token gets correct specular spin (SpinRewards reads them by model).
			if (levelID < AH_WP_SLIDE_COLISEUM)
#else
			if (CHECK_ADV_BIT(sdata->advProgress.rewards, levelID + ADV_REWARD_FIRST_TROPHY) == 0)
#endif
			{
				// open for trophy
				t->modelIndex = 1;

#ifdef CTR_AP
				warppadObj->lightDirRelic = D232.lightDirRelic[0];
				warppadObj->lightDirToken =
				    D232.lightDirToken[data.metaDataLEV[levelID].ctrTokenGroupID];
				// Seed the gem light dir too: _ThTick can turn a slot into STATIC_GEM
				// (own gem-cup gem, or a white-gem FOREIGN marker), and SpinRewards
				// reads lightDirGem for specular spin on gem models. All 5 D232 table
				// entries are identical, so [0] is the universal value.
				warppadObj->lightDirGem = D232.lightDirGem[0];
#endif

				rewardAngle = 0;
				for (i = 0; i < 3; i++)
				{
					rewardModelID = s_warpPadRewardModelIDs[i];
					newInst = INSTANCE_Birth3D(gGT->modelPtr[rewardModelID], "prize1", t);
					warppadObj->inst[WPIS_OPEN_PRIZE1 + i] = newInst;

					// copy matrix
					*(int *)((int)&newInst->matrix + 0x0) = *(int *)((int)&inst->matrix + 0x0);
					*(int *)((int)&newInst->matrix + 0x4) = *(int *)((int)&inst->matrix + 0x4);
					*(int *)((int)&newInst->matrix + 0x8) = *(int *)((int)&inst->matrix + 0x8);
					*(int *)((int)&newInst->matrix + 0xC) = *(int *)((int)&inst->matrix + 0xC);
					*(s16 *)((int)&newInst->matrix + 0x10) = *(s16 *)((int)&inst->matrix + 0x10);
					newInst->matrix.t[0] = inst->matrix.t[0] + ((MATH_Sin(rewardAngle) * 0xc0) >> 0xc);
					newInst->matrix.t[1] = inst->matrix.t[1] + 0x100;
					newInst->matrix.t[2] = inst->matrix.t[2] + ((MATH_Cos(rewardAngle) * 0xc0) >> 0xc);

					if (rewardModelID == STATIC_RELIC)
					{
						newInst->colorRGBA = 0x20a5ff0;
						newInst->flags |= USE_SPECULAR_LIGHT;
						newInst->scale.x = 0x1800;
						newInst->scale.y = 0x1800;
						newInst->scale.z = 0x1800;
					}

					else if (rewardModelID == STATIC_TOKEN)
					{
						tokenGroupID = data.metaDataLEV[levelID].ctrTokenGroupID;

						// token color
						newInst->colorRGBA = ((u32)data.AdvCups[tokenGroupID].color[0] << 0x14) | ((u32)data.AdvCups[tokenGroupID].color[1] << 0xc) |
						                     ((u32)data.AdvCups[tokenGroupID].color[2] << 0x4);

						newInst->flags |= (DRAW_TRANSPARENT | USE_SPECULAR_LIGHT);

						warppadObj->lightDirToken = D232.lightDirToken[tokenGroupID];

						newInst->scale.x = 0x2000;
						newInst->scale.y = 0x2000;
						newInst->scale.z = 0x2000;
					}

					else
					{
						newInst->scale.x = 0x2800;
						newInst->scale.y = 0x2800;
						newInst->scale.z = 0x2800;
					}

					rewardAngle += 0x555;
				}

				return;
			}

			// if relic not owned
			if (CHECK_ADV_BIT(sdata->advProgress.rewards, levelID + ADV_REWARD_FIRST_SAPPHIRE_RELIC) == 0)
			{
				// open for relic/token
				t->modelIndex = 3;

				rewardModelID = STATIC_RELIC;
#ifdef CTR_AP
				{
					// Reward glow: the AP item at this track's (DEST) sapphire
					// relic location.
					int apModel = AP_WarpPadRewardModel(warppadObj->levelID + ADV_REWARD_FIRST_SAPPHIRE_RELIC);
					if (apModel >= 0)
						rewardModelID = apModel;
				}
#endif
				newInst = INSTANCE_Birth3D(gGT->modelPtr[rewardModelID], "prize2", t);

				// relic blue
				newInst->colorRGBA = 0x20a5ff0;
#ifdef CTR_AP
				{
					// Tint the relic-time-trial glow by the scouted relic TIER
					// (sapphire/gold/platinum) rather than always-blue.
					int tint = AP_WarpPadRewardTint(warppadObj->levelID + ADV_REWARD_FIRST_SAPPHIRE_RELIC);
					if (tint)
						newInst->colorRGBA = tint;
				}
#endif

				newInst->flags |= USE_SPECULAR_LIGHT;

				warppadObj->lightDirRelic = D232.lightDirRelic[0];

				// copy matrix
				*(int *)((int)&newInst->matrix + 0x0) = *(int *)((int)&inst->matrix + 0x0);
				*(int *)((int)&newInst->matrix + 0x4) = *(int *)((int)&inst->matrix + 0x4);
				*(int *)((int)&newInst->matrix + 0x8) = *(int *)((int)&inst->matrix + 0x8);
				*(int *)((int)&newInst->matrix + 0xC) = *(int *)((int)&inst->matrix + 0xC);
				*(s16 *)((int)&newInst->matrix + 0x10) = *(s16 *)((int)&inst->matrix + 0x10);
				newInst->matrix.t[0] = inst->matrix.t[0];
				newInst->matrix.t[1] = inst->matrix.t[1] + 0x100;
				newInst->matrix.t[2] = inst->matrix.t[2];

				newInst->scale.x = 0x1800;
				newInst->scale.y = 0x1800;
				newInst->scale.z = 0x1800;

				warppadObj->inst[WPIS_OPEN_PRIZE1] = newInst;
			}

			// if token owned
			if (CHECK_ADV_BIT(sdata->advProgress.rewards, levelID + ADV_REWARD_FIRST_CTR_TOKEN) != 0)
			{
				return;
			}

			tokenGroupID = data.metaDataLEV[levelID].ctrTokenGroupID;

			// open for relic/token
			t->modelIndex = 3;

			rewardModelID = STATIC_TOKEN;
#ifdef CTR_AP
			{
				// Reward glow: the AP item at this track's (DEST) token location.
				int apModel = AP_WarpPadRewardModel(warppadObj->levelID + ADV_REWARD_FIRST_CTR_TOKEN);
				if (apModel >= 0)
					rewardModelID = apModel;
			}
#endif
			newInst = INSTANCE_Birth3D(gGT->modelPtr[rewardModelID], "prize2", t);

			// token color
			newInst->colorRGBA = ((u32)data.AdvCups[tokenGroupID].color[0] << 0x14) | ((u32)data.AdvCups[tokenGroupID].color[1] << 0xc) |
			                     ((u32)data.AdvCups[tokenGroupID].color[2] << 0x4);

			newInst->flags |= (DRAW_TRANSPARENT | USE_SPECULAR_LIGHT);

			warppadObj->lightDirToken = D232.lightDirToken[tokenGroupID];

			// copy matrix
			*(int *)((int)&newInst->matrix + 0x0) = *(int *)((int)&inst->matrix + 0x0);
			*(int *)((int)&newInst->matrix + 0x4) = *(int *)((int)&inst->matrix + 0x4);
			*(int *)((int)&newInst->matrix + 0x8) = *(int *)((int)&inst->matrix + 0x8);
			*(int *)((int)&newInst->matrix + 0xC) = *(int *)((int)&inst->matrix + 0xC);
			*(s16 *)((int)&newInst->matrix + 0x10) = *(s16 *)((int)&inst->matrix + 0x10);
			newInst->matrix.t[0] = inst->matrix.t[0];
			newInst->matrix.t[1] = inst->matrix.t[1] + 0x100;
			newInst->matrix.t[2] = inst->matrix.t[2];

			newInst->scale.x = 0x2000;
			newInst->scale.y = 0x2000;
			newInst->scale.z = 0x2000;

			warppadObj->inst[WPIS_OPEN_PRIZE2] = newInst;

			return;

		SlideColTurboTrack:

			// if relic not owned
			if (levelID < AH_WP_NITRO_COURT)
			{
				if (CHECK_ADV_BIT(sdata->advProgress.rewards, levelID + ADV_REWARD_FIRST_SAPPHIRE_RELIC) == 0)
				{
					// SlideCol/TurboTrack
					if (levelID >= AH_WP_SLIDE_COLISEUM)
					{
						t->modelIndex = 4;
					}
					// open for token/relic
					else if (t->modelIndex != 1)
					{
						t->modelIndex = 3;
					}

					newInst = INSTANCE_Birth3D(gGT->modelPtr[STATIC_RELIC], "prize2", t);

					// relic blue
					newInst->colorRGBA = 0x20a5ff0;

					newInst->flags |= USE_SPECULAR_LIGHT;

					warppadObj->lightDirRelic = D232.lightDirRelic[0];

					newInst->scale.x = 0x1800;
					newInst->scale.y = 0x1800;
					newInst->scale.z = 0x1800;

					warppadObj->inst[WPIS_OPEN_PRIZE1] = newInst;
				}
			}

			for (i = 0; i < 3; i++)
			{
				newInst = warppadObj->inst[WPIS_OPEN_PRIZE1 + i];

				if (newInst == 0)
				{
					continue;
				}

				// copy matrix
				*(int *)((int)&newInst->matrix + 0x0) = *(int *)((int)&inst->matrix + 0x0);
				*(int *)((int)&newInst->matrix + 0x4) = *(int *)((int)&inst->matrix + 0x4);
				*(int *)((int)&newInst->matrix + 0x8) = *(int *)((int)&inst->matrix + 0x8);
				*(int *)((int)&newInst->matrix + 0xC) = *(int *)((int)&inst->matrix + 0xC);
				*(s16 *)((int)&newInst->matrix + 0x10) = *(s16 *)((int)&inst->matrix + 0x10);
				newInst->matrix.t[0] = inst->matrix.t[0];
				newInst->matrix.t[1] = inst->matrix.t[1] + 0x100;
				newInst->matrix.t[2] = inst->matrix.t[2];
			}
		}

		// slide col, turbo track
		else if (levelID < AH_WP_NITRO_COURT)
		{
			// already unlocked
			t->modelIndex = 2;

			goto SlideColTurboTrack;
		}

		// battle tracks
		else if ((((u16)(levelID - AH_WP_NITRO_COURT)) < 2) || (levelID == 21) || (levelID == 23))
		{
			i = R232.battleTrackArr[levelID - AH_WP_NITRO_COURT] + ADV_REWARD_FIRST_PURPLE_TOKEN;

			// already unlocked
			t->modelIndex = 2;

#ifdef CTR_AP
			// AP GLOW: ALWAYS birth the battle-arena prize slot so AH_WarpPad_ThTick's
			// glow pass can drive its model + hide-on-collect off AP checked-state.
			// Mirrors the trophy always-birth (~1413); do NOT gate on the cosmetic
			// crystal bit -- AP_ApplyItems clears it every frame, which left the pad
			// showing a stale vanilla CTR coin that never hid on collect (image-124).
			if ((((u16)(levelID - AH_WP_NITRO_COURT)) < 2) || (levelID == 21) || (levelID == 23))
#else
			if (CHECK_ADV_BIT(sdata->advProgress.rewards, i) == 0)
#endif
			{
				// rainbow
				t->modelIndex = 4;

				newInst = INSTANCE_Birth3D(gGT->modelPtr[STATIC_TOKEN], "prize2", t);

				newInst->flags |= USE_SPECULAR_LIGHT;

				tokenGroupID = 4;

				// token color
				newInst->colorRGBA = ((u32)data.AdvCups[tokenGroupID].color[0] << 0x14) | ((u32)data.AdvCups[tokenGroupID].color[1] << 0xc) |
				                     ((u32)data.AdvCups[tokenGroupID].color[2] << 0x4);

				warppadObj->lightDirToken = D232.lightDirToken[tokenGroupID];

				newInst->scale.x = 0x2000;
				newInst->scale.y = 0x2000;
				newInst->scale.z = 0x2000;

				warppadObj->inst[WPIS_OPEN_PRIZE1] = newInst;

				// for matrix copy
				goto SlideColTurboTrack;
			}
		}

		// gem cups
		else if (((u16)(levelID - AH_WP_ADV_CUP)) < 5)
		{
			// bit index of gem
			i = (levelID - AH_WP_ADV_CUP) + ADV_REWARD_FIRST_GEM;

#ifndef CTR_AP
			// if gem is already unlocked, quit
			if (CHECK_ADV_BIT(sdata->advProgress.rewards, i) != 0)
			{
				// beaten
				t->modelIndex = 2;

				return;
			}
#else
			// AP GLOW: ALWAYS birth the gem-cup prize slot (do NOT early-return on the
			// cosmetic gem bit, which AP_ApplyItems clears every frame) so ThTick's
			// glow pass can drive its model + hide-on-collect off AP checked-state.
			// Mirrors the trophy / battle-arena always-birth.
#endif

			// rainbow color
			t->modelIndex = 4;

			newInst = INSTANCE_Birth3D(gGT->modelPtr[STATIC_GEM], "prize2", t);

			newInst->flags |= USE_SPECULAR_LIGHT;

			i = levelID - AH_WP_ADV_CUP;

			// token color
			newInst->colorRGBA = ((u32)data.AdvCups[i].color[0] << 0x14) | ((u32)data.AdvCups[i].color[1] << 0xc) | ((u32)data.AdvCups[i].color[2] << 0x4);

			warppadObj->inst[WPIS_OPEN_PRIZE1] = newInst;

			// store in Gem array
			warppadObj->lightDirGem = D232.lightDirGem[i];

			newInst->scale.x = 0x2000;
			newInst->scale.y = 0x2000;
			newInst->scale.z = 0x2000;

			// for matrix copy
			goto SlideColTurboTrack;
		}

		return;
	}

	// === if locked ===

	if (unlockItem_numNeeded < 10)
	{
		warppadObj->digit10s = 0;
		warppadObj->digit1s = unlockItem_numNeeded;
	}

	else
	{
		warppadObj->digit10s = 1;
		warppadObj->digit1s = unlockItem_numNeeded - 10;
	}

	// ====== Item ========

	// WPIS_CLOSED_ITEM
	newInst = INSTANCE_Birth3D(gGT->modelPtr[unlockItem_modelID], "reqObj", t);

	// copy matrix
	*(int *)((int)&newInst->matrix + 0x0) = *(int *)((int)&inst->matrix + 0x0);
	*(int *)((int)&newInst->matrix + 0x4) = *(int *)((int)&inst->matrix + 0x4);
	*(int *)((int)&newInst->matrix + 0x8) = *(int *)((int)&inst->matrix + 0x8);
	*(int *)((int)&newInst->matrix + 0xC) = *(int *)((int)&inst->matrix + 0xC);
	*(s16 *)((int)&newInst->matrix + 0x10) = *(s16 *)((int)&inst->matrix + 0x10);
	newInst->matrix.t[0] = inst->matrix.t[0];
	newInst->matrix.t[1] = inst->matrix.t[1] + 0x100;
	newInst->matrix.t[2] = inst->matrix.t[2];

	newInst->scale.x = 0x2000;
	newInst->scale.y = 0x2000;
	newInst->scale.z = 0x2000;

	// no specular for trophy
	if (unlockItem_modelID != STATIC_TROPHY)
	{
		newInst->flags |= USE_SPECULAR_LIGHT;

		// relic
		if (unlockItem_modelID == STATIC_RELIC)
		{
			// Relic blue color
			newInst->colorRGBA = 0x20a5ff0;

			warppadObj->lightDirRelic = D232.lightDirRelic[0];
		}

		// Key
		else if (unlockItem_modelID == STATIC_KEY)
		{
			// Key color
			newInst->colorRGBA = 0xdca6000;

			// store in Gem array (intended by ND, not a bug)
			warppadObj->lightDirGem = D232.lightDirGem[0];
		}

		// Gem
		else if (unlockItem_modelID == STATIC_GEM)
		{
			newInst->colorRGBA = ((u32)data.AdvCups[0].color[0] << 0x14) | ((u32)data.AdvCups[0].color[1] << 0xc) | ((u32)data.AdvCups[0].color[2] << 0x4);

			// store in Gem array
			warppadObj->lightDirGem = D232.lightDirGem[0];
		}

		// assume token
		else
		{
			i = levelID - AH_WP_ADV_CUP;

#ifdef CTR_AP
			// Cosmetic-index fix: `i` is the cup colour ONLY for gem-cup pads
			// (levelID 100..104 -> 0..4). Under AP randomization a NON-cup pad
			// (race/trial/arena, levelID < 100) can carry a Token requirement,
			// making i negative -> data.AdvCups[i] / D232.lightDirToken[i] read
			// out of bounds (garbage tint / possible bad light dir). Clamp to a
			// valid colour slot; the closed-req token tint is purely cosmetic.
			if (i < 0 || i > 4)
				i = 0;
#endif

			// token color
			newInst->colorRGBA = ((u32)data.AdvCups[i].color[0] << 0x14) | ((u32)data.AdvCups[i].color[1] << 0xc) | ((u32)data.AdvCups[i].color[2] << 0x4);

			warppadObj->lightDirToken = D232.lightDirToken[i];
		}
	}

	warppadObj->inst[WPIS_CLOSED_ITEM] = newInst;

	// ====== "X" ========

	// WPIS_CLOSED_X
	newInst = INSTANCE_Birth3D(gGT->modelPtr[STATIC_BIGX], "x", t);

	// copy matrix
	*(int *)((int)&newInst->matrix + 0x0) = 0x1000;
	*(int *)((int)&newInst->matrix + 0x4) = 0;
	*(int *)((int)&newInst->matrix + 0x8) = 0x1000;
	*(int *)((int)&newInst->matrix + 0xC) = 0;
	*(s16 *)((int)&newInst->matrix + 0x10) = 0x1000;
	newInst->matrix.t[0] = inst->matrix.t[0];
	newInst->matrix.t[1] = inst->matrix.t[1] + 0x100;
	newInst->matrix.t[2] = inst->matrix.t[2];

	newInst->scale.x = 0x2000;
	newInst->scale.y = 0x2000;
	newInst->scale.z = 0x2000;

	// always face camera
	newInst->model->headers[0].flags |= 1;

	warppadObj->inst[WPIS_CLOSED_X] = newInst;

	// ====== "10s" ========

	if (warppadObj->digit10s != 0)
	{
		// WPIS_CLOSED_10S
		newInst = INSTANCE_Birth3D(gGT->modelPtr[STATIC_BIG1], "warpnum", t);

		// copy matrix
		*(int *)((int)&newInst->matrix + 0x0) = 0x1000;
		*(int *)((int)&newInst->matrix + 0x4) = 0;
		*(int *)((int)&newInst->matrix + 0x8) = 0x1000;
		*(int *)((int)&newInst->matrix + 0xC) = 0;
		*(s16 *)((int)&newInst->matrix + 0x10) = 0x1000;
		newInst->matrix.t[0] = inst->matrix.t[0];
		newInst->matrix.t[1] = inst->matrix.t[1] + 0x100;
		newInst->matrix.t[2] = inst->matrix.t[2];

		newInst->scale.x = 0x2000;
		newInst->scale.y = 0x2000;
		newInst->scale.z = 0x2000;

		// always face camera
		for (i = 0; i < newInst->model->numHeaders; i++)
		{
			newInst->model->headers[i].flags |= 1;
		}

		warppadObj->inst[WPIS_CLOSED_10S] = newInst;
	}

	// ====== "1s" ========

	// STATIC_BIG (1-8)
	i = 0x38;
	if (warppadObj->digit1s == 0)
	{
		i = 0x6d; // '0'
	}
	if (warppadObj->digit1s == 9)
	{
		i = 0x6e; // '9'
	}

	// WPIS_CLOSED_1S
	newInst = INSTANCE_Birth3D(gGT->modelPtr[i], "warpnum", t);

	// copy matrix
	*(int *)((int)&newInst->matrix + 0x0) = 0x1000;
	*(int *)((int)&newInst->matrix + 0x4) = 0;
	*(int *)((int)&newInst->matrix + 0x8) = 0x1000;
	*(int *)((int)&newInst->matrix + 0xC) = 0;
	*(s16 *)((int)&newInst->matrix + 0x10) = 0x1000;
	newInst->matrix.t[0] = inst->matrix.t[0];
	newInst->matrix.t[1] = inst->matrix.t[1] + 0x100;
	newInst->matrix.t[2] = inst->matrix.t[2];

	newInst->scale.x = 0x2000;
	newInst->scale.y = 0x2000;
	newInst->scale.z = 0x2000;

	// always face camera
	for (i = 0; i < newInst->model->numHeaders; i++)
	{
		newInst->model->headers[i].flags |= 1;
	}

	warppadObj->inst[WPIS_CLOSED_1S] = newInst;
}
