#ifndef CTR_NATIVE_CLIP_SCRATCH_H
#define CTR_NATIVE_CLIP_SCRATCH_H

#include <stdint.h>

// Per-player clip records are per-frame render scratch. MainInit_JitPoolsNew
// hands every player a block of MainDB_GetClipSize(levelID, players) 32-bit
// entries, the DrawLevelOvr* overlays fill it while drawing the frame, and
// nothing reads it once the frame ends. Retail took that block from MEMPACK,
// which makes it the largest single allocation a level load performs (96,000
// bytes at the adventure garage, 10,000-12,000 on a race track) and puts it in
// direct competition with level and racer assets for the 0x144e10 arena.
//
// Keeping it in process-owned storage removes that competition entirely: the
// buffer's capacity no longer depends on which racer model happened to load
// first, which is what decided whether a track load survived.
//
// Capacity is the worst case of the MainDB_GetClipSize table rather than a
// guess. The largest entry count that table can return is ADVENTURE_GARAGE's
// 24,000, an entry is four bytes, and data.PtrClipBuffer holds at most four
// players. tools/test-native-clip-scratch.c re-derives the 24,000 by reading
// game/MAIN/MainDB.c, so a future table edit that outgrows this buffer fails
// the harness instead of silently overrunning it.
#define CTR_NATIVE_CLIP_SCRATCH_MAX_ENTRIES 24000u
#define CTR_NATIVE_CLIP_SCRATCH_MAX_PLAYERS 4u
#define CTR_NATIVE_CLIP_SCRATCH_CAPACITY \
	(CTR_NATIVE_CLIP_SCRATCH_MAX_ENTRIES * 4u * CTR_NATIVE_CLIP_SCRATCH_MAX_PLAYERS)

static inline uint32_t NativeClipScratch_PlayerBytes(uint32_t clipEntries)
{
	return clipEntries * 4u;
}

static inline int NativeClipScratch_CanFit(uint32_t clipEntries, uint32_t players, uint32_t capacity)
{
	if ((players == 0u) || (players > CTR_NATIVE_CLIP_SCRATCH_MAX_PLAYERS))
		return 0;

	// Reject before the multiply so an absurd table value cannot wrap into a
	// small, plausible-looking byte count.
	if (clipEntries > (UINT32_MAX / (4u * CTR_NATIVE_CLIP_SCRATCH_MAX_PLAYERS)))
		return 0;

	return (NativeClipScratch_PlayerBytes(clipEntries) * players) <= capacity;
}

void *NativeClipScratch_Player(uint32_t playerIndex, uint32_t clipEntries, uint32_t players);
uint32_t NativeClipScratch_Capacity(void);

#endif
