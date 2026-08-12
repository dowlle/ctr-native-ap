// Focused host harness for #145 itemsanity. It links nothing from the engine:
// every rule it exercises lives in ap/ap_itemsanity_logic.h, so the runtime hook
// and this harness run the same code. The engine ORDER around those rules is
// mirrored here (see model_settle_item and model_circle_press) with the vanilla
// file:line each step comes from, because the two defects the first candidate
// shipped were both ordering defects, not rule defects.
//
// Build:
//   gcc -std=c99 -Wall -Wextra -Werror tools/test-itemsanity.c -o /tmp/test-itemsanity
//   /tmp/test-itemsanity

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ap/ap_itemsanity_logic.h"

#define ALL_WEAPON_IDS {0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11}

// ==============================================================
// Mirrors of the vanilla code the hooks sit in. Kept deliberately dumb and
// annotated, so a drift in the engine shows up as a harness lie rather than a
// silent pass.
// ==============================================================

// game/Vehicle/VehPickupItem.c:1142-1147 -- the shared Bomb/Missile fire branch.
// Bomb, Bomb x3 and Missile x3 all arrive at ShootNow as plain Missile. This is
// the exact rewrite that made the rejected head mint Missile checks for four
// different weapons.
static int model_vanilla_fire_id(int heldItemID)
{
	if (heldItemID == 1 || heldItemID == 10 || heldItemID == 11)
		return 2;
	return heldItemID;
}

// Model of the emitter's own guards (ap_hooks.c AP_EmitClassCheck): absent codes
// are dropped, and an already-checked code never re-sends. `liveActive` is the
// server location set, which is also itemsanity's authoritative on/off signal.
struct emitter
{
	long sent[64];
	int sentCount;
	int liveActive;
};

static int emitter_has(const struct emitter *e, long code)
{
	for (int i = 0; i < e->sentCount; i++)
		if (e->sent[i] == code)
			return 1;
	return 0;
}

static void emitter_emit(struct emitter *e, long code)
{
	if (code < 0)
		return; // location absent this seed
	if (!e->liveActive)
		return; // seed off / not connected: the class never resolves
	if (emitter_has(e, code))
		return; // already checked (re-fire, reload, duplicate press)
	assert(e->sentCount < (int)(sizeof e->sent / sizeof e->sent[0]));
	e->sent[e->sentCount++] = code;
}

// game/Vehicle/VehPickupItem.c:1122-1160 -- one player circle press, in engine
// order. VehPhysProc has already spent the item before the fire request reaches
// here (VehPhysProc.c:530-566), the request flag is cleared first, and the hook
// then reads d->heldItemID BEFORE the fold above. `fired` receives the id the
// engine actually shoots with, to keep the two visibly separate.
static void model_circle_press(struct emitter *e, int isLocal, int heldItemID,
	int numWumpas, int *fired)
{
	long plain;
	long juiced;

	if (isLocal) // ap_hooks.c: driver == gGT->drivers[0]
	{
		(void)AP_ItemsanityUseCodes(heldItemID, numWumpas, &plain, &juiced);
		emitter_emit(e, plain);
		emitter_emit(e, juiced);
	}
	*fired = model_vanilla_fire_id(heldItemID);
}

// game/Vehicle/VehPhysGeneral.c:988-1145 -- the settle chain after the roulette
// draw, in engine order, with the AP hooks at exactly the sites they occupy in
// the source. bossFails < 0 means "not a boss race".
struct settle_ctx
{
	int filterActive;      // AP_ItemsanityShouldFilter result
	int bossFails;         // <0 = not a boss race
	int isDragonMines;
	int warpballAlreadyHeld;
	int holdersOf3Missiles;
	int numPlayers;
	int wumpaGrants;       // ruled Empty Crates fallback, counted
};

