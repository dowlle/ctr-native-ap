#include <assert.h>
#include <stdio.h>

#include "ap/ap_useful_logic.h"

int main(void)
{
	unsigned received = 0;
	received = AP_UsefulReceiveBit(received, 0);
	received = AP_UsefulReceiveBit(received, 2);
	assert(received == 5u);
	assert(AP_UsefulReceiveBit(received, 2) == 5u);
	assert(AP_UsefulReceiveBit(received, -1) == 5u);
	assert(AP_UsefulPendingMask(received, 0) == 5u);
	assert(AP_UsefulPendingMask(received, 1) == 4u);
	assert(AP_UsefulPendingMask(received, 7) == 0u);
	assert(AP_UsefulPendingMask(0xffu, 0) == 7u);
	puts("H-dossier useful grants: PASS");
	return 0;
}
