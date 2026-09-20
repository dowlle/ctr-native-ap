// cc -std=c99 -Wall -Wextra -DCTR_AP -I ap -I . -I include
//    -o /tmp/test-hit-chooser tools/test-hit-chooser.c -lm
//
// Coverage for the ordinary Hit chooser's frame-to-frame state machine
// (ap/ap_hit_chooser.h). AH_WarpPad.c is a large overlay that cannot be linked
// off-engine, so the decisions that must survive across frames were extracted
// into that pure header and are driven here. Each round-2 defect gets a case
// that fails against the pre-fix flow:
//   B. a chosen CTR/Relic route must not reopen the chooser this warp;
//   C. the unseen-Aku hint wait must not reopen it, and cancel restores the
//      pre-chooser mode/pending/menu snapshot;
//   D. a vanished opportunity closes the menu and resumes normal routing.

#include <stdio.h>

#include "../ap/ap_hit_chooser.h"

static int g_checks;
static int g_failures;

static void expect_eq(int got, int want, const char *what)
{
	g_checks++;
	if (got != want)
	{
		printf("FAIL: %s (got %d, want %d)\n", what, got, want);
		g_failures++;
	}
}

// One frame with a fixed opportunity; returns the action.
static int frame(ap_hit_chooser *c, int hasOpp, int token, int relic, int *route)
{
	return AP_HitChooserFrame(c, hasOpp, token, relic, 0x11, 0x22, 0x33, 0x44, 0x55,
	                          0x66, 1, route);
}

static void test_open_wait_apply(void)
{
	ap_hit_chooser c;
	int route = -1;
	AP_HitChooserWarpStart(&c);

	expect_eq(frame(&c, 1, 1, 1, &route), AP_HIT_CHOOSER_OPEN, "first frame opens");
	expect_eq(frame(&c, 1, 1, 1, &route), AP_HIT_CHOOSER_WAIT, "waiting for input");

	AP_HitChooserSetChoice(&c, 1); // CTR
	expect_eq(frame(&c, 1, 1, 1, &route), AP_HIT_CHOOSER_APPLY, "CTR applies");
	expect_eq(route, 1, "CTR route reported");
}

// Defect B: after a pick, the chooser must not reopen (the unseen-Aku hint wait
// in AH_WarpPad.c re-enters the chooser every frame).
static void test_no_reopen_after_apply(void)
{
	ap_hit_chooser c;
	int route = -1;
	AP_HitChooserWarpStart(&c);

	expect_eq(frame(&c, 1, 1, 1, &route), AP_HIT_CHOOSER_OPEN, "open");
	AP_HitChooserSetChoice(&c, 2); // Relic
	expect_eq(frame(&c, 1, 1, 1, &route), AP_HIT_CHOOSER_APPLY, "relic applies");
	expect_eq(route, 2, "relic route reported");

	// The hint wait: the opportunity is still present, but this warp is resolved.
	expect_eq(frame(&c, 1, 1, 1, &route), AP_HIT_CHOOSER_NONE, "does not reopen after apply");
	expect_eq(frame(&c, 1, 1, 1, &route), AP_HIT_CHOOSER_NONE, "stays closed");
}

// Defect C: cancel restores the exact pre-chooser snapshot and re-arms.
static void test_cancel_restores_snapshot(void)
{
	ap_hit_chooser c;
	int route = -1;
	AP_HitChooserWarpStart(&c);

	// Pre-chooser values.
	int gm1 = 0x100000; // ADVENTURE_ARENA, say
	int gm2 = 0x8;
	unsigned a0 = 0x1, r0 = 0x2, a8 = 0x3, r8 = 0x4;
	int menuFlag = 0;

	// Open snapshots the values passed on the opening frame.
	AP_HitChooserFrame(&c, 1, 1, 1, gm1, gm2, a0, r0, a8, r8, menuFlag, &route);
	expect_eq(c.savedValid, 1, "snapshot taken on open");
	expect_eq(c.savedGm1, gm1, "saved gameMode1");
	expect_eq(c.savedGm2, gm2, "saved gameMode2");
	expect_eq((int)c.savedAdd0, (int)a0, "saved AddBitsConfig0");
	expect_eq((int)c.savedRem0, (int)r0, "saved RemBitsConfig0");
	expect_eq((int)c.savedAdd8, (int)a8, "saved AddBitsConfig8");
	expect_eq((int)c.savedRem8, (int)r8, "saved RemBitsConfig8");
	expect_eq(c.savedMenuFlag, menuFlag, "saved menu flag");

	AP_HitChooserSetChoice(&c, -1); // cancel
	expect_eq(frame(&c, 1, 1, 1, &route), AP_HIT_CHOOSER_CANCEL, "cancel returns");
	expect_eq(c.warpResolved, 0, "cancel re-arms a later choice");
	expect_eq(c.menuOpen, 0, "cancel closes the menu");

	// A fresh warp opens again.
	AP_HitChooserWarpStart(&c);
	expect_eq(frame(&c, 1, 1, 1, &route), AP_HIT_CHOOSER_OPEN, "fresh warp reopens");
}

