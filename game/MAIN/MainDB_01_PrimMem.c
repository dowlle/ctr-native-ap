#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800349c4-0x80034a28.
void MainDB_PrimMem(struct PrimMem *primMem, u32 size)
{
	u32 alignedSize;
	void *pvVar1;

	pvVar1 = MEMPACK_AllocMem(size);
	primMem->size = size;
	primMem->unk2 = (int)pvVar1;
	primMem->curr = pvVar1;
	primMem->start = pvVar1;

	alignedSize = (size >> 2) << 2;
	pvVar1 = (void *)((int)pvVar1 + alignedSize);
	primMem->end = pvVar1;
	primMem->endMin100 = (void *)((int)pvVar1 - 0x100);
}
