#ifndef AP_DRAM_PTR_MAP_LOGIC_H
#define AP_DRAM_PTR_MAP_LOGIC_H

// Freestanding, overflow-safe validation for a DRAM file's pointer map, and the
// bounded relocation walk that follows it.
//
// WHY THIS EXISTS
// ---------------
// LOAD_RunPtrMap (game/LOAD/LOAD_Assets.c) trusts its caller completely: it
// walks `numPtrs` entries of the offsets array and writes
// `*(int *)&origin[offset] += (int)origin` for each, where `offset` is masked to
// a four-byte boundary. The retail loader guards that call with nothing but a
// `ptrMapOffset >= 0` test (LOAD_File.c:117-135), so a file whose stored
// numBytes or patch offsets were corrupted would have LOAD_RunPtrMap write the
// buffer base into arbitrary offsets until it walked off the end of the
// allocation. #256's AP_RetailAsset_ReadSubfile inherited exactly that trust: it
// bounded only the pointer-map HEADER (that ptrMapOffset + 4 fit), not the
// offsets array the header describes.
//
// #222 harvests a model out of the player's own BIGFILE and must guarantee a
// bounded failure rather than a crash if that data is not the shape it expects.
// So the read validates the WHOLE map before LOAD_RunPtrMap is allowed to run:
//
//   - the header word (numBytes) must fit inside the DRAM body;
//   - numBytes must be non-negative, a multiple of four, and describe an offsets
//     array that also fits inside the body;
//   - every decoded patch offset must be four-byte aligned and must name a
//     complete 32-bit word inside the body;
//   - every size computation is done in unsigned arithmetic so no input can wrap
//     the bounds checks into passing.
//
// This is pure integer work over offsets, so it compiles and is driven without
// the engine (tools/test-wumpa-residency.c). ap_retail_asset.c calls into it
// rather than keeping a second copy of the rule, so the harness pins the shipped
// behaviour.
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

// The map is valid when body[ptrMapOffset] holds a numBytes whose offsets array
// fits in the body and every entry names an aligned, complete word inside it.
// Returns 1 and writes *outNumPtrs on success, 0 otherwise (outNumPtrs
// untouched). `bodySize` is the size of the caller's readable body.
//
// numBytes is the byte count of the offsets array (DRAM_GETOFFSETS walks it as
// numBytes >> 2 ints), so it must be a non-negative multiple of four that leaves
// room for the header itself before the body ends.
static inline int AP_DramPtrMap_Validate(const unsigned char *body, unsigned int bodySize, int ptrMapOffset, int *outNumPtrs)
{
	unsigned int count, i;

	if (body == 0 || outNumPtrs == 0)
		return 0;

	// The header word must sit wholly inside the body.
	if (ptrMapOffset < 0 || (unsigned int)ptrMapOffset > bodySize)
		return 0;
	if (bodySize - (unsigned int)ptrMapOffset < 4u)
		return 0;

	{
		int numBytes;

		// Read through a locally-assembled value so the helper stays alignment
		// agnostic on the host and works when the map is not four-byte aligned.
		unsigned int b0 = body[(unsigned int)ptrMapOffset + 0u];
		unsigned int b1 = body[(unsigned int)ptrMapOffset + 1u];
		unsigned int b2 = body[(unsigned int)ptrMapOffset + 2u];
		unsigned int b3 = body[(unsigned int)ptrMapOffset + 3u];

		numBytes = (int)(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
		if (numBytes < 0)
			return 0;
		if ((numBytes & 3) != 0)
			return 0;

		count = (unsigned int)numBytes >> 2;

		// The complete offsets array must fit after the header word.
		if ((unsigned int)numBytes > bodySize - (unsigned int)ptrMapOffset - 4u)
			return 0;
	}

	// Every entry must name an aligned, complete 32-bit word inside the body.
	for (i = 0; i < count; i++)
	{
		unsigned int off = (unsigned int)ptrMapOffset + 4u + i * 4u;
		unsigned int raw = (unsigned int)body[off + 0u] | ((unsigned int)body[off + 1u] << 8) |
		                   ((unsigned int)body[off + 2u] << 16) | ((unsigned int)body[off + 3u] << 24);
		unsigned int decoded = (raw >> 2) << 2; // LOAD_RunPtrMap's own alignment

		if (decoded > bodySize)
			return 0;
		if (bodySize - decoded < 4u)
			return 0;
	}

	*outNumPtrs = (int)count;
	return 1;
}

#endif // CTR_AP
#endif // AP_DRAM_PTR_MAP_LOGIC_H
