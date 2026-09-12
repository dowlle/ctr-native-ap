// cc -std=c99 -Wall -Wextra -Werror -o /tmp/test-instance-pool tools/test-instance-pool.c
#include <assert.h>
#include <stdio.h>
#include "../ap/ap_instance_pool_logic.h"

int main(void)
{
	assert(AP_InstancePoolCapacity(128, 0, 180) == 128);
	assert(AP_InstancePoolCapacity(176, 0, 180) == 176);
	assert(AP_InstancePoolCapacity(128, 1, 180) == 308);
	assert(AP_InstancePoolCapacity(128, 1, 0) == 128);
	/* Reproduce the pool accounting: 180 authored births plus eight drivers
	 * and two turbo objects must fit; the former 128-slot budget cannot. */
	int freeSlots = AP_InstancePoolCapacity(128, 1, 180);
	freeSlots -= 180;
	freeSlots -= 8;
	freeSlots -= 2;
	assert(freeSlots == 118);
	puts("PASS: retail budgets retained; Cortex 180 authored + runtime births fit");
	return 0;
}
