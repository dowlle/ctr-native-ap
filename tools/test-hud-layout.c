// Host harness for the AP speed readout placement (VF2 item 2).
//
//   cc -std=c99 -Wall -Wextra -o /tmp/test-hud-layout tools/test-hud-layout.c && /tmp/test-hud-layout

#include <stdio.h>

#include "../ap/ap_hud_layout.h"

static int failures;

static void expect(const char *name, int ok)
{
	printf("%s %s\n", ok ? "PASS" : "FAIL", name);
	if (!ok)
		failures++;
}

int main(void)
{
	// hud_1P_P1[8] in game/zGlobal_DATA.c: the slide (boost) meter anchor.
	const int boostX = 490, boostY = 206;
	const int top = AP_HudSpeedNumY(boostY);
	const int bottom = top + AP_HUD_FONT_BIG_HEIGHT;
	const int right = AP_HudSpeedNumRightX(boostX);

	// The old placement, for the record: 215 + 17 = 232 on a 216 row area.
	expect("old placement ran off the draw area", (boostY + 9) + AP_HUD_FONT_BIG_HEIGHT > AP_HUD_DRAW_HEIGHT);
	expect("readout starts inside the draw area", top >= 0);
	expect("readout ends inside the draw area", bottom <= AP_HUD_DRAW_HEIGHT);
	expect("readout leaves two rows below it", bottom <= AP_HUD_DRAW_HEIGHT - 2);
	expect("readout stays left of the bars", right < boostX - AP_HUD_BAR_WIDTH);
	expect("readout right edge is 438", right == 438);
	printf("%d failure(s)\n", failures);
	return failures ? 1 : 0;
}
