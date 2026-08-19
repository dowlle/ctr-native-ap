// Focused host harness for the boss-race catch-up ownership guard (#145
// follow-up, ruled 2026-08-19). It links nothing from the engine: the rule under
// test lives in ap/ap_itemsanity_logic.h, so the runtime hook and this harness
// run the same code. The vanilla chain the guard wraps is mirrored here with the
// file:line each step comes from, because the defect being fixed was an ordering
// and scope defect rather than a rule defect.
//
// Build:
//   gcc -std=c99 -Wall -Wextra -Werror tools/test-boss-assist.c -o /tmp/test-boss-assist
//   /tmp/test-boss-assist

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ap/ap_itemsanity_logic.h"

#define OWNED_SET_COUNT (1 << AP_ITEMSANITY_WEAPON_COUNT)

// Every held id the roulette can settle on before the boss block runs, plus the
// no-item sentinel the draw filter emits when nothing is owned.
static const int allHeldIDs[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                 AP_ITEMSANITY_NO_ITEM};
#define ALL_HELD_COUNT ((int)(sizeof allHeldIDs / sizeof allHeldIDs[0]))

// ==============================================================
// Mirror of the vanilla block, game/Vehicle/VehPhysGeneral.c:1055-1099
// ==============================================================

// The three catch-up branches and the Komodo Joe cap, unmodified. bossFails is
// advProgress.timesLostBossRace[bossID]; the escalation widens as it falls.
static int model_vanilla_boss_block(int rolled, int bossFails, int isDragonMines)
{
	int held = rolled;

	if (bossFails < 0x3 && (unsigned)held - 0x7 < 0x3)
		held = 0xb;
	else if (bossFails < 0x4 && (unsigned)held - 0x7 < 0x2)
		held = 0xb;
	else if (bossFails < 0x5 && held == 0x8)
		held = 0xb;

	if (isDragonMines && held == 0xb)
		held = 0x2;

	return held;
}

// game/Vehicle/VehPhysGeneral.c:1101-1110, the guard itself: called once with
// the draw the block started from and the item the block settled on.
static int model_guarded_boss_block(int rolled, int bossFails, int isDragonMines,
	const unsigned char *owned)
{
	int proposed = model_vanilla_boss_block(rolled, bossFails, isDragonMines);
	return AP_ItemsanityBossAssistWeapon(rolled, proposed, owned);
}

static void owned_clear(unsigned char *owned)
{
	memset(owned, 0, AP_ITEMSANITY_WEAPON_COUNT);
}

// Strict ownership: unlike AP_ItemsanityRollAllowed this is false for an id that
// is not one of the 11 gated weapons, so a leaked Spring or sentinel fails here.
static int owned_holds(const unsigned char *owned, int heldItemID)
{
	int index = AP_ItemsanityWeaponIndex(AP_ItemsanityCanonicalWeapon(heldItemID));
	return index >= 0 && owned[index] != 0;
}

static void owned_from_mask(unsigned char *owned, unsigned mask)
{
	int i;
	for (i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
		owned[i] = (mask >> i) & 1u;
}

static int ladder_rung(int heldItemID)
{
	static const unsigned char ladder[AP_ITEMSANITY_BOSS_ASSIST_LADDER_COUNT] =
		AP_ITEMSANITY_BOSS_ASSIST_LADDER;
	int i;
	for (i = 0; i < AP_ITEMSANITY_BOSS_ASSIST_LADDER_COUNT; i++)
		if (ladder[i] == heldItemID)
			return i;
	return -1;
}

// ==============================================================
// Gate 1: the rewrite still fires when the player owns the catch-up weapon
// ==============================================================

static void test_owned_missiles_still_rewrite(void)
{
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};
	const int replaced[3] = {0x7, 0x8, 0x9}; // Mask, Clock, Warpball
	int i;

	// Missile x3 received. Every draw the block replaces must still become
	// Missile x3, at every loss count where retail escalates that draw.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(0xb)] = 1;
	for (i = 0; i < 3; i++)
		owned[AP_ItemsanityWeaponIndex(replaced[i])] = 1;

	for (i = 0; i < 3; i++)
		assert(model_guarded_boss_block(replaced[i], 0, 0, owned) == 0xb);
	assert(model_guarded_boss_block(0x7, 3, 0, owned) == 0xb);
	assert(model_guarded_boss_block(0x8, 3, 0, owned) == 0xb);
	assert(model_guarded_boss_block(0x8, 4, 0, owned) == 0xb);

	// Komodo Joe still caps the same escalation at one Missile when it is owned.
	owned[AP_ItemsanityWeaponIndex(0x2)] = 1;
	for (i = 0; i < 3; i++)
		assert(model_guarded_boss_block(replaced[i], 0, 1, owned) == 0x2);

	// Outside the escalation window the block does nothing, guarded or not.
	assert(model_guarded_boss_block(0x9, 3, 0, owned) == 0x9);
	assert(model_guarded_boss_block(0x9, 4, 0, owned) == 0x9);
	assert(model_guarded_boss_block(0x7, 4, 0, owned) == 0x7);
	assert(model_guarded_boss_block(0x8, 5, 0, owned) == 0x8);
	// A rolled weapon reaches the block only through the draw filter, so it is
	// owned by construction; the guard's fail-closed precondition makes an
	// UNOWNED pass-through impossible (see test_unfiltered_roll_fails_closed).
	owned[AP_ItemsanityWeaponIndex(0x3)] = 1;
	assert(model_guarded_boss_block(0x3, 0, 0, owned) == 0x3);
}

