#include "native_clip_scratch.h"

#include <stdio.h>
#include <stdlib.h>

// The union gives the block the 32-bit alignment MEMPACK_AllocMem used to
// guarantee; the overlays write the records as words.
static union
{
	unsigned char bytes[CTR_NATIVE_CLIP_SCRATCH_CAPACITY];
	uint32_t words[CTR_NATIVE_CLIP_SCRATCH_CAPACITY / 4u];
} s_nativeClipScratch;

uint32_t NativeClipScratch_Capacity(void)
{
	return (uint32_t)sizeof(s_nativeClipScratch.bytes);
}

void *NativeClipScratch_Player(uint32_t playerIndex, uint32_t clipEntries, uint32_t players)
{
	if ((playerIndex >= players) ||
	    !NativeClipScratch_CanFit(clipEntries, players, (uint32_t)sizeof(s_nativeClipScratch.bytes)))
	{
		// A miss here means the clip-size table outgrew this buffer. Say so
		// rather than returning a short block the overlays would write past.
		fprintf(stderr,
		        "FATAL: clip buffer exceeds native scratch capacity "
		        "(player=%u players=%u entries=%u capacity=%u)\n",
		        playerIndex, players, clipEntries,
		        (unsigned)sizeof(s_nativeClipScratch.bytes));
		abort();
	}

	return &s_nativeClipScratch.bytes[playerIndex * NativeClipScratch_PlayerBytes(clipEntries)];
}
