#ifndef AP_CHARSTATROW_H
#define AP_CHARSTATROW_H

// ---------------------------------------------------------------------------
// The picker's stat ROWS, which are no longer its stat SLOTS (#54/#209, #220).
//
// The picker now shows the same three bars as the Garage -- SPEED, ACCEL, TURN,
// in that order -- and ACCSPD is not shown or edited there any more. The edit
// PACKAGE still carries four stats per racer, and must: it is the wire shape of
// the per-slot data-storage value (AP_NET_EDITSTAT_COUNT is asserted against
// AP_CS_STATS * (1 + AP_CS_TILES) in ap_charswap.c), a server may already hold a
// package written by an older client, and the apworld sends the same four. So
// dropping a row from the UI must not drop a value from the wire, and the two
// counts diverge from here on.
//
// THAT DIVERGENCE IS THE HAZARD THIS HEADER EXISTS FOR. Every edit the player
// makes indexes an array by row: the global slot, the per-character slot, the
// step size, the clamp, and the vanilla base the offset is applied to. Feed a
// row index straight into any of those now and it writes the WRONG STAT -- row 2
// is TURN on screen and ACCSPD in the package -- and the damage is persisted
// immediately, because an edit writes through on every keypress rather than on
// close. It would look like nothing at all until a player tuned a racer and
// found a different number had moved, on a value that had already been saved.
//
// So the mapping is one table and one function, driven by the harness rather
// than read for correctness. The ORDER is the Garage's own draw order
// (game/233/CS_Garage.c draws SPEED, ACCEL, TURN top to bottom), not the
// package's, which is why this is a translation and not a coincidence.
// ---------------------------------------------------------------------------

// Rows on screen. Not the number of stats in the package.
#define AP_CS_UI_ROWS 3

// Package slot for each row, in ap_cs_stats[] order:
//   0 ACCEL, 1 SPEED, 2 ACCSPD, 3 TURN
// Screen order is SPEED, ACCEL, TURN. ACCSPD (2) is deliberately absent.
static const int AP_CS_UI_ROW_STAT[AP_CS_UI_ROWS] = {1, 0, 3};

// The package slot a screen row edits. Out-of-range rows clamp to the first row
// rather than reading past the table: a junk row must not become a junk write
// into the persisted package.
static inline int AP_CharStatRow_ToStat(int row)
{
	if (row < 0)
		row = 0;
	if (row >= AP_CS_UI_ROWS)
		row = AP_CS_UI_ROWS - 1;
	return AP_CS_UI_ROW_STAT[row];
}

// Is this package slot shown on screen at all? ACCSPD is not, and the editor
// must not be able to reach it through row navigation.
static inline int AP_CharStatRow_IsShown(int stat)
{
	int r;
	for (r = 0; r < AP_CS_UI_ROWS; r++)
	{
		if (AP_CS_UI_ROW_STAT[r] == stat)
			return 1;
	}
	return 0;
}

// Row navigation, wrapping, in screen order. Separate from the table so the
// harness can prove the two directions are exact inverses -- the same property
// ap_pauserow.h pins for the pause menu, and for the same reason: an off-by-one
// makes a row unreachable or traps the cursor on it.
static inline int AP_CharStatRow_Next(int row, int dir)
{
	if (row < 0)
		row = 0;
	if (row >= AP_CS_UI_ROWS)
		row = AP_CS_UI_ROWS - 1;

	if (dir < 0)
		return (row + AP_CS_UI_ROWS - 1) % AP_CS_UI_ROWS;
	return (row + 1) % AP_CS_UI_ROWS;
}

#endif // AP_CHARSTATROW_H
