#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800ae8a0-0x800ae8e0.
void AH_Garage_ThDestroy(struct Thread *t)
{
	struct BossGarageDoor *garage;
	garage = t->object;

	if (garage->garageTopInst != NULL)
	{
		INSTANCE_Death(garage->garageTopInst);
		garage->garageTopInst = NULL;
	}

	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800ae8e0-0x800ae988.
void AH_Garage_Open(struct ScratchpadStruct *sps, void *hitObject)
{
	struct Thread *otherTh = hitObject;
	s16 sound;
	struct Instance *garageInst;
	struct BossGarageDoor *garage;
	struct Thread *garageThread;

	if (otherTh->modelIndex != DYNAMIC_PLAYER)
	{
		return;
	}

	garageThread = sps->Union.ThBuckColl.thread;

	garage = garageThread->object;
	garageInst = garageThread->inst;

	if (
	    // if door is not opening
	    (garage->direction != 1) &&

	    // if door is closed,
	    // if posY is the same as instDef posY
	    (garageInst->matrix.t[1] == garageInst->instDef->pos.y))
	{
		// if you are not in gemstone valley
		// play sound of normal boss door opening
		sound = 0x95;

		// Level ID
		// if you are in Gemstone Valley
		if (sdata->gGT->levelID == GEM_STONE_VALLEY)
		{
			// play sound of oxide door opening
			sound = 0x96;
		}

		// Play sound
		OtherFX_Play(sound, 1);
	}

	// door is now opening
	garage->direction = 1;

	// enable access through a door (disable collision)
	sdata->doorAccessFlags |= 1;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800ae988-0x800af070.
void AH_Garage_ThTick(struct Thread *t)
{
	char bossIsOpen, i;
	int levelID;
	int hubID;
	int top;
	int move;
	int ratio;
	int bottom;
	s16 *check;
	u32 bitIndex;
	u32 uVar8;
	int dist[3];
	int pos[3];
	struct BossGarageDoor *garage;
	struct Instance *inst;
	struct Instance *drv_inst;
	struct GameTracker *gGT;
	struct AdvProgress *adv;
	struct ScratchpadStruct *sps = CTR_SCRATCHPAD_PTR(struct ScratchpadStruct, 0x108);

	bossIsOpen = true;

	gGT = sdata->gGT;
	adv = &sdata->advProgress;
	garage = t->object;
	inst = t->inst;
	drv_inst = gGT->drivers[0]->instSelf;
	levelID = gGT->levelID;
	hubID = levelID - GEM_STONE_VALLEY;

	// if door is not opening or closing
	if (garage->direction == 0)
	{
		// if door is fully closed
		if (garage->cooldown == 0)
		{
			inst->flags |= SPLIT_SPECIAL;
			inst->flags &= ~(SPLIT_LINE | REFLECTION_FUNC23 | WATER_SPLIT_WHITE);
		}

		// if door is not fully closed
		else
		{
			// subtract frame timer
			garage->cooldown -= gGT->elapsedTimeMS;

			// if countdown is not done, dont close door
			if (garage->cooldown > 0)
			{
				goto LAB_800aeb6c;
			}

			// play sound of normal boss door opening, except for Oxide
			uVar8 = (levelID == GEM_STONE_VALLEY) ? 0x96 : 0x95;

			// Play sound
			OtherFX_Play(uVar8, 1);

			// erase cooldown
			garage->cooldown = 0;

			// door is closing
			garage->direction = -1;

			inst->flags &= ~HIDE_MODEL;
		}
	}
	// if door is opening or closing
	else
	{
		// Increment animation by 0x20 in either direction
		move = inst->matrix.t[1] + garage->direction * 0x20;
		inst->matrix.t[1] = move;

		top = inst->instDef->pos.y + 0x300;
		bottom = inst->instDef->pos.y;

		// If the door has gone past the top (height=0x300)
		if (move > top)
		{
			// Set position to the top
			inst->matrix.t[1] = top;

			// Door is now open (not moving)
			garage->direction = 0;

			// Cooldown for 2 seconds
			garage->cooldown = 0x780;

			// Make invisible
			inst->flags |= HIDE_MODEL;
		}
		// If the door has gone past the bottom
		else if (move < bottom)
		{
			// Set position to the bottom
			inst->matrix.t[1] = bottom;

			garage->direction = 0;

			garage->cooldown = 0;

			// Enable door collision
			sdata->doorAccessFlags &= 0xfffffffe;
		}
		// If the door is between the top and bottom positions
		else if (garage->garageTopInst != 0)
		{
			// Update rotation of garagetop
			garage->rot.x += (s16)garage->direction * 0x40;

			// converted to TEST in rebuildPS1
			ConvertRotToMatrix(&garage->garageTopInst->matrix, &garage->rot);
		}

		inst->flags &= ~SPLIT_SPECIAL;
		inst->flags |= (SPLIT_LINE | REFLECTION_FUNC23 | WATER_SPLIT_WHITE);
	}

LAB_800aeb6c:

	// If you're in Gemstone Valley
	if (levelID == GEM_STONE_VALLEY)
	{
#ifdef CTR_AP
		// AP: THE canonical Oxide entry gate (WO-A1). Was the per-seed garage
		// requirement (boss_req[4], normally four received Keys) alone, which
		// opened the door on `Any% + All Four Bosses` with zero boss races won.
		// AP_OxideGarageOpen composes that same requirement with every active
		// companion goal condition when goal_oxide != 0, and still falls back to
		// the unchanged Phase-1 four-Key rule without slot_data. The map icon and
		// AH_Garage_LInB call the same function, so no entry path is left open.
		if (!AP_OxideGarageOpen())
			goto LAB_800aebd0;
#else
		// ripper roo boss key
		bitIndex = ADV_REWARD_FIRST_BOSS_KEY;

		// check four boss keys
		for (i = 0; i < 4; i++)
		{
			if (CHECK_ADV_BIT(adv->rewards, bitIndex) == 0)
			{
				goto LAB_800aebd0;
			}
			bitIndex++;
		}
#endif
	}
	// If you're not in Gemstone Valley
	else
	{
#ifdef CTR_AP
		// AP Phase 2: per-seed boss garage requirement when slot_data active.
		// hubID = levelID - GEM_STONE_VALLEY is 1..4 for the four boss hubs
		// (Roo, Papu, Komodo, Pinstripe) -> boss_req[0..3]. Phase-1 fallback is
		// the flat cumulative Trophy total (4, 8, 12, 16).
		if (ctr_cfg_active())
		{
			// AP Phase 2 (item #6): per-mode boss gate. Modes 0/1 (Original4Tracks /
			// SameHubTracks) check WON tracks; mode 2 (Trophies) falls back to the
			// resolved trophy-count requirement -- all inside AP_BossGarageOpen.
			if (!AP_BossGarageOpen((levelID - GEM_STONE_VALLEY) - 1))
				goto LAB_800aebd0;
		}
		else if (AP_GateCount(AP_IDX_TROPHY) < (levelID - GEM_STONE_VALLEY) * 4)
			goto LAB_800aebd0;
#else
		check = &data.advHubTrackIDs[(levelID - N_SANITY_BEACH) * 4];
		// check all four tracks on hub
		for (i = 0; i < 4; i++)
		{
			// if any trophy on this hub is not unlocked
			if (CHECK_ADV_BIT(adv->rewards, check[(s32)i] + ADV_REWARD_FIRST_TROPHY) == 0)
			{
				// boss is not open
				goto LAB_800aebd0;
			}
		}
#endif
	}
	goto LAB_800aec34;

LAB_800aebd0:
	bossIsOpen = false;

LAB_800aec34:
	dist[0] = drv_inst->matrix.t[0] - inst->instDef->pos.x;
	dist[1] = drv_inst->matrix.t[1] - inst->instDef->pos.y;
	dist[2] = drv_inst->matrix.t[2] - inst->instDef->pos.z;

	// if in a state where you're seeing the boss key open an adv door,
	// or some other kind of cutscene where you can't move
	if ((gGT->gameMode2 & 4) != 0)
	{
		return;
	}

	// check distance
	if (0x143fff < dist[0] * dist[0] + dist[1] * dist[1] + dist[2] * dist[2])
	{
		goto LAB_800aede0;
	}

	RECT view = gGT->pushBuffer[0].rect;

	// if aku is not giving a hint
	if (sdata->AkuAkuHintState == 0)
	{
		// draw string, lng_challenge
		DecalFont_DrawLine(

		    sdata->lngStrings[data.lng_challenge[R232.bossIDs[hubID]]],

		    (view.x + (view.w >> 1)), ((view.y + view.h) - 0x1e), 1, 0xffff8000);
	}

	if (bossIsOpen)
	{
		goto LAB_800aede8;
	}

#ifdef CTR_AP
	// #24: the door is locked and the player is standing at it. The vanilla Aku
	// hint below can only say "four trophies" / "four keys", so under slot_data it
	// misstates the requirement this gate actually enforces. Add one plain text
	// line carrying the real requirement, directly under the challenge name (the
	// hint itself still fires -- it is the retail flavour text, and suppressing it
	// would change vanilla behaviour for a cosmetic reason). Only drawn while Aku
	// is silent, matching the challenge-name line above, so the two cannot overlap
	// his hint box. Oxide's door is hubID 0 -> boss index 4; the four boss garages
	// are hubID 1..4 -> boss index 0..3.
	{
		// 128, not 64: Oxide's line (advertBoss 4) can now carry the composed
		// goal's companion terms as well as the door requirement (WO-A1), e.g.
		// "Requires: 4 Keys (have 4) + win 4 of 4 boss races (have 1)".
		char advert[128];
		int advertBoss = (levelID == GEM_STONE_VALLEY) ? 4 : (hubID - 1);

		// The challenge name above is FONT_BIG drawn from its TOP edge, so the
		// advert has to start a full FONT_BIG line lower or it lands inside the
		// title's glyph band (0x14 did exactly that: the title occupies
		// bottom-0x1e .. bottom-0x0d, the advert started at bottom-0x14). Stack it
		// the way the engine stacks its own wrapped lines -- advance by
		// font_charPixHeight of the line above (DecalFont_DrawMultiLineStrlen) --
		// which is data-driven and stays correct if the region tables differ. The
		// FONT_SMALL line is 8 px, so it still clears the 1P viewport bottom
		// (rect.h, 0xd8 NTSC) with room to spare.
		int advertY = ((view.y + view.h) - 0x1e) + data.font_charPixHeight[FONT_BIG];

		if (sdata->AkuAkuHintState == 0 &&
		    AP_BossGateAdvert(advertBoss, advert, (int)sizeof advert))
		{
			DecalFont_DrawLine(advert, (view.x + (view.w >> 1)), advertY, FONT_SMALL,
			                   0xffff8000);
		}
	}
#endif

	uVar8 = 0;

	// if this is gemstone valley
	if (levelID == GEM_STONE_VALLEY)
	{
		// if hint is not unlocked "need 4 keys for oxide"
		if (CHECK_ADV_BIT(sdata->advProgress.rewards, ADV_REWARD_HINT_NEED_FOUR_KEYS_FOR_OXIDE) == 0)
		{
			// HintID: need four keys to race oxide
			uVar8 = ADV_MASK_HINT_ID_NEED_FOUR_KEYS_FOR_OXIDE;
		}
	}
	// not gemstone valley
	else
	{
		//  if hint is not unlocked "to access this boss garage..."
		if (CHECK_ADV_BIT(sdata->advProgress.rewards, ADV_REWARD_HINT_NEED_FOUR_TROPHIES_FOR_BOSS) == 0)
		{
			// HintID: need four trophies to enter boss
			uVar8 = ADV_MASK_HINT_ID_NEED_FOUR_TROPHIES_FOR_BOSS;
		}
	}

	if (uVar8 != 0)
	{
		MainFrame_RequestMaskHint(uVar8, 0);
	}

LAB_800aede0:

	if (!bossIsOpen)
	{
		return;
	}

LAB_800aede8:

	sps->Input1.pos = inst->instDef->pos;
	sps->Input1.hitRadius = 0x300;
	sps->Input1.hitRadiusSquared = 0x90000;
	sps->Input1.modelID = STATIC_PINGARAGE;

	sps->Union.ThBuckColl.thread = t;
	sps->Union.ThBuckColl.funcCallback = AH_Garage_Open;

	// Open garage door when player gets within radius of door
	PROC_CollideHitboxWithBucket(gGT->threadBuckets[PLAYER].thread, sps, 0);

	ratio = MATH_Sin((int)inst->instDef->rot.y);

	pos[0] = (int)inst->instDef->pos.x + (ratio * -0x280 >> 0xc);
	pos[1] = (int)inst->instDef->pos.y;

	ratio = MATH_Cos((int)inst->instDef->rot.y);

	pos[2] = (int)inst->instDef->pos.z + (ratio * -0x280 >> 0xc);

	// DriverPos - DoorPos
	dist[0] = drv_inst->matrix.t[0] - pos[0];
	dist[1] = drv_inst->matrix.t[1] - pos[1];
	dist[2] = drv_inst->matrix.t[2] - pos[2];

	// If small distance (inside garage)
	if (dist[0] * dist[0] + dist[1] * dist[1] + dist[2] * dist[2] < 0x40000)
	{
		// Fade To Black
		gGT->pushBuffer_UI.fadeFromBlack_desiredResult = 0;
		gGT->pushBuffer_UI.fade_step = -0x2AA;
	}

	// If fade complete, start loading level
	if (gGT->pushBuffer_UI.fadeFromBlack_currentValue == 0)
	{
		sdata->Loading.OnBegin.RemBitsConfig0 |= ADVENTURE_ARENA;
		sdata->Loading.OnBegin.AddBitsConfig0 |= ADVENTURE_BOSS;

#ifdef CTR_AP
		// AP Phase 2 (issue #23): Oxide's Final Challenge unlock honours the
		// per-seed relic-goal MODE + COUNT (ctr_cfg.oxide_final_unlock /
		// oxide_final_count), resolved by AP_OxideFinalOpen against the received
		// relic-tier counts. Phase-1 fallback (no slot_data) = vanilla 18 Sapphire.
		if ((levelID == GEM_STONE_VALLEY) && AP_OxideFinalOpen())
#else
		if ((levelID == GEM_STONE_VALLEY) && (gGT->currAdvProfile.numRelics == 18))
#endif
		{
			// set string index (0-5) to "N Oxide's Final Challenge"
			gGT->bossID = 5;
		}

		else
		{
			gGT->bossID = R232.bossIDs[hubID];
		}

		// new levelID
		levelID = R232.bossTracks[hubID];

		// Set the boss character (P2)
		data.characterIDs[1] = data.metaDataLEV[levelID].characterID_Boss;

		RaceFlag_SetDrawOrder(1);
		MainRaceTrack_RequestLoad(levelID);
	}
	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800af070-0x800af3a4.
void AH_Garage_LInB(struct Instance *inst)
{
	char bossIsOpen, i;
	s16 *check;
	u32 bitIndex;
	int levelID;
	int ratio;
	struct AdvProgress *adv;
	struct Thread *t;
	struct Instance *garageTop;
	struct BossGarageDoor *garage;
	struct GameTracker *gGT;

	gGT = sdata->gGT;
	adv = &sdata->advProgress;
	bossIsOpen = true;
	levelID = gGT->levelID;

	if (inst->thread != NULL)
	{
		return;
	}

	t = PROC_BirthWithObject(SIZE_RELATIVE_POOL_BUCKET(sizeof(struct BossGarageDoor), NONE, SMALL, STATIC),

	                         AH_Garage_ThTick, // behavior
	                         R232.s_garage,    // debug name
	                         0                 // thread relative
	);

	if (t == NULL)
	{
		return;
	}

	inst->thread = t;
	t->inst = inst;
	t->funcThDestroy = AH_Garage_ThDestroy;

	garage = t->object;
	garage->direction = 0;
	garage->cooldown = 0;

	// if it is Oxide's Door
	if (inst->model->id == STATIC_OXIDEGARAGE)
	{
		garage->garageTopInst = NULL;
	}

	// if this is not oxide's door
	else
	{
		// make a "garagetop" to make door appear to roll up

		garageTop = INSTANCE_Birth3D(gGT->modelPtr[STATIC_GARAGETOP], R232.s_garagetop, t);

		// copy matrix from one instance to the other
		CTR_MatrixCopyRot(&garageTop->matrix, &inst->matrix);
		garageTop->matrix.t[0] = inst->matrix.t[0];
		garageTop->matrix.t[1] = inst->matrix.t[1];
		garageTop->matrix.t[2] = inst->matrix.t[2];

		ratio = MATH_Sin((int)inst->instDef->rot.y);

		// continue setting GarageTop position
		garageTop->matrix.t[0] = inst->matrix.t[0] + (ratio * 0x4c >> 0xc);
		garageTop->matrix.t[1] = inst->matrix.t[1] + 0x300;

		ratio = MATH_Cos((int)inst->instDef->rot.y);

		// continue setting GarageTop position
		garageTop->matrix.t[2] = inst->matrix.t[2] + (ratio * 0x4c >> 0xc);

		garageTop->depthBiasNormal = 0xfe;

		garage->garageTopInst = garageTop;
	}

	if (levelID == GEM_STONE_VALLEY)
	{
#ifdef CTR_AP
		// AP: the same canonical entry gate the ThTick collision/load path uses
		// (WO-A1). Presentation and gate MUST come from one predicate, or the
		// door renders open while the load refuses (or the reverse).
		if (!AP_OxideGarageOpen())
			goto GarageLocked;
		bossIsOpen = true;
#else
		// ripper roo boss key
		bitIndex = ADV_REWARD_FIRST_BOSS_KEY;
		// check four boss keys
		for (i = 0; i < 4; i++)
		{
			if (CHECK_ADV_BIT(adv->rewards, bitIndex) == 0)
			{
				goto GarageLocked;
			}
			bitIndex++;
		}
		bossIsOpen = true;
#endif
	}

	// if not gemstone valley
	else
	{
#ifdef CTR_AP
		// AP Phase 2: per-seed boss garage requirement (boss_req[hubID-1]) when
		// active; else Phase-1 flat cumulative Trophy total (Roo 4, Papu 8,
		// Komodo 12, Pinstripe 16). hubID = levelID - GEM_STONE_VALLEY is 1..4.
		if (ctr_cfg_active())
		{
			// AP Phase 2 (item #6): per-mode boss gate (see AH_Garage_ThTick).
			if (!AP_BossGarageOpen((levelID - GEM_STONE_VALLEY) - 1))
				goto GarageLocked;
		}
		else if (AP_GateCount(AP_IDX_TROPHY) < (levelID - GEM_STONE_VALLEY) * 4)
			goto GarageLocked;
#else
		check = &data.advHubTrackIDs[(levelID - N_SANITY_BEACH) * 4];
		// check all four tracks on hub
		for (i = 0; i < 4; i++)
		{
			// if any trophy on this hub is not unlocked
			if (CHECK_ADV_BIT(adv->rewards, check[(s32)i] + ADV_REWARD_FIRST_TROPHY) == 0)
			{
				// boss is not open
				goto GarageLocked;
			}
		}
#endif
	}

	// if boss is open
	if (bossIsOpen)
	{
		// check if boss was NOT beaten on this hub (levelID - 0x19)
		bitIndex = data.BeatBossPrize[levelID - GEM_STONE_VALLEY];
		t->modelIndex = (CHECK_ADV_BIT(adv->rewards, bitIndex) != 0) ? 2 : 1;
	}

	// if boss is not open
	else
	{
	GarageLocked:
		// locked
		t->modelIndex = 0;
	}

	garage->rot.x = inst->instDef->rot.x;
	garage->rot.y = inst->instDef->rot.y;
	garage->rot.z = inst->instDef->rot.z;

	inst->depthBiasNormal = 1;
	inst->depthBiasSecondary = inst->depthBiasNormal;
	inst->unk53 = 0;
	inst->vertSplit = inst->instDef->pos.y + 0x300;
}
