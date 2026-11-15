#include <common.h>
#if defined(CTR_NATIVE)
#include "../../platform/native_clip_scratch.h"
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80034960-0x800349c4.
int MainDB_GetClipSize(u32 levelID, int numPlyrCurrGame)
{
	switch (levelID)
	{
	case ADVENTURE_GARAGE:
		return 24000;

	case MAIN_MENU_LEVEL:
		return 16;

	case SEWER_SPEEDWAY:
		return 6000;

	case MYSTERY_CAVES:
		return 2500;

	case PAPU_PYRAMID:
	case POLAR_PASS:
		if (numPlyrCurrGame < 3)
		{
			return 3000;
		}

		return 2500;

	default:
		return 3000;
	}
}

// The per-player clip-record entry count the game actually allocates and
// bounds its writers with. Retail's table above stays as retail computes it;
// native multiplies it by CTR_NATIVE_CLIP_SCRATCH_SCALE (see
// platform/native_clip_scratch.h). Allocation (MainInit_JitPoolsNew), the
// checkpoint rebind and the writers' end test must all ask here, never the
// retail table directly, or the slices and the end test disagree.
int MainDB_GetClipEntries(u32 levelID, int numPlyrCurrGame)
{
#if defined(CTR_NATIVE)
	return (int)NativeClipScratch_Entries((u32)MainDB_GetClipSize(levelID, numPlyrCurrGame));
#else
	return MainDB_GetClipSize(levelID, numPlyrCurrGame);
#endif
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800349c4-0x80034a28.
void MainDB_PrimMem(struct PrimMem *primMem, u32 size)
{
	u32 alignedSize;
	void *pvVar1;

	pvVar1 = MEMPACK_AllocMem(size);
	primMem->capacityBytes = size;
	primMem->allocationStart = pvVar1;
	primMem->cursor = pvVar1;
	primMem->start = pvVar1;

	alignedSize = (size >> 2) << 2;
	pvVar1 = (void *)((int)pvVar1 + alignedSize);
	primMem->end = pvVar1;
	primMem->guardEnd = (void *)((int)pvVar1 - 0x100);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80034a28-0x80034a80.
void MainDB_OTMem(struct OTMem *otMem, u32 size)
{
	u32 alignedSize;
	void *pvVar1;

	pvVar1 = MEMPACK_AllocMem(size);
	otMem->capacityBytes = size;
	otMem->cursor = pvVar1;
	otMem->start = pvVar1;

	alignedSize = (size >> 2) << 2;
	otMem->end = (void *)((int)pvVar1 + alignedSize);
}
