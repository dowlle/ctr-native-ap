#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8004d614-0x8004d8b4.
void UI_Map_DrawMap(struct Icon *mapTop, struct Icon *mapBottom, s16 posX, s16 posY, struct PrimMem *primMem, uint32_t *otMem, u32 colorID)
{
	s16 mapBottomHeight;
	s16 mapTopHeight;
	int iVar9;
	POLY_FT4 *p;
	u32 color;
	u32 transparency;
	struct GameTracker *gGT;

	gGT = sdata->gGT;

	iVar9 = 0;

	// draw minimap with neutral/none vertex color, minimap's regular color is white
	color = 0x808080;
	transparency = colorID;

	// draw map black
	// used for the minimap shadow in the track select screen
	if (colorID == 2)
	{
		color = 0;
		transparency = 0;
	}

	// draw minimap blue
	// used for the minimap outline in the track select screen
	if (colorID == 3)
	{
		color = 0x402000;
		transparency = 0;
	}

	if ((gGT->level1->ptrSpawnType1 != NULL) && (gGT->level1->ptrSpawnType1->count != 0))
	{
		void **pointers = ST1_GETPOINTERS(gGT->level1->ptrSpawnType1);
		iVar9 = (int)pointers[ST1_MAP];
	}

	// position of the bottom margin of the primitive for the bottom half of the minimap
	mapBottomHeight = mapBottom->texLayout.v2 - mapBottom->texLayout.v0;

	p = (POLY_FT4 *)primMem->cursor;

	// if these conditions are met, then draw the top half of the minimap; otherwise, only draw the bottom half
	// not sure when the game ever draws only the bottom half
	if (((iVar9 != 0) && (*(s16 *)(iVar9 + 0x12) == 0)) ||

	    // if in main menu (character selection, track selection, any part of it)
	    ((gGT->gameMode1 & MAIN_MENU) != 0))
	{
		// r0, g0, b0 (vertex color)
		CtrGpu_WriteColorCode(&p->r0, color);

		// position of the top margin of the primitive for the top half of the minimap
		mapTopHeight = posY - (((u16)mapTop->texLayout.v2 - (u16)mapTop->texLayout.v0) + mapBottomHeight);

		p->y0 = mapTopHeight;
		p->y1 = mapTopHeight;
		p->y2 = posY - mapBottomHeight;
		p->y3 = posY - mapBottomHeight;

		UI_Map_DrawMap_ExtraFunc(mapTop, p, posX, 0, primMem, otMem, transparency);

		p = p + 1;
	}

	// r0, g0, b0 (vertex color)
	CtrGpu_WriteColorCode(&p->r0, color);

	p->y0 = posY - mapBottomHeight;
	p->y1 = posY - mapBottomHeight;
	p->y2 = posY;
	p->y3 = posY;

	UI_Map_DrawMap_ExtraFunc(mapBottom, p, posX, 0, primMem, otMem, transparency);

	primMem->cursor = p + 1;
}

void UI_Map_DrawMap_ExtraFunc(struct Icon *icon, POLY_FT4 *p, s16 posX, s16 empty, struct PrimMem *primMem, uint32_t *otMem, u32 transparency)
{
	s16 leftX;
	s16 sizeX;

	sizeX = icon->texLayout.u1 - icon->texLayout.u0;

	// posX is the right side,
	// letftX is the left side
	leftX = posX - sizeX;

	p->x0 = leftX;
	p->x1 = posX;
	p->x2 = leftX;
	p->x3 = posX;

	// set header
	setPolyFT4(p);

	// UVs
	CtrGpu_WritePackedUVWord(&p->u0, CTR_ReadU32LE(&icon->texLayout.u0));
	CtrGpu_WritePackedUVWord(&p->u1, CTR_ReadU32LE(&icon->texLayout.u1));
	CtrGpu_WritePackedUVWord(&p->u2, CTR_ReadU32LE(&icon->texLayout.u2));
	CtrGpu_WritePackedUV(&p->u3, CTR_ReadU16LE(&icon->texLayout.u3));

	if (transparency != 0)
	{
		p->tpage = (p->tpage & 0xff9f) | ((u16)transparency << 5);
	}

	p->code |= 2;

	AddPrim(otMem, p);
}

// move to headers later
struct Map
{
	s16 worldEndX;
	s16 worldEndY;
	s16 worldStartX;
	s16 worldStartY;

