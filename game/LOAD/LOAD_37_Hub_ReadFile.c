#include <common.h>

// NOTE(aalhendi): ASM-audited NTSC-U 926 0x80032ffc-0x80033108.
// packID will always be 3-gGT->activeMempackIndex
void LOAD_Hub_ReadFile(struct BigHeader *bigfile, int levID, int packID)
{
	struct GameTracker *gGT = sdata->gGT;

	// if level is already loaded, quit
	if (gGT->levID_in_each_mempack[packID] == levID)
		return;

	sdata->modelMaskHints3D = 0;

	// Swap to pack of hub you're NOT on,
	// wipe the pack to reload the new hub
	MEMPACK_SwapPacks(packID);
	MEMPACK_ClearLowMem();

	sdata->PatchMem_Size = 1;
	gGT->level2 = 0;
	gGT->levID_in_each_mempack[packID] = levID;

	LOAD_AppendQueue(bigfile, LT_VRAM, LOAD_GetBigfileIndex(levID, 1, LVI_VRAM), NULL, NULL);
	LOAD_AppendQueue(bigfile, LT_GETADDR, LOAD_GetBigfileIndex(levID, 1, LVI_LEV), NULL, LOAD_Callback_LEV);
	LOAD_AppendQueue(bigfile, LT_SETADDR, LOAD_GetBigfileIndex(levID, 1, LVI_PTR), (void *)sdata->PatchMem_Ptr, LOAD_HubCallback);
}
