#ifndef CTR_NATIVE_PTR_SCRATCH_H
#define CTR_NATIVE_PTR_SCRATCH_H

#include <stdint.h>

// Native PTR files are temporary pointer-map data. Keep them outside the
// gameplay mempack so their capacity cannot depend on prior game allocations.
#define CTR_NATIVE_PTR_SCRATCH_CAPACITY 0x10000u

static inline uint32_t NativePtrScratch_PhysicalBytes(uint32_t payloadBytes)
{
	return (payloadBytes + 0x7ffu) & ~0x7ffu;
}

static inline int NativePtrScratch_CanFit(uint32_t payloadBytes, uint32_t capacity)
{
	if (payloadBytes > UINT32_MAX - 0x7ffu)
		return 0;
	return NativePtrScratch_PhysicalBytes(payloadBytes) <= capacity;
}

void *NativePtrScratch_Data(void);
uint32_t NativePtrScratch_Capacity(void);
void NativePtrScratch_Require(uint32_t payloadBytes, const char *context);

#endif