static int settle_ap_downstream(struct settle_ctx *ctx, int proposed,
	unsigned roll, const unsigned char *table, int tableCount,
	const unsigned char *owned)
{
	int filtered;
	if (!ctx->filterActive)
		return proposed;
	filtered = AP_ItemsanitySubstituteDownstream(proposed, roll, table,
	                                            tableCount, owned);
	if (filtered == AP_ITEMSANITY_NO_ITEM)
		ctx->wumpaGrants++;
	return filtered;
}

static int model_settle_item(struct settle_ctx *ctx, int rolled, unsigned roll,
	const unsigned char *table, int tableCount, const unsigned char *owned)
{
	int held = rolled;

	// :1000-1017 the draw plus the AP receive filter
	if (ctx->filterActive)
	{
		held = AP_ItemsanitySubstituteRoll(held, roll, table, tableCount, owned);
		if (held == AP_ITEMSANITY_NO_ITEM)
			ctx->wumpaGrants++;
	}

	// :1044-1080 boss-race rewrite. RULED BYPASS: no AP hook here.
	if (ctx->bossFails >= 0)
	{
		if (ctx->bossFails < 3 && (unsigned)held - 0x7 < 0x3)
			held = 0xb;
		else if (ctx->bossFails < 4 && (unsigned)held - 0x7 < 0x2)
			held = 0xb;
		else if (ctx->bossFails < 5 && held == 0x8)
			held = 0xb;
		if (ctx->isDragonMines && held == 0xb)
			held = 0x2;
	}

	// :1083-1087 Spring rewrite. Ownership-safe already: the filter canonicalises
	// Spring to Turbo, so a surviving 5 means Turbo was received.
	if (held == 0x5)
		held = 0x0;

	// :1089-1113 single-warpball rule. ORDINARY substitution, AP-gated.
	if (held == 0x9)
	{
		if (!ctx->warpballAlreadyHeld)
			ctx->warpballAlreadyHeld = 1;
		else
		{
			held = 0xb;
			held = settle_ap_downstream(ctx, held, roll, table, tableCount, owned);
		}
	}

	// :1115-1145 two-holders 3-missile cap. ORDINARY substitution, AP-gated.
	if (held == 0xb && ctx->numPlayers > 2)
	{
		if (ctx->holdersOf3Missiles < 2)
			ctx->holdersOf3Missiles++;
		else
		{
			held = 0x2;
			held = settle_ap_downstream(ctx, held, roll, table, tableCount, owned);
		}
	}

	return held;
}

// Crystal Challenge hardcode, game/Vehicle/VehPhysGeneral.c:1023-1033.
// RULED BYPASS: the filter is inactive for Crystal Challenge, so this is vanilla.
static int model_crystal_item(int isSkullRockOrRampage)
{
	return isSkullRockOrRampage ? 0x0 : 0x1;
}

static void owned_clear(unsigned char *owned)
{
	memset(owned, 0, AP_ITEMSANITY_WEAPON_COUNT);
}

static int owned_holds(const unsigned char *owned, int heldItemID)
{
	int index = AP_ItemsanityWeaponIndex(AP_ItemsanityCanonicalWeapon(heldItemID));
	return index >= 0 && owned[index];
}

// ==============================================================
// Frozen mapping and fan-out
// ==============================================================

