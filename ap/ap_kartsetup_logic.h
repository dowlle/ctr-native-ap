#ifndef AP_KARTSETUP_LOGIC_H
#define AP_KARTSETUP_LOGIC_H

// ---------------------------------------------------------------------------
// In-race Kart Setup: the Adventure race pause menu rows (ruling of 2026-11-18).
//
// Ruling: "The kart setup options should essentially follow the character
// selector screen and have the same rules as there." The page itself reuses
// the hub picker's panel, rows, steps and persistence (ap_charswap.c). This
// header owns only the small index arithmetic of adding one KART SETUP row to
// the retail race pause menus, for the same reason ap_pauserow.h exists for the
// hub: an off-by-one in a MenuRow's neighbour indices makes a row unreachable or
// traps the cursor, and no build gate notices.
//
// The retail Adventure race menu is RESUME, RESTART, EXIT TO MAP, OPTIONS
// (data.rowsAdvRace) and the Adventure cup menu is RESUME, EXIT TO MAP, OPTIONS
// (data.rowsAdvCup). KART SETUP goes directly below RESUME in both, the place
// the hub menu gives SELECT CHARACTER.
//
// Header-only and free of engine types so tools/test-kart-setup.c exercises the
// same rows MainFreeze.c builds.
// ---------------------------------------------------------------------------

// AP-owned row label, resolved to "KART SETUP" by RECTMENU.c. No retail string
// index comes near the 0x7f00 block (see ap_pauserow.h).
#define AP_LNG_KART_SETUP 0x7f20

// Largest retail race row set this header is asked to extend (rowsAdvRace).
#define AP_KS_RETAIL_MAX 4
#define AP_KS_ROWS_MAX (AP_KS_RETAIL_MAX + 1)

// Row strings in order, KART SETUP inserted below the first (RESUME) row.
// Returns the new row count, or the retail count unchanged when `withSetup` is
// 0 or the retail set is empty or too large.
static inline int AP_KartSetupRows_Build(const short *retail, int retailCount, int withSetup, short *out)
{
	int i, n = 0;
	if (retailCount <= 0 || retailCount > AP_KS_RETAIL_MAX)
		withSetup = 0;
	for (i = 0; i < retailCount && i < AP_KS_RETAIL_MAX; i++)
	{
		out[n++] = retail[i];
		if (i == 0 && withSetup)
			out[n++] = (short)AP_LNG_KART_SETUP;
	}
	return n;
}

// Carry a highlight from the retail row set into the KART SETUP set. RESUME
// stays row 0; every retail row below it moves down one. Out-of-range rows
// (rowSelected is an s16 a savestate can restore) land on RESUME.
static inline int AP_KartSetupRows_ToAp(int retailRow, int retailCount)
{
	if (retailRow <= 0 || retailRow >= retailCount)
		return 0;
	return retailRow + 1;
}

// The inverse. KART SETUP has no retail counterpart, so it lands on RESUME, the
// harmless row, never on whatever slid into its index.
static inline int AP_KartSetupRows_ToRetail(int apRow, int retailCount)
{
	if (apRow <= 1 || apRow > retailCount)
		return 0;
	return apRow - 1;
}

// Vertical wiring for row `row` of a `count`-row set, wrapping at both ends as
// the retail tables do.
static inline int AP_KartSetupRows_Up(int row, int count)
{
	return count > 0 ? (row + count - 1) % count : 0;
}
static inline int AP_KartSetupRows_Down(int row, int count)
{
	return count > 0 ? (row + 1) % count : 0;
}

// Game-mode bits the row gate reads, mirrored so the harness needs no engine
// headers. ap_kartsetup.c pins them against the engine's own values.
#define AP_KS_ADVENTURE_ARENA 0x100000u
#define AP_KS_ADVENTURE_MODE 0x80000u

// Whether a race pause menu carries KART SETUP. `selectorLive` is the hub
// picker's own gate (AP_CharSwap_PauseRowLive), `roomStats` is the room-option
// test the stat writers use (AP_CapabilityRoomStatsRace), so the row exists
// exactly where the selector's settings drive the kart. One local player only:
// the selector's settings belong to player 1.
static inline int AP_KartSetup_RowLive(unsigned gameMode1, int players, int selectorLive, int roomStats)
{
	if (!selectorLive || !roomStats || players != 1)
		return 0;
	if ((gameMode1 & AP_KS_ADVENTURE_ARENA) != 0)
		return 0; // the hub has SELECT CHARACTER, which already holds these controls
	return (gameMode1 & AP_KS_ADVENTURE_MODE) != 0;
}

#endif // AP_KARTSETUP_LOGIC_H
