#include <stdio.h>

#include "../ap/ap_ceremony_logic.h"

static int failures;

#define CHECK(name, condition) do { \
	if (!(condition)) { \
		printf("FAIL %s\n", name); \
		failures++; \
	} \
} while (0)

static void test_every_entry_appears(void)
{
	const int windows[] = {120, 240};
	for (int w = 0; w < 2; w++)
	{
		for (int count = 1; count <= 8; count++)
		{
			int seen[8] = {0};
			for (int frame = 0; frame < windows[w]; frame++)
			{
				int index = AP_CeremonyCycleIndex(count, frame, windows[w]);
				CHECK("cycle index stays in range", index >= 0 && index < count);
				seen[index] = 1;
			}
			for (int i = 0; i < count; i++)
				CHECK("every entry appears before fly-out", seen[i]);
		}
	}
}

static void test_dwell_caps_at_shipped_value(void)
{
	CHECK("long windows keep entry zero for 90 frames",
	      AP_CeremonyCycleIndex(2, 89, 1000) == 0);
	CHECK("long windows advance at 90 frames",
	      AP_CeremonyCycleIndex(2, 90, 1000) == 1);
}

static void test_flyout_clears_logical_screen(void)
{
	const int screen = 0x200;
	const int wrap = 0x1b0;
	const int center = AP_CeremonyOffscreenCenterX(screen, wrap);
	const int leftEdge = center - (wrap + 1) / 2;
	CHECK("wide centered line is fully beyond right edge", leftEdge > screen);
	CHECK("old vanilla target leaves AP text visible",
	      0x296 - (wrap + 1) / 2 <= screen);
}

int main(void)
{
	test_every_entry_appears();
	test_dwell_caps_at_shipped_value();
	test_flyout_clears_logical_screen();
	if (failures)
		return 1;
	puts("ceremony banner: timing coverage and fly-out geometry passed");
	return 0;
}
