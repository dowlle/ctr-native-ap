// Host harness for the #224 Turbo Grant's pure rules (ap/ap_turbogrant_logic.h).
//
// Every rule in here is unreachable from a build machine: the accounting needs a
// live server and a slot's item history, and the reserve rail needs a kart in a
// race. So the arithmetic is lifted into a header with no engine, no network and
// no config dependency, and driven here directly.
//
// WHAT IS COVERED
//
//   1. THE PENDING ARITHMETIC, including every lifecycle case the issue's rule 7
//      names: a reconnect replaying the same list, a duplicate grant, a delivery
//      that is fired, a delivery lost before firing (death, race restart), and a
//      seed or slot change reading a different persisted count.
//   2. HOSTILE PERSISTED COUNTS. Negative, absurdly large, and larger than the
//      number of receipts the server has actually sent.
//   3. THE RULED ITEMSANITY GATE, exhaustively -- both itemsanity states crossed
//      with both `Turbo` weapon states.
//   4. THE RESERVE RAIL, per VehFire_Increment branch, including the exact
//      boundary and the fact that it is a no-op for every payload the game
//      actually produces today.
//
// WHAT IS NOT COVERED, and cannot be here: whether a delivered Turbo actually
// appears in the weapon slot on a real kart, whether the ping sounds right, and
// what each Progressive Boost tier does when it is fired. Those are in-game
// observations and they are queued as such.

#include <stdio.h>

#include "../ap/ap_turbogrant_logic.h"

static int g_failures;

static void check(int condition, const char *what)
{
	if (!condition)
	{
		printf("FAIL: %s\n", what);
		g_failures++;
	}
}

static void check_eq(int got, int want, const char *what)
{
	if (got != want)
	{
		printf("FAIL: %s (got %d, want %d)\n", what, got, want);
		g_failures++;
	}
}

// ---------------------------------------------------------------------------
// 1. The pending arithmetic and the lifecycle cases
// ---------------------------------------------------------------------------

static void test_nothing_received_owes_nothing(void)
{
	check_eq(AP_TurboGrantPending(0, 0, 0), 0, "no receipts, nothing pending");
	check_eq(AP_TurboGrantPending(0, 0, 1), 0,
	         "no receipts cannot go negative through an in-flight bit");
}

static void test_one_receipt_is_owed_until_it_is_fired(void)
{
	check_eq(AP_TurboGrantPending(1, 0, 0), 1, "received, not delivered -> owed");
	check_eq(AP_TurboGrantPending(1, 0, 1), 0, "delivered, unfired -> not owed again");
	check_eq(AP_TurboGrantPending(1, 1, 0), 0, "fired -> settled");
}

static void test_a_delivery_lost_before_firing_is_owed_again(void)
{
	// Death, race restart, level change: the caller drops the in-flight bit and
	// never touches the fired count. Nothing else has to remember the requeue.
	check_eq(AP_TurboGrantPending(1, 0, 1), 0, "in flight, not owed");
	check_eq(AP_TurboGrantPending(1, 0, 0), 1, "lost before firing -> owed again");
}

static void test_duplicate_grants_are_counted_not_deduped(void)
{
	check_eq(AP_TurboGrantPending(3, 0, 0), 3, "three receipts, three owed");
	check_eq(AP_TurboGrantPending(3, 1, 1), 1,
	         "one fired, one in the slot -> exactly one still owed");
	check_eq(AP_TurboGrantPending(3, 3, 0), 0, "all three fired -> settled");
}

static void test_a_reconnect_replaying_the_same_list_changes_nothing(void)
{
	// received rebuilds to the same number out of the server's full replay while
	// fired is read back off disk, so the pair -- and therefore pending -- is
	// exactly what it was before the disconnect.
	const int received = 4, fired = 2;
	check_eq(AP_TurboGrantPending(received, fired, 0), 2, "pending before reconnect");
	check_eq(AP_TurboGrantPending(received, fired, 0), 2, "pending after replay");
}

static void test_a_process_restart_does_not_credit_an_undelivered_grant(void)
{
	// The in-flight bit is deliberately not persisted: a process that exited with
	// a Turbo sitting in the slot never let the player spend it, so the grant is
	// owed again rather than lost.
	check_eq(AP_TurboGrantPending(2, 1, 1), 0, "before the quit: one in the slot");
	check_eq(AP_TurboGrantPending(2, 1, 0), 1, "after the restart: owed again");
}

static void test_a_slot_change_reads_its_own_count(void)
{
	// Same seed, two slots. Slot A has fired both of its grants; slot B has
	// received one and fired none. Reading B's row must not let A's count
	// suppress it, which is what per-identity rows buy.
	check_eq(AP_TurboGrantPending(2, 2, 0), 0, "slot A settled");
	check_eq(AP_TurboGrantPending(1, 0, 0), 1, "slot B still owed its grant");
}