	s16 iconSizeX;
	s16 iconSizeY;
	s16 iconStartX;
	s16 iconStartY;

	s16 mode;
};

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8004d8b4-0x8004dbac.
void UI_Map_GetIconPos(s16 *m, int *posX, int *posY)

{
	s16 mode;
	int addX;
	int addY;
	int worldRangeX;
	int worldRangeY;

	struct Map *map = (struct Map *)m;

#if 0
  // trap() functions were removed from original,
  // we assume dividing by zero will never happen
#endif

	// rendering mode (forward, sideways, etc)
	mode = map->mode;

	worldRangeX = map->worldEndX - map->worldStartX;
	worldRangeY = map->worldEndY - map->worldStartY;

	if (mode == 0)
	{
		// 0 degrees
		addX = (*posX * map->iconSizeX) / worldRangeX;
		addY = (*posY * map->iconSizeY * 2) / worldRangeY;
	}

	else if (mode == 1)
	{
		// 90 degrees
		addX = -(*posY * map->iconSizeX) / worldRangeY;
		addY = (*posX * map->iconSizeY * 2) / worldRangeX;
	}

	else if (mode == 2)
	{
		// 180 degrees
		addX = -(*posX * map->iconSizeX) / worldRangeX;
		addY = -(*posY * map->iconSizeY * 2) / worldRangeY;
	}

	else
	{
		// 270 degrees
		addX = (*posY * map->iconSizeX) / worldRangeY;
		addY = -(*posX * map->iconSizeY * 2) / worldRangeX;
	}

	if (sdata->gGT->numPlyrCurrGame == 3)
	{
		addX -= 60;
		addY += 10;
	}

	*posX = map->iconStartX + addX;
	*posY = map->iconStartY + addY - 0x10;
	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8004dbac-0x8004dc44.
// Draw dot for Player on 2D Adv Map
void UI_Map_DrawAdvPlayer(int ptrMap, int *matrix, int unused1, int unused2, s16 param_5, s16 param_6)
{
	int *arrowColor;
	int posX;
	int posY;

	posX = *matrix;
	posY = matrix[2];

	UI_Map_GetIconPos((s16 *)ptrMap, &posX, &posY);

	arrowColor = &data.playerIconAdvMap.vertCol1[0];
	if ((sdata->gGT->timer & 2) != 0)
	{
		arrowColor = &data.playerIconAdvMap.vertCol2[0];
	}

	AH_Map_HubArrow(posX, posY, &data.playerIconAdvMap.unk_playerAdvMap[0], (char *)arrowColor, (int)param_6, (int)param_5);

	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8004dc44-0x8004dd5c.
// Draw icon on map
void UI_Map_DrawRawIcon(int ptrMap, int *param_2, int iconID, int colorID, int unused, s16 scale)
{
	int posX;
	int posY;
	u32 *ptrColor;
	struct GameTracker *gGT = sdata->gGT;

	(void)unused;

	posX = param_2[0];
	posY = param_2[2];

	UI_Map_GetIconPos((s16 *)ptrMap, &posX, &posY);

	ptrColor = data.ptrColor[colorID];

	struct Icon **iconPtrArray = ICONGROUP_GETICONS(sdata->gGT->iconGroup[5]);

	DecalHUD_DrawPolyGT4(iconPtrArray[iconID], posX, posY, &gGT->backBuffer->primMem, gGT->pushBuffer_UI.ptrOT, ptrColor[0], ptrColor[1], ptrColor[2],
	                     ptrColor[3], 0, (int)scale);

	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8004dd5c-0x8004dee8.
void UI_Map_DrawDrivers(int ptrMap, struct Thread *bucket, s16 *param_3)

{
	int kartColor;
	int iconID;
	struct Driver *d;
	struct GameTracker *gGT = sdata->gGT;

	for (/* bucket */; bucket != 0; bucket = bucket->siblingThread, *param_3 = *param_3 + 1)
	{
		// if 2P or 4P
		if ((gGT->numPlyrCurrGame & 1) == 0)
		{
			// no map icon drawn, but the icon counter still advances
			continue;
		}

		// Player structure
		d = bucket->object;

		// characterID + 5
		// corresponds with ptrColors
		kartColor = data.characterIDs[d->driverID] + 5;

		// default (AI)
		iconID = 0x31;

		// TO-DO: Should we just spawn player threads
		// and enable the AI flag anyway? What would it do?
		if ((d->actionsFlagSet & ACTION_BOT) == 0)

		{
			// If this is an even numbered frame
			// ptrColors white value
			if ((gGT->timer & 2) == 0)
			{
				kartColor = WHITE;
			}

			// If you're in Adventure Arena
			if ((gGT->gameMode1 & ADVENTURE_ARENA) != 0)
			{
				// Draw dot for Player on 2D Adv Map
				UI_Map_DrawAdvPlayer(ptrMap, (int *)&bucket->inst->matrix.t[0], 0x32, kartColor, (d->rotCurr.y + 0x800U) | 0x1000, 0x800);

				continue;
			}

			// Player
			iconID = 0x32;
		}

		UI_Map_DrawRawIcon(ptrMap, (int *)&bucket->inst->matrix.t[0], iconID, (s16)kartColor, 0, 0x1000);
	}
	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8004dee8-0x8004dffc.
void UI_Map_DrawGhosts(int ptrMap, struct Thread *bucket)
{
	int color;
	struct Driver *d;
	struct GameTracker *gGT = sdata->gGT;

	for (/* bucket */; bucket != 0; bucket = bucket->siblingThread)
	{
		d = bucket->object;

		// Need to finish Driver struct

		// if ghost not initialized
		if (*(s16 *)((int)d + 0x632) == 0)
		{
			continue;
		}

		// ghost made by player
		if (*(s16 *)((int)d + 0x630) == 0)
		{
			// flash red and blue

			color = CORTEX_RED;
			if ((gGT->timer & 1) != 0)
			{
				color = CRASH_BLUE;
			}
		}

		// ghost is N Tropy or Oxide
		else
		{
			// N Tropy doesn't flicker
			color = TROPY_LIGHT_BLUE;

			// if timeTrialFlags for this track show [ n tropy beaten, oxide open ]
#ifdef CTR_CUSTOM_PACKAGES
			if (!MainRaceTrack_OfflineCustomLoad() && (sdata->gameProgress.highScoreTracks[gGT->levelID].timeTrialFlags & 2) != 0)
#else
			if ((sdata->gameProgress.highScoreTracks[gGT->levelID].timeTrialFlags & 2) != 0)
#endif
			{
				// oxide flickers

				color = RED;
				if ((gGT->timer & 1) != 0)
				{
					color = WHITE;
				}
			}
		}

		UI_Map_DrawRawIcon(ptrMap, (int *)&bucket->inst->matrix.t[0], 0x31, color, 0, 0x1000);
	}
	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8004dffc-0x8004e0e0.
void UI_Map_DrawTracking(int ptrMap, struct Thread *bucket)

{
	int uVar1;
	struct Instance *inst;
	struct TrackerWeapon *tw;
	struct Driver *d;

	for (/* bucket */; bucket != 0; bucket = bucket->siblingThread)
	{
		// thread -> instance
		inst = bucket->inst;

		// instance -> model -> modelID != warpball
		if (inst->model->id != DYNAMIC_WARPBALL)
		{
			continue;
		}

		// == only draw warpball ==

		// draw warpball
		UI_Map_DrawRawIcon(ptrMap, (int *)&inst->matrix.t[0], 0x20, 0, 0, 0x1000);

		// driver target
		tw = (struct TrackerWeapon *)inst->thread->object;
		d = tw->driverTarget;

		// check if target exists
		if (d == 0)
		{
			continue;
		}

		// == only draw target if target exists ==

		// flicker
		uVar1 = 4;
		if ((sdata->gGT->timer & 1) != 0)
		{
			uVar1 = 3;
		}

		UI_Map_DrawRawIcon(ptrMap, (int *)&d->instSelf->matrix.t[0], 0x21, uVar1, 0, 0x1000);
	}
	return;
}

#if defined(CTR_NATIVE) && defined(CTR_CUSTOM_TRACKS)
// Minimap sanity for custom levels (A4-64). The decision and the measurements
// behind it are in native_custom_tracks_policy.h ("Minimap sanity").
static int s_ctMinimapOverrideLev;
static int s_ctMinimapLevelIsCustom; // the last loaded level came from a custom override
static int s_ctMinimapMode = CTR_POLICY_MINIMAP_TEXTURE;

// The level the mode was decided for. A save-state restore brings back a level
// and its VRAM without running the loader; NativeCheckpoint_Restore then calls
// UI_Map_CustomAfterRestore, which measures the restored VRAM again. As a second
// guard, a mode whose level, icons or icon layouts no longer match falls back
// to the texture path.
static struct
{
	struct Level *level;
	struct Icon *top, *bottom;
	u8 topLayout[12], bottomLayout[12];
} s_ctMinimapFor;

static int UI_Map_CustomSameLevel(struct GameTracker *gGT)
{
	struct Icon *top = gGT->ptrIcons[3], *bottom = gGT->ptrIcons[4];

	return gGT->level1 == s_ctMinimapFor.level && top == s_ctMinimapFor.top && bottom == s_ctMinimapFor.bottom &&
	       (top == NULL || memcmp(&top->texLayout, s_ctMinimapFor.topLayout, 12) == 0) &&
	       (bottom == NULL || memcmp(&bottom->texLayout, s_ctMinimapFor.bottomLayout, 12) == 0);
}

void UI_Map_CustomNoteOverrideLev(void)
{
	s_ctMinimapOverrideLev = 1;
}

int UI_Map_CustomMode(void)
{
	if (s_ctMinimapMode == CTR_POLICY_MINIMAP_TEXTURE || !UI_Map_CustomSameLevel(sdata->gGT))
		return CTR_POLICY_MINIMAP_TEXTURE;
	return s_ctMinimapMode;
}

// Off-grey texels of one icon, read back from VRAM. Returns 1 when the icon is
// usable. A layout with the reserved colour depth, or a rect past the edge of
// VRAM, is left as usable: the retail path draws it as before.
static int UI_Map_CustomIconUsable(struct Icon *icon, unsigned long *nonGreyOut, unsigned long *totalOut)
{
	static u16 tex[256 * 256]; // a 15-bit page is 256 halfwords wide
	static u16 clut[256];
	struct CustomTrackMinimapRects r;
	RECT rect;
	void *texDst = tex, *clutDst = clut; // StoreImage takes u32 *; the rows are halfwords

	*nonGreyOut = 0;
	*totalOut = 0;
	if (icon == NULL)
		return 0;
	if (!CustomTrackPolicy_MinimapIconRects((const unsigned char *)&icon->texLayout, &r))
		return 1;
	if (r.clutW == 0)
		memset(clut, 0, sizeof clut);
	if (r.texW <= 0 || r.texW > 256 || r.texH <= 0 || r.texH > 256 || r.texX + r.texW > 1024 || r.texY + r.texH > 512 ||
	    (r.clutW > 0 && (r.clutX + r.clutW > 1024 || r.clutY >= 512)))
		return 1;

	rect.x = (s16)r.texX;
	rect.y = (s16)r.texY;
	rect.w = (s16)r.texW;
	rect.h = (s16)r.texH;
	StoreImage(&rect, (u32 *)texDst);
	if (r.clutW > 0)
	{
		rect.x = (s16)r.clutX;
		rect.y = (s16)r.clutY;
		rect.w = (s16)r.clutW;
		rect.h = 1;
		StoreImage(&rect, (u32 *)clutDst);
	}

	*nonGreyOut = CustomTrackPolicy_MinimapNonGrey(&r, tex, clut, totalOut);
	return CustomTrackPolicy_MinimapHalfUsable(*nonGreyOut, *totalOut);
}

static void UI_Map_CustomDecide(struct GameTracker *gGT, int isCustom)
{
	struct Level *lev = gGT->level1;
	unsigned long bad3, tot3, bad4, tot4;
	int top, bottom, outline = 0;
	struct Map *map = NULL;

	s_ctMinimapMode = CTR_POLICY_MINIMAP_TEXTURE;
	if (!isCustom || lev == NULL)
		return;

	top = UI_Map_CustomIconUsable(gGT->ptrIcons[3], &bad3, &tot3);
	bottom = UI_Map_CustomIconUsable(gGT->ptrIcons[4], &bad4, &tot4);

	if ((lev->ptrSpawnType1 != NULL) && (lev->ptrSpawnType1->count != 0))
	{
		void **pointers = ST1_GETPOINTERS(lev->ptrSpawnType1);
		map = (struct Map *)pointers[ST1_MAP];
	}
	if (map != NULL && lev->ptr_restart_points != NULL)
	{
		outline = CustomTrackPolicy_MinimapOutlineUsable(map->worldEndX, map->worldEndY, map->worldStartX, map->worldStartY,
		                                                 map->iconSizeX, map->iconSizeY, map->mode, lev->cnt_restart_points);
	}

	s_ctMinimapMode = CustomTrackPolicy_MinimapMode(top && bottom, outline);
	s_ctMinimapFor.level = lev;
	s_ctMinimapFor.top = gGT->ptrIcons[3];
	s_ctMinimapFor.bottom = gGT->ptrIcons[4];
	if (s_ctMinimapFor.top != NULL)
		memcpy(s_ctMinimapFor.topLayout, &s_ctMinimapFor.top->texLayout, 12);
	if (s_ctMinimapFor.bottom != NULL)
		memcpy(s_ctMinimapFor.bottomLayout, &s_ctMinimapFor.bottom->texLayout, 12);
	if (s_ctMinimapMode != CTR_POLICY_MINIMAP_TEXTURE)
	{
		CustomTrack_Log("[CustomTracks] minimap texture is not a minimap (off-grey texels %lu/%lu and %lu/%lu); %s\n", bad3, tot3,
		                bad4, tot4,
		                s_ctMinimapMode == CTR_POLICY_MINIMAP_OUTLINE ? "drawing the checkpoint outline" : "minimap hidden");
	}
}

void UI_Map_CustomEvaluate(struct GameTracker *gGT)
{
	s_ctMinimapLevelIsCustom = s_ctMinimapOverrideLev;
	s_ctMinimapOverrideLev = 0;
	UI_Map_CustomDecide(gGT, s_ctMinimapLevelIsCustom);
}

// A restored state is the same level kind as the one loaded: with custom
// packages a restore across tracks is refused (native_checkpoint.c), and the
// VRAM is measured again, so a state saved on another level cannot carry its
// mode across.
void UI_Map_CustomAfterRestore(struct GameTracker *gGT)
{
	UI_Map_CustomDecide(gGT, s_ctMinimapLevelIsCustom);
}

// One outline segment as a three-pixel POLY_F4, thickened across its minor
// axis, about the stroke width of a retail minimap outline.
static int UI_Map_CustomSegment(struct PrimMem *primMem, uint32_t *otMem, int x0, int y0, int x1, int y1)
{
	POLY_F4 *p = (POLY_F4 *)primMem->cursor;
	int dx = x1 - x0, dy = y1 - y0;
	int ox = 0, oy = 0;

	if (!CustomTrackPolicy_PrimFits(primMem->cursor, sizeof(POLY_F4), primMem->guardEnd))
		return 0;
	if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy))
		oy = 3;
	else
		ox = 3;

	CtrGpu_WriteColorCode(&p->r0, 0xffffff);
	setPolyF4(p);
	p->x0 = (s16)x0;
	p->y0 = (s16)y0;
	p->x1 = (s16)x1;
	p->y1 = (s16)y1;
	p->x2 = (s16)(x0 + ox);
	p->y2 = (s16)(y0 + oy);
	p->x3 = (s16)(x1 + ox);
	p->y3 = (s16)(y1 + oy);
	AddPrim(otMem, p);
	primMem->cursor = p + 1;
	return 1;
}

void UI_Map_CustomDraw(int ptrMap, struct PrimMem *primMem, uint32_t *otMem)
{
	struct Level *lev = sdata->gGT->level1;
	struct CheckpointNode *cp;
	int count, i, k;

	if (UI_Map_CustomMode() != CTR_POLICY_MINIMAP_OUTLINE || ptrMap == 0 || lev == NULL)
		return;
	cp = lev->ptr_restart_points;
	count = lev->cnt_restart_points;
	if (cp == NULL || count < 2 || count > 255)
		return;

	for (i = 0; i < count; i++)
	{
		const u8 next[3] = {cp[i].nextIndex_forward, cp[i].nextIndex_left, cp[i].nextIndex_right};
		for (k = 0; k < 3; k++)
		{
			int ax = cp[i].pos.x, ay = cp[i].pos.z, bx, by;
			if (next[k] >= count)
				continue;
			bx = cp[next[k]].pos.x;
			by = cp[next[k]].pos.z;
			UI_Map_GetIconPos((s16 *)ptrMap, &ax, &ay);
			UI_Map_GetIconPos((s16 *)ptrMap, &bx, &by);
			if (!UI_Map_CustomSegment(primMem, otMem, ax, ay, bx, by))
				return;
		}
	}
}
#endif
