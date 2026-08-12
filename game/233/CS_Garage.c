#include <common.h>

#ifdef CTR_AP
// Five equal visual steps across the Garage's six 13-pixel fill segments.
// The final step stops at the existing 78-pixel fill ceiling.
static const s16 AP_GARAGE_STAT_BAR_BY_RANK[AP_CAP_STAT_RANK_COUNT] = {
	13, 26, 39, 52, 78,
};
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b7784-0x800b7834
void CS_Garage_ZoomOut(char zoomState)
{
	if (zoomState != 0)
	{
		// number of frames to zoom in, or out,
		// when selecting or cancelling OSK
		gGarage.numFramesCurr_ZoomIn = gGarage.numFramesMax_Zoom;
		gGarage.numFramesCurr_ZoomOut = gGarage.numFramesMax_Zoom;
	}
	else
	{
		gGarage.numFramesCurr_ZoomIn = 0;
		gGarage.numFramesCurr_ZoomOut = 0;
	}

	gGarage.numFramesCurr_GarageMove = 0;
	gGarage.boolSelected = 0;
	gGarage.delayOneSecond = 0;

	sdata->gGT->gameMode2 &= ~(GARAGE_OSK);

	// if just entered garage
	if (zoomState == 0)
	{
		Garage_Init();
		Garage_Enter(sdata->advCharSelectIndex_curr);

		Audio_SetState_Safe(AUDIO_GARAGE);
	}
}