static void test_frozen_classification(void)
{
	const int held[AP_ITEMSANITY_WEAPON_COUNT] = ALL_WEAPON_IDS;
	for (int i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
	{
		assert(AP_ItemsanityWeaponIndex(held[i]) == i);
		assert(AP_ItemsanityLocationCode(held[i], 0) == 35016000L + i * 2);
		assert(AP_ItemsanityLocationCode(held[i], 1) == 35016001L + i * 2);
	}
	assert(AP_ItemsanityWeaponIndex(5) == -1);
	assert(AP_ItemsanityWeaponIndex(12) == -1);
	assert(AP_ItemsanityWeaponIndex(13) == -1);
	assert(AP_ItemsanityLocationCode(5, 0) == -1);
	assert(AP_ItemsanityLocationCode(12, 1) == -1);
	assert(AP_ItemsanityLocationCode(13, 0) == -1);
}

static void test_use_code_fanout(void)
{
	const int held[AP_ITEMSANITY_WEAPON_COUNT] = ALL_WEAPON_IDS;
	const int mintsNothing[3] = {5, 12, 13};
	long plain;
	long juiced;

	for (int i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
	{
		// below the juice threshold: plain only, on every count from 0 to 9
		for (int wumpas = 0; wumpas < 10; wumpas++)
		{
			assert(AP_ItemsanityUseCodes(held[i], wumpas, &plain, &juiced) == 0);
			assert(plain == 35016000L + i * 2);
			assert(juiced == -1);
		}
		// at and above 10: the pair
		for (int wumpas = 10; wumpas <= 12; wumpas++)
		{
			assert(AP_ItemsanityUseCodes(held[i], wumpas, &plain, &juiced) == 1);
			assert(plain == 35016000L + i * 2);
			assert(juiced == 35016001L + i * 2);
		}
	}

	// Spring and the two battle-only ids mint nothing at any wumpa count
	for (int i = 0; i < 3; i++)
		for (int wumpas = 0; wumpas <= 12; wumpas++)
		{
			AP_ItemsanityUseCodes(mintsNothing[i], wumpas, &plain, &juiced);
			assert(plain == -1);
			assert(juiced == -1);
		}
}

// ==============================================================
// Defect 1: circle press to firing path
// ==============================================================

// The regression the rejected head shipped: reading the fire-time weapon id
// collapses four weapons onto Missile's two codes.
static void test_fire_id_is_not_an_identity_source(void)
{
	const int folded[3] = {1, 10, 11};
	const int held[AP_ITEMSANITY_WEAPON_COUNT] = ALL_WEAPON_IDS;

	for (int i = 0; i < 3; i++)
	{
		assert(model_vanilla_fire_id(folded[i]) == 2);
		assert(AP_ItemsanityLocationCode(model_vanilla_fire_id(folded[i]), 0) ==
		       AP_ItemsanityLocationCode(2, 0));
		// the true identity must NOT be the missile pair
		assert(AP_ItemsanityLocationCode(folded[i], 0) !=
		       AP_ItemsanityLocationCode(2, 0));
		assert(AP_ItemsanityLocationCode(folded[i], 1) !=
		       AP_ItemsanityLocationCode(2, 1));
	}
	// every other weapon reaches ShootNow unfolded
	for (int i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
		if (held[i] != 1 && held[i] != 10 && held[i] != 11)
			assert(model_vanilla_fire_id(held[i]) == held[i]);
}

static void test_press_path_all_ids_plain_and_juiced(void)
{
	const int held[AP_ITEMSANITY_WEAPON_COUNT] = ALL_WEAPON_IDS;
	const int shared[4] = {1, 2, 10, 11};
	struct emitter e;
	int fired = -1;

	// plain only, one press per weapon on a fresh emitter
	for (int i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
	{
		memset(&e, 0, sizeof e);
		e.liveActive = 1;
		model_circle_press(&e, 1, held[i], 0, &fired);
		assert(e.sentCount == 1);
		assert(e.sent[0] == 35016000L + i * 2);
		assert(fired == model_vanilla_fire_id(held[i]));
	}

	// juiced: the pair, and the plain code is still the weapon's own
	for (int i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
	{
		memset(&e, 0, sizeof e);
		e.liveActive = 1;
		model_circle_press(&e, 1, held[i], 10, &fired);
		assert(e.sentCount == 2);
		assert(e.sent[0] == 35016000L + i * 2);
		assert(e.sent[1] == 35016001L + i * 2);
	}

	// the four weapons that share the Missile fire branch keep four distinct
	// pairs across one session
	memset(&e, 0, sizeof e);
	e.liveActive = 1;
	for (int i = 0; i < 4; i++)
		model_circle_press(&e, 1, shared[i], 10, &fired);
	assert(e.sentCount == 8);
	for (int i = 0; i < e.sentCount; i++)
		for (int k = i + 1; k < e.sentCount; k++)
			assert(e.sent[i] != e.sent[k]);
	for (int i = 0; i < 4; i++)
	{
		assert(emitter_has(&e, AP_ItemsanityLocationCode(shared[i], 0)));
		assert(emitter_has(&e, AP_ItemsanityLocationCode(shared[i], 1)));
	}
}

static void test_press_emits_exactly_once(void)
{
	const int mintsNothing[3] = {5, 12, 13};
	struct emitter e;
	int fired = -1;

	// duplicate use of the same weapon: one pair, no re-send
	memset(&e, 0, sizeof e);
	e.liveActive = 1;
	for (int press = 0; press < 8; press++)
		model_circle_press(&e, 1, 10, 12, &fired);
	assert(e.sentCount == 2);
	assert(e.sent[0] == AP_ItemsanityLocationCode(10, 0));
	assert(e.sent[1] == AP_ItemsanityLocationCode(10, 1));

	// plain first at low wumpa, juiced later once the player is juiced
	memset(&e, 0, sizeof e);
	e.liveActive = 1;
	model_circle_press(&e, 1, 9, 3, &fired);
	assert(e.sentCount == 1);
	model_circle_press(&e, 1, 9, 10, &fired);
	assert(e.sentCount == 2);
	assert(e.sent[0] == AP_ItemsanityLocationCode(9, 0));
	assert(e.sent[1] == AP_ItemsanityLocationCode(9, 1));
	model_circle_press(&e, 1, 9, 11, &fired);
	assert(e.sentCount == 2);

	// Spring and the battle-only ids never emit, however often they are pressed
	memset(&e, 0, sizeof e);
	e.liveActive = 1;
	for (int i = 0; i < 3; i++)
		for (int press = 0; press < 3; press++)
			model_circle_press(&e, 1, mintsNothing[i], 12, &fired);
	assert(e.sentCount == 0);

	// a bot / remote driver press emits nothing, and still fires normally
	memset(&e, 0, sizeof e);
	e.liveActive = 1;
	model_circle_press(&e, 0, 11, 12, &fired);
	assert(e.sentCount == 0);
	assert(fired == 2);

	// seed off / class absent: the live location set is the authoritative toggle
	memset(&e, 0, sizeof e);
	e.liveActive = 0;
	model_circle_press(&e, 1, 3, 12, &fired);
	assert(e.sentCount == 0);
}

// ==============================================================
// Defect 2: draw filter and every downstream substitution
// ==============================================================

static void test_roll_then_substitute(void)
{
	const unsigned char table[] = {0, 0, 1, 2, 2, 2, 5, 7, 11};
	const int all[AP_ITEMSANITY_WEAPON_COUNT] = ALL_WEAPON_IDS;
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

	owned[2] = 1; // Missile only
	assert(AP_ItemsanitySubstituteRoll(2, 77, table, 9, owned) == 2);
	for (unsigned roll = 0; roll < 1000; roll++)
		assert(AP_ItemsanityCanonicalWeapon(
			AP_ItemsanitySubstituteRoll(1, roll, table, 9, owned)) == 2);

	owned[0] = 1; // Turbo plus Missile; Spring table entries count as Turbo
	for (unsigned roll = 0; roll < 1000; roll++)
	{
		int got = AP_ItemsanityCanonicalWeapon(
			AP_ItemsanitySubstituteRoll(7, roll, table, 9, owned));
		assert(got == 0 || got == 2);
	}

	// deterministic: the same roll always resolves the same way, no extra draw
	for (unsigned roll = 0; roll < 1000; roll++)
		assert(AP_ItemsanitySubstituteRoll(7, roll, table, 9, owned) ==
		       AP_ItemsanitySubstituteRoll(7, roll, table, 9, owned));

	// exhaustion
	owned_clear(owned);
	assert(AP_ItemsanitySubstituteRoll(1, 42, table, 9, owned) == AP_ITEMSANITY_NO_ITEM);

	// sparse owned sets: every single-weapon set, over the whole table
	for (int only = 0; only < AP_ITEMSANITY_WEAPON_COUNT; only++)
	{
		owned_clear(owned);
		owned[only] = 1;
		for (int t = 0; t < 9; t++)
			for (unsigned roll = 0; roll < 64; roll++)
			{
				int got = AP_ItemsanitySubstituteRoll(table[t], roll, table, 9, owned);
				if (got == AP_ITEMSANITY_NO_ITEM)
				{
					// only legitimate when the table holds nothing received
					int any = 0;
					for (int k = 0; k < 9; k++)
						if (owned_holds(owned, table[k]))
							any = 1;
					assert(!any);
					continue;
				}
				assert(owned_holds(owned, got));
				assert(AP_ItemsanityCanonicalWeapon(got) == all[only]);
			}
	}
}

static void test_downstream_warpball_substitution(void)
{
	// A rank-2 style table holding the interesting entries.
	const unsigned char table[] = {0, 1, 2, 3, 4, 7, 8, 9, 10, 11};
	const int tableCount = 10;
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

	// Received Warpball, someone else already holds one: vanilla hands 3
	// Missiles. Missile x3 is NOT received, so the substitution must move off it.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(9)] = 1;  // Warpball
	owned[AP_ItemsanityWeaponIndex(3)] = 1;  // TNT
	for (unsigned roll = 0; roll < 256; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 1, 0, 4, 0};
		int got = model_settle_item(&ctx, 9, roll, table, tableCount, owned);
		assert(got != 0xb);            // never the unreceived cap item
		assert(got != 0x9);            // never a second warpball
		assert(owned_holds(owned, got));
		assert(got == 0x3);            // TNT is the only eligible entry left
		assert(ctx.wumpaGrants == 0);
	}

	// Missile x3 received: vanilla's own substitution is already legal, so it
	// must survive untouched.
	owned[AP_ItemsanityWeaponIndex(11)] = 1;
	for (unsigned roll = 0; roll < 256; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 1, 0, 2, 0};
		int got = model_settle_item(&ctx, 9, roll, table, tableCount, owned);
		assert(got == 0xb);
		assert(ctx.wumpaGrants == 0);
	}

	// Warpball is free: vanilla keeps it and claims the flag, no substitution.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(9)] = 1;
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 0, 4, 0};
		int got = model_settle_item(&ctx, 9, 5, table, tableCount, owned);
		assert(got == 0x9);
		assert(ctx.warpballAlreadyHeld == 1);
		assert(ctx.wumpaGrants == 0);
	}

	// Warpball received and nothing else: the cap has no eligible replacement,
	// so the ruled Empty Crates fallback applies instead of an unreceived weapon.
	for (unsigned roll = 0; roll < 64; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 1, 0, 4, 0};
		int got = model_settle_item(&ctx, 9, roll, table, tableCount, owned);
		assert(got == AP_ITEMSANITY_NO_ITEM);
		assert(ctx.wumpaGrants == 1);
	}

	// Filter inactive: vanilla behaviour, unreceived cap item and all.
	owned_clear(owned);
	{
		struct settle_ctx ctx = {0, -1, 0, 1, 0, 4, 0};
		assert(model_settle_item(&ctx, 9, 7, table, tableCount, owned) == 0xb);
		assert(ctx.wumpaGrants == 0);
	}
}