// ---------------------------------------------------------------------------
// 2. Hostile persisted counts
// ---------------------------------------------------------------------------

static void test_a_negative_persisted_count_cannot_manufacture_grants(void)
{
	check_eq(AP_TurboGrantClampFired(-1, 5), 0, "negative fired clamps to zero");
	check_eq(AP_TurboGrantClampFired(-100000, 5), 0, "very negative clamps to zero");
	check_eq(AP_TurboGrantPending(1, -50, 0), 1,
	         "a negative row cannot inflate what is owed");
}

static void test_an_oversized_persisted_count_cannot_go_negative(void)
{
	check_eq(AP_TurboGrantClampFired(9999, 3), 3, "fired clamps down to received");
	check_eq(AP_TurboGrantPending(3, 9999, 0), 0, "an oversized row settles, never owes");
	check_eq(AP_TurboGrantPending(3, 9999, 1), 0, "and still not with one in flight");
}

static void test_a_corrupt_in_flight_value_cannot_double_count(void)
{
	check_eq(AP_TurboGrantPending(5, 0, 7), 4, "any non-zero in-flight counts as one");
	check_eq(AP_TurboGrantPending(5, 0, -3), 4, "including a negative one");
}

static void test_a_negative_received_count_is_inert(void)
{
	check_eq(AP_TurboGrantPending(-4, 0, 0), 0, "a negative receipt count owes nothing");
}

// ---------------------------------------------------------------------------
// 3. The ruled itemsanity gate
// ---------------------------------------------------------------------------

static void test_itemsanity_off_needs_no_weapon_item(void)
{
	check(AP_TurboGrantDeliverable(0, 0),
	      "itemsanity off: deliverable without the Turbo weapon item");
	check(AP_TurboGrantDeliverable(0, 1),
	      "itemsanity off: deliverable with it too");
}

static void test_itemsanity_on_needs_the_turbo_weapon_item(void)
{
	check(!AP_TurboGrantDeliverable(1, 0),
	      "itemsanity on: NOT deliverable before the Turbo weapon item");
	check(AP_TurboGrantDeliverable(1, 1),
	      "itemsanity on: deliverable once the Turbo weapon item is in");
}

static void test_the_gate_never_consumes_a_grant(void)
{
	// The gate decides deliverability only. A grant blocked by it is still owed,
	// which is the whole of the issue's "no received grant is silently
	// discarded" for the pre-unlock case.
	check(!AP_TurboGrantDeliverable(1, 0), "blocked by the gate");
	check_eq(AP_TurboGrantPending(1, 0, 0), 1, "and still owed while blocked");
}

// ---------------------------------------------------------------------------
// 4. The reserve rail
// ---------------------------------------------------------------------------

// VehFire_Increment's three branches, by the type flags that select them.
#define TYPE_PAD          9u    /* turbo pad / boost powerup; the Turbo weapon uses 9 */
#define TYPE_POWERSLIDE   2u    /* start line, hang time, powerslide */
#define TYPE_SUPER_ENGINE 0x10u /* super engine: assigns rather than adds */

static void test_the_rail_is_a_no_op_for_real_payloads(void)
{
	// The two payloads the game actually produces, from an empty reserve and from
	// a healthy one. Nothing may change here or the rail would be a balance edit.
	check_eq(AP_TurboGrantClampReserves(0, 0, 0x960, TYPE_PAD), 0x960,
	         "Turbo weapon payload from empty reserves is untouched");
	check_eq(AP_TurboGrantClampReserves(0x3c0, 0, 0x3c0, TYPE_PAD), 0x3c0,
	         "normal pad payload on top of a pad's worth is untouched");
	check_eq(AP_TurboGrantClampReserves(0x1000, 0, 0x100, TYPE_POWERSLIDE), 0x100,
	         "powerslide payload is untouched");
}

static void test_the_pad_branch_clamps_the_resulting_sum(void)
{
	// The pad branch adds (reserves - outsideTimer), so the rail has to bound the
	// DELTA, not the argument.
	check_eq(AP_TurboGrantClampReserves(0x7000, 0, 0x2000, TYPE_PAD),
	         0x0fff, "pad delta is capped at the headroom");
	check_eq(AP_TurboGrantClampReserves(0x7000, 0x100, 0x2000, TYPE_PAD),
	         0x10ff, "an outside timer shifts the cap by exactly its own value");
	check_eq(AP_TurboGrantClampReserves(0x7fff, 0, 0x2000, TYPE_PAD),
	         0, "at the boundary the pad branch adds nothing");
}

static void test_the_pad_branch_ignores_a_delta_that_is_not_positive(void)
{
	// VehFire_Increment only adds when reserves > outsideTimer. A non-positive
	// delta must pass through untouched or the rail would change what the engine
	// stores back into turbo_outsideTimer.
	check_eq(AP_TurboGrantClampReserves(0x7fff, 0x2000, 0x1000, TYPE_PAD),
	         0x1000, "reserves below the outside timer are left alone");
	check_eq(AP_TurboGrantClampReserves(0x7fff, 0x1000, 0x1000, TYPE_PAD),
	         0x1000, "an exactly-equal outside timer is left alone");
}

