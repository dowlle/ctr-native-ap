#ifndef AP_BOX_MAP_H
#define AP_BOX_MAP_H

// AP box location bookkeeping (#109), deliberately freestanding.
//
// Everything in this header is pure arithmetic over the shipped location table:
// no engine types, no IO, no globals that outlive a call. That is what lets
// tools/test-box-map.c compile and exercise the real code out of engine, which
// is the only way any of this gets tested before a disc and a display exist.
//
// The engine half lives in ap_boxes.c: spawning crates, the player proximity
// test, the break animation and the check emit. This half answers only "which
// location code is this box, and which of a track's boxes should be standing".
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

#include "ap_locations.h" // AP_LOCATION_TABLE: the shipped location code table

// ── the location code block ─────────────────────────────────────────────────
//
// From the 0.2.0 datapackage name freeze (#177): location class `item_boxes`,
// 270 names, codes 35014000-35014269, stride 15 per track over 18 box tracks,
// names "<Track>: Item Box N" with N 1-based.
//
// ⚠ THE FREEZE HAD NOT MERGED WHEN THIS WAS BUILT (apworld PR #44 open). These
// three constants are the entire re-pointing seam: if the freeze lands with a
// different block, changing AP_BOX_CODE_BASE is the whole edit, and the harness
// re-checks the arithmetic. Nothing else in the client hardcodes a box code.
#define AP_BOX_CODE_BASE       35014000L
#define AP_BOX_SLOTS_PER_TRACK 15
#define AP_BOX_TRACK_COUNT     18
#define AP_BOX_LOCATION_COUNT  (AP_BOX_TRACK_COUNT * AP_BOX_SLOTS_PER_TRACK) // 270

// ── engine LevelID -> apworld box track index ───────────────────────────────
//
// DERIVED, never restated (Lessons Learned §5). The freeze note §2.3 says the
// box class uses "the same canonical set the relic races use ... derived at
// import from the Sapphire trial block so the box order can never drift from
// it". So the client derives it from the same place, out of the location table
// it already ships:
//
//   * the Sapphire Time Trial block occupies AdvProgress bits 22..39,
//   * those 18 bits are laid out in engine LevelID order (bit 22 = LevelID 0
//     DINGO_CANYON ... bit 39 = LevelID 17 TURBO_TRACK),
//   * and the block's location codes are 35012000 + <apworld track index>.
//
// Therefore apTrack(levelID) = code(bit 22 + levelID) - 35012000, with no table
// of track names anywhere in this client that could disagree with the apworld's.
#define AP_BOX_SAPPHIRE_BIT_BASE  22
#define AP_BOX_SAPPHIRE_CODE_BASE 35012000L

// The apworld box-track index for an engine LevelID, or -1 when that level is
// not one of the 18 box tracks (hubs, arenas, menus, the ids past TURBO_TRACK).
static inline int AP_BoxMap_ApTrack(int levelID)
{
	int wantBit;
	unsigned i;

	if (levelID < 0 || levelID >= AP_BOX_TRACK_COUNT)
		return -1;

	wantBit = AP_BOX_SAPPHIRE_BIT_BASE + levelID;

	for (i = 0; i < sizeof(AP_LOCATION_TABLE) / sizeof(AP_LOCATION_TABLE[0]); i++)
	{
		long code;

		if (AP_LOCATION_TABLE[i].bit_index != wantBit)
			continue;

		code = AP_LOCATION_TABLE[i].location_code - AP_BOX_SAPPHIRE_CODE_BASE;

		// A Sapphire row outside its own 18-code block would mean the table and
		// this derivation disagree; refuse rather than mint a wrong box code.
		if (code < 0 || code >= AP_BOX_TRACK_COUNT)
			return -1;

		return (int)code;
	}

	return -1;
}