// ==============================================================
// Gate 2: unowned missiles yield an owned catch-up weapon, never the missiles
// ==============================================================

static void test_unowned_missiles_step_down_the_ladder(void)
{
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

	// This is the reported defect: Ripper Roo, Mask drawn, Missile x3 never
	// unlocked. The rewrite must not hand it over.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(0x7)] = 1; // Mask, the draw
	owned[AP_ItemsanityWeaponIndex(0xa)] = 1; // Bomb x3, the next rung down
	assert(model_guarded_boss_block(0x7, 0, 0, owned) == 0xa);

	// One rung further: only single Missile owned.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(0x7)] = 1;
	owned[AP_ItemsanityWeaponIndex(0x2)] = 1;
	assert(model_guarded_boss_block(0x7, 0, 0, owned) == 0x2);

	// Bottom rung: only Bomb owned.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(0x8)] = 1;
	owned[AP_ItemsanityWeaponIndex(0x1)] = 1;
	assert(model_guarded_boss_block(0x8, 4, 0, owned) == 0x1);

	// Strongest owned rung wins, not the first one found by held id order.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(0x9)] = 1;
	owned[AP_ItemsanityWeaponIndex(0x1)] = 1;
	owned[AP_ItemsanityWeaponIndex(0x2)] = 1;
	owned[AP_ItemsanityWeaponIndex(0xa)] = 1;
	assert(model_guarded_boss_block(0x9, 0, 0, owned) == 0xa);

	// A weapon that is not on the ladder is never used as the catch-up item,
	// even when it is the only thing owned besides the draw. TNT, Beaker, Shield
	// and Turbo are all owned here and none of them may be selected.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(0x7)] = 1;
	owned[AP_ItemsanityWeaponIndex(0x0)] = 1;
	owned[AP_ItemsanityWeaponIndex(0x3)] = 1;
	owned[AP_ItemsanityWeaponIndex(0x4)] = 1;
	owned[AP_ItemsanityWeaponIndex(0x6)] = 1;
	assert(model_guarded_boss_block(0x7, 0, 0, owned) == 0x7);

	// Warpball is owned and is itself a replaced draw, so it is never the
	// substitute: handing it back would undo the rewrite, and the single-warpball
	// flag is claimed later in the settle chain, not here.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(0x9)] = 1;
	owned[AP_ItemsanityWeaponIndex(0x7)] = 1;
	assert(model_guarded_boss_block(0x7, 0, 0, owned) == 0x7);
	assert(model_guarded_boss_block(0x9, 0, 0, owned) == 0x9);
}

