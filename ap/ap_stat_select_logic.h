#ifndef AP_STAT_SELECT_LOGIC_H
#define AP_STAT_SELECT_LOGIC_H

// ---------------------------------------------------------------------------
// Progressive Stats / Progressive Boost rank selector (ruling 2026-09-11,
// extended to Boost 2026-10-26).
//
// With a capability pack live, the player may drive any rank from the bottom of
// that chain up to the rank their received items unlock, and never above it.
// Independently per chain (Boost, Top Speed, Acceleration, Turning) and, in
// per_character mode, per racer. When an upgrade arrives, a selection that was
// at the old ceiling follows it up; a deliberately lowered selection stays put.
//
// Freestanding on purpose: no engine or AP headers, so
// tools/test-stat-select.c compiles exactly the code production runs.
//
// ── Representation ──
// One small int per (row, chain):
//   AP_SS_FOLLOW (-1)  follow the ceiling. The default, and what "at the
//                      ceiling" means, so an upgrade carries it along without
//                      the selector ever observing the upgrade.
//   0..AP_SS_VALUE_MAX an explicit LOWERED rank.
// The value the kart drives is min(explicit, ceiling), and FOLLOW reads as the
// ceiling. A step that reaches the ceiling stores FOLLOW again, so "lowered"
// and "explicit" stay the same thing.
//
// Why not store the rank and compare it with the ceiling on each receipt: the
// ceiling is rebuilt from zero on every reconnect (AP_CapabilityReset, then the
// server's full ReceivedItems replay), so a receipt-time rule would see a
// lowered rank equal the half-replayed ceiling and drag it upwards. Clamp on
// read has no receipt-time step and so cannot misfire during a replay.
//
// ── Logic ──
// Nothing here is a logic input. Item counts stay the only thing win_logic,
// the content-plan verifier, the Oxide gate and the tracker read; this only
// changes which rank the kart expresses.
// ---------------------------------------------------------------------------

#define AP_SS_FOLLOW (-1)

// The four capability chains, in ap_capability.h's AP_CAP_CHAIN_* order
// (boost, top speed, acceleration, turning). ap_capability.c pins the count.
#define AP_SS_CHAINS 4

// Row 0 is the shared_global row; rows 1..16 are the per-character rows in the
// apworld ROSTER order (the order the per-character items are minted in and the
// order ap_capability.c already keys its received counts by).
#define AP_SS_ROSTER 16
#define AP_SS_ROWS (1 + AP_SS_ROSTER)

// Wire: one flat JSON array, row-major, AP_SS_CHAINS values per row.
#define AP_SS_WIRE_COUNT (AP_SS_ROWS * AP_SS_CHAINS) // 68

// Highest rank any chain has: Progressive Stats VERY HIGH. Boost tops out at 3
// (Blue Fire); a stored boost value above its ceiling is clamped on read like
// any other over-ceiling value.
#define AP_SS_VALUE_MAX 4

// Capability mode values, mirrored from ap_capability.h (AP_CAP_MODE_*).
#define AP_SS_MODE_SHARED 1
#define AP_SS_MODE_PER_CHARACTER 2

// 1 if `v` is a value this client could have stored. Untrusted data storage is
// range-checked wide, before any narrowing, and a package with any bad value is
// rejected whole by the caller.
static inline int AP_StatSel_ValueOk(long long v)
{
	return v >= (long long)AP_SS_FOLLOW && v <= (long long)AP_SS_VALUE_MAX;
}

// The rank the kart drives. `ceiling` is the received-item rank (already
// clamped to the chain's top); a negative ceiling means the pack is not live
// and is passed straight through so callers keep their fail-open path.
static inline int AP_StatSel_Applied(int sel, int ceiling)
{
	if (ceiling < 0)
		return ceiling;
	if (sel < 0 || sel >= ceiling)
		return ceiling;
	return sel;
}

// 1 when the stored selection is below the ceiling, i.e. the player chose to
// drive weaker than they could.
static inline int AP_StatSel_IsLowered(int sel, int ceiling)
{
	return ceiling >= 0 && sel >= 0 && sel < ceiling;
}

// One LEFT (dir < 0) or RIGHT (dir > 0) press. Returns the new stored value.
// The floor is rank 0 (VERY LOW, or Boost tier NONE); the top is the ceiling,
// which is stored as FOLLOW so the next upgrade carries it along. With the pack
// not live the selection is returned unchanged.
static inline int AP_StatSel_Step(int sel, int ceiling, int dir)
{
	int next;

	if (ceiling < 0)
		return sel;
	next = AP_StatSel_Applied(sel, ceiling);
	if (dir < 0)
		next--;
	else if (dir > 0)
		next++;
	if (next < 0)
		next = 0;
	if (next >= ceiling)
		return AP_SS_FOLLOW;
	return next;
}

// The selection row for a chain under its pack's mode: 0 in shared_global mode,
// 1 + roster slot in per_character mode. -1 for a pack that is off, or a
// per-character row whose racer cannot be identified.
static inline int AP_StatSel_Row(int mode, int rosterSlot)
{
	if (mode == AP_SS_MODE_SHARED)
		return 0;
	if (mode == AP_SS_MODE_PER_CHARACTER)
	{
		if (rosterSlot < 0 || rosterSlot >= AP_SS_ROSTER)
			return -1;
		return 1 + rosterSlot;
	}
	return -1;
}

static inline int AP_StatSel_Index(int row, int chain)
{
	if (row < 0 || row >= AP_SS_ROWS || chain < 0 || chain >= AP_SS_CHAINS)
		return -1;
	return row * AP_SS_CHAINS + chain;
}

// ── Picker panel rows ──
// The hub picker's edit focus walks four rows, top to bottom as drawn:
//   0 BOOST (the line under the name), 1 SPEED, 2 ACCEL, 3 TURN (the bars).
// A row is in `availMask` (bit r) when something can be changed on it for this
// seed. Navigation wraps and skips unavailable rows; -1 means nothing is
// editable and the picker must not enter edit focus at all.
#define AP_SS_PANEL_ROWS 4
#define AP_SS_PANEL_BOOST 0

static inline int AP_StatSel_FirstRow(unsigned availMask)
{
	int r;
	for (r = 0; r < AP_SS_PANEL_ROWS; r++)
		if ((availMask >> r) & 1u)
			return r;
	return -1;
}

static inline int AP_StatSel_NextRow(int row, int dir, unsigned availMask)
{
	int i;
	int r;

	if (AP_StatSel_FirstRow(availMask) < 0)
		return -1;
	if (row < 0 || row >= AP_SS_PANEL_ROWS)
		return AP_StatSel_FirstRow(availMask);
	r = row;
	for (i = 0; i < AP_SS_PANEL_ROWS; i++)
	{
		r = (dir < 0) ? (r + AP_SS_PANEL_ROWS - 1) % AP_SS_PANEL_ROWS : (r + 1) % AP_SS_PANEL_ROWS;
		if ((availMask >> r) & 1u)
			return r;
	}
	return row;
}

// A focused row that has stopped being available (the cursor moved to a racer
// where it does not apply) moves to the first available one.
static inline int AP_StatSel_ValidRow(int row, unsigned availMask)
{
	if (row >= 0 && row < AP_SS_PANEL_ROWS && ((availMask >> row) & 1u))
		return row;
	return AP_StatSel_FirstRow(availMask);
}

#endif // AP_STAT_SELECT_LOGIC_H