static void test_downstream_missile_cap_substitution(void)
{
	const unsigned char table[] = {0, 1, 2, 3, 4, 7, 8, 9, 10, 11};
	const int tableCount = 10;
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

	// Missile x3 received, two other drivers already hold 3 missiles: vanilla
	// downgrades to a single Missile, which was never received here.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(11)] = 1; // Missile x3
	owned[AP_ItemsanityWeaponIndex(7)] = 1;  // Mask
	for (unsigned roll = 0; roll < 256; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 2, 4, 0};
		int got = model_settle_item(&ctx, 11, roll, table, tableCount, owned);
		assert(got != 0x2);            // never the unreceived downgrade
		assert(got != 0xb);            // never back to 3 missiles
		assert(got == 0x7);
		assert(owned_holds(owned, got));
		assert(ctx.holdersOf3Missiles == 2);
	}

	// Missile received as well: vanilla's downgrade is legal and stands.
	owned[AP_ItemsanityWeaponIndex(2)] = 1;
	for (unsigned roll = 0; roll < 256; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 2, 4, 0};
		assert(model_settle_item(&ctx, 11, roll, table, tableCount, owned) == 0x2);
	}

	// Under the cap: vanilla keeps 3 Missiles and counts the holder.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(11)] = 1;
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 1, 4, 0};
		assert(model_settle_item(&ctx, 11, 3, table, tableCount, owned) == 0xb);
		assert(ctx.holdersOf3Missiles == 2);
	}

	// Two-player race: the cap does not apply at all.
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 2, 2, 0};
		assert(model_settle_item(&ctx, 11, 3, table, tableCount, owned) == 0xb);
	}

	// Nothing else received: ruled Empty Crates fallback, not a free Missile.
	for (unsigned roll = 0; roll < 64; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 2, 4, 0};
		assert(model_settle_item(&ctx, 11, roll, table, tableCount, owned) ==
		       AP_ITEMSANITY_NO_ITEM);
		assert(ctx.wumpaGrants == 1);
	}

	// Warpball received and in the table: the cap's substitute must still never
	// be a Warpball. The single-warpball rule has already run by this point and
	// would not claim WARPBALL_HELD for an item handed out here, so issuing one
	// would put two in play.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(11)] = 1;
	owned[AP_ItemsanityWeaponIndex(9)] = 1;
	for (unsigned roll = 0; roll < 256; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 2, 4, 0};
		int got = model_settle_item(&ctx, 11, roll, table, tableCount, owned);
		assert(got != 0x9);
		assert(got == AP_ITEMSANITY_NO_ITEM);
		assert(ctx.warpballAlreadyHeld == 0);
	}
	// with a third received weapon the substitution lands there instead
	owned[AP_ItemsanityWeaponIndex(0)] = 1;
	for (unsigned roll = 0; roll < 256; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 2, 4, 0};
		int got = model_settle_item(&ctx, 11, roll, table, tableCount, owned);
		assert(got == 0x0);
	}
}

