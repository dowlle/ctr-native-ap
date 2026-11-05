// cc -m32 -Wall -Wextra -DCTR_NATIVE -DBUILD=926 -I include -I . tools/test-author-pad.c -o /tmp/test-author-pad
//
// Box Author Mode's controller keys (ap/ap_author_pad.h), frame by frame:
// Select alone drops on release, hold Select + L1 deletes, hold Select + R1
// saves and lists, Select + Square / Circle pick a box, Select + Cross starts
// move mode, Select + Triangle exports. Select never reaches the game while
// author input is live, and a chord button never reaches it either, not even
// while still held after Select comes up. Move mode takes the D-pad and the
// shoulders until Select ends it. Outside the live context (paused, boss race,
// custom track not loaded, author mode off) nothing fires and nothing is
// stripped. Then the selection and move rules both track kinds share.
#include <common.h>
#include <stdio.h>

#include "../ap/ap_author_pad.h"

struct sData sdata_static; // common.h binds sdata to it

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static const AP_AuthorPadButtons k_buttons = {BTN_SELECT, BTN_L1, BTN_R1, BTN_SQUARE, BTN_CIRCLE, BTN_CROSS,
                                              BTN_TRIANGLE, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT};
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

	// ── the face-button chords ──────────────────────────────────────────────
	AP_AuthorPad_Reset(&s);
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_SELECT | BTN_SQUARE_one) == AP_AUTHOR_PAD_PREV && (strip & BTN_SQUARE_one));
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_SELECT | BTN_CIRCLE) == AP_AUTHOR_PAD_NEXT && (strip & BTN_CIRCLE)); // no item fired
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_SELECT | BTN_TRIANGLE) == AP_AUTHOR_PAD_EXPORT && (strip & BTN_TRIANGLE));
	CHECK(frame(1, BTN_TRIANGLE) == AP_AUTHOR_PAD_NONE && (strip & BTN_TRIANGLE)); // Select up: no drop, still taken
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT);
	// Cross spans two engine bits; either one alone is the chord.
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_SELECT | BTN_CROSS_two) == AP_AUTHOR_PAD_MOVE);
	CHECK(frame(1, BTN_SELECT | BTN_CROSS_two) == AP_AUTHOR_PAD_NONE); // held: once
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE);                          // the caller said no: no move mode
	CHECK(frame(1, BTN_DOWN) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT); // the D-pad is the game's
	// Two chord buttons at once: neither, and no drop.
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_SELECT | BTN_SQUARE_one | BTN_CIRCLE) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE);
	// A face button held from before Select (accelerating) is not taken.
	CHECK(frame(1, BTN_CROSS_one) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_CROSS_one | BTN_SELECT) == AP_AUTHOR_PAD_NONE && (strip & BTN_CROSS_one) == 0);
	CHECK(frame(1, BTN_CROSS_one) == AP_AUTHOR_PAD_DROP && (strip & BTN_CROSS_one) == 0);

	// ── move mode ───────────────────────────────────────────────────────────
	AP_AuthorPad_Reset(&s);
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_SELECT) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_SELECT | BTN_CROSS_one) == AP_AUTHOR_PAD_MOVE);
	AP_AuthorPad_SetMoving(&s, 1); // the caller found a box
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE);
	CHECK(strip == (BTN_SELECT | BTN_L1 | BTN_R1 | BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT));
	CHECK(frame(1, BTN_LEFT) == AP_AUTHOR_PAD_NUDGE_XNEG);
	CHECK(frame(1, BTN_LEFT) == AP_AUTHOR_PAD_NONE); // one step per press
	CHECK(frame(1, BTN_RIGHT) == AP_AUTHOR_PAD_NUDGE_XPOS);
	CHECK(frame(1, BTN_UP) == AP_AUTHOR_PAD_NUDGE_ZPOS);
	CHECK(frame(1, BTN_DOWN) == AP_AUTHOR_PAD_NUDGE_ZNEG);
	CHECK(frame(1, BTN_L1) == AP_AUTHOR_PAD_LOWER && (strip & BTN_L1)); // no hop
	CHECK(frame(1, BTN_R1) == AP_AUTHOR_PAD_RAISE && (strip & BTN_R1));
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_L1 | BTN_R1) == AP_AUTHOR_PAD_NONE);          // both at once: ambiguous
	CHECK(frame(1, BTN_L1 | BTN_R1) == AP_AUTHOR_PAD_NONE);          // held: no new edge
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(1, BTN_CROSS_one) == AP_AUTHOR_PAD_NONE && (strip & BTN_CROSS_one) == 0); // throttle stays
	// Select ends it, its release drops nothing, and a direction still held
	// stays away from the game until it is released.
	CHECK(frame(1, BTN_SELECT | BTN_LEFT) == AP_AUTHOR_PAD_MOVE_DONE && !s.moving);
	CHECK(strip & BTN_LEFT);
	CHECK(frame(1, BTN_LEFT) == AP_AUTHOR_PAD_NONE && (strip & BTN_LEFT));
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT);
	CHECK(frame(1, BTN_LEFT) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT); // steering again
	// Pausing ends move mode and gives everything back.
	AP_AuthorPad_SetMoving(&s, 1);
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE);
	CHECK(frame(0, BTN_UP) == AP_AUTHOR_PAD_NONE && strip == 0 && !s.moving);
	CHECK(frame(1, 0) == AP_AUTHOR_PAD_NONE && strip == BTN_SELECT);

	// ── the selection rule ──────────────────────────────────────────────────
	{
		const int idx[3] = {4, 9, 12}; // this track's rows in the table
		CHECK(AP_AuthorSel_Step(idx, 0, -1, 1) == -1);
		CHECK(AP_AuthorSel_Step(idx, 3, -1, 1) == 12);  // nothing selected: the newest
		CHECK(AP_AuthorSel_Step(idx, 3, -1, -1) == 12);
		CHECK(AP_AuthorSel_Step(idx, 3, 7, 1) == 12);   // a row of another track: the newest
		CHECK(AP_AuthorSel_Step(idx, 3, 12, 1) == 4);   // wraps
		CHECK(AP_AuthorSel_Step(idx, 3, 4, -1) == 12);
		CHECK(AP_AuthorSel_Step(idx, 3, 9, -1) == 4);
		CHECK(AP_AuthorSel_Step(idx, 3, 9, 1) == 12);
		CHECK(AP_AuthorSel_Ordinal(idx, 3, 9) == 2 && AP_AuthorSel_Ordinal(idx, 3, 5) == 0);
	}

	// ── the move rule ───────────────────────────────────────────────────────
	{
		short x = 0, y = 0, z = 0;
		int dx, dy, dz;
		CHECK(AP_AuthorNudge_Delta(AP_AUTHOR_PAD_NUDGE_XPOS, &dx, &dy, &dz) && dx == 16 && dy == 0 && dz == 0);
		CHECK(AP_AuthorNudge_Delta(AP_AUTHOR_PAD_NUDGE_ZNEG, &dx, &dy, &dz) && dz == -16 && dx == 0);
		CHECK(AP_AuthorNudge_Delta(AP_AUTHOR_PAD_RAISE, &dx, &dy, &dz) && dy == 8);   // +Y is up
		CHECK(AP_AuthorNudge_Delta(AP_AUTHOR_PAD_LOWER, &dx, &dy, &dz) && dy == -8);
		CHECK(!AP_AuthorNudge_Delta(AP_AUTHOR_PAD_DROP, &dx, &dy, &dz) && dx == 0 && dy == 0 && dz == 0);
		CHECK(AP_AuthorNudge_Apply(&x, &y, &z, 16, -8, 0) && x == 16 && y == -8 && z == 0);
		x = 32760;
		CHECK(AP_AuthorNudge_Apply(&x, &y, &z, 16, 0, 0) && x == 32767); // clamped to the LEV range
		CHECK(!AP_AuthorNudge_Apply(&x, &y, &z, 16, 0, 0));             // at the edge: no change
		z = -32768;
		CHECK(!AP_AuthorNudge_Apply(&x, &y, &z, 0, 0, -16) && z == -32768);
	}

	printf("author pad: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
