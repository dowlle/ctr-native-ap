#include <common.h>

enum AHMapIconID
{
	AH_MAP_ICON_WARPPAD = 0x31,
	AH_MAP_ICON_BOSS_STAR = 0x37,
};

enum AHMapArrowType
{
	AH_MAP_ARROW_WARPPAD_TROPHY = 0,
	AH_MAP_ARROW_HUB_ROUTE,
	AH_MAP_ARROW_BOSS,
};

enum AHMapColor
{
	AH_MAP_COLOR_COMPLETE = RED,
	AH_MAP_COLOR_FLASH_PRIMARY = CRASH_BLUE,
	AH_MAP_COLOR_FLASH_SECONDARY = WHITE,
	AH_MAP_COLOR_RELIC_TOKEN = PAPU_YELLOW,
	AH_MAP_COLOR_INVALID = BLACK,
	AH_MAP_COLOR_LOCKED = GRAY,
};

CTR_STATIC_ASSERT(AH_MAP_ICON_WARPPAD == 0x31);
CTR_STATIC_ASSERT(AH_MAP_ICON_BOSS_STAR == 0x37);
CTR_STATIC_ASSERT(AH_MAP_ARROW_WARPPAD_TROPHY == 0);
CTR_STATIC_ASSERT(AH_MAP_ARROW_HUB_ROUTE == 1);
CTR_STATIC_ASSERT(AH_MAP_ARROW_BOSS == 2);
CTR_STATIC_ASSERT(AH_MAP_COLOR_COMPLETE == 3);
CTR_STATIC_ASSERT(AH_MAP_COLOR_FLASH_PRIMARY == 5);
CTR_STATIC_ASSERT(AH_MAP_COLOR_FLASH_SECONDARY == 4);
CTR_STATIC_ASSERT(AH_MAP_COLOR_RELIC_TOKEN == 0xe);
CTR_STATIC_ASSERT(AH_MAP_COLOR_INVALID == 0x15);
CTR_STATIC_ASSERT(AH_MAP_COLOR_LOCKED == 0x17);

enum AHMapBossItemState
{
	AH_MAP_BOSS_ITEM_NONE = -1,
	AH_MAP_BOSS_ITEM_LOCKED = 0,
	AH_MAP_BOSS_ITEM_OPEN = 1,
	AH_MAP_BOSS_ITEM_COMPLETE = 2,
};

CTR_STATIC_ASSERT(AH_MAP_BOSS_ITEM_NONE == -1);
CTR_STATIC_ASSERT(AH_MAP_BOSS_ITEM_LOCKED == 0);
CTR_STATIC_ASSERT(AH_MAP_BOSS_ITEM_OPEN == 1);
CTR_STATIC_ASSERT(AH_MAP_BOSS_ITEM_COMPLETE == 2);

enum AHMapMainConstants
{
	AH_MAP_HUD_AND_DEBUG_SPEEDOMETER = 0x8,
	AH_MAP_ICON_TOP_HALF = 3,
	AH_MAP_ICON_BOTTOM_HALF = 4,
	AH_MAP_SCREEN_POS_X = 500,
	AH_MAP_SCREEN_POS_Y = 195,
	AH_MAP_HUD_SLOT_SLIDE_METER = 8,
	AH_MAP_HUD_SLOT_RELIC_COUNT = 0xe,
	AH_MAP_HUD_SLOT_KEY_COUNT = 0xf,
	AH_MAP_HUD_SLOT_TROPHY_COUNT = 0x10,
	AH_MAP_HUD_COUNTER_OFFSET_X = 0x10,
	AH_MAP_HUD_COUNTER_OFFSET_Y = -10,
};