// The draw filter runs upstream of both caps, so a substitution there may yield
// Warpball or Missile x3 and must then meet the caps' own bookkeeping.
static void test_draw_substitution_still_meets_the_caps(void)
{
	const unsigned char table[] = {0, 2, 9, 11};
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

	// Only Warpball received, nobody holding one: the substituted Warpball is
	// kept and claims the flag exactly like a natural roll.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(9)] = 1;
	for (unsigned roll = 0; roll < 64; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 0, 4, 0};
		assert(model_settle_item(&ctx, 2, roll, table, 4, owned) == 0x9);
		assert(ctx.warpballAlreadyHeld == 1);
	}

	// Only Missile x3 received, one other holder: substituted, kept, counted.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(11)] = 1;
	for (unsigned roll = 0; roll < 64; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 1, 4, 0};
		assert(model_settle_item(&ctx, 2, roll, table, 4, owned) == 0xb);
		assert(ctx.holdersOf3Missiles == 2);
	}

	// Same, but the cap is already full: the substituted Missile x3 is downgraded
	// and the downgrade itself is ownership-filtered, so nothing unreceived lands.
	for (unsigned roll = 0; roll < 64; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 2, 4, 0};
		assert(model_settle_item(&ctx, 2, roll, table, 4, owned) ==
		       AP_ITEMSANITY_NO_ITEM);
		assert(ctx.wumpaGrants == 1);
	}
}

