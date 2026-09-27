// Host harness for the AP reserves meter display math (#387).
//
//   cc -std=c99 -Wall -Wextra -o /tmp/test-reserves-meter tools/test-reserves-meter.c && /tmp/test-reserves-meter

#include <stdio.h>

#include "../ap/ap_reserves_meter_logic.h"

static int failures;
static int checks;

static void expect(const char *name, int got, int want)
{
	checks++;
	if (got != want)
	{
		printf("FAIL %-52s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

int main(void)
{
	int raw;
	int bad = 0;

	// Retail-range values keep the ReservesMeter module's tiers and lengths.
	expect("empty is red", AP_ReservesMeterTier(0), AP_RESERVES_TIER_RED);
	expect("empty draws nothing", AP_ReservesMeterLength(0), 0);
	expect("1600 is still red", AP_ReservesMeterTier(1600), AP_RESERVES_TIER_RED);
	expect("1601 is yellow", AP_ReservesMeterTier(1601), AP_RESERVES_TIER_YELLOW);
	expect("3839 is yellow", AP_ReservesMeterTier(3839), AP_RESERVES_TIER_YELLOW);
	expect("3840 is green", AP_ReservesMeterTier(3840), AP_RESERVES_TIER_GREEN);
	expect("960 (one engine second) is 5 px", AP_ReservesMeterLength(960), 5);
	expect("8400 fills exactly", AP_ReservesMeterLength(8400), 49);
	expect("8400 is still green", AP_ReservesMeterTier(8400), AP_RESERVES_TIER_GREEN);
	expect("8571 is the last green value", AP_ReservesMeterTier(8571), AP_RESERVES_TIER_GREEN);
	expect("8572 pegs blue", AP_ReservesMeterTier(8572), AP_RESERVES_TIER_BLUE);
	expect("8572 is full width", AP_ReservesMeterLength(8572), 49);
	expect("0x7fff pegs blue", AP_ReservesMeterTier(0x7fff), AP_RESERVES_TIER_BLUE);
	expect("0x7fff is full width", AP_ReservesMeterLength(0x7fff), 49);

	// The reported case: past 0x7fff the signed field reads negative.
	expect("0x8000 (wrapped negative) pegs blue",
	       AP_ReservesMeterTier((short)0x8000), AP_RESERVES_TIER_BLUE);
	expect("0x8000 is full width, not negative",
	       AP_ReservesMeterLength((short)0x8000), 49);
	expect("-1 (0xffff) pegs blue", AP_ReservesMeterTier((short)-1), AP_RESERVES_TIER_BLUE);
	expect("-1 (0xffff) is full width", AP_ReservesMeterLength((short)-1), 49);

	// No stored value can draw outside the bar or turn a large value red.
	for (raw = -32768; raw <= 32767; raw++)
	{
		int len = AP_ReservesMeterLength((short)raw);
		if (len < 0 || len > AP_RESERVES_METER_WIDTH)
			bad++;
		if (raw < 0 && AP_ReservesMeterTier((short)raw) != AP_RESERVES_TIER_BLUE)
			bad++;
	}
	expect("every stored value stays inside the bar", bad, 0);

	printf("%s: %d checks\n", failures ? "FAIL" : "PASS", checks);
	return failures != 0;
}