// The Komodo Joe cap is an input to the guard, not something applied after it:
// the ladder never climbs above the rung vanilla settled on.
static void test_komodo_joe_cap_is_never_undone(void)
{
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

	// Missile x3 and Bomb x3 owned, single Missile not: at Dragon Mines the cap
	// says one projectile, so the answer is Bomb, not either x3 stack.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(0x8)] = 1;
	owned[AP_ItemsanityWeaponIndex(0xa)] = 1;
	owned[AP_ItemsanityWeaponIndex(0xb)] = 1;
	owned[AP_ItemsanityWeaponIndex(0x1)] = 1;
	assert(model_guarded_boss_block(0x8, 0, 1, owned) == 0x1);
	// and away from Dragon Mines the same set escalates all the way up
	assert(model_guarded_boss_block(0x8, 0, 0, owned) == 0xb);

	// A natural Missile x3 draw at Dragon Mines is capped by vanilla to a single
	// Missile the player does not own. Nothing below that rung is owned either,
	// and handing the uncapped x3 back would undo the cap, so this is the ruled
	// Empty Crates shape instead.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(0xb)] = 1;
	assert(model_guarded_boss_block(0xb, 0, 1, owned) == AP_ITEMSANITY_NO_ITEM);
	// with Bomb owned there is a legal capped answer
	owned[AP_ItemsanityWeaponIndex(0x1)] = 1;
	assert(model_guarded_boss_block(0xb, 0, 1, owned) == 0x1);
}

// ==============================================================
// Gate 3: no owned alternative keeps the original roll
// ==============================================================

static void test_no_alternative_keeps_the_roll(void)
{
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};
	const int replaced[3] = {0x7, 0x8, 0x9};
	int i;

	// Each replaced draw owned on its own: no ladder rung is available, so the
	// draw survives untouched. Assistance is skipped, never faked.
	for (i = 0; i < 3; i++)
	{
		owned_clear(owned);
		owned[AP_ItemsanityWeaponIndex(replaced[i])] = 1;
		assert(model_guarded_boss_block(replaced[i], 0, 0, owned) == replaced[i]);
		assert(model_guarded_boss_block(replaced[i], 0, 1, owned) == replaced[i]);
	}

	// The no-item sentinel from an exhausted draw filter is not a weapon and is
	// not escalated into one.
	owned_clear(owned);
	for (i = 0; i < 9; i++)
		assert(model_guarded_boss_block(AP_ITEMSANITY_NO_ITEM, i, 0, owned) ==
		       AP_ITEMSANITY_NO_ITEM);
}

// ==============================================================
// Gate 4: the guard reads the same ownership source as the initial roll
// ==============================================================

// AP_ItemsanityFilterRoll and AP_ItemsanityBossAssist are both handed
// ap_itemsanity_owned in ap/ap_hooks.c, under the same AP_ItemsanityShouldFilter
// gate. What is checkable out of engine is the consequence: for one owned array,
// anything the boss guard emits is something the draw filter would also have
// accepted, and flipping a bit moves both verdicts together.
static void test_same_ownership_source_as_the_draw(void)
{
	static const unsigned char ladder[AP_ITEMSANITY_BOSS_ASSIST_LADDER_COUNT] =
		AP_ITEMSANITY_BOSS_ASSIST_LADDER;
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};
	const unsigned char table[] = {0, 1, 2, 3, 4, 7, 8, 9, 10, 11};
	const int tableCount = 10;
	unsigned mask;
	int rung;

	// Lockstep, rung by rung: flipping one ownership bit moves the draw filter's
	// verdict on that weapon and the boss guard's willingness to escalate to it
	// at the same time, because both read the same array.
	for (rung = 0; rung < AP_ITEMSANITY_BOSS_ASSIST_LADDER_COUNT; rung++)
	{
		int id = ladder[rung];

		owned_clear(owned);
		owned[AP_ItemsanityWeaponIndex(0x7)] = 1; // the Mask draw, and nothing else
		assert(AP_ItemsanitySubstituteRoll(id, 0, table, tableCount, owned) != id);
		assert(model_guarded_boss_block(0x7, 0, 0, owned) == 0x7);

		owned[AP_ItemsanityWeaponIndex(id)] = 1;
		assert(AP_ItemsanitySubstituteRoll(id, 0, table, tableCount, owned) == id);
		assert(model_guarded_boss_block(0x7, 0, 0, owned) == id);
	}

	// Same array, whole space: a weapon the boss guard hands out always passes
	// the draw filter's own ownership predicate. Only draws the filter itself
	// would have produced are fed in, which is the guard's real precondition.
	for (mask = 0; mask < OWNED_SET_COUNT; mask++)
	{
		int rolled;
		int fails;
		int dm;
		owned_from_mask(owned, mask);
		for (rolled = 0; rolled < ALL_HELD_COUNT; rolled++)
		{
			int in = allHeldIDs[rolled];
			if (in != AP_ITEMSANITY_NO_ITEM &&
			    AP_ItemsanitySubstituteRoll(in, 0, table, tableCount, owned) != in)
				continue;
			for (fails = 0; fails < 9; fails++)
				for (dm = 0; dm < 2; dm++)
				{
					int got = model_guarded_boss_block(in, fails, dm, owned);
					if (got == AP_ITEMSANITY_NO_ITEM)
						continue;
					assert(AP_ItemsanitySubstituteRoll(got, 0, table,
					                                   tableCount, owned) == got);
				}
		}
	}
}