// Warpball cap feeding the missile cap in the same settle pass.
static void test_downstream_chained_substitutions(void)
{
	const unsigned char table[] = {0, 2, 3, 9, 11};
	const int tableCount = 5;
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

	// Warpball -> 3 Missiles (received, so it stands) -> cap -> Missile
	// (not received) -> must land on a received weapon.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(9)] = 1;
	owned[AP_ItemsanityWeaponIndex(11)] = 1;
	owned[AP_ItemsanityWeaponIndex(3)] = 1;
	for (unsigned roll = 0; roll < 256; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 1, 2, 4, 0};
		int got = model_settle_item(&ctx, 9, roll, table, tableCount, owned);
		assert(owned_holds(owned, got));
		assert(got != 0x2);
		assert(got != 0x9);
		assert(got == 0x3);
	}
}

// Spring can never leak out of a downstream substitution: vanilla's own Spring
// rewrite has already run by then, so the selection returns canonical ids.
static void test_downstream_never_returns_spring(void)
{
	const unsigned char table[] = {5, 5, 5, 9};
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(0)] = 1; // Turbo, which Spring canonicalises to
	owned[AP_ItemsanityWeaponIndex(9)] = 1;
	for (unsigned roll = 0; roll < 256; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 1, 0, 4, 0};
		int got = model_settle_item(&ctx, 9, roll, table, 4, owned);
		assert(got == 0x0);
		assert(got != 0x5);
	}
	// and a Spring draw still rides through vanilla's own rewrite
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 0, 4, 0};
		assert(model_settle_item(&ctx, 5, 1, table, 4, owned) == 0x0);
	}
}

