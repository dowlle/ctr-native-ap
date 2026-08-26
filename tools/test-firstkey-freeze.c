#include <stdio.h>
#include "../ap/ap_firstkey_freeze.h"

static int checks;
static int failures;

static void expect(int got, int want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		printf("FAIL %s (got %d, want %d)\n", name, got, want);
	}
}

int main(void)
{
	expect(AP_FirstKeyFreezeShouldArm(1, 0), 0,
	       "unbacked local boss Key never arms the AP door freeze");
	expect(AP_FirstKeyFreezeShouldArm(1, 1), 1,
	       "one genuinely received Key preserves the first-Key presentation");
	expect(AP_FirstKeyFreezeShouldArm(0, 0), 0,
	       "zero profile Keys does not arm");
	expect(AP_FirstKeyFreezeShouldArm(2, 1), 0,
	       "later-Key profile shape does not arm");
	expect(AP_FirstKeyFreezeShouldArm(1, 2), 1,
	       "received inventory is authoritative even during cosmetic lag");

	printf("%s first-Key freeze gate (%d checks, %d failures)\n",
	       failures ? "FAIL" : "PASS", checks, failures);
	return failures != 0;
}
