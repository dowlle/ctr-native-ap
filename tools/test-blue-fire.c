#include <stdio.h>

#include "../ap/ap_blue_fire_logic.h"

static int failures;

static void expect(const char *name, int got, int want)
{
	if (got != want)
	{
		printf("FAIL %-38s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

int main(void)
{
	expect("unknown menu is quiet",
	       AP_BlueFirePaletteActionFor(AP_BLUE_FIRE_PALETTE_UNKNOWN, 0, 0),
	       AP_BLUE_FIRE_ACTION_NONE);
	expect("race entry establishes red",
	       AP_BlueFirePaletteActionFor(AP_BLUE_FIRE_PALETTE_UNKNOWN, 1, 0),
	       AP_BLUE_FIRE_ACTION_RED);
	expect("blue condition swaps once",
	       AP_BlueFirePaletteActionFor(AP_BLUE_FIRE_PALETTE_RED, 1, 1),
	       AP_BLUE_FIRE_ACTION_BLUE);
	expect("held blue is quiet",
	       AP_BlueFirePaletteActionFor(AP_BLUE_FIRE_PALETTE_BLUE, 1, 1),
	       AP_BLUE_FIRE_ACTION_NONE);
	expect("lost reserves restores red",
	       AP_BlueFirePaletteActionFor(AP_BLUE_FIRE_PALETTE_BLUE, 1, 0),
	       AP_BLUE_FIRE_ACTION_RED);
	expect("loading forgets without write",
	       AP_BlueFirePaletteActionFor(AP_BLUE_FIRE_PALETTE_BLUE, 0, 0),
	       AP_BLUE_FIRE_ACTION_FORGET);
	expect("new race re-establishes blue",
	       AP_BlueFirePaletteActionFor(AP_BLUE_FIRE_PALETTE_UNKNOWN, 1, 1),
	       AP_BLUE_FIRE_ACTION_BLUE);

	printf("%s: 7 checks\n", failures ? "FAIL" : "PASS");
	return failures != 0;
}
