#ifndef AP_BOX_SEAT_LOGIC_H
#define AP_BOX_SEAT_LOGIC_H

#include <stdint.h>

// Item Box seats on custom bytes (#133 PR 4e), deliberately freestanding like
// ap_box_map.h: the engine half (ap_boxes.c) walks the live LEV instance table,
// the arithmetic lives here so tools/test-box-seat.c pins it out of engine.
//
// A custom track has no authored AP box placement. A content-plan track on a
// named block gets Item Box 1 to min(weapon_crates, 15), and each box takes the
// seat of one of the track's own weapon crates (the "?" item crate,
// PU_RANDOM_CRATE): it stands where that crate stands, and the crate is hidden
// and cannot be collected while its AP box stands. Breaking the box gives the
// seat back to the crate. The package states how many weapon crates its LEV
// holds (`capabilities.weapon_crates`), native counts them on load and refuses
// a disagreement (ap_content_capability.h), and the apworld computes the same
// seats (worlds/ctr/content_item_boxes.py seat_indices).
//
// WHICH CRATES. With more crates than seats the seats are spread over the whole
// instance table, seat i on crate floor(i * crates / seats) counted in table
// order, so ordinary crates remain between the AP boxes rather than the first
// rows of the track all turning into AP boxes.

#define AP_BOX_SEAT_MAX 15
// The verifier refuses a LEV with more instances than this
// (native_custom_content_verify.c), so a count above it is never real.
#define AP_BOX_SEAT_CRATES_MAX 65536L

// How many seats a track with `crates` weapon crates has: min(crates, 15), 0
// for no crates or a count outside the measurable range.
static inline int AP_BoxSeat_Count(long crates)
{
	if (crates <= 0 || crates > AP_BOX_SEAT_CRATES_MAX)
		return 0;
	return crates < AP_BOX_SEAT_MAX ? (int)crates : AP_BOX_SEAT_MAX;
}

// The 0-based ordinal, among the track's weapon crates in instance-table order,
// of the crate seat `seat` takes, or -1 when there is no such seat.
static inline long AP_BoxSeat_Ordinal(int seat, long crates)
{
	const int seats = AP_BoxSeat_Count(crates);
	if (seat < 0 || seat >= seats)
		return -1;
	return (long)(((long long)seat * crates) / seats);
}

// 1 when the loaded bytes can seat the plan's boxes: the live crate count is
// the package's measured count the plan was built from, and the plan's seat
// count is the one that count gives. Anything else stands no box, because a
// seat taken from a different crate table would be the wrong place.
static inline int AP_BoxSeat_Agrees(long liveCrates, int planCrates, int planSeats)
{
	if (planSeats <= 0 || planSeats > AP_BOX_SEAT_MAX)
		return 0;
	if (planCrates <= 0 || (long)planCrates != liveCrates)
		return 0;
	return AP_BoxSeat_Count(liveCrates) == planSeats;
}

// 1 when `inst` is the start of an item in the instance pool that starts at
// `base`, holds `maxItems` items and steps by `itemSize` rounded down to 4
// (JitPool_Clear's stride). A crate's InstDef.ptrInstance is only written by
// INSTANCE_LevInitAll, which returns early when the pool runs out; every later
// entry then still holds the raw value from the LEV file. Seating must not
// touch such a pointer, so anything outside the pool counts as no crate.
static inline int AP_BoxSeat_PoolItem(uintptr_t inst, uintptr_t base, long maxItems, unsigned long itemSize)
{
	const uintptr_t stride = (uintptr_t)((itemSize >> 2) << 2);
	if (inst == 0 || base == 0 || maxItems <= 0 || stride == 0 || inst < base)
		return 0;
	if ((inst - base) % stride != 0)
		return 0;
	return (inst - base) / stride < (uintptr_t)maxItems;
}

#endif
