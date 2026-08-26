#include <stdio.h>

#include "../ap/ap_retro_fueled_logic.h"

static int checks;
static int failures;

static void expect(int condition, const char *name)
{
	checks++;
	if (!condition)
	{
		failures++;
		printf("FAIL: %s\n", name);
	}
}

int main(void)
{
	const unsigned pad = 4;
	const unsigned engine = 16;

	expect(!AP_RetroFueledShouldRewritePad(0, pad, pad, engine),
	       "disabled leaves pads unchanged");
	expect(AP_RetroFueledShouldRewritePad(1, pad, pad, engine),
	       "enabled rewrites a physical pad");
	expect(AP_RetroFueledShouldRewritePad(1, pad | 1, pad, engine),
	       "pad reserve flags do not prevent rewrite");
	expect(!AP_RetroFueledShouldRewritePad(1, pad | engine, pad, engine),
	       "wumpa super engine is not a physical pad");
	expect(!AP_RetroFueledShouldRewritePad(1, 2, pad, engine),
	       "powerslide is not rewritten as a pad");

	expect(AP_RetroFueledShouldKeepFireCap(1, 0x4800, 0x1000),
	       "blue fire survives an ordinary stacked boost");
	expect(!AP_RetroFueledShouldKeepFireCap(0, 0x4800, 0x1000),
	       "tier two keeps retail cap demotion");
	expect(!AP_RetroFueledShouldKeepFireCap(1, 0x1000, 0x1000),
	       "sacred fire alone is not blue fire");

	expect(AP_RetroFueledShouldKeepReserves(1, 1, 0),
	       "down and brake retains reserves");
	expect(AP_RetroFueledShouldKeepReserves(1, 0, 1),
	       "landing boost window retains reserves");
	expect(!AP_RetroFueledShouldKeepReserves(1, 0, 0),
	       "ordinary braking still cancels reserves");
	expect(!AP_RetroFueledShouldKeepReserves(0, 1, 1),
	       "lower tiers retain retail cancellation");

	expect(AP_RetroFueledShouldSuppressForcedCross(1, 1, 1, 0, 0),
	       "u-turn input frees the brake from the forced accelerator");
	expect(AP_RetroFueledShouldSuppressForcedCross(1, 1, 0, 1, 0),
	       "landing boost window frees the brake too");
	expect(!AP_RetroFueledShouldSuppressForcedCross(1, 1, 1, 0, 1),
	       "holding cross keeps the forced accelerator");
	expect(!AP_RetroFueledShouldSuppressForcedCross(1, 0, 1, 0, 0),
	       "no brake means the retail forced accelerator stands");
	expect(!AP_RetroFueledShouldSuppressForcedCross(1, 1, 0, 0, 0),
	       "plain braking keeps the retail forced accelerator");
	expect(!AP_RetroFueledShouldSuppressForcedCross(0, 1, 1, 0, 0),
	       "lower tiers keep the retail forced accelerator");

	expect(AP_RETRO_FUELED_PAD_RESERVES == 960,
	       "pad payload is exactly one second");
	expect(AP_RETRO_FUELED_FIRE_LEVEL == 0x800,
	       "pad payload uses the retail blue-fire cap");

	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