// Defect D: a vanished opportunity closes the menu and resumes normal routing.
static void test_vanish(void)
{
	ap_hit_chooser c;
	int route = -1;
	AP_HitChooserWarpStart(&c);

	expect_eq(frame(&c, 1, 1, 0, &route), AP_HIT_CHOOSER_OPEN, "open with token");
	expect_eq(frame(&c, 0, 0, 0, &route), AP_HIT_CHOOSER_VANISH, "vanished -> hide");
	expect_eq(c.menuOpen, 0, "vanish closes the menu");
	expect_eq(frame(&c, 0, 0, 0, &route), AP_HIT_CHOOSER_NONE, "normal routing resumes");
}

// The Hit-only case routes a plain rerace.
static void test_plain_only(void)
{
	ap_hit_chooser c;
	int route = -1;
	AP_HitChooserWarpStart(&c);
	expect_eq(frame(&c, 1, 0, 0, &route), AP_HIT_CHOOSER_PLAIN, "hit only -> plain");
}

// Without an opportunity the chooser never engages.
static void test_no_opportunity(void)
{
	ap_hit_chooser c;
	int route = -1;
	AP_HitChooserWarpStart(&c);
	expect_eq(frame(&c, 0, 1, 1, &route), AP_HIT_CHOOSER_NONE, "no opportunity -> none");
}

// Correction B: ONE shared restore helper for both cancel paths. Only the Relic
// and Token bits return to their snapshot; unrelated bits changed while the menu
// was open survive.
static void test_restore_bits(void)
{
	const unsigned token = 0x8u;        // stand-in for TOKEN_RACE (gameMode2)
	const unsigned relic = 0x4000000u;  // stand-in for RELIC_RACE (gameMode1)
	const unsigned other1 = 0x100u;     // unrelated gameMode1 bit
	const unsigned other2 = 0x200u;     // unrelated gameMode2 bit

	// Snapshot: Relic set, Token clear.
	int savedGm1 = (int)relic;
	int savedGm2 = 0;
	unsigned savedAdd0 = relic, savedRem0 = 0;
	unsigned savedAdd8 = 0, savedRem8 = token;

	// Current: Relic cleared, Token set, unrelated bits changed while open.
	int gm1 = (int)other1;
	int gm2 = (int)(token | other2);
	unsigned add0 = 0, rem0 = relic;
	unsigned add8 = token, rem8 = 0;

	AP_HitChooserRestoreBitsPure(token, relic, savedGm1, savedGm2,
	                             savedAdd0, savedRem0, savedAdd8, savedRem8,
	                             &gm1, &gm2, &add0, &rem0, &add8, &rem8);

	expect_eq((gm1 & relic) != 0, 1, "relic bit restored to snapshot");
	expect_eq((gm1 & other1) != 0, 1, "unrelated gameMode1 bit survives");
	expect_eq((gm2 & token) == 0, 1, "token bit restored to snapshot (clear)");
	expect_eq((gm2 & other2) != 0, 1, "unrelated gameMode2 bit survives");
	expect_eq((add0 & relic) != 0, 1, "AddBitsConfig0 relic restored");
	expect_eq((rem0 & relic) == 0, 1, "RemBitsConfig0 relic restored (clear)");
	expect_eq((add8 & token) == 0, 1, "AddBitsConfig8 token restored (clear)");
	expect_eq((rem8 & token) != 0, 1, "RemBitsConfig8 token restored");
}

int main(void)
{
	test_open_wait_apply();
	test_no_reopen_after_apply();
	test_cancel_restores_snapshot();
	test_vanish();
	test_plain_only();
	test_no_opportunity();
	test_restore_bits();

	printf("%s: %d checks, %d failures\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