CTR_STATIC_ASSERT(AH_MAP_HUD_AND_DEBUG_SPEEDOMETER == 0x8);
CTR_STATIC_ASSERT(AH_MAP_ICON_TOP_HALF == 3);
CTR_STATIC_ASSERT(AH_MAP_ICON_BOTTOM_HALF == 4);
CTR_STATIC_ASSERT(AH_MAP_SCREEN_POS_X == 500);
CTR_STATIC_ASSERT(AH_MAP_SCREEN_POS_Y == 195);
CTR_STATIC_ASSERT(AH_MAP_HUD_SLOT_SLIDE_METER == 8);
CTR_STATIC_ASSERT(AH_MAP_HUD_SLOT_RELIC_COUNT == 0xe);
CTR_STATIC_ASSERT(AH_MAP_HUD_SLOT_KEY_COUNT == 0xf);
CTR_STATIC_ASSERT(AH_MAP_HUD_SLOT_TROPHY_COUNT == 0x10);
CTR_STATIC_ASSERT(AH_MAP_HUD_COUNTER_OFFSET_X == 0x10);
CTR_STATIC_ASSERT(AH_MAP_HUD_COUNTER_OFFSET_Y == -10);

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b0b98-0x800b0ce0.
void AH_Map_LoadSave_Prim(s16 *vertPos, char *vertCol, void *ot, struct PrimMem *primMem)
{
	POLY_G4 *p = primMem->cursor;

	if (primMem->end < (void *)p)
	{
		return;
	}

	primMem->cursor = p + 1;

	setPolyG4(p);

	p->r0 = vertCol[0];
	p->g0 = vertCol[1];
	p->b0 = vertCol[2];

	p->r1 = vertCol[4];
	p->g1 = vertCol[5];
	p->b1 = vertCol[6];

	p->r2 = vertCol[8];
	p->g2 = vertCol[9];
	p->b2 = vertCol[10];

	p->r3 = vertCol[12];
	p->g3 = vertCol[13];
	p->b3 = vertCol[14];

	p->x0 = vertPos[0x0];
	p->y0 = vertPos[0x1];

	p->x1 = vertPos[0x2];
	p->y1 = vertPos[0x3];

	p->x2 = vertPos[0x4];
	p->y2 = vertPos[0x5];

	p->x3 = vertPos[0x6];
	p->y3 = vertPos[0x7];

	AddPrim(ot, p);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b0ce0-0x800b0f18.
void AH_Map_LoadSave_Full(int posX, int posY, s16 *vertPos, char *vertCol, int scale, int angle)
{
	s16 local_30[8];
	s16 local_20[8];

	struct GameTracker *gGT = sdata->gGT;

	int sin = MATH_Sin(angle);
	int cos = MATH_Cos(angle);

	for (int i = 0; i < 4; i++)
	{
		local_30[i * 2 + 0] = posX + 6 +
		                      (s16)(((((vertPos[2 * i + 0] * cos) >> 0xc) + ((vertPos[2 * i + 1] * sin) >> 0xc)) * ((scale * 8) / 5)

		                                 ) >>
		                            0xc);

		local_30[i * 2 + 1] = posY + 4 +
		                      (s16)(((((vertPos[2 * i + 1] * cos) >> 0xc) - ((vertPos[2 * i + 0] * sin) >> 0xc)) * scale

		                             ) >>
		                            0xc);
	}

	s16 *offset = &D232.primOffsetXY_LoadSave[0];

	for (int i = 0; i < 5; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			local_20[j * 2 + 0] = local_30[j * 2 + 0] + offset[i * 2 + 0];

			local_20[j * 2 + 1] = local_30[j * 2 + 1] + offset[i * 2 + 1];
		}

		AH_Map_LoadSave_Prim(&local_20[0], vertCol, gGT->pushBuffer_UI.ptrOT, &gGT->backBuffer->primMem);

		vertCol = (char *)&D232.colorQuad[0];
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b0f18-0x800b1150.
void AH_Map_HubArrow(int posX, int posY, s16 *vertPos, char *vertCol, int scale, int angle)
{
	s16 local_30[6];
	s16 local_20[6];

	struct GameTracker *gGT = sdata->gGT;

	int sin = MATH_Sin(angle);
	int cos = MATH_Cos(angle);

	for (int i = 0; i < 3; i++)
	{
		local_30[i * 2 + 0] = posX + 6 +
		                      (s16)(((((vertPos[2 * i + 0] * cos) >> 0xc) + ((vertPos[2 * i + 1] * sin) >> 0xc)) * ((scale * 8) / 5)

		                                 ) >>
		                            0xc);

		local_30[i * 2 + 1] = posY + 4 +
		                      (s16)(((((vertPos[2 * i + 1] * cos) >> 0xc) - ((vertPos[2 * i + 0] * sin) >> 0xc)) * scale

		                             ) >>
		                            0xc);
	}

	s16 *offset = &D232.primOffsetXY_HubArrow[0];

	for (int i = 0; i < 5; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			local_20[j * 2 + 0] = local_30[j * 2 + 0] + offset[i * 2 + 0];

			local_20[j * 2 + 1] = local_30[j * 2 + 1] + offset[i * 2 + 1];
		}

		RECTMENU_DrawRwdTriangle(&local_20[0], vertCol, gGT->pushBuffer_UI.ptrOT, &gGT->backBuffer->primMem);

		vertCol = (char *)&D232.colorTri[0];
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b1150-0x800b14f4.
void AH_Map_HubArrowOutter(s16 *mapData, int arrowIndex, int posX, int posY, int inputAngle, int type)
{
	struct GameTracker *gGT;
	gGT = sdata->gGT;

	(void)mapData;

	arrowIndex = (s16)arrowIndex;
	type = (s16)type;

	posX += D232.hubArrowXY_Inner[2 * type + 0];
	posY += D232.hubArrowXY_Inner[2 * type + 1];

	int timer = gGT->timer >> 0;

	int var14;
	int var15;
	int var8;

	var15 = 0x40;
	if ((timer & 1) != 0)
	{
		var15 = 0xe0;
	}

	if (type == 0)
	{
		var14 = var15;
		var8 = 0x200;
	}

	else if (type == 1)
	{
		var14 = 0xff;
		var8 = 0x555;

		posX += D232.hubArrowXY_Outter[2 * (((inputAngle >> 0x8) & 0xc) >> 2) + 0];
		posY += D232.hubArrowXY_Outter[2 * (((inputAngle >> 0x8) & 0xc) >> 2) + 1];
	}

	else
	{
		var14 = var15;
		var8 = 0x199;
		inputAngle ^= 0x800;
	}

	inputAngle = (s16)inputAngle;

	for (int iVar10 = 0; iVar10 < 3; iVar10++)
	{
		u32 var5 = (~(timer + (int)arrowIndex * 0xc) & 0x3f) + (2 - (int)(s16)iVar10) * -6;

		if (var5 >= 0xc)
		{
			continue;
		}

		int iVar16 = ((var5 * 0x2aa + 0x1000) * 0x10000) >> 0x1a;

		int bVar1 = 1;

		int shiftToggle = 1;

		int iVar6 = 0;
		int iVar9 = 0;

		for (int iVar13 = 0; iVar13 < var8 + 0xfff; iVar13 += var8)
		{
			if (type != 2)
			{
				shiftToggle = 0;
			}

			int angle = iVar13 + inputAngle;

			int sin = MATH_Sin(angle);
			int cos = MATH_Cos(angle);

			int iVar4 = (shiftToggle & 1) + 0xc;

			sin = posX + ((((iVar16 << 3) / 5) * sin) >> iVar4);
			cos = posY - ((((iVar16)) * cos) >> iVar4);

			if (!bVar1)
			{
				CTR_Box_DrawWirePrims((Point){{iVar9, iVar6}}, (Point){{sin, cos}}, MakeColor(var14, var15, 0xff), (void *)gGT->pushBuffer_UI.ptrOT);
			}

			bVar1 = 0;
			iVar9 = sin;
			iVar6 = cos;
			shiftToggle++;
		}
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b14f4-0x800b1a18.
void AH_Map_HubItems(s16 *mapData, s16 *arrowCounter)
{
	struct GameTracker *gGT = sdata->gGT;
	struct AdvProgress *adv = &sdata->advProgress;
	s16 levelID = gGT->levelID;
	struct HubItem *item = D232.hubItemsXY_ptrArray[levelID - GEM_STONE_VALLEY];
	int pos3D[3];

	if (item->posX != -1)
	{
		do
		{
			AdventureHubItemType iconType = item->iconType;
			s16 routeLockState = -1;
			s16 bossState = AH_MAP_BOSS_ITEM_NONE;

			b32 open = true;

			// One-key route arrow, only locked in N. Sanity Beach.
			if (iconType == AH_HUB_ITEM_ROUTE_KEY1_IF_BEACH)
			{
				routeLockState = 0;

				if (levelID == N_SANITY_BEACH)
				{
					// locked if key < 1
					routeLockState = (gGT->currAdvProfile.numKeys < 1);
				}
			}
			else if (AH_HUB_ITEM_ROUTE_KEY1_IF_BEACH < iconType)
			{
				// gemstone valley
				if (iconType == AH_HUB_ITEM_OXIDE_WARPPAD)
				{
					// check 4 boss keys
					for (int i = 0; i < 4; i++)
					{
						u32 bit = i + ADV_REWARD_FIRST_BOSS_KEY;

						if (CHECK_ADV_BIT(adv->rewards, bit) == 0)
						{
							open = false;
							break;
						}
					}

					if (open)
					{
						bossState = ((adv->storyFlags & ADV_REWARD_BEAT_OXIDE_FIRST_BOSS_MASK) != 0) ? AH_MAP_BOSS_ITEM_COMPLETE : AH_MAP_BOSS_ITEM_OPEN;
					}
					else
					{
						bossState = AH_MAP_BOSS_ITEM_LOCKED;
					}
				}
				else if (AH_HUB_ITEM_PINSTRIPE_GARAGE < iconType)
				{
					// save/load screen synthetic hub marker
					if (iconType == AH_HUB_ITEM_SAVE_LOAD_MARKER)
					{
						int saveLoadPosX = (int)item->posX - 0x200;
						int saveLoadPosY = (int)item->posY - 0x100;

						UI_Map_GetIconPos(mapData, &saveLoadPosX, &saveLoadPosY);

						AH_Map_LoadSave_Full(saveLoadPosX, saveLoadPosY, &D232.loadSave_pos[0], (char *)&D232.loadSave_col[0], 0x800, (int)item->angle);
					}
				}
				else
				{
					int base = levelID - N_SANITY_BEACH;
					s16 *trophies = &data.advHubTrackIDs[base * 4];

					for (int i = 0; i < 4; i++)
					{
						if (CHECK_ADV_BIT(adv->rewards, trophies[i] + ADV_REWARD_FIRST_TROPHY) == 0)
						{
							open = false;
							break;
						}
					}

					if (open)
					{
						bossState = CHECK_ADV_BIT(adv->rewards, base + ADV_REWARD_FIRST_BOSS_KEY) ? AH_MAP_BOSS_ITEM_COMPLETE : AH_MAP_BOSS_ITEM_OPEN;
					}
					else
					{
						bossState = AH_MAP_BOSS_ITEM_LOCKED;
					}
				}
			}
			// Two-key route arrow.
			else if (iconType == AH_HUB_ITEM_ROUTE_KEY2)
			{
				// locked if keys < 2
				routeLockState = (gGT->currAdvProfile.numKeys < 2);
			}
			else if (iconType < AH_HUB_ITEM_ROUTE_OPEN_B)
			{
				// Three-key route arrow.
				if (iconType == AH_HUB_ITEM_ROUTE_KEY3)
				{
					// locked if keys < 3
					routeLockState = (gGT->currAdvProfile.numKeys < 3);
				}
			}
			// Open route arrows.
			else if ((iconType == AH_HUB_ITEM_ROUTE_OPEN_B) || (iconType == AH_HUB_ITEM_ROUTE_OPEN_A))
			{
				// never locked
				routeLockState = 0;
			}

			if (routeLockState >= 0)
			{
				int routePosX = (int)item->posX - 0x200;
				int routePosY = (int)item->posY - 0x100;

				UI_Map_GetIconPos(mapData, &routePosX, &routePosY);
				if ((routeLockState == 0) && (D232.mapPriorityArrowDrawn == 0))
				{
					AH_Map_HubArrowOutter(mapData, (int)*arrowCounter, routePosX, routePosY, (0x1000 - (u16)item->angle), AH_MAP_ARROW_HUB_ROUTE);
					*arrowCounter = *arrowCounter + 1;
				}

				int colorOffset;

				// if even frame
				if ((gGT->timer & 2) == 0)
				{
					colorOffset = (int)routeLockState * 6;
				}
				else
				{
					colorOffset = ((int)routeLockState * 2 + 1) * 3;
				}

				AH_Map_HubArrow(routePosX, routePosY, &D232.hubArrow_pos[0], (char *)&D232.hubArrow_col1[colorOffset], 0x800, (int)item->angle);
			}

			if (bossState >= AH_MAP_BOSS_ITEM_LOCKED)
			{
				pos3D[0] = (int)item->posX;
				pos3D[1] = 0;
				pos3D[2] = (int)item->posY;

				// if beat boss race
				int bossIconColor;

				if (bossState == AH_MAP_BOSS_ITEM_COMPLETE)
				{
					// red
					bossIconColor = AH_MAP_COLOR_COMPLETE;
				}
				else
				{
					// locked boss race
					// bossState == AH_MAP_BOSS_ITEM_LOCKED

					// grey
					bossIconColor = AH_MAP_COLOR_LOCKED;

					// open, not beaten
					if (bossState == AH_MAP_BOSS_ITEM_OPEN)
					{
						// blue and white
						// depending on frames
						bossIconColor = AH_MAP_COLOR_FLASH_PRIMARY;
						if ((gGT->timer & 2) != 0)
						{
							bossIconColor = AH_MAP_COLOR_FLASH_SECONDARY;
						}
					}
				}

				// open, not beaten
				if (bossState == AH_MAP_BOSS_ITEM_OPEN)
				{
					D232.mapPriorityArrowDrawn = bossState;

					int bossArrowPosX = pos3D[0];
					int bossArrowPosY = pos3D[2];

					UI_Map_GetIconPos(mapData, &bossArrowPosX, &bossArrowPosY);

					AH_Map_HubArrowOutter(mapData, (int)*arrowCounter, bossArrowPosX, bossArrowPosY, 0, AH_MAP_ARROW_BOSS);

					*arrowCounter = *arrowCounter + 1;
				}

				// draw star icon for boss
				UI_Map_DrawRawIcon((int)mapData, &pos3D[0], AH_MAP_ICON_BOSS_STAR, bossIconColor, 0, 0x1000);
			}
			item++;
		} while (item->posX != -1);
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b1a18-0x800b1c90.
void AH_Map_Warppads(s16 *mapData, struct Thread *warppadThread, s16 *arrowCounter)
{
	struct GameTracker *gGT = sdata->gGT;

	// find minDistance, set to max
	int minDistance = 0x7fffffff;
	struct Instance *closestWarppadInst = NULL;

	MATRIX *driverMatrix = &gGT->drivers[0]->instSelf->matrix;

	for (
	    /**/; warppadThread != NULL; warppadThread = warppadThread->siblingThread)
	{
		int visualState = warppadThread->modelIndex;
		b32 drawsTrophyArrow = false;
		b32 includeInSoundDistance = true;

		struct Instance *warppadInst = warppadThread->inst;
		int color;

		switch ((u32)visualState)
		{
		case AH_WP_VISUAL_LOCKED:
			color = AH_MAP_COLOR_LOCKED;
			includeInSoundDistance = false;
			break;
		case AH_WP_VISUAL_TROPHY_OPEN:
			color = AH_MAP_COLOR_FLASH_PRIMARY;
			if ((gGT->timer & 2) != 0)
			{
				color = AH_MAP_COLOR_FLASH_SECONDARY;
			}
			drawsTrophyArrow = true;
			break;
		case AH_WP_VISUAL_COMPLETE:
			color = AH_MAP_COLOR_COMPLETE;
			break;
		case AH_WP_VISUAL_RELIC_TOKEN_OPEN:
			color = AH_MAP_COLOR_RELIC_TOKEN;
			break;
		case AH_WP_VISUAL_COLOR_CYCLE_OPEN:
			// Each Slide Coliseum/Turbo Track color lasts two frames.
			color = ((gGT->timer >> 1) & 7) + AH_MAP_COLOR_FLASH_PRIMARY;
			break;
		default:
			color = AH_MAP_COLOR_INVALID;
			includeInSoundDistance = false;
			break;
		}

		if (drawsTrophyArrow)
		{
			// get posZ in 3D, turns into posY in 2D
			int arrowPosX = warppadInst->matrix.t[0];
			int arrowPosY = warppadInst->matrix.t[2];

			D232.mapPriorityArrowDrawn = 1;

			// Get Icon Dimensions
			UI_Map_GetIconPos(mapData, &arrowPosX, &arrowPosY);

			AH_Map_HubArrowOutter(mapData, (int)*arrowCounter, arrowPosX, arrowPosY, 0, AH_MAP_ARROW_WARPPAD_TROPHY);

			*arrowCounter = *arrowCounter + 1;
		}

		UI_Map_DrawRawIcon((int)mapData, (int *)&warppadInst->matrix.t[0], AH_MAP_ICON_WARPPAD, color, 0, 0x1000);

		if (!includeInSoundDistance)
		{
			// skip distance check
			continue;
		}

		int distX = warppadInst->matrix.t[0] - driverMatrix->t[0];
		int distY = warppadInst->matrix.t[1] - driverMatrix->t[1];
		int distZ = warppadInst->matrix.t[2] - driverMatrix->t[2];

		int currDistance = SquareRoot0_stub(distX * distX + distY * distY + distZ * distZ);

		if (minDistance > currDistance)
		{
			minDistance = currDistance;
			closestWarppadInst = warppadInst;
		}
	}

	// play sound from closest unlocked warppad
	if (closestWarppadInst != NULL)
	{
		PlayWarppadSound(minDistance << 1);
	}

	return;
}

#if defined(CTR_NATIVE)
force_inline void AH_MaskHint_DrawRepeatPrompt(void);
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b1c90-0x800b1ef8.
void AH_Map_Main(void)
{
	struct GameTracker *gGT = sdata->gGT;

	sdata->HudAndDebugFlags &= ~AH_MAP_HUD_AND_DEBUG_SPEEDOMETER;

	s16 driverIconCounter = 0;
	s16 arrowCounter = 0;
	struct Driver *advDriver = gGT->drivers[0];
	struct UiElement2D *hud = data.hudStructPtr[gGT->numPlyrCurrGame - 1];
	s16 *mapData = NULL;

	int raceFlagState = RaceFlag_GetCanDraw();
	if (raceFlagState == 0)
	{
		RaceFlag_SetCanDraw(1);
	}

	if (
	    // if Aku Hint is not unlocked
	    (CHECK_ADV_BIT(sdata->advProgress.rewards, ADV_REWARD_HINT_WELCOME_TO_ARENA) == 0) &&

	    (RaceFlag_IsFullyOffScreen() != 0))
	{
		// Trigger Aku Hint:
		// Welcome to Adventure Arena
		MainFrame_RequestMaskHint(ADV_MASK_HINT_ID_WELCOME_TO_ARENA, 0);
	}


	// NOTE(aalhendi): Retail keeps this AI-only Adventure Hub speedometer fallback.
	if ((gGT->numPlyrCurrGame == 0) && ((advDriver->actionsFlagSet & ACTION_BOT) != 0))
	{
		sdata->HudAndDebugFlags = AH_MAP_HUD_AND_DEBUG_SPEEDOMETER;
	}

	if (gGT->level1->ptrSpawnType1->count != 0)
	{
		void **pointers = ST1_GETPOINTERS(gGT->level1->ptrSpawnType1);
		mapData = (s16 *)pointers[ST1_MAP];
	}

	// if game is not paused
	if ((gGT->gameMode1 & PAUSE_ALL) == 0)
	{
		// Jump meter and landing boost
		UI_JumpMeter_Update(advDriver);
	}

	if ((gGT->hudFlags & AH_HUD_FLAG_HIDE_MAP) == 0)
	{
		arrowCounter = 0;

		D232.mapPriorityArrowDrawn = 0;

		UI_Map_DrawDrivers((int)mapData, gGT->threadBuckets[PLAYER].thread, &driverIconCounter);

		AH_Map_Warppads(mapData, gGT->threadBuckets[WARPPAD].thread, &arrowCounter);

		AH_Map_HubItems(mapData, &arrowCounter);

		UI_Map_DrawMap(gGT->ptrIcons[AH_MAP_ICON_TOP_HALF], gGT->ptrIcons[AH_MAP_ICON_BOTTOM_HALF],

		               AH_MAP_SCREEN_POS_X, AH_MAP_SCREEN_POS_Y,

		               &gGT->backBuffer->primMem, gGT->pushBuffer_UI.ptrOT, 1);

		UI_DrawSlideMeter(hud[AH_MAP_HUD_SLOT_SLIDE_METER].x, hud[AH_MAP_HUD_SLOT_SLIDE_METER].y, advDriver);
	}

	UI_DrawNumRelic(hud[AH_MAP_HUD_SLOT_RELIC_COUNT].x + AH_MAP_HUD_COUNTER_OFFSET_X, hud[AH_MAP_HUD_SLOT_RELIC_COUNT].y + AH_MAP_HUD_COUNTER_OFFSET_Y);
	UI_DrawNumKey(hud[AH_MAP_HUD_SLOT_KEY_COUNT].x + AH_MAP_HUD_COUNTER_OFFSET_X, hud[AH_MAP_HUD_SLOT_KEY_COUNT].y + AH_MAP_HUD_COUNTER_OFFSET_Y);
	UI_DrawNumTrophy(hud[AH_MAP_HUD_SLOT_TROPHY_COUNT].x + AH_MAP_HUD_COUNTER_OFFSET_X, hud[AH_MAP_HUD_SLOT_TROPHY_COUNT].y + AH_MAP_HUD_COUNTER_OFFSET_Y);

#if defined(CTR_NATIVE)
	// NOTE(aalhendi): Retail appends this prompt after DrawOTag starts; the PS1
	// GPU can still consume that late OT write. Native DrawOTag parses
	// synchronously, so emit only this static prompt during the hub UI pass and
	// leave AH_MaskHint_Update to run the real state/audio timing later.
	if (sdata->AkuAkuHintState == 5)
	{
		AH_MaskHint_DrawRepeatPrompt();
	}
#endif
}
