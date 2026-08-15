#include <assert.h>
#include <stdio.h>

#include "ap/ap_wumpa_logic.h"

int main(void)
{
	int count = 0;
	int i;
	for (i = 0; i < 14; i++)
		count = AP_WumpaStartingIncrement(count);
	assert(count == 10);
	assert(AP_WumpaStartingIncrement(-4) == 1);
	assert(AP_WumpaStartingValue(-3, 0, 0) == 0);
	assert(AP_WumpaStartingValue(6, 0, 0) == 6);
	assert(AP_WumpaStartingValue(99, 0, 0) == 10);
	assert(AP_WumpaStartingValue(6, 1, 99) == 99);
	puts("H-dossier Wumpa accounting: PASS");
	return 0;
}
