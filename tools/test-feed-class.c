// Out-of-engine assertions for the AP item-feed classification precedence
// (issue #195). Compiles the REAL freestanding logic -- ap/ap_item_flags.h is
// self-contained by design, so this harness links nothing from the game.
//
//   cc -Wall -Wextra -o /tmp/test-feed-class tools/test-feed-class.c && /tmp/test-feed-class
//
// Exit 0 = every assertion held; the failing case is printed otherwise.
//
// Source-level matrix this pins (flags -> class -> hub-feed font colour):
//   flags   class        font colour (DecalFontStyle)
//   0       FILLER       POLAR_CYAN   (cyan)
//   1       PROGRESSION  N_GIN_PURPLE (purple)
//   2       USEFUL       CRASH_BLUE   (blue)
//   4       TRAP         CORTEX_RED   (red)
//   3       PROGRESSION  (progression wins over useful)
//   5       PROGRESSION  (progression wins over trap)
//   6       USEFUL       (useful beats trap)
//   7       PROGRESSION  (progression wins over the combination)
//   0x10000 FILLER       (unknown bits never change the class)

#include <stdio.h>

#include "../ap/ap_item_flags.h"

static int g_failures = 0;

static void expect_class(unsigned flags, AP_ItemClass want, const char *why)
{
	AP_ItemClass got = AP_ItemFlagsClass(flags);
	printf("%-4s AP_ItemFlagsClass(0x%x) = %d  [%s]\n",
	       got == want ? "ok" : "FAIL", flags, (int)got, why);
	if (got != want)
		g_failures++;
}

int main(void)
{
	// The four single-flag classes.
	expect_class(0, AP_ITEM_CLASS_FILLER, "no flags = filler default");
	expect_class(AP_ITEM_FLAG_PROGRESSION, AP_ITEM_CLASS_PROGRESSION, "progression");
	expect_class(AP_ITEM_FLAG_USEFUL, AP_ITEM_CLASS_USEFUL, "useful");
	expect_class(AP_ITEM_FLAG_TRAP, AP_ITEM_CLASS_TRAP, "trap");

	// Combined flags: progression wins, then useful, then trap.
	expect_class(AP_ITEM_FLAG_PROGRESSION | AP_ITEM_FLAG_USEFUL,
	             AP_ITEM_CLASS_PROGRESSION, "progression beats useful");
	expect_class(AP_ITEM_FLAG_PROGRESSION | AP_ITEM_FLAG_TRAP,
	             AP_ITEM_CLASS_PROGRESSION, "progression beats trap");
	expect_class(AP_ITEM_FLAG_USEFUL | AP_ITEM_FLAG_TRAP,
	             AP_ITEM_CLASS_USEFUL, "useful beats trap");
	expect_class(AP_ITEM_FLAG_PROGRESSION | AP_ITEM_FLAG_USEFUL | AP_ITEM_FLAG_TRAP,
	             AP_ITEM_CLASS_PROGRESSION, "progression beats the combination");

	// Unknown / missing flags fall back safely to the filler default.
	expect_class(0x10000u, AP_ITEM_CLASS_FILLER, "unknown bit alone = filler");
	expect_class(AP_ITEM_FLAG_PROGRESSION | 0x8000u,
	             AP_ITEM_CLASS_PROGRESSION, "unknown bit does not change a known class");
	expect_class(0xFFFF0000u, AP_ITEM_CLASS_FILLER, "high garbage bits = filler");

	printf("\n%s\n", g_failures ? "FAILURES PRESENT" : "all assertions passed");
	return g_failures != 0;
}
