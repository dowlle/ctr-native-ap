#include <stdio.h>
#include "../ap/ap_oxide_weapon_schedule.h"

static int checks;
static int failures;

static void expect(int got, int want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		printf("FAIL %s: got %d want %d\n", name, got, want);
	}
}

int main(void)
{
	int last;
	int retail;

	expect(AP_OxideWeaponCheckpoint(0, 63), 0, "lap start stays start");
	expect(AP_OxideWeaponCheckpoint(0x7f, 63), 63, "lap end stays end");
	expect(AP_OxideWeaponCheckpoint(0x40, 0x7f), 0x40,
	       "Oxide Station graph is identity");
	expect(AP_OxideWeaponCheckpoint(0x10, -1), -1,
	       "missing checkpoint graph is refused");

	for (last = 0; last < 256; last++)
	{
		int previous = -1;
		for (retail = 0; retail <= 0x7f; retail++)
		{
			int mapped = AP_OxideWeaponCheckpoint(retail, last);
			checks += 2;
			if (mapped < 0 || mapped > last)
				failures++;
			if (mapped < previous)
				failures++;
			previous = mapped;
		}
	}

	printf("test-oxide-weapon-schedule: %d checks, %d failures\n",
	       checks, failures);
	return failures ? 1 : 0;
}