// The undecided-rank roll has no weighted table in scope. Ownership still holds.
static void test_downstream_without_a_table(void)
{
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(4)] = 1; // Beaker only
	for (unsigned roll = 0; roll < 64; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 1, 0, 4, 0};
		assert(model_settle_item(&ctx, 9, roll, 0, 0, owned) == 0x4);
	}
	owned_clear(owned);
	for (unsigned roll = 0; roll < 8; roll++)
	{
		struct settle_ctx ctx = {1, -1, 0, 1, 0, 4, 0};
		assert(model_settle_item(&ctx, 9, roll, 0, 0, owned) == AP_ITEMSANITY_NO_ITEM);
	}
}

// ==============================================================
// The two ruled bypasses stay bypasses
// ==============================================================

static void test_ruled_boss_and_crystal_overrides(void)
{
	const unsigned char table[] = {0, 2, 7, 8, 9};
	const int tableCount = 5;
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

	// Boss race, nothing received but Mask: vanilla rewrites Mask/Clock/Warpball
	// to 3 Missiles regardless of ownership. Ruled over-permissive slack, kept.
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(7)] = 1;
	{
		struct settle_ctx ctx = {1, 0, 0, 0, 0, 2, 0};
		assert(model_settle_item(&ctx, 7, 11, table, tableCount, owned) == 0xb);
		assert(ctx.wumpaGrants == 0);
	}
	// Clock at 4 losses, still rewritten
	owned_clear(owned);
	owned[AP_ItemsanityWeaponIndex(8)] = 1;
	{
		struct settle_ctx ctx = {1, 4, 0, 0, 0, 2, 0};
		assert(model_settle_item(&ctx, 8, 11, table, tableCount, owned) == 0xb);
	}
	// Komodo Joe: 3 Missiles become 1 Missile, also unfiltered
	{
		struct settle_ctx ctx = {1, 0, 1, 0, 0, 2, 0};
		assert(model_settle_item(&ctx, 8, 11, table, tableCount, owned) == 0x2);
	}
	// Crystal Challenge hardcode: vanilla item by level, filter inactive
	assert(model_crystal_item(0) == 0x1);
	assert(model_crystal_item(1) == 0x0);
}

// ==============================================================
// Policy, receipts and session lifecycle
// ==============================================================