// ==============================================================
// Whole-space invariants
// ==============================================================

static void test_exhaustive_invariants(void)
{
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT];
	unsigned mask;

	for (mask = 0; mask < OWNED_SET_COUNT; mask++)
	{
		int rolled;
		int fails;
		int dm;
		owned_from_mask(owned, mask);

		for (rolled = 0; rolled < ALL_HELD_COUNT; rolled++)
		{
			// The guard's precondition: `rolled` has already been through the
			// draw filter, so it is either an owned weapon or the sentinel.
			if (allHeldIDs[rolled] != AP_ITEMSANITY_NO_ITEM &&
			    !AP_ItemsanityRollAllowed(allHeldIDs[rolled], owned))
				continue;
			for (fails = 0; fails < 9; fails++)
				for (dm = 0; dm < 2; dm++)
				{
					int in = allHeldIDs[rolled];
					int proposed = model_vanilla_boss_block(in, fails, dm);
					int got = AP_ItemsanityBossAssistWeapon(in, proposed, owned);

					// deterministic: no RNG draw is consumed
					assert(AP_ItemsanityBossAssistWeapon(in, proposed, owned) == got);

					// the mechanic is preserved: a legal vanilla outcome is never
					// second-guessed
					if (AP_ItemsanityRollAllowed(proposed, owned))
						assert(got == proposed);

					// never a weapon the player does not own
					if (got != AP_ITEMSANITY_NO_ITEM &&
					    AP_ItemsanityWeaponIndex(
					        AP_ItemsanityCanonicalWeapon(got)) >= 0)
						assert(owned_holds(owned, got));

					// the outcome is always the vanilla one, a strictly weaker
					// ladder rung, the draw it started from, or the sentinel
					assert(got == proposed || got == in ||
					       got == AP_ITEMSANITY_NO_ITEM ||
					       (ladder_rung(got) > ladder_rung(proposed) &&
					        ladder_rung(proposed) >= 0));

					// the escalation never climbs above what vanilla settled on,
					// which is what keeps the Komodo Joe cap intact
					if (ladder_rung(got) >= 0 && ladder_rung(proposed) >= 0)
						assert(ladder_rung(got) >= ladder_rung(proposed));

					// a block that rewrote nothing is passed straight through
					if (proposed == in)
						assert(got == in);

					// Spring never leaks out: the draw filter canonicalises it
					// and this guard only ever returns ladder rungs or the draw
					assert(got != 0x5 || in == 0x5);
				}
		}
	}
}

// The guard is gated on exactly the predicate the draw filter is gated on, so a
// Crystal Challenge, a battle, an Arcade race or a bot driver never reaches it.
// Crystal Challenge is the one remaining ruled ownership exception, and this
// predicate is where it is enforced.
static void test_gate_matches_the_draw_filter(void)
{
	assert(AP_ItemsanityShouldFilter(1, 1, 1, 0, 0));  // Adventure race, local
	assert(!AP_ItemsanityShouldFilter(0, 1, 1, 0, 0)); // seed off / class absent
	assert(!AP_ItemsanityShouldFilter(1, 0, 1, 0, 0)); // bot or remote driver
	assert(!AP_ItemsanityShouldFilter(1, 1, 0, 0, 0)); // Arcade or VS
	assert(!AP_ItemsanityShouldFilter(1, 1, 1, 1, 0)); // battle
	assert(!AP_ItemsanityShouldFilter(1, 1, 1, 0, 1)); // ruled crystal exception
}

