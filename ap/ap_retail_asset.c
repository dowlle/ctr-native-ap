// Raw BIGFILE subfile reads for AP-owned asset harvests.
//
// This is the #256 crate harvest's reader, moved out unchanged so more than one
// harvest can use it. The two comments below are the whole reason it exists as
// its own thing: both traps cost a live debugging session each, and neither is
// visible from the call site.

#ifdef CTR_AP

#include <common.h>
#include <stdio.h>

#include "ap_retail_asset.h"

#include <platform/native_assets.h>     // NativeAssets_OpenHostBigfile
#include <platform/native_disc_image.h> // NativeDiscImage_* (disc-image players)

// Two sources, matching how the engine itself resolves assets: a loose
// BIGFILE.BIG extracted next to the executable, or BIGFILE.BIG inside the
// player's disc image. Whichever the player has, we read the same sectors.
static int AP_RetailAsset_ReadSectors(int sector, int sectorCount, u8 *dst)
{
	struct NativeDiscImageFile file;
	FILE                      *fp;

	fp = NativeAssets_OpenHostBigfile("rb");
	if (fp != 0)
	{
		int ok = 0;

		if (fseek(fp, (long)sector * 2048, SEEK_SET) == 0)
			ok = (fread(dst, 1, (size_t)sectorCount * 2048, fp) == (size_t)sectorCount * 2048);

		fclose(fp);
		if (ok)
			return 1;
	}

	if (NativeDiscImage_FindFile("BIGFILE.BIG", &file))
		return NativeDiscImage_ReadDataSectors(&file, (u32)sector, (u32)sectorCount, dst);

	return 0;
}

// DELIBERATELY NOT via LOAD_ReadFile_ex. That is the engine's loader: it calls
// CDSYS_SetMode_StreamData(), drives the CD callback machinery and borrows the
// single global data.currSlot, all of which only ever run from the load state
// machine. Calling it from a gameplay frame crashed on entry to a relic race
// (access violation, before the crate harvest logged anything at all,
// 2026-08-17).
//
// The subfile is just bytes at a known offset, so we read them ourselves: the
// entry table is already in memory at sdata->ptrBigfile1, entry.offset is a
// sector index relative to BIGFILE, and the native asset layer can serve those
// sectors from either a loose BIGFILE.BIG or the player's disc image. No engine
// state is touched.
//
// isDramFile distinguishes the TWO subfile shapes, which the engine handles with
// two different callbacks and which are NOT interchangeable:
//
//   DRAM file (levels, model packs) -- word 0 is the pointer-map offset, body
//                          starts at +4, and the fixup must run
//                          (LOAD_DramFileCallback).
//   VRAM file (textures) -- word 0 is 0x20, the packed-TIM marker, body starts
//                          at 0, and there is NO pointer map
//                          (LOAD_VramFileCallback).
//
// Running the fixup on a texture file reads 0x20 as a pointer-map offset and
// then writes the buffer's base address into arbitrary offsets across the pixel
// data until it walks off the end. That is exactly what crashed the first four
// crate-harvest attempts (2026-08-17).
u8 *AP_RetailAsset_ReadSubfile(int subfileIndex, int isDramFile, u8 *dst, int dstSize, int *outSize)
{
	struct BigHeader *bigfile;
	struct BigEntry  *entries;
	int               offsetSectors, size, sectorCount;
	int               ptrMapOffset;
	u8               *body;

	if (dst == 0 || dstSize <= 0)
		return 0;

	if (sdata == 0 || sdata->ptrBigfile1 == 0)
		return 0;

	bigfile = sdata->ptrBigfile1;
	if (subfileIndex < 0 || subfileIndex >= bigfile->numEntry)
		return 0;

	entries = BIG_GETENTRY(bigfile);
	offsetSectors = entries[subfileIndex].offset;
	size = entries[subfileIndex].size;

	if (size <= 4 || size > dstSize)
		return 0;

	// The reader writes whole sectors, so the buffer must hold the rounded size.
	sectorCount = (size + 0x7FF) >> 11;
	if (sectorCount * 2048 > dstSize)
		return 0;

	if (!AP_RetailAsset_ReadSectors(offsetSectors, sectorCount, dst))
		return 0;

	if (!isDramFile)
	{
		// Texture file: raw from byte 0, no pointer map.
		if (outSize != 0)
			*outSize = size;
		return dst;
	}

	// Word 0 is the pointer-map offset relative to the body at dst+4.
	ptrMapOffset = *(const int *)dst;
	body = dst + 4;

	if (ptrMapOffset >= 0 && ptrMapOffset + 4 <= size - 4)
	{
		struct DramPointerMap *dpm = (struct DramPointerMap *)(body + ptrMapOffset);
		LOAD_RunPtrMap((char *)body, (int *)DRAM_GETOFFSETS(dpm), dpm->numBytes >> 2);
	}

	if (outSize != 0)
		*outSize = size - 4;

	return body;
}

#endif // CTR_AP
