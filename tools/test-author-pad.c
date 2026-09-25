// cc -m32 -Wall -Wextra -DCTR_NATIVE -DBUILD=926 -I include -I . tools/test-author-pad.c -o /tmp/test-author-pad
//
// Box Author Mode's controller keys (ap/ap_author_pad.h), frame by frame:
// Select alone drops on release, hold Select + L1 removes the newest box, hold
// Select + R1 saves and lists. Select never reaches the game while author input
// is live, and L1/R1 pressed during the chord never make the kart hop, not
// even while still held after Select comes up. Outside the live context
// (paused, boss race, custom track not loaded, author mode off) nothing fires
// and nothing is stripped.
#include <common.h>
#include <stdio.h>

#include "../ap/ap_author_pad.h"

struct sData sdata_static; // common.h binds sdata to it

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static const AP_AuthorPadButtons k_buttons = {BTN_SELECT, BTN_L1, BTN_R1};
static AP_AuthorPadState s;
static unsigned int strip;

static int frame(int live, unsigned int held)
{
	return AP_AuthorPad_Step(&s, &k_buttons, live, held, &strip);
}

int main(void)
{
	// The engine's button bits the mapping relies on.
	CHECK(BTN_SELECT == 0x2000 && BTN_L1 == 0x800 && BTN_R1 == 0x400);

	// Idle: nothing fires, Select is still taken while live (the game has no use
	// for it in a race and must not see it).
	AP_AuthorPad_Reset(&s);
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT);
	CHECK(frame(1, BTN_CROSS_one) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT);

	// Select alone: press, hold, release. The drop is on release.
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT);
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_DROP);
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE); // once

	// Select while accelerating: Cross is untouched, the drop still happens.
	CHECK(frame(1, BTN_CROSS_one | BTN_SELECT) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT);
	CHECK(frame(1, BTN_CROSS_one) == AP_AUTHOR_PAD_DROP && (strip & BTN_CROSS_one) == 0);

	// Hold Select, press L1: undo at once, L1 never reaches the game, no drop.
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_SELECT | BTN_L1) == AP_AUTHOR_PAD_UNDO && strip == (BTN_SELECT | BTN_L1));
	CHECK(frame(1, BTN_SELECT | BTN_L1) == AP_AUTHOR_PAD_NONE && strip == (BTN_SELECT | BTN_L1)); // held: once
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT);
	CHECK(frame(1, BTN_SELECT | BTN_L1) == AP_AUTHOR_PAD_UNDO); // pressed again: again
	CHECK(frame(1, BTN_L1) == AP_AUTHOR_PAD_NONE);               // Select up, L1 still held
	CHECK(strip == (BTN_SELECT | BTN_L1));                       // still no hop
	CHECK(frame(1, BTN_L1) == AP_AUTHOR_PAD_NONE && strip == (BTN_SELECT | BTN_L1));
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT); // released: back to the game
	CHECK(frame(1, BTN_L1) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT); // a normal hop again

	// Hold Select, press R1: save and list, no drop, no hop.
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_SELECT | BTN_R1) == AP_AUTHOR_PAD_LIST && strip == (BTN_SELECT | BTN_R1));
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT);

	// Both shoulders in the same frame: ambiguous, neither, and no drop.
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_SELECT | BTN_L1 | BTN_R1) == AP_AUTHOR_PAD_NONE);
	CHECK(strip == (BTN_SELECT | BTN_L1 | BTN_R1));
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE);

	// A shoulder already held (a powerslide) when Select goes down is taken for
	// the chord, and pressing the other shoulder is the chord.
	CHECK(frame(1, BTN_R1) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT);
	CHECK(frame(1, BTN_R1 | BTN_SELECT) == AP_AUTHOR_PAD_NONE && strip == (BTN_SELECT | BTN_R1));
	CHECK(frame(1, BTN_R1 | BTN_SELECT | BTN_L1) == AP_AUTHOR_PAD_UNDO);
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT);

	// Not live (paused, boss race, custom track not loaded, author mode off):
	// nothing fires and nothing is stripped, so the game sees its own buttons.
	CHECK(frame(0, BTN_SELECT) == AP_AUTHOR_PAD_NONE && strip == 0);
	CHECK(frame(0, 0) == AP_AUTHOR_PAD_NONE && strip == 0);
	CHECK(frame(0, BTN_SELECT | BTN_L1) == AP_AUTHOR_PAD_NONE && strip == 0);

	// A gesture cut by the context closing never fires later.
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(0, BTN_SELECT) == AP_AUTHOR_PAD_NONE && strip == 0); // e.g. the pause menu opens
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE);               // still held when it closes
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE);                        // no stale drop
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_DROP);                        // a fresh press works

	// Only the three buttons are ever stripped.
	CHECK((strip & ~(unsigned int)(BTN_SELECT | BTN_L1 | BTN_R1)) == 0);

	printf("author pad: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