static void test_the_additive_branch_clamps_the_resulting_sum(void)
{
	check_eq(AP_TurboGrantClampReserves(0x7000, 0, 0x2000, TYPE_POWERSLIDE),
	         0x0fff, "additive payload is capped at the headroom");
	check_eq(AP_TurboGrantClampReserves(0x7ffe, 0, 0x2000, TYPE_POWERSLIDE),
	         1, "one below the boundary leaves exactly one unit of room");
	check_eq(AP_TurboGrantClampReserves(0x7fff, 0, 0x2000, TYPE_POWERSLIDE),
	         0, "at the boundary the additive branch adds nothing");
}

static void test_the_super_engine_branch_only_caps_the_ceiling(void)
{
	// This branch ASSIGNS, so a full reserve bar is not a reason to reduce it.
	check_eq(AP_TurboGrantClampReserves(0x7000, 0, 0x2000, TYPE_SUPER_ENGINE),
	         0x2000, "an assignment below the ceiling is untouched");
	check_eq(AP_TurboGrantClampReserves(0, 0, 0x10000, TYPE_SUPER_ENGINE),
	         AP_TURBOGRANT_RESERVE_MAX, "an assignment past the ceiling is capped");
}

static void test_a_negative_current_reserve_is_treated_as_empty(void)
{
	// A reserve that already wrapped negative in an older session must not be
	// read as enormous headroom or as a reason to refuse the grant outright.
	check_eq(AP_TurboGrantClampReserves(-500, 0, 0x100, TYPE_POWERSLIDE), 0x100,
	         "a negative current reserve still accepts a normal payload");
}

static void test_the_rail_never_produces_an_overflowing_sum(void)
{
	// Exhaustive over a coarse grid: for every branch, every clamped result must
	// keep the sum VehFire_Increment would compute inside the s16 range.
	int current, payload;
	for (current = 0; current <= AP_TURBOGRANT_RESERVE_MAX; current += 337)
	{
		for (payload = 0; payload <= 0x20000; payload += 1319)
		{
			int pad = AP_TurboGrantClampReserves(current, 0, payload, TYPE_PAD);
			int add = AP_TurboGrantClampReserves(current, 0, payload, TYPE_POWERSLIDE);
			int set = AP_TurboGrantClampReserves(current, 0, payload, TYPE_SUPER_ENGINE);
			if (pad > 0 && current + pad > AP_TURBOGRANT_RESERVE_MAX)
			{
				printf("FAIL: pad branch overflowed at current=%d payload=%d\n",
				       current, payload);
				g_failures++;
				return;
			}
			if (current + add > AP_TURBOGRANT_RESERVE_MAX)
			{
				printf("FAIL: additive branch overflowed at current=%d payload=%d\n",
				       current, payload);
				g_failures++;
				return;
			}
			if (set > AP_TURBOGRANT_RESERVE_MAX)
			{
				printf("FAIL: super engine exceeded the ceiling at payload=%d\n",
				       payload);
				g_failures++;
				return;
			}
		}
	}
}

int main(void)
{
	test_nothing_received_owes_nothing();
	test_one_receipt_is_owed_until_it_is_fired();
	test_a_delivery_lost_before_firing_is_owed_again();
	test_duplicate_grants_are_counted_not_deduped();
	test_a_reconnect_replaying_the_same_list_changes_nothing();
	test_a_process_restart_does_not_credit_an_undelivered_grant();
	test_a_slot_change_reads_its_own_count();

	test_a_negative_persisted_count_cannot_manufacture_grants();
	test_an_oversized_persisted_count_cannot_go_negative();
	test_a_corrupt_in_flight_value_cannot_double_count();
	test_a_negative_received_count_is_inert();

	test_itemsanity_off_needs_no_weapon_item();
	test_itemsanity_on_needs_the_turbo_weapon_item();
	test_the_gate_never_consumes_a_grant();

	test_the_rail_is_a_no_op_for_real_payloads();
	test_the_pad_branch_clamps_the_resulting_sum();
	test_the_pad_branch_ignores_a_delta_that_is_not_positive();
	test_the_additive_branch_clamps_the_resulting_sum();
	test_the_super_engine_branch_only_caps_the_ceiling();
	test_a_negative_current_reserve_is_treated_as_empty();
	test_the_rail_never_produces_an_overflowing_sum();

	if (g_failures != 0)
	{
		printf("Turbo Grant accounting: %d FAILURE(S)\n", g_failures);
		return 1;
	}
	printf("Turbo Grant accounting: PASS\n");
	return 0;
}
