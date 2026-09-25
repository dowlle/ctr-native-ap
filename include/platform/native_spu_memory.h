#ifndef NATIVE_SPU_MEMORY_H
#define NATIVE_SPU_MEMORY_H
// Size of the emulated SPU sound memory and the unit of the engine's SPU
// addresses.
//
// Retail: 512 KB. The HOWL allocator keeps addresses in 8-byte units in u16
// fields (SpuAddrEntry.spuAddr, Bank.min and Bank.max, sdata->audioAllocPtr),
// so 512 KB is also the most they can express, and Bank_AssignSpuAddrs stops
// a bank that would end at or past 0x7e000.
//
// Box authoring build (CTR_CUSTOM_PACKAGES): 1 MiB, so a custom track's own
// Saphi sound bank fits next to bank 0, the eight-driver bank 54 and the
// character banks. The same u16 fields then hold addresses in 16-byte units,
// which reach 1 MiB. Nothing read from KART.HWL changes: spuSize stays in
// 8-byte units as on disc, and every spuAddr is 0 on disc and assigned at run
// time. A sample size is a whole number of 16-byte ADPCM blocks (all 528
// retail entries are even in 8-byte units), so every retail bank lands at the
// same byte address as before; only the ceiling grows. The top 8 KB stay
// unused, as in retail. The native reverb keeps its own buffer
// (native_audio.c) and never uses SPU memory.
//
// The player client builds without CTR_CUSTOM_PACKAGES, where every macro
// below reduces to the retail constant.

#define CTR_SPU_RETAIL_BYTES   (512 * 1024)
#define CTR_SPU_RETAIL_CEILING 0x7e000
#define CTR_SPU_WIDE_BYTES     (1024 * 1024)
#define CTR_SPU_WIDE_CEILING   (CTR_SPU_WIDE_BYTES - (CTR_SPU_RETAIL_BYTES - CTR_SPU_RETAIL_CEILING))

#ifdef CTR_CUSTOM_PACKAGES
#define CTR_SPU_BYTES      CTR_SPU_WIDE_BYTES
#define CTR_SPU_CEILING    CTR_SPU_WIDE_CEILING
#define CTR_SPU_ADDR_SHIFT 4
#else
#define CTR_SPU_BYTES      CTR_SPU_RETAIL_BYTES
#define CTR_SPU_CEILING    CTR_SPU_RETAIL_CEILING
#define CTR_SPU_ADDR_SHIFT 3
#endif

// Byte count to address units, address units to bytes, and a disc spuSize
// (8-byte units) to address units.
#define CTR_SPU_UNITS(bytes)     ((bytes) >> CTR_SPU_ADDR_SHIFT)
#define CTR_SPU_BYTES_AT(units)  ((units) * (1 << CTR_SPU_ADDR_SHIFT))
#define CTR_SPU_SIZE_UNITS(size) ((size) >> (CTR_SPU_ADDR_SHIFT - 3))

#endif
