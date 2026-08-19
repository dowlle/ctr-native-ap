#include "native_ptr_scratch.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned char s_nativePtrScratch[CTR_NATIVE_PTR_SCRATCH_CAPACITY];

void *NativePtrScratch_Data(void)
{
	return s_nativePtrScratch;
}

uint32_t NativePtrScratch_Capacity(void)
{
	return (uint32_t)sizeof(s_nativePtrScratch);
}

void NativePtrScratch_Require(uint32_t payloadBytes, const char *context)
{
	uint32_t physical = NativePtrScratch_PhysicalBytes(payloadBytes);
	if (NativePtrScratch_CanFit(payloadBytes, sizeof(s_nativePtrScratch)))
		return;

	fprintf(stderr,
	        "FATAL: PTR read exceeds native scratch capacity "
	        "(context=%s payload=%u physical=%u capacity=%u)\n",
	        context ? context : "unknown", payloadBytes, physical,
	        (unsigned)sizeof(s_nativePtrScratch));
	abort();
}