// The location code for slot `slot` (0-based) on an engine LevelID, or -1 when
// the level is not a box track or the slot is past the frozen per-track ceiling.
static inline long AP_BoxMap_Code(int levelID, int slot)
{
	int apTrack = AP_BoxMap_ApTrack(levelID);

	if (apTrack < 0)
		return -1;
	if (slot < 0 || slot >= AP_BOX_SLOTS_PER_TRACK)
		return -1;

	return AP_BOX_CODE_BASE + (long)apTrack * AP_BOX_SLOTS_PER_TRACK + slot;
}

// ── the spawn set ───────────────────────────────────────────────────────────

// One authored placement, in the shape the placement file stores it (LEV
// InstDef world units; rot_y is an engine angle, 0x1000 = one full turn).
typedef struct
{
	int   level;
	short x, y, z, rotY;
} AP_BoxPlacement;

// One box that should be standing on the track right now.
typedef struct
{
	int   slot; // 0-based; the player-visible name is "Item Box <slot+1>"
	long  code;
	short x, y, z, rotY;
} AP_BoxSlot;

// Build the spawn set for one level.
//
// SLOT ASSIGNMENT IS POSITIONAL AND UNCONDITIONAL: the Nth placement listed for
// a level is slot N, whether or not it spawns. A box that is absent from the
// seed or already checked still consumes its slot, because the slot is what the
// frozen name is keyed to -- letting a skipped box shift the ones after it down
// would silently re-point every later name on that track.
//
// seedFn(code, ctx)    -> 1 if this location exists in the current seed
// checkedFn(code, ctx) -> 1 if it has already been checked (server truth)
// Either may be NULL, which reads as "yes, present" / "no, not checked".
//
// Returns the number of entries written to out[]. *droppedOverflow, when not
// NULL, receives the number of placements past the frozen per-track ceiling,
// which are dropped: they have no name to send and never spawn. Never silent
// (Lessons Learned §4) -- the caller logs it.
static inline int AP_BoxMap_BuildSet(const AP_BoxPlacement *placements, int count, int levelID,
                                     int (*seedFn)(long, void *), int (*checkedFn)(long, void *),
                                     void *ctx, AP_BoxSlot *out, int outMax, int *droppedOverflow)
{
	int i;
	int slot = 0;
	int written = 0;
	int overflow = 0;

	if (droppedOverflow != 0)
		*droppedOverflow = 0;

	if (placements == 0 || out == 0 || outMax <= 0)
		return 0;
	if (AP_BoxMap_ApTrack(levelID) < 0)
		return 0;

	for (i = 0; i < count; i++)
	{
		long code;

		if (placements[i].level != levelID)
			continue;

		if (slot >= AP_BOX_SLOTS_PER_TRACK)
		{
			overflow++;
			continue;
		}

		code = AP_BoxMap_Code(levelID, slot);
		slot++;

		if (code < 0)
			continue;
		if (seedFn != 0 && !seedFn(code, ctx))
			continue;
		if (checkedFn != 0 && checkedFn(code, ctx))
			continue; // broken earlier this seed: gone, across reconnects too

		if (written >= outMax)
		{
			overflow++;
			continue;
		}

		out[written].slot = slot - 1;
		out[written].code = code;
		out[written].x = placements[i].x;
		out[written].y = placements[i].y;
		out[written].z = placements[i].z;
		out[written].rotY = placements[i].rotY;
		written++;
	}

	if (droppedOverflow != 0)
		*droppedOverflow = overflow;

	return written;
}