static void test_inactive_and_vanilla_policy(void)
{
	const unsigned char table[] = {0, 2, 9, 11};
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

	assert(!AP_ItemsanityShouldFilter(0, 1, 1, 0, 0)); // absent/inactive seed
	assert(!AP_ItemsanityShouldFilter(1, 0, 1, 0, 0)); // bot/nonlocal driver
	assert(!AP_ItemsanityShouldFilter(1, 1, 0, 0, 0)); // non-Adventure mode
	assert(!AP_ItemsanityShouldFilter(1, 1, 1, 1, 0)); // battle stays vanilla
	assert(!AP_ItemsanityShouldFilter(1, 1, 1, 0, 1)); // ruled crystal override
	assert(AP_ItemsanityShouldFilter(1, 1, 1, 0, 0));

	// the same policy gates the downstream substitution, so an Arcade or VS
	// settle pass keeps vanilla's caps untouched
	owned_clear(owned);
	{
		struct settle_ctx ctx = {0, -1, 0, 1, 2, 4, 0};
		assert(model_settle_item(&ctx, 9, 3, table, 4, owned) == 0x2);
		assert(ctx.wumpaGrants == 0);
	}
}

static void test_duplicate_receipts_reconnect_and_slot_switch(void)
{
	const unsigned char table[] = {0, 2, 9, 11};
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

	owned[4] = 1;
	owned[4] = 1; // duplicate receipt stays boolean
	assert(owned[4] == 1);

	// reconnect / slot switch: the set is cleared before the authoritative
	// ReceivedItems replay rebuilds it, so a stale slot's unlocks cannot leak
	owned[9] = 1;
	owned_clear(owned);
	for (int i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
		assert(owned[i] == 0);

	// a cleared set gates everything again until the replay lands
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 0, 4, 0};
		assert(model_settle_item(&ctx, 2, 5, table, 4, owned) == AP_ITEMSANITY_NO_ITEM);
		assert(ctx.wumpaGrants == 1);
	}

	// authoritative replay: Turbo, which this table can actually roll
	owned[AP_ItemsanityWeaponIndex(0)] = 1;
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 0, 4, 0};
		assert(model_settle_item(&ctx, 2, 5, table, 4, owned) == 0x0);
	}
	// a weapon received but absent from this rank's table is still no substitute:
	// eligibility is table membership AND receipt, never receipt alone
	owned[4] = 1;
	assert(owned[4] == 1);
	{
		struct settle_ctx ctx = {1, -1, 0, 0, 0, 4, 0};
		assert(model_settle_item(&ctx, 2, 5, table, 4, owned) == 0x0);
	}
}

// The 22 codes stay one contiguous, collision-free block: this is what the
// connect-time scout list and the peer sent-item feed resolve against.
static void test_live_location_block_and_feed_membership(void)
{
	const int held[AP_ITEMSANITY_WEAPON_COUNT] = ALL_WEAPON_IDS;
	long codes[22];
	int n = 0;

	for (int i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
	{
		codes[n++] = AP_ItemsanityLocationCode(held[i], 0);
		codes[n++] = AP_ItemsanityLocationCode(held[i], 1);
	}
	assert(n == 22);
	for (int i = 0; i < n; i++)
	{
		assert(codes[i] >= 35016000L && codes[i] <= 35016021L);
		for (int k = i + 1; k < n; k++)
			assert(codes[i] != codes[k]);
	}
	// the scout list walks 35016000..35016021 in order; every code in that span
	// must be one of the frozen 22, so no foreign code rides the class feed
	for (long code = 35016000L; code <= 35016021L; code++)
	{
		int found = 0;
		for (int i = 0; i < n; i++)
			if (codes[i] == code)
				found = 1;
		assert(found);
	}
}

int main(void)
{
	test_frozen_classification();
	test_use_code_fanout();
	test_fire_id_is_not_an_identity_source();
	test_press_path_all_ids_plain_and_juiced();
	test_press_emits_exactly_once();
	test_roll_then_substitute();
	test_downstream_warpball_substitution();
	test_downstream_missile_cap_substitution();
	test_draw_substitution_still_meets_the_caps();
	test_downstream_chained_substitutions();
	test_downstream_never_returns_spring();
	test_downstream_without_a_table();
	test_ruled_boss_and_crystal_overrides();
	test_inactive_and_vanilla_policy();
	test_duplicate_receipts_reconnect_and_slot_switch();
	test_live_location_block_and_feed_membership();
	puts("itemsanity harness: all gates passed");
	return 0;
}
