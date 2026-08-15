#ifndef AP_PAUSEROW_H
#define AP_PAUSEROW_H

// ---------------------------------------------------------------------------
// The adventure-hub pause menu's AP row set (#238).
//
// The retail hub pause menu has four rows: RESUME, hints, QUIT, OPTIONS
// (data.rowsAdvHub, game/zGlobal_DATA.c:4377-4384). With the character phase
// live, a SELECT CHARACTER row is inserted directly below RESUME, which shifts
// every row under it down by one.
//
// Two things live here rather than in MainFreeze.c, because both are the kind of
// small index arithmetic that reads as obviously correct and is not:
//
//   1. The up/down wiring. A MenuRow names its neighbours by index, so an
//      off-by-one produces a row that cannot be reached or a cursor that traps,
//      neither of which any build gate would notice.
//   2. The selection shift. rowSelected persists across pause opens, so swapping
//      row sets mid-session (a connect, a disconnect, a slot switch to a seed
//      without the phase) has to carry the highlight across, and the two
//      directions have to be exact inverses or the highlight walks.
//
// Header-only and free of engine types so tools/test-character-persistence.cpp
// exercises the SAME wiring the menu is built from, instead of a copy of it.
// ---------------------------------------------------------------------------

// Row indices in the AP row set.
#define AP_PAUSEROW_RESUME    0
#define AP_PAUSEROW_CHARACTER 1
#define AP_PAUSEROW_HINTS     2
#define AP_PAUSEROW_QUIT      3
#define AP_PAUSEROW_OPTIONS   4
#define AP_PAUSEROW_COUNT     5

// Vertical wiring, {rowOnPressUp, rowOnPressDown} per row, wrapping at both
// ends exactly as the retail four-row table does. Left and right are omitted
// because every row points at itself on both, again as retail does.
static const signed char AP_PAUSEROW_NAV[AP_PAUSEROW_COUNT][2] = {
    /* RESUME    */ {AP_PAUSEROW_OPTIONS, AP_PAUSEROW_CHARACTER},
    /* CHARACTER */ {AP_PAUSEROW_RESUME, AP_PAUSEROW_HINTS},
    /* HINTS     */ {AP_PAUSEROW_CHARACTER, AP_PAUSEROW_QUIT},
    /* QUIT      */ {AP_PAUSEROW_HINTS, AP_PAUSEROW_OPTIONS},
    /* OPTIONS   */ {AP_PAUSEROW_QUIT, AP_PAUSEROW_RESUME},
};

// Carry a highlight from the retail four-row set into the AP five-row set.
// RESUME keeps index 0; everything below it moves down one to make room for
// SELECT CHARACTER.
static inline int AP_PauseRow_ToApIndex(int vanillaRow)
{
	if (vanillaRow <= AP_PAUSEROW_RESUME)
		return AP_PAUSEROW_RESUME;
	if (vanillaRow >= AP_PAUSEROW_COUNT - 1)
		return AP_PAUSEROW_COUNT - 1;
	return vanillaRow + 1;
}

// The inverse. SELECT CHARACTER has no retail counterpart, so it collapses onto
// RESUME rather than onto the hints row: losing the row means the thing the
// player had highlighted no longer exists, and RESUME is the harmless landing.
static inline int AP_PauseRow_ToVanillaIndex(int apRow)
{
	if (apRow <= AP_PAUSEROW_CHARACTER)
		return 0;
	if (apRow >= AP_PAUSEROW_COUNT)
		return AP_PAUSEROW_COUNT - 2;
	return apRow - 1;
}

#endif // AP_PAUSEROW_H