// ── how many boxes still stand behind a track ───────────────────────────────
//
// The §6 pad predicate (ruled 2026-08-11): AP_PadState must not Re-lock or mark
// Done a pad that still has breakable boxes behind it. This answers only "how
// many", because the pad needs a count and not the geometry -- boxes carry no
// AdvProgress bit and so cannot ride in the pad's glow-bit array.
//
// `slots` is how many placements that level actually holds, capped by the caller
// at the frozen ceiling. The predicate is deliberately the SAME pair the spawn
// set uses: a box counts only if it could actually be broken, meaning it has
// geometry (it is within `slots`), a live location in this slot's seed (§7) and
// is not yet checked. Counting bare locations instead would let a seed that
// created more box locations than the placement set covers keep a pad green with
// nothing on the track to break.
static inline int AP_BoxMap_CountStanding(int levelID, int slots,
                                          int (*seedFn)(long, void *),
                                          int (*checkedFn)(long, void *), void *ctx)
{
	int slot, standing = 0;

	if (AP_BoxMap_ApTrack(levelID) < 0)
		return 0; // hub, arena, menu: not a box track
	if (slots > AP_BOX_SLOTS_PER_TRACK)
		slots = AP_BOX_SLOTS_PER_TRACK;

	for (slot = 0; slot < slots; slot++)
	{
		long code = AP_BoxMap_Code(levelID, slot);

		if (code < 0)
			continue;
		if (seedFn != 0 && !seedFn(code, ctx))
			continue;
		if (checkedFn != 0 && checkedFn(code, ctx))
			continue;
		standing++;
	}
	return standing;
}

// ── which box codes may go on the wire ──────────────────────────────────────
//
// The connect-time scout list (ap_net.cpp). The item feed resolves its
// "ITEM TO PLAYER" line out of the scout cache, and box locations were absent
// from that list entirely, which is why a peer-bound box was silent while an
// own-bound one toasted through the ReceivedItems echo (found in the 2026-08-11
// v2 retest: 5 of 12 boxes fed the other slot, none toasted).
//
// ONLY codes this world actually created may be listed. MultiServer 0.6.7 hard
// drops a connection on an invalid id in the scout path, so pushing the whole
// 270-code block blind is a disconnect and not a diagnostic; a seed without the
// box class contributes nothing at all. `inWorldFn` is the same checked+missing
// membership test ap_net_location_exists uses (#217).
//
// Returns the number of codes written to out[]. Writing stops at outMax.
static inline int AP_BoxMap_ScoutCodes(int (*inWorldFn)(long, void *), void *ctx,
                                       long *out, int outMax)
{
	int i, written = 0;

	if (inWorldFn == 0 || out == 0 || outMax <= 0)
		return 0;

	for (i = 0; i < AP_BOX_LOCATION_COUNT; i++)
	{
		long code = (long)(AP_BOX_CODE_BASE + i);

		if (!inWorldFn(code, ctx))
			continue;
		if (written >= outMax)
			break;
		out[written++] = code;
	}
	return written;
}

// ── proximity, overflow-safe ─────────────────────────────────────────────────
//
// Is point (px,py,pz) within `radius` (LEV world units, NOT squared) of centre
// (cx,cy,cz)? Extracted 2026-08-13 after a DeepSeek review REJECTed the box-
// breakability (#234) candidate for exactly the bug this shape prevents: a
// naive `dx*dx + dy*dy + dz*dz` in a 32-bit int overflows and wraps NEGATIVE
// once a single axis delta exceeds ~46,341, and real LEV-scale positions on a
// large track can be that far apart. A wrapped-negative distSq passes a
// `distSq > radiusSquared` skip test, so the failure silently turns "far
// away" into "in range" -- a wrong location check sent, not a missed one.
//
// The per-axis early-out below runs BEFORE any squaring, so every squared
// term is already bounded to `radius` by the time it is computed: the sum can
// never exceed 3 * radius * radius, which is safe in a 32-bit int for any
// radius this project actually uses (the largest AP call site is 0x200 = 512,
// giving a worst case of 786,432, nowhere near the ~2.1 billion int32 ceiling).
//
// Freestanding like the rest of this header: pure arithmetic, no engine
// types, testable out of engine (tools/test-box-map.c).
static inline int AP_BoxMap_WithinRadius(int px, int py, int pz, int cx, int cy, int cz, int radius)
{
	int dx = px - cx;
	int dy = py - cy;
	int dz = pz - cz;

	if (dx > radius || dx < -radius)
		return 0;
	if (dy > radius || dy < -radius)
		return 0;
	if (dz > radius || dz < -radius)
		return 0;

	return (dx * dx + dy * dy + dz * dz) <= (radius * radius);
}

#endif // CTR_AP
#endif // AP_BOX_MAP_H
