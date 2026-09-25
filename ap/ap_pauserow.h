#ifndef AP_PAUSEROW_H
#define AP_PAUSEROW_H

// ---------------------------------------------------------------------------
// The adventure-hub pause menu's AP row sets (#238, tracker row 2026-09-25).
//
// The retail hub pause menu has four rows: RESUME, hints, QUIT, OPTIONS
// (data.rowsAdvHub, game/zGlobal_DATA.c:4377-4384). An AP seed adds up to two
// rows directly below RESUME, in this order:
//   SELECT CHARACTER  when the character phase is live
//   TRACKER           whenever the enlarged tracker is available
// so the menu has four, five or six rows.
//
// Two things live here rather than in MainFreeze.c, because both are the kind of
// small index arithmetic that reads as obviously correct and is not:
//
//   1. The up/down wiring. A MenuRow names its neighbours by index, so an
//      off-by-one produces a row that cannot be reached or a cursor that traps,
//      neither of which any build gate would notice.
//   2. Carrying the highlight. rowSelected persists across pause opens, so
//      swapping row sets mid-session (a connect, a disconnect, a slot switch to
//      a seed without the phase) has to keep the player on the same row, and a
//      row that disappears must land on RESUME, never on whatever row slid into
//      its index or on the terminator.
//
// Header-only and free of engine types so tools/test-pause-rows.c and
// tools/test-character-persistence.cpp exercise the SAME rows the menu is built
// from, instead of a copy of them.
// ---------------------------------------------------------------------------

// Row kinds, independent of where a kind sits in a given row set.
#define AP_PAUSEROW_RESUME    0
#define AP_PAUSEROW_CHARACTER 1
#define AP_PAUSEROW_TRACKER   2
#define AP_PAUSEROW_HINTS     3
#define AP_PAUSEROW_QUIT      4
#define AP_PAUSEROW_OPTIONS   5
#define AP_PAUSEROW_KINDS     6

// The TRACKER row has no retail string. MenuRow.stringIndex indexes
// sdata->lngStrings (masked with 0x7fff), so the row carries this AP-owned
// index instead, and RECTMENU.c resolves it to "TRACKER" in the two places it
// reads a row label. No retail string index comes near it.
#define AP_LNG_TRACKER 0x7f00

// Rows in order for the given AP rows. Returns the row count (4, 5 or 6).
// (0, 0) is the retail set.
static inline int AP_PauseRow_Build(int character, int tracker, signed char kinds[AP_PAUSEROW_KINDS])
{
	int n = 0;
	kinds[n++] = AP_PAUSEROW_RESUME;
	if (character)
		kinds[n++] = AP_PAUSEROW_CHARACTER;
	if (tracker)
		kinds[n++] = AP_PAUSEROW_TRACKER;
	kinds[n++] = AP_PAUSEROW_HINTS;
	kinds[n++] = AP_PAUSEROW_QUIT;
	kinds[n++] = AP_PAUSEROW_OPTIONS;
	return n;
}

// Vertical wiring for row `row` of a `count`-row set, wrapping at both ends
// exactly as the retail four-row table does. Left and right are omitted because
// every row points at itself on both, again as retail does.
static inline int AP_PauseRow_Up(int row, int count)
{
	return count > 0 ? (row + count - 1) % count : 0;
}
static inline int AP_PauseRow_Down(int row, int count)
{
	return count > 0 ? (row + 1) % count : 0;
}

// Carry a highlight from one row set to another by row kind. A kind the new set
// does not have, or an out-of-range row (rowSelected is an s16 a savestate can
// restore), lands on RESUME, which is row 0 in every set.
static inline int AP_PauseRow_Carry(const signed char *from, int fromCount, int row,
                                    const signed char *to, int toCount)
{
	int i;
	if (row < 0 || row >= fromCount)
		return 0;
	for (i = 0; i < toCount; i++)
		if (to[i] == from[row])
			return i;
	return 0;
}

#endif // AP_PAUSEROW_H
