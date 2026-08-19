// cc -Wall -Wextra -o /tmp/test-native-ptr-scratch tools/test-native-ptr-scratch.c
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../platform/native_ptr_scratch.h"

static int failures;

static void expect(int condition, const char *name)
{
	printf("%s  %s\n", condition ? "ok  " : "FAIL", name);
	if (!condition)
		failures++;
}

int main(void)
{
	const uint32_t shippedBigfileMaxPayload = 48032;
	const uint32_t shippedBigfileMaxPhysical = 49152;
	unsigned char guarded[CTR_NATIVE_PTR_SCRATCH_CAPACITY + 32];
	memset(guarded, 0xa5, sizeof guarded);

	expect(NativePtrScratch_PhysicalBytes(45072) == 47104,
	       "captured N. Sanity PTR payload rounds to 47,104 bytes");
	expect(!NativePtrScratch_CanFit(45072, 38072),
	       "captured 38,072-byte mempack tail rejects that read");
	expect(NativePtrScratch_CanFit(45072, CTR_NATIVE_PTR_SCRATCH_CAPACITY),
	       "dedicated native scratch accepts the captured read");
	expect(NativePtrScratch_PhysicalBytes(shippedBigfileMaxPayload) ==
	                                   shippedBigfileMaxPhysical,
	       "largest shipped PTR entry rounds to 49,152 bytes");
	expect(NativePtrScratch_CanFit(shippedBigfileMaxPayload,
	                               CTR_NATIVE_PTR_SCRATCH_CAPACITY),
	       "dedicated native scratch accepts every shipped PTR entry");
	expect(NativePtrScratch_CanFit(CTR_NATIVE_PTR_SCRATCH_CAPACITY,
	                               CTR_NATIVE_PTR_SCRATCH_CAPACITY),
	       "exact-capacity sector-aligned payload fits");
	expect(!NativePtrScratch_CanFit(CTR_NATIVE_PTR_SCRATCH_CAPACITY + 1,
	                                CTR_NATIVE_PTR_SCRATCH_CAPACITY),
	       "one byte beyond capacity is rejected before reading");
	expect(!NativePtrScratch_CanFit(UINT32_MAX, CTR_NATIVE_PTR_SCRATCH_CAPACITY),
	       "rounding overflow is rejected");

	// Model the largest accepted write and prove the adjacent canaries survive.
	memset(guarded, 0x3c, CTR_NATIVE_PTR_SCRATCH_CAPACITY);
	for (int i = CTR_NATIVE_PTR_SCRATCH_CAPACITY; i < (int)sizeof guarded; i++)
		if (guarded[i] != 0xa5)
			failures++;
	expect(failures == 0, "accepted write leaves all 32 guard bytes unchanged");

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
