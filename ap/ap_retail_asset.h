#ifndef AP_RETAIL_ASSET_H
#define AP_RETAIL_ASSET_H

#ifdef CTR_AP

// Read one subfile out of the player's own BIGFILE into a caller-owned buffer.
//
// Extracted verbatim from the #256 crate harvest so the crystal harvest (#219)
// and any later asset (#221 token ceremonies, #222 wumpa packages) share ONE
// copy of the two traps this reader exists to get right: the engine's loader
// must not be called from a gameplay frame, and level files and texture files
// are different shapes that are not interchangeable. Both are spelled out at the
// implementation.
//
// `isDramFile` picks the shape:
//   1 -- DRAM file (levels, model packs). Word 0 is the pointer-map offset, the
//        body starts at +4, and the engine's own fixup is run so the file
//        becomes walkable with the ordinary struct definitions. The returned
//        pointer is the BODY, and *outSize is the body's size.
//   0 -- VRAM file (textures). Raw from byte 0, no pointer map. The returned
//        pointer is the buffer itself.
//
// Returns the pointer described above, or 0 on any failure. Nothing is written
// outside `dst`, and no engine state is touched.
unsigned char *AP_RetailAsset_ReadSubfile(int subfileIndex, int isDramFile, unsigned char *dst, int dstSize,
                                          int *outSize);

#endif // CTR_AP
#endif // AP_RETAIL_ASSET_H
