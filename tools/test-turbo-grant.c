// Host harness for the Turbo Grant's pure rules (ap/ap_turbogrant_logic.h,
// #224, a Turbo every race since #487).
//
// Every rule in here is unreachable from a build machine: the per-race latch
// needs a live race and the reserve rail needs a kart in a race. So the rules
// are lifted into a header with no engine, no network and no config dependency,
// and driven here directly.
//
// WHAT IS COVERED
//
//   1. THE PER-RACE LATCH: nothing before the item, one Turbo per race attempt,
//      a new Turbo after a restart (a new countdown) and after the next race,
//      none in the race-load gap before the countdown is seen, and more copies
//      of the item still giving one Turbo per race.
//   2. THE RULED ITEMSANITY GATE, exhaustively -- both itemsanity states crossed
//      with both `Turbo` weapon states, and the race's Turbo still owed while
//      the gate blocks it.
//   3. THE RESERVE RAIL, per VehFire_Increment branch, including the exact
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
// 1. The per-race latch
// ---------------------------------------------------------------------------

// One race start: on track, countdown running for a few frames, then lights out.
static void start_race(AP_TurboGrantRace *r)
{
	AP_TurboGrantRaceStep(r, 1, 0, 1);
	AP_TurboGrantRaceStep(r, 1, 0, 1);
	AP_TurboGrantRaceStep(r, 1, 0, 0);
}

static void test_nothing_before_the_item(void)
{
	AP_TurboGrantRace r = {0, 0};
	start_race(&r);
	check(!AP_TurboGrantOwedThisRace(&r, 0), "no receipt, no Turbo");
	check(!AP_TurboGrantOwedThisRace(&r, -3), "a negative receipt count is inert");
}

static void test_one_turbo_per_race(void)
{
	AP_TurboGrantRace r = {0, 0};
	start_race(&r);
	check(AP_TurboGrantOwedThisRace(&r, 1), "received: this race's Turbo is owed");
	r.givenThisRace = 1; // the runtime delivered it
	AP_TurboGrantRaceStep(&r, 1, 0, 0);
	check(!AP_TurboGrantOwedThisRace(&r, 1), "delivered: not owed again this race");
	// Fired, rerolled or lost: the latch does not reopen mid-race.
	AP_TurboGrantRaceStep(&r, 1, 0, 0);
	check(!AP_TurboGrantOwedThisRace(&r, 1), "a Turbo that left the slot is not replaced");
}

static void test_more_copies_still_give_one_turbo(void)
{
	AP_TurboGrantRace r = {0, 0};
	start_race(&r);
	check(AP_TurboGrantOwedThisRace(&r, 3), "three copies: owed");
	r.givenThisRace = 1;
	check(!AP_TurboGrantOwedThisRace(&r, 3), "three copies: still one Turbo per race");
}

static void test_a_restart_is_a_new_race(void)
{
	AP_TurboGrantRace r = {0, 0};
	start_race(&r);
	r.givenThisRace = 1;
	// A restart re-runs the countdown on the same track.
	start_race(&r);
	check(AP_TurboGrantOwedThisRace(&r, 1), "restart: a new Turbo is owed");
}

static void test_the_next_race_owes_a_new_turbo(void)
{
	AP_TurboGrantRace r = {0, 0};
	start_race(&r);
	r.givenThisRace = 1;
	AP_TurboGrantRaceStep(&r, 1, 1, 0); // end of race
	check(!AP_TurboGrantOwedThisRace(&r, 1), "end of race: nothing owed");
	AP_TurboGrantRaceStep(&r, 0, 0, 0); // hub
	check(!AP_TurboGrantOwedThisRace(&r, 1), "hub: nothing owed");
	start_race(&r);
	check(AP_TurboGrantOwedThisRace(&r, 1), "next race: owed again");
}

static void test_nothing_before_the_countdown_is_seen(void)
{
	// The race-load gap: on track, lights not observed yet. Anything written to
	// the kart now would be wiped by VehBirth.
	AP_TurboGrantRace r = {0, 0};
	AP_TurboGrantRaceStep(&r, 1, 0, 0);
	check(!AP_TurboGrantOwedThisRace(&r, 1), "no countdown seen: not owed yet");
	AP_TurboGrantRaceStep(&r, 1, 0, 1);
	check(AP_TurboGrantOwedThisRace(&r, 1), "countdown seen: owed");
}

static void test_receipt_mid_race_is_delivered_that_race(void)
{
	AP_TurboGrantRace r = {0, 0};
	start_race(&r);
	check(!AP_TurboGrantOwedThisRace(&r, 0), "before the receipt");
	check(AP_TurboGrantOwedThisRace(&r, 1), "received mid-race: owed in this race");
}

// ---------------------------------------------------------------------------
// 2. The ruled itemsanity gate
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
	// The gate decides deliverability only. The race's Turbo blocked by it is
	// still owed, so a `Turbo` weapon item that arrives mid-race delivers it.
	AP_TurboGrantRace r = {0, 0};
	start_race(&r);
	check(!AP_TurboGrantDeliverable(1, 0), "blocked by the gate");
	check(AP_TurboGrantOwedThisRace(&r, 1), "and still owed while blocked");
}

// ---------------------------------------------------------------------------
// 3. The reserve rail
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
	test_nothing_before_the_item();
	test_one_turbo_per_race();
	test_more_copies_still_give_one_turbo();
	test_a_restart_is_a_new_race();
	test_the_next_race_owes_a_new_turbo();
	test_nothing_before_the_countdown_is_seen();
	test_receipt_mid_race_is_delivered_that_race();

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
		printf("Turbo Grant rules: %d FAILURE(S)\n", g_failures);
		return 1;
	}
	printf("Turbo Grant rules: PASS\n");
	return 0;
}