#ifdef CTR_AP
// Force the AP-authoritative racer over whatever a Garage confirm just wrote.
//
// The two commit sites below (:380 and :466 in the retail flow) are the only
// places in the tree that can seat a racer of the player's choosing at
// adventure start. When the seed owns the racer they are unreachable, because
// CS_Garage_MenuProc returns early -- but writing the invariant at the point of
// the write, rather than trusting the arrangement that keeps us away from it,
// is what stops a future refactor of the skip from silently reopening the
// unlock bypass.
static void CS_Garage_ApplyApRacerOverride(void)
{
	int apRacer = AP_CharSwap_GarageRacer();

	if (apRacer < 0)
		return; // no character phase on this seed: retail behaviour, untouched
	if (data.characterIDs[0] == (s16)apRacer)
		return;

	AP_LogLine("[AP CHARSWAP] garage commit overridden; the seed owns the racer\n");

	data.characterIDs[0] = (s16)apRacer;
	sdata->advProgress.characterID = (s16)apRacer;
}
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b7834-0x800b854c
void CS_Garage_MenuProc(struct RectMenu *param_1)
{
	s16 garageFrames;
	s16 *barLen;
	u16 classNamePosX;
	int i;
	u32 statNamePosX;
	u32 statBarPosX;

	u32 currSelectIndex = sdata->advCharSelectIndex_curr;
	struct GameTracker *gGT = sdata->gGT;
	struct PrimMem *primMem = &gGT->backBuffer->primMem;
	s16 currCharacterID = gGarage.unusedArr_garageChars[currSelectIndex];
	struct MetaDataCHAR *MDC = &data.MetaDataCharacters[currCharacterID];
	int nameIndex = MDC->name_LNG_long;
	RECT r;
	Color white = MakeColor(0xFF, 0xFF, 0xFF);
	Color black = MakeColor(0, 0, 0);

	// CameraDC, freecam mode
	gGT->cameraDC[0].cameraMode = 3;

#ifdef CTR_AP
	// ------------------------------------------------------------------
	// The adventure-start character select is SKIPPED when the seed owns the
	// racer (#54/#209).
	//
	// THE BYPASS THIS CLOSES. This function is the whole of the vanilla garage
	// picker (registered as the menuGarage funcPtr, game/233/D233.c:11-23, run
	// per frame from game/RECTMENU.c:943-954), and it commits the highlighted
	// racer to data.characterIDs[0] plus sdata->advProgress.characterID at :380
	// and again at :466. Those are precisely the two fields the AP layer seats
	// the seed's racer into, and the garage writes them afterwards, so the
	// player's pick simply won. Observed live on a seed whose starting racer was
	// Neo Cortex with character unlocks on: picking Dingodile made you Dingodile.
	// data.characterIDs[0] is then the authority for both the adventure MPK fetch
	// (game/LOAD/LOAD_Assets.c:128) and the driver birth (VehBirth.c:688-696), so
	// the wrong racer really is the one that races.
	//
	// WHY SKIP RATHER THAN FILTER. The garage's roster is
	// gGarage.unusedArr_garageChars, the eight vanilla starters (D233.c:29). It
	// has no row for Penta, Oxide or any unlockable, so a seed that starts you as
	// one of them cannot be expressed here at all. Constraining the picker to a
	// single legal tile would also present a choice that is not one.
	//
	// HOW THE SKIP WORKS. It performs exactly what the :375-385 confirm branch
	// performs -- the same two writes, the same SubmitName_RestoreName(0), the
	// same handoff to the OSK -- with the racer AP resolved instead of the
	// highlighted one, and returns before any input or drawing. Everything
	// downstream (name entry, profile slot, hub load) is the untouched retail
	// path. This runs only with LOAD_IDLE, because MainFrame_RenderFrame.c:54-60
	// gates RECTMENU_ProcessState on it, so the handoff can never fire mid-load.
	//
	// It sits AFTER the freecam camera-mode write above deliberately. That line
	// runs on every frame of the retail garage, and the on-screen keyboard this
	// hands off to draws over the garage scene, so skipping it would hand the OSK
	// a camera state no retail path ever gives it.
	//
	// The garage may briefly have loaded the previous racer's model, since the
	// MPK was queued from data.characterIDs[0] before we ran. That is cosmetic
	// and lasts a frame: the hub's own load re-queues from the value written
	// here.
	{
		// File-static rather than a gGarage field: gGarage mirrors a retail
		// overlay-233 data layout and is size-asserted, so the latch stays
		// outside it.
		static int apSkipDone = 0;

		int apRacer = AP_CharSwap_GarageRacer();

		if (apRacer >= 0)
		{
			// One-shot. ptrDesiredMenu takes a frame or two to become the active
			// menu, and this funcPtr keeps running meanwhile; the writes are
			// idempotent but the sound effect and the name restore are not.
			if (!apSkipDone)
			{
				apSkipDone = 1;

				sdata->ptrDesiredMenu = &data.menuSubmitName;

				data.characterIDs[0] = (s16)apRacer;
				sdata->advProgress.characterID = (s16)apRacer;

				SubmitName_RestoreName(0);
				OtherFX_Play(1, 1);

				AP_LogLine("[AP CHARSWAP] adventure-start garage select skipped; the seed owns the racer\n");
			}
			return;
		}

		apSkipDone = 0;
	}
#endif

	// subtract transition timer by one frame
	garageFrames = gGarage.numFramesCurr_GarageMove - 1;

	// if mid-transition, skip some code
	if (gGarage.numFramesCurr_GarageMove != 0)
	{
		goto SKIP_CONTROLS;
	}

	// At this point, there must not be a transition
	// between drivers, so start drawing the UI

	// count frames in garage?
	gGarage.unusedFrameCount++;

	// animate growth of all three stat bars
	for (i = 0; i < 3; i++)
	{
		barLen = &gGarage.barLen[i];
		s16 stat = gGarage.barStat[MDC->engineID * 3 + i];

#ifdef CTR_AP
		// Replacement design: when Progressive Stats is live, show the viewed
		// racer's current AP rank outright. In shared_global mode the explicit
		// character is intentionally ignored; in per_character mode it selects
		// the PR #216 received-count row. An inactive or unknown configuration
		// returns -1 and preserves the retail class-table bar byte-for-byte.
		int rank = AP_CapabilityStatRankForCharacter(AP_CAP_CHAIN_TOP_SPEED + i,
		                                                   currCharacterID);
		if (rank >= 0)
			stat = AP_GARAGE_STAT_BAR_BY_RANK[rank];
#endif


#define BAR_RATE 3

		if (*barLen < stat)
		{
			*barLen = *barLen + BAR_RATE;
		}
		if (stat < *barLen)
		{
			*barLen = stat;
		}
	}

	if (
	    // Tiny Tiger
	    (nameIndex == 46) ||

	    (statNamePosX = 383,

	     // Pura
	     nameIndex == 51))
	{
		statNamePosX = 129;
		classNamePosX = 128;
		statBarPosX = 139;
	}
	else
	{
		classNamePosX = 384;
		statBarPosX = 393;
	}

	DecalFont_DrawLine(sdata->lngStrings[LNG_SPEED], statNamePosX, 30, FONT_BIG, JUSTIFY_RIGHT | ORANGE_RED);
	DecalFont_DrawLine(sdata->lngStrings[LNG_ACCEL], statNamePosX, 0x2d, 1, 0x4021);
	DecalFont_DrawLine(sdata->lngStrings[LNG_TURN], statNamePosX, 60, FONT_BIG, JUSTIFY_RIGHT | BLUE);

	int engineID = MDC->engineID;

	// 0x248 - Beginner
	// EngineID == 3
	i = 0;

	// 0x24A - Advanced
	if (engineID == SPEED)
	{
		i = 2;
	}

	// 0x249 - Intermediate
	if (engineID < SPEED)
	{
		i = 1;
	}

	// 7 pixels tall
	u16 statBarStart_Y = 33;
	u16 statBarEnd_Y = 40;

	u16 statBarShadows_Y = 34;

	// Draw class name
	DecalFont_DrawLine(sdata->lngStrings[gGarage.unusedArr_lngIndex[i]], classNamePosX, 15, FONT_BIG, (JUSTIFY_CENTER | ORANGE));

	// bar length (animated)

	for (i = 0; i < 3; i++)
	{
		barLen = &gGarage.barLen[i];

		// bar outline
		r.x = statBarPosX;
		r.y = statBarStart_Y;
		r.w = *barLen;
		r.h = 7;

		// outline color white at 0x800b7780
		CTR_Box_DrawWireBox(&r, &white, gGT->pushBuffer_UI.ptrOT, primMem);

		// bar shadows
		r.x = statBarPosX + 1;
		r.y = statBarShadows_Y;
		r.w = *barLen - 2;
		r.h = 5;

		// outline color black (shadows)
		CTR_Box_DrawWireBox(&r, &black, gGT->pushBuffer_UI.ptrOT, primMem);

		int segmentLen = 13;
		int segmentStart = 0;
		int segmentEnd = segmentLen;

		for (int segmentIndex = 0; segmentIndex < 6; segmentIndex++)
		{
			// color data of bars (blue green yellow red)
			u32 *barColor = &gGarage.barColors[segmentIndex];
			s16 currSegmentLen = (s16)segmentLen;

			if (*barLen <= segmentEnd)
			{
				currSegmentLen = *barLen - segmentStart;
			}

			if ((int)currSegmentLen << 0x10 < 0)
			{
				currSegmentLen = 0;
			}

			if (segmentStart + currSegmentLen <= *barLen)
			{
				// primMem curr
				POLY_G4 *p = primMem->cursor;

				// quit if prim mem runs out
				if (primMem->end < (void *)p)
				{
					return;
				}

				primMem->cursor = p + 1;

				// color data
				CtrGpu_WriteColorCode(&p->r0, barColor[0] | 0x38000000);
				CtrGpu_WriteColorCode(&p->r1, barColor[1] | 0x38000000);
				CtrGpu_WriteColorCode(&p->r2, barColor[0] | 0x38000000);
				CtrGpu_WriteColorCode(&p->r3, barColor[1] | 0x38000000);

				s16 segmentX = statBarPosX + segmentStart;

				// top left
				p->x0 = segmentX;
				p->y0 = statBarStart_Y;

				// top right
				p->x1 = segmentX + currSegmentLen;
				p->y1 = statBarStart_Y;

				// bottom left
				p->x2 = segmentX;
				p->y2 = statBarEnd_Y;

				// bottom right
				p->x3 = segmentX + currSegmentLen;
				p->y3 = statBarEnd_Y;

				// pointer to OT memory
				void *ot = gGT->pushBuffer_UI.ptrOT;

				*(int *)p = CtrGpu_PackOTTag(*(uint32_t *)ot, 0x8000000);
				*(int *)ot = (int)CtrGpu_PrimToOTLink24(p);
			}

			segmentStart += segmentLen;
			segmentEnd += segmentLen;
		}

		// 15 pixels lower Y axis
		statBarStart_Y += 15;
		statBarEnd_Y += 15;
		statBarShadows_Y += 15;
	}

	s16 classMaxLen = DecalFont_GetLineWidth(sdata->lngStrings[LNG_INTERMEDIATE], 1);

	// Stats box
	r.x = (classNamePosX - (classMaxLen >> 1)) - 6;
	r.y = 11;
	r.w = classMaxLen + 12;
	r.h = 68;

	// Draw 2D Menu rectangle background
	RECTMENU_DrawInnerRect(&r, 4, gGT->backBuffer->otMem.uiOT);

	char *name = sdata->lngStrings[nameIndex];

	// Draw character name
	DecalFont_DrawLine(name, 0x100, 0xb4, 1, 0xffff8000);

	char arrowColor = ORANGE;

	// blink arrows
	if ((sdata->frameCounter & 4) == 0)
	{
		arrowColor = RED;
	}

	// Color data
	u32 *arrowColors = data.ptrColor[(s32)arrowColor];

	int nameLen = DecalFont_GetLineWidth(name, 1) >> 1;

	int arrowPos[2] = {236 - nameLen, nameLen + 274};
	int arrowRot[2] = {0x800, 0};

	struct Icon **iconPtrArray = ICONGROUP_GETICONS(gGT->iconGroup[4]);

	for (i = 0; i < 2; i++)
	{
		DecalHUD_Arrow2D(iconPtrArray[0x38], arrowPos[i], 187,

		                 primMem, gGT->pushBuffer_UI.ptrOT,

		                 arrowColors[0], arrowColors[1], arrowColors[2], arrowColors[3],

		                 0, 0x1000, arrowRot[i]);
	}

	garageFrames = gGarage.numFramesCurr_GarageMove;

	if (((gGT->renderFlags & 0x1000) != 0) ||

	    (
	        // If you dont press Triangle, Cross, Circle, or Square
	        ((sdata->AnyPlayerTap & 0x40070) == 0) &&

	        // If you dont press D-pad
	        ((sdata->AnyPlayerHold & 0xc) == 0)))
	{
		goto SKIP_CONTROLS;
	}

	// If you dont press D-pad
	if ((sdata->AnyPlayerHold & 0xc) == 0)
	{
		// If you do not press Cross or Circle
		if ((sdata->AnyPlayerTap & 0x50) == 0)
		{
			// If you press Triangle or Square
			if ((sdata->AnyPlayerTap & 0x40020) != 0)
			{
				// Play Sound
				OtherFX_Play(2, 1);

				garageFrames = gGarage.numFramesCurr_ZoomIn;
				if (gGarage.boolSelected == 1)
				{
					gGarage.boolSelected = 0;
					gGT->gameMode2 &= ~GARAGE_OSK;

					if (garageFrames < gGarage.numFramesMax_Zoom)
					{
						gGarage.numFramesCurr_ZoomOut = gGarage.numFramesMax_Zoom - garageFrames;
					}
				}
				else
				{
					// return to main menu
					sdata->mainMenuState = MAIN_MENU_TITLE;

					Garage_Leave();

					// load main menu LEV
					MainRaceTrack_RequestLoad(0x27);
				}
			}
		}

		// If you press Cross or Circle
		else
		{
			// "Have you selected character?"
			// If true, it will show an animation, and then show the
			// OSK (keyboard) screen. If set to 0 after in that screen,
			// the screen does not disappear

			// if false
			if (gGarage.boolSelected == 0)
			{
				// make it true
				gGarage.boolSelected = 1;
			}

			// if true
			else
			{
				// if pressed X twice quickly
				if (gGarage.boolSelected == 1)
				{
					// set desiredMenu to OSK (on-screen keyboard)
					sdata->ptrDesiredMenu = &data.menuSubmitName;

					data.characterIDs[0] = gGarage.unusedArr_garageChars[currSelectIndex];
					sdata->advProgress.characterID = data.characterIDs[0];

#ifdef CTR_AP
					// Belt and braces on the unlock bypass (#54/#209). The skip
					// at the top of this function returns before we can get
					// here whenever the seed owns the racer, so this cannot
					// currently fire -- but this is one of only two lines in
					// the tree that can seat a racer the seed did not choose,
					// and the invariant is worth stating where it is enforced
					// rather than only where it is arranged.
					CS_Garage_ApplyApRacerOverride();
#endif

					SubmitName_RestoreName(0);
					OtherFX_Play(1, 1);
				}
			}
		}
	}

	// if using D-pad
	else
	{
		// erase animated bars
		for (i = 2; i > -1; i--)
		{
			barLen = &gGarage.barLen[i];
			*barLen = 0;
		}
		// Play Sound
		OtherFX_Play(0, 1);

		// If you dont press Left
		if ((sdata->AnyPlayerHold & 4) == 0)
		{
			// If you dont press Right
			if ((sdata->AnyPlayerHold & 8) != 0)
			{
				currSelectIndex++;
				goto LAB_800b8084;
			}
		}

		// If you press Left
		else
		{
			currSelectIndex--;

		LAB_800b8084:

			// previous equals current
			sdata->advCharSelectIndex_prev = sdata->advCharSelectIndex_curr;

			// clamp 0-7
			currSelectIndex &= 7;
			sdata->advCharSelectIndex_curr = currSelectIndex;

			Garage_MoveLR(currSelectIndex);
		}

		// reset frame counter to max number of frames
		gGarage.numFramesCurr_GarageMove = gGarage.numFramesMax_GarageMove;

		if (gGarage.numFramesCurr_ZoomIn < gGarage.numFramesMax_Zoom)
		{
			gGarage.numFramesCurr_ZoomOut = gGarage.numFramesMax_Zoom - gGarage.numFramesCurr_ZoomIn;
		}

		gGarage.boolSelected = 0;
		gGT->gameMode2 &= ~GARAGE_OSK;
	}

	// clear gamepad input (for menus)
	RECTMENU_ClearInput();

	garageFrames = gGarage.numFramesCurr_GarageMove;
SKIP_CONTROLS:
	gGarage.numFramesCurr_GarageMove = garageFrames;

	// if frames remaing for zoom camera
	if (0 < gGarage.numFramesCurr_ZoomIn)
	{
		// decrease zoom frame timer
		gGarage.numFramesCurr_ZoomIn--;
	}

	// if pressed X once, and waited for countdown clock
	if ((gGarage.boolSelected == 1) && (gGarage.numFramesCurr_ZoomIn == 0))
	{
		if (
		    // frames remaining for animation
		    (59 < gGarage.delayOneSecond) || ((gGT->gameMode2 & GARAGE_OSK) != 0))
		{
			// set desiredMenu to OSK (on-screen keyboard)
			sdata->ptrDesiredMenu = &data.menuSubmitName;

			data.characterIDs[0] = gGarage.unusedArr_garageChars[currSelectIndex];
			sdata->advProgress.characterID = data.characterIDs[0];

#ifdef CTR_AP
			// The second of the two commit sites; same reasoning as :380.
			CS_Garage_ApplyApRacerOverride();
#endif

			SubmitName_RestoreName(0);
			OtherFX_Play(1, 1);
		}
		else
		{
			gGarage.delayOneSecond++;
		}
	}

#ifdef CTR_NATIVE
	if (sdata->ptrDesiredMenu == &data.menuSubmitName)
	{
		// NOTE(aalhendi): PC-only keyboard shim; retail gamepad flow above stays unchanged.
		// flush async key state buffer. If not, tapping Enter "before" picking a garage character,
		//  then picking character, will immediately warp you to the adv hub, with no time to type the name
		NikoGetEnterKey();
	}
#endif

	if (gGarage.boolSelected == 0)
	{
		gGarage.numFramesCurr_ZoomIn = gGarage.numFramesMax_Zoom;
	}

	if (gGarage.numFramesCurr_ZoomOut != 0)
	{
		gGarage.numFramesCurr_ZoomOut--;
	}

	u32 prevSelectIndex = sdata->advCharSelectIndex_prev;

	// Pura->Crash
	if ((currSelectIndex == 0) && (prevSelectIndex == 7))
	{
		garageFrames = 240 - gGarage.numFramesCurr_GarageMove;
	}
	// Crash->Pura
	else if ((currSelectIndex == 7) && (prevSelectIndex == 0))
	{
		garageFrames = gGarage.numFramesCurr_GarageMove + 210;
	}
	// Move Right
	else if (prevSelectIndex < currSelectIndex)
	{
		garageFrames = currSelectIndex * 30 - gGarage.numFramesCurr_GarageMove;
	}
	// Move Left
	else
	{
		garageFrames = currSelectIndex * 30 + gGarage.numFramesCurr_GarageMove;
	}

	// animation frame index,
	// pointer to position,
	// pointer to rotation

	s16 getPath;
	SVec3 camPos;
	SVec3 camRot;
	CAM_Path_Move((int)garageFrames, camPos.v, camRot.v, &getPath);

	// set position and rotation to pushBuffer
	gGT->pushBuffer[0].pos = camPos;

	gGT->pushBuffer[0].rot = camRot;

	int zoom = gGarage.numFramesCurr_ZoomOut;
	if (zoom == 0)
	{
		zoom = (gGarage.numFramesMax_Zoom - gGarage.numFramesCurr_ZoomIn) * (gGarage.fovMax - gGarage.fovMin);
	}
	else
	{
		zoom = zoom * (gGarage.fovMax - gGarage.fovMin);
	}

	zoom = gGarage.fovMin + zoom / gGarage.numFramesMax_Zoom;

	gGT->pushBuffer[0].distanceToScreen_CURR = zoom;
	gGT->pushBuffer[0].distanceToScreen_PREV = zoom;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b854c-0x800b8558
struct RectMenu *CS_Garage_GetMenuPtr(void)
{
	return &gGarage.menuGarage;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b8558-0x800b8598
void CS_Garage_Init(void)
{
	// go to 3D character selection
	sdata->ptrActiveMenu = &gGarage.menuGarage;

	gGarage.menuGarage.state &= ~(ONLY_DRAW_TITLE);

	// 0 = just entered garage
	CS_Garage_ZoomOut(0);
}