// The catch-up guard is a ladder, not a pool: given the same inputs it is
// deterministic and roll-independent, where the downstream cap substitution
// varies with the roll. Both refuse to hand out an unowned weapon, which is the
// property they share.
static void test_ladder_is_not_the_downstream_pool(void)
{
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};
	const unsigned char table[] = {0, 1, 2, 3, 4, 7, 8, 9, 10, 11};
	unsigned roll;
	int poolVaried = 0;
	int first;

	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(0x7)] = 1;
	owned[AP_ItemsanityWeaponIndex(0x3)] = 1;
	owned[AP_ItemsanityWeaponIndex(0x0)] = 1;

	first = AP_ItemsanitySubstituteDownstream(0xb, 0, table, 10, owned);
	for (roll = 0; roll < 64; roll++)
	{
		int pool = AP_ItemsanitySubstituteDownstream(0xb, roll, table, 10, owned);
		assert(owned_holds(owned, pool));
		if (pool != first)
			poolVaried = 1;
		// the boss guard ignores the roll entirely
		assert(model_guarded_boss_block(0x7, 0, 0, owned) == 0x7);
	}
	assert(poolVaried);
}

// A substituted catch-up weapon is an ordinary held item afterwards, so it mints
// its own itemsanity check pair rather than the one the rewrite aimed at.
static void test_substitute_mints_its_own_checks(void)
{
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};
	long plain;
	long juiced;
	int got;

	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(0x7)] = 1;
	owned[AP_ItemsanityWeaponIndex(0xa)] = 1;
	got = model_guarded_boss_block(0x7, 0, 0, owned);
	assert(got == 0xa);

	assert(AP_ItemsanityUseCodes(got, 10, &plain, &juiced) == 1);
	assert(plain == AP_ItemsanityLocationCode(0xa, 0));
	assert(juiced == AP_ItemsanityLocationCode(0xa, 1));
	assert(plain != AP_ItemsanityLocationCode(0xb, 0));
	assert(juiced != AP_ItemsanityLocationCode(0xb, 1));
}

// The ladder is the load-bearing statement of retail intent, so its shape is
// asserted rather than left implicit.
static void test_ladder_shape(void)
{
	static const unsigned char ladder[AP_ITEMSANITY_BOSS_ASSIST_LADDER_COUNT] =
		AP_ITEMSANITY_BOSS_ASSIST_LADDER;
	int i;
	int k;

	// strongest first, and exactly the two rewrite targets vanilla names plus
	// their weaker equivalents
	assert(ladder[0] == 0xb); // Missile x3, the catch-up target
	assert(ladder[1] == 0xa); // Bomb x3
	assert(ladder[2] == 0x2); // Missile, the Komodo Joe capped target
	assert(ladder[3] == 0x1); // Bomb

	for (i = 0; i < AP_ITEMSANITY_BOSS_ASSIST_LADDER_COUNT; i++)
	{
		// every rung is a real gated weapon, so ownership is checkable for it
		assert(AP_ItemsanityWeaponIndex(ladder[i]) >= 0);
		// no rung is a draw the block replaces
		assert(ladder[i] != 0x7 && ladder[i] != 0x8 && ladder[i] != 0x9);
		for (k = i + 1; k < AP_ITEMSANITY_BOSS_ASSIST_LADDER_COUNT; k++)
			assert(ladder[i] != ladder[k]);
	}
}

// Review wave, 2026-08-19: the guard's self-enforced precondition. A rolled
// weapon that never passed the draw filter fails closed to the sentinel
// instead of being handed back by the proposed==rolled or fallback paths.
static void test_unfiltered_roll_fails_closed(void)
{
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};
	owned_clear(owned);
	assert(AP_ItemsanityBossAssistWeapon(0xb, 0xb, owned) ==
	       AP_ITEMSANITY_NO_ITEM);
	assert(AP_ItemsanityBossAssistWeapon(0x9, 0xb, owned) ==
	       AP_ITEMSANITY_NO_ITEM);
	// The sentinel itself passes through untouched: FilterRoll already granted
	// its Wumpa, and the wrapper must not grant a second one.
	assert(AP_ItemsanityBossAssistWeapon(AP_ITEMSANITY_NO_ITEM,
	       AP_ITEMSANITY_NO_ITEM, owned) == AP_ITEMSANITY_NO_ITEM);
}

int main(void)
{
	test_ladder_shape();
	test_owned_missiles_still_rewrite();
	test_unowned_missiles_step_down_the_ladder();
	test_komodo_joe_cap_is_never_undone();
	test_no_alternative_keeps_the_roll();
	test_same_ownership_source_as_the_draw();
	test_gate_matches_the_draw_filter();
	test_ladder_is_not_the_downstream_pool();
	test_substitute_mints_its_own_checks();
	test_exhaustive_invariants();
	test_unfiltered_roll_fails_closed();
	puts("boss assist harness: all gates passed");
	return 0;
}
