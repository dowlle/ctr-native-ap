#include <common.h>

#ifdef CTR_AP
// A3-12: hub streaming waits for a running mask hint (ap/ap_hub_hint_hold.h).
static ap_hub_hint_hold s_apHubHintHold = {-1, -1, 0, -1};

// LOAD_TalkingMask is about to load the mask over the second pack's hub.
void LOAD_Hub_ApMaskEvicts(int packID)
{
	struct GameTracker *gGT = sdata->gGT;
	ap_hub_hint_hold_evict(&s_apHubHintHold, gGT->levID_in_each_mempack[packID], gGT->levelID);
}
#endif

// NOTE(aalhendi): ASM-audited NTSC-U 926 0x80032ffc-0x80033108.
// packID will always be 3-gGT->activeMempackIndex
void LOAD_Hub_ReadFile(struct BigHeader *bigfile, int levID, int packID)
{
	struct GameTracker *gGT = sdata->gGT;

	// if level is already loaded, quit
	if (gGT->levID_in_each_mempack[packID] == levID)
	{
		return;
	}

	sdata->modelMaskHints3D = 0;

	// Swap to pack of hub you're NOT on,
	// wipe the pack to reload the new hub
	MEMPACK_SwapPacks(packID);
	MEMPACK_ClearLowMem();

#if defined(CTR_NATIVE)
	// Native PTR scratch has process lifetime and fixed capacity. Rebind here as
	// well as at initial load so a checkpoint or older state can never restore
	// the former mempack-owned pointer.
	sdata->PatchMem_Ptr = NativePtrScratch_Data();
	sdata->PatchMem_Size = NativePtrScratch_Capacity();
#else
	sdata->PatchMem_Size = 1;
#endif
	gGT->level2 = 0;
	gGT->levID_in_each_mempack[packID] = levID;

	LOAD_AppendQueue(bigfile, LT_VRAM, LOAD_GetBigfileIndex(levID, 1, LVI_VRAM), NULL, NULL);
	LOAD_AppendQueue(bigfile, LT_GETADDR, LOAD_GetBigfileIndex(levID, 1, LVI_LEV), NULL, LOAD_Callback_LEV);
	{
		int ptrIndex = LOAD_GetBigfileIndex(levID, 1, LVI_PTR);
#if defined(CTR_NATIVE)
		struct BigHeader *ptrBigfile = bigfile ? bigfile : sdata->ptrBigfile1;
		struct BigEntry *entries = BIG_GETENTRY(ptrBigfile);
		NativePtrScratch_Require((u32)entries[ptrIndex].size, "hub background PTR");
#endif
		LOAD_AppendQueue(bigfile, LT_SETADDR, ptrIndex, sdata->PatchMem_Ptr, LOAD_HubCallback);
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80033108-0x80033318.
void LOAD_Hub_SwapNow()
{
	struct Level *level1;
	struct VisMem *visMem;
	struct CameraDC *cDC;
	struct GameTracker *gGT = sdata->gGT;

	// stall until load is done
	while (gGT->level2 == 0)
	{
		LOAD_NextQueuedFile();
		VSync(0);
	}

	// Aug 5
	// ptrintf("gGT->level2 = 0x%08x\n",gGT->level2);
	// ptrintf("SWAPPING 1...\n");

	LevInstDef_RePack(gGT->level1->ptr_mesh_info, 1);

	// Aug 5
	// ptrintf("SWAPPING 2...\n");

	LOAD_HubSwapPtrs(gGT);

	// 0,1,2
	gGT->activeMempackIndex = 3 - gGT->activeMempackIndex;

	gGT->prevLEV = gGT->levelID;
	gGT->levelID = gGT->levID_in_each_mempack[gGT->activeMempackIndex];

	Audio_AdvHub_SwapSong(gGT->levelID);

	// Aug 5
	// ptrintf("SWAPPING 3...\n");

	LibraryOfModels_Clear(gGT);

	/*
	In Aug 5
	if (sdata->PLYROBJECTLIST == 0)
	{
	    printf("ERROR: No PLYROBJECTLIST!\n");
	}
	*/

	if (sdata->PLYROBJECTLIST != 0)
	{
		LOAD_GlobalModelPtrs_MPK();
	}

	level1 = gGT->level1;

	/*
	In Aug 5
	if (level1 == 0)
	{
	    printf("ERROR: No LEVEL!\n");
	}
	*/

	if (level1 != 0)
	{
		LibraryOfModels_Store(gGT, level1->numModels, level1->ptrModelsPtrArray);

		INSTANCE_LevInitAll(level1->ptrInstDefs, level1->numInstances);

		LevInstDef_UnPack(level1->ptr_mesh_info);

		DecalGlobal_Store(gGT, level1->levTexLookup);
	}

	MEMPACK_SwapPacks(gGT->activeMempackIndex);
	MainInit_VisMem(gGT);

	visMem = gGT->visMem1;
	int views = 1;
#ifdef CTR_AP
	if (AP_CoopHub(gGT))
		views = 2;
#endif
	for (int i = 0; i < views; i++)
	{
		cDC = &gGT->cameraDC[i];
		cDC->ptrQuadBlock = 0;
		cDC->visLeafSrc = 0;
		cDC->visFaceSrc = 0;
		cDC->visInstSrc = 0;
		cDC->visOVertSrc = 0;
		cDC->visSCVertSrc = 0;
		visMem->visLeafSrc[i] = 0;
		visMem->visFaceSrc[i] = 0;
		visMem->visOVertSrc[i] = 0;
		visMem->visSCVertSrc[i] = 0;
		gGT->drivers[i]->underDriver = 0;
	}

	gGT->framesInThisLEV = 0;
	gGT->msInThisLEV = 0;
}

// NOTE(aalhendi): Native mirrors retail rdata 0x80011180 because CTR_NATIVE
// does not expose the retail rdata object.
#if defined(CTR_NATIVE)
static const int s_advHubConnectedLevID[5][3] = {
    {N_SANITY_BEACH, THE_LOST_RUINS, -1},
    {GEM_STONE_VALLEY, GLACIER_PARK, -1},
    {GEM_STONE_VALLEY, GLACIER_PARK, -1},
    {N_SANITY_BEACH, THE_LOST_RUINS, CITADEL_CITY},
    {GLACIER_PARK, -1, -1},
};
#define LOAD_HUB_CONNECTED_LEV(hub, index) s_advHubConnectedLevID[(hub)][(index)]
#else
#define LOAD_HUB_CONNECTED_LEV(hub, index) rdata.MetaDataHubs[(hub)].connectedHub_LevID[(index)]
#endif

#ifdef CTR_AP
static void LOAD_Hub_Coop(struct BigHeader *bigfile, struct GameTracker *gt)
{
	const int hub = gt->levelID - GEM_STONE_VALLEY;
	const int otherPack = 3 - gt->activeMempackIndex;
	const int held = AP_CoopHubHeldDriver(gt);
	if (hub < 0 || hub >= 5 || held >= 0 || !gt->drivers[0] || !gt->drivers[1])
		return;

	// Keep each driver's last tunnel target. Either player can prefetch,
	// but swapping waits for the hub requested by the driver who crossed.
	static int targets[2] = {-1, -1};
	if (gt->framesInThisLEV == 0)
		targets[0] = targets[1] = -1;
	for (int i = 0; i < 2; i++)
	{
		const CollStepFlags flags = gt->drivers[i]->stepFlagSet;
		const int entry = (flags & COLL_STEP_TRIGGER_HUB_LEVEL_ID_MASK) >> COLL_STEP_TRIGGER_HUB_LEVEL_ID_SHIFT;
		if (entry > 0 && entry <= 3)
			targets[i] = LOAD_HUB_CONNECTED_LEV(hub, entry - 1);
	}
	for (int i = 0; i < 2; i++)
	{
		const CollStepFlags flags = gt->drivers[i]->stepFlagSet;
		if (!(flags & COLL_STEP_TRIGGER_HUB_SWAP_NOW_MASK))
			continue;
		const int target = targets[i] >= 0 ? targets[i] : gt->levID_in_each_mempack[otherPack];
		if (target < GEM_STONE_VALLEY || target > CITADEL_CITY || target == gt->levelID)
			continue;
		if (sdata->queueLength || !sdata->queueReady)
			return;
		if (!gt->level2)
			gt->levID_in_each_mempack[otherPack] = -1;
		LOAD_Hub_ReadFile(bigfile, target, otherPack);
		LOAD_Hub_SwapNow();
		for (int p = 0; p < 2; p++)
		{
			gt->drivers[p]->currBlockTouching = NULL;
			gt->drivers[p]->lastValid = NULL;
			gt->drivers[p]->stepFlagSet = 0;
		}
		AP_CoopPlaceBeside(gt, i);
		targets[0] = targets[1] = -1;
		return;
	}
	if (sdata->queueLength || !sdata->queueReady)
		return;
	for (int i = 0; i < 2; i++)
	{
		const CollStepFlags flags = gt->drivers[i]->stepFlagSet;
		if (targets[i] >= 0 && (flags & COLL_STEP_TRIGGER_HUB_LEVEL_ID_MASK))
		{
			LOAD_Hub_ReadFile(bigfile, targets[i], otherPack);
			return;
		}
	}
}
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80033318-0x80033474.
void LOAD_Hub_Main(struct BigHeader *bigfilePtr)
{
	struct GameTracker *gGT;

	// quit if already loading
	if (sdata->Loading.stage != LOAD_IDLE)
	{
		return;
	}

	gGT = sdata->gGT;

#ifdef CTR_AP
	if (AP_CoopHub(gGT) && !gGT->bool_AdvHub_NeedToSwapLEV)
	{
		LOAD_Hub_Coop(bigfilePtr, gGT);
		return;
	}

	// A read held for a mask hint goes first once the hint is over; a held
	// swap follows on a later tick, after the read is queued, and only into a
	// hub (ap/ap_hub_hint_hold.h).
	{
		int heldLev = ap_hub_hint_hold_release(&s_apHubHintHold, sdata->AkuAkuHintState, gGT->levelID);
		if (heldLev >= 0)
		{
			LOAD_Hub_ReadFile(bigfilePtr, heldLev, 3 - gGT->activeMempackIndex);
			return;
		}
		if (ap_hub_hint_hold_swap_release(&s_apHubHintHold, sdata->AkuAkuHintState, gGT->levelID,
		                                  gGT->levID_in_each_mempack[3 - gGT->activeMempackIndex]))
		{
			gGT->bool_AdvHub_NeedToSwapLEV = 0;
			LOAD_Hub_SwapNow();
			return;
		}
	}
#endif

	for (int i = 0; i < gGT->numPlyrCurrGame; i++)
	{
		CollStepFlags stepFlagSet = gGT->drivers[i]->stepFlagSet;
		int nextLevelID = (stepFlagSet & COLL_STEP_TRIGGER_HUB_LEVEL_ID_MASK) >> COLL_STEP_TRIGGER_HUB_LEVEL_ID_SHIFT;
		int needSwapNow = (stepFlagSet & COLL_STEP_TRIGGER_HUB_SWAP_NOW_MASK) >> COLL_STEP_TRIGGER_HUB_SWAP_NOW_SHIFT;

		// if new level does not need to load
		if (nextLevelID == 0)
		{
			if ((needSwapNow != 0) || (gGT->bool_AdvHub_NeedToSwapLEV != 0))
			{
#ifdef CTR_AP
				// The second pack may hold the mask, a hub the mask evicted, or
				// nothing after a dropped hold.
				if (ap_hub_hint_hold_swap(&s_apHubHintHold, sdata->AkuAkuHintState, gGT->levelID,
				                          gGT->levID_in_each_mempack[3 - gGT->activeMempackIndex]))
					continue;
#endif
				gGT->bool_AdvHub_NeedToSwapLEV = 0;
				LOAD_Hub_SwapNow();
			}
		}

		// if new level needs to load
		else
		{
			// only in AdvHub, or else the game
			// crashes in 4P Nitro Court Life Limit
			u32 currLevelID = gGT->levelID - GEM_STONE_VALLEY;

			// ctr hubs are 0-4
			if (currLevelID >= 5)
			{
				return;
			}

#ifdef CTR_AP
			// Reading into the second pack now would wipe a running hint's mask.
			if (ap_hub_hint_hold_read(&s_apHubHintHold, sdata->AkuAkuHintState,
			                          LOAD_HUB_CONNECTED_LEV(currLevelID, nextLevelID - 1), gGT->levelID))
				continue;
#endif
			LOAD_Hub_ReadFile(bigfilePtr, LOAD_HUB_CONNECTED_LEV(currLevelID, nextLevelID - 1), 3 - gGT->activeMempackIndex);
		}
	}
}
