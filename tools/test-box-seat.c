// Item Box seats on custom bytes (#133 PR 4e): the freestanding seat arithmetic
// in ap/ap_box_seat_logic.h, the same rule the apworld's
// content_item_boxes.seat_indices uses.
// cc -Wall -Wextra -Werror -o /tmp/test-box-seat tools/test-box-seat.c
//
// What it pins:
//   1. the seat count: min(crates, 15), 0 for none and for a count past the
//      verifier's instance ceiling,
//   2. the spread: seat i takes crate floor(i * crates / seats), strictly
//      increasing and inside the crate table, with the apworld test's examples,
//   3. the agreement test that refuses a crate table the seed was not built for,
//   4. the instance-pool test that keeps seating off a crate pointer the
//      engine never wrote (a raw LEV value after the pool ran out).

#include <stdio.h>
#include "../ap/ap_box_seat_logic.h"

static int failures, checks;

static void expect(int ok, const char *what)
{
	++checks;
	if (!ok)
	{
		++failures;
		printf("FAIL %s\n", what);
	}
}

int main(void)
{
	long crates;
	int seat;

	expect(AP_BoxSeat_Count(0) == 0 && AP_BoxSeat_Count(-3) == 0, "no crates, no seats");
	expect(AP_BoxSeat_Count(1) == 1 && AP_BoxSeat_Count(8) == 8 && AP_BoxSeat_Count(15) == 15, "a seat per crate up to 15");
	expect(AP_BoxSeat_Count(16) == 15 && AP_BoxSeat_Count(69) == 15 && AP_BoxSeat_Count(65536) == 15, "never more than 15 seats");
	expect(AP_BoxSeat_Count(65537) == 0, "a count past the verifier's instance ceiling seats nothing");

	// The apworld test's examples (worlds/ctr/test/test_content_item_boxes.py).
	for (seat = 0; seat < 8; ++seat)
		expect(AP_BoxSeat_Ordinal(seat, 8) == seat, "eight crates seat all eight in order");
	for (seat = 0; seat < 15; ++seat)
		expect(AP_BoxSeat_Ordinal(seat, 30) == 2 * seat, "thirty crates seat every second one");
	expect(AP_BoxSeat_Ordinal(0, 69) == 0 && AP_BoxSeat_Ordinal(1, 69) == 4 && AP_BoxSeat_Ordinal(2, 69) == 9,
	       "Cortex Vortex's 69 crates spread from the first");
	expect(AP_BoxSeat_Ordinal(-1, 8) == -1 && AP_BoxSeat_Ordinal(8, 8) == -1 && AP_BoxSeat_Ordinal(15, 69) == -1 &&
	       AP_BoxSeat_Ordinal(0, 0) == -1, "no seat outside the seat range");

	for (crates = 1; crates <= 65536; crates += (crates < 200 ? 1 : 997))
	{
		const int seats = AP_BoxSeat_Count(crates);
		long previous = -1;
		int ok = seats == (crates < 15 ? (int)crates : 15);
		for (seat = 0; seat < seats; ++seat)
		{
			const long at = AP_BoxSeat_Ordinal(seat, crates);
			ok &= at > previous && at < crates;
			previous = at;
		}
		if (!ok)
		{
			printf("crate count %ld\n", crates);
			expect(0, "seats are distinct, ordered and inside the crate table");
		}
	}
	expect(1, "every crate count from 1 to 65536 seats distinct, ordered crates");

	expect(AP_BoxSeat_Agrees(8, 8, 8), "the measured table the seed was built for agrees");
	expect(AP_BoxSeat_Agrees(69, 69, 15), "a long table agrees with 15 seats");
	expect(!AP_BoxSeat_Agrees(9, 8, 8), "one more crate than the seed knows refuses");
	expect(!AP_BoxSeat_Agrees(7, 8, 8), "one crate fewer refuses");
	expect(!AP_BoxSeat_Agrees(69, 69, 14), "a forged seat count refuses");
	expect(!AP_BoxSeat_Agrees(0, 0, 0) && !AP_BoxSeat_Agrees(8, 8, 16) && !AP_BoxSeat_Agrees(8, -1, 8),
	       "no seats, too many seats or an unstated count refuse");

	{
		const uintptr_t base = 0x10000;
		expect(AP_BoxSeat_PoolItem(base, base, 4, 0x8c), "the first pool item is a crate instance");
		expect(AP_BoxSeat_PoolItem(base + 3 * 0x8c, base, 4, 0x8c), "the last pool item is a crate instance");
		expect(AP_BoxSeat_PoolItem(base + 2 * 0x8c, base, 4, 0x8e), "the stride rounds down to 4 like JitPool_Clear");
		expect(!AP_BoxSeat_PoolItem(base + 4 * 0x8c, base, 4, 0x8c), "one past the pool is no instance");
		expect(!AP_BoxSeat_PoolItem(base - 0x8c, base, 4, 0x8c), "before the pool is no instance");
		expect(!AP_BoxSeat_PoolItem(base + 1, base, 4, 0x8c) && !AP_BoxSeat_PoolItem(1, base, 4, 0x8c),
		       "a misaligned or raw file value is no instance");
		expect(!AP_BoxSeat_PoolItem(0, base, 4, 0x8c) && !AP_BoxSeat_PoolItem(base, 0, 4, 0x8c) &&
		       !AP_BoxSeat_PoolItem(base, base, 0, 0x8c) && !AP_BoxSeat_PoolItem(base, base, 4, 3),
		       "no pointer, no pool, no items or no stride is no instance");
	}

	printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
