#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80034a28-0x80034a80.
void MainDB_OTMem(struct OTMem *otMem, u32 size)
{
	u32 alignedSize;
	void *pvVar1;

	pvVar1 = MEMPACK_AllocMem(size);
	otMem->size = size;
	otMem->curr = pvVar1;
	otMem->start = pvVar1;

	alignedSize = (size >> 2) << 2;
	otMem->end = (void *)((int)pvVar1 + alignedSize);
}
