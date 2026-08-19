#ifndef AP_ITEMSANITY_LOGIC_H
#define AP_ITEMSANITY_LOGIC_H

// Pure, engine-independent itemsanity rules. Kept in a header so the runtime
// hook and the focused host harness exercise the exact same classification and
// roll-then-substitute algorithm.

#define AP_ITEMSANITY_WEAPON_COUNT 11
#define AP_ITEMSANITY_NO_ITEM 0xF

// Frozen 0.2.0 datapackage append: the weapon items occupy apworld item indexes
// 95..105 in held ID order (0..4, 6..11). It lives here rather than beside the
// receive path in ap_hooks.c so the reward-display policy can classify a weapon
// unlock (#219) without pulling in the runtime.
#define AP_ITEMSANITY_ITEM_FIRST_INDEX 95

static int AP_ItemsanityWeaponIndex(int heldItemID)
{
	if (heldItemID >= 0 && heldItemID <= 4)
		return heldItemID;
	if (heldItemID >= 6 && heldItemID <= 11)
		return heldItemID - 1;
	return -1;
}

static int AP_ItemsanityCanonicalWeapon(int heldItemID)
{
	// Spring is rewritten to Turbo by vanilla immediately after the roll.
	return heldItemID == 5 ? 0 : heldItemID;
}

static long AP_ItemsanityLocationCode(int heldItemID, int juiced)
{
	int index = AP_ItemsanityWeaponIndex(heldItemID);
	return index < 0 ? -1 : 35016000L + index * 2 + (juiced != 0);
}

// CONSTRAINT: `!isCrystal` is the single enforcement point for the ruled Crystal
// Challenge ownership exception. Every itemsanity filter, including the boss
// catch-up guard, is gated on this predicate, so the arena hardcode in
// VehPhysGeneral_SetHeldItem keeps granting its Bomb or Turbo regardless of the
// received set. A Crystal Challenge is unwinnable without that item, so gating
// it would lock the check behind itself.
static int AP_ItemsanityShouldFilter(int featureActive, int isLocal,
	int isAdventure, int isBattle, int isCrystal)
{
	return featureActive && isLocal && isAdventure && !isBattle && !isCrystal;
}

// Check fan-out for one committed weapon use. `heldItemID` must be the id the
// player actually held. It is NOT the id the fire path receives: the shared
// Bomb/Missile code in VehPickupItem_ShootOnCirclePress rewrites 1 (Bomb),
// 10 (Bomb x3) and 11 (Missile x3) to 2 (Missile) before ShootNow runs, so a
// hook reading the fire-time weapon id would mint Missile checks for four
// different weapons. Writes -1 for an id that mints nothing (Spring and the two
// battle-only ids). Returns 1 when the juiced companion is earned as well.
static int AP_ItemsanityUseCodes(int heldItemID, int numWumpas,
	long *plain, long *juiced)
{
	int isJuiced = numWumpas >= 10;
	*plain = AP_ItemsanityLocationCode(heldItemID, 0);
	*juiced = isJuiced ? AP_ItemsanityLocationCode(heldItemID, 1) : -1;
	return isJuiced;
}

// True when `heldItemID` needs no ownership substitution: either it is not one
// of the 11 gated weapons at all (nothing to gate, so vanilla wins) or the
// player has received it.
static int AP_ItemsanityRollAllowed(int heldItemID,
	const unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT])
{
	int index = AP_ItemsanityWeaponIndex(AP_ItemsanityCanonicalWeapon(heldItemID));
	return index < 0 || owned[index] != 0;
}

// Held-id bit, for the substitution exclusion masks below.
#define AP_ITEMSANITY_ID_BIT(id) (1u << (id))

// Vanilla keeps two engine-wide invariants by rewriting an already-settled item:
// at most one Warpball in play (WARPBALL_HELD), and at most two drivers holding
// 3 Missiles (numPlayersWith3Missiles). A substitution that could hand either of
// those back would break the very rule it is standing inside, and neither cap
// site does the flag/counter bookkeeping for a fresh grant. Both stay out of
// every downstream substitution pool.
#define AP_ITEMSANITY_CAPPED_IDS \
	(AP_ITEMSANITY_ID_BIT(0x9) | AP_ITEMSANITY_ID_BIT(0xb))

// Shared eligibility walk. Selects the roll-th received entry of the same
// rank-weighted table, skipping any canonical id whose bit is set in
// `excludedMask` (0 excludes nothing). Reusing the original roll value keeps the
// choice deterministic and consumes no extra RNG draw. Callers with no weighted
// table in scope (the undecided-rank roll) fall back to the frozen 11-weapon
// list, which still preserves ownership. Returns the canonical id, so a Spring
// table entry resolves to Turbo directly: downstream substitutions run after
// vanilla's own Spring rewrite, so returning the raw entry there would leave a
// live Spring in Adventure. Returns the no-item sentinel when nothing is
// eligible.
static int AP_ItemsanitySelectOwned(unsigned roll, unsigned excludedMask,
	const unsigned char *table, int tableCount,
	const unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT])
{
	static const unsigned char allWeapons[AP_ITEMSANITY_WEAPON_COUNT] =
		{0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11};
	int eligible = 0;
	int selected;
	int canonical;
	int index;
	int i;

	if (table == 0 || tableCount <= 0)
	{
		table = allWeapons;
		tableCount = AP_ITEMSANITY_WEAPON_COUNT;
	}

	for (i = 0; i < tableCount; i++)
	{
		canonical = AP_ItemsanityCanonicalWeapon(table[i]);
		index = AP_ItemsanityWeaponIndex(canonical);
		if (index >= 0 && owned[index] &&
		    !(excludedMask & AP_ITEMSANITY_ID_BIT(canonical)))
			eligible++;
	}
	if (eligible == 0)
		return AP_ITEMSANITY_NO_ITEM;

	selected = (int)(roll % (unsigned)eligible);
	for (i = 0; i < tableCount; i++)
	{
		canonical = AP_ItemsanityCanonicalWeapon(table[i]);
		index = AP_ItemsanityWeaponIndex(canonical);
		if (index >= 0 && owned[index] &&
		    !(excludedMask & AP_ITEMSANITY_ID_BIT(canonical)) && selected-- == 0)
			return canonical;
	}
	return AP_ITEMSANITY_NO_ITEM;
}

// Substitution pool for a filtered roll: the FULL weapon list, never the
// position-weighted table the roll came from.
//
// Ruled 2026-08-17. Substituting only from the rank-weighted table left an
// unlocked weapon unusable purely because of race position: vanilla offers Mask
// in back-of-pack tables only, so a player who owned Mask and nothing else drew
// Empty Crates on every roll while leading, and itemsanity read as broken rather
// than restrictive. Unlocking a weapon has to mean it can actually appear.
//
// Rank weighting is NOT discarded: both filters below honour the vanilla roll
// first whenever it is a weapon the player owns, so ordinary position weighting
// still decides every roll it can pay out. This pool governs only the case that
// previously fell through to Wumpa.
//
// A null table is the documented "no weighted table in scope" path in
// AP_ItemsanitySelectOwned, which is exactly the semantics wanted here, so this
// reuses it rather than adding a second walk.
#define AP_ITEMSANITY_FULL_POOL      0
#define AP_ITEMSANITY_FULL_POOL_SIZE 0

// Roulette draw filter: preserve the original weighted roll when it is received,
// otherwise substitute from the full owned pool. Nothing is excluded here: this
// runs upstream of every vanilla cap, so a substituted Warpball or Missile x3
// still passes through the caps' own bookkeeping exactly like a natural roll.
//
// `table`/`tableCount` stay in the signature: the caller's table is still the
// thing `rolled` came from, and keeping the parameters means a future ruling can
// re-narrow the pool without touching every call site.
static int AP_ItemsanitySubstituteRoll(int rolled, unsigned roll,
	const unsigned char *table, int tableCount,
	const unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT])
{
	(void)table;
	(void)tableCount;
	if (AP_ItemsanityRollAllowed(rolled, owned))
		return rolled;
	return AP_ItemsanitySelectOwned(roll, 0, AP_ITEMSANITY_FULL_POOL,
	                                AP_ITEMSANITY_FULL_POOL_SIZE, owned);
}

// Downstream substitution filter. Vanilla's single-warpball rule and its
// two-holders-of-3-missiles cap rewrite the settled item AFTER the draw filter
// has run, so on their own they can hand out a weapon that was never received.
// Every ordinary substitution goes through here; the Crystal Challenge hardcode
// is the only remaining ruled bypass. Both capped ids are always excluded, per
// AP_ITEMSANITY_CAPPED_IDS.
static int AP_ItemsanitySubstituteDownstream(int proposed, unsigned roll,
	const unsigned char *table, int tableCount,
	const unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT])
{
	(void)table;
	(void)tableCount;
	if (AP_ItemsanityRollAllowed(proposed, owned))
		return proposed;
	// Same full pool as the draw filter, but the two capped ids stay excluded:
	// this runs AFTER vanilla's single-warpball and 3-missile caps, so handing
	// one back here would undo the cap that just ran.
	return AP_ItemsanitySelectOwned(roll, AP_ITEMSANITY_CAPPED_IDS,
	                                AP_ITEMSANITY_FULL_POOL,
	                                AP_ITEMSANITY_FULL_POOL_SIZE, owned);
}

// Boss-race catch-up ladder, strongest rung first. Retail's block names only
// two of these ids itself (Missile x3 as the escalation target, single Missile
// as the Komodo Joe cap); Bomb x3 and Bomb are a RULING by analogy, chosen as
// the closest owned firepower when missiles are locked, not a derivation from
// retail data. Mask, Clock and Warpball are the draws the rewrite REPLACES, so
// no rung may hand one back. (Warpball's one-at-a-time flag is not the reason:
// the boss block runs before the claim, so a Warpball leaving this block is
// claimed normally.)
#define AP_ITEMSANITY_BOSS_ASSIST_LADDER       {0xb, 0xa, 0x2, 0x1}
#define AP_ITEMSANITY_BOSS_ASSIST_LADDER_COUNT 4

// Ownership guard for the whole boss-race rewrite block.
//
// Ruled 2026-08-19: boss assistance is preserved but stops being an ownership
// bypass. `rolled` is the already draw-filtered item the block started from and
// `proposed` is what vanilla settled on. The result is, in order: `proposed`
// when the block rewrote nothing or the player owns it; otherwise the strongest
// owned rung strictly below `proposed`; otherwise `rolled`. A rung is never
// selected ABOVE `proposed`, which is what keeps the Komodo Joe cap intact:
// that line rewrites an owned Missile x3 DOWN, so handing the draw back there
// would undo the cap, and the no-item sentinel applies instead.
//
// No roll is consumed. Assistance is a fixed ladder rather than a draw, so
// taking the strongest owned rung preserves its "you are losing, here is your
// best weapon" meaning and stays deterministic for a given owned set.
static int AP_ItemsanityBossAssistWeapon(int rolled, int proposed,
	const unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT])
{
	static const unsigned char ladder[AP_ITEMSANITY_BOSS_ASSIST_LADDER_COUNT] =
		AP_ITEMSANITY_BOSS_ASSIST_LADDER;
	int proposedRung = -1;
	int rolledRung = -1;
	int i;

	// Self-enforced precondition: `rolled` must already have passed the draw
	// filter. Today every path into the boss block does (the two-driver boss
	// itemsets are both filtered), but the undecided-rank default arm is not,
	// so if a future config ever routes it here the guard fails closed to the
	// sentinel instead of handing the unfiltered draw back.
	if (rolled != AP_ITEMSANITY_NO_ITEM &&
	    !AP_ItemsanityRollAllowed(rolled, owned))
		return AP_ITEMSANITY_NO_ITEM;

	if (proposed == rolled)
		return proposed;

	if (AP_ItemsanityRollAllowed(proposed, owned))
		return proposed;

	for (i = 0; i < AP_ITEMSANITY_BOSS_ASSIST_LADDER_COUNT; i++)
	{
		if (ladder[i] == proposed)
			proposedRung = i;
		if (ladder[i] == rolled)
			rolledRung = i;
	}

	// Every rewrite the block performs today lands on a ladder rung. An
	// unrecognised target means the vanilla chain grew a case this ladder does
	// not describe, so keep the draw rather than invent a replacement for it.
	if (proposedRung < 0)
		return rolled;

	for (i = proposedRung + 1; i < AP_ITEMSANITY_BOSS_ASSIST_LADDER_COUNT; i++)
		if (AP_ItemsanityRollAllowed((int)ladder[i], owned))
			return (int)ladder[i];

	// Nothing at or below the proposed rung is owned. Keeping the draw is only
	// legal when the draw is not a STRONGER rung than the one vanilla settled
	// on, which is the Komodo Joe case: handing that draw back would undo the
	// cap, so the ruled no-item sentinel applies instead.
	if (rolledRung >= 0 && rolledRung < proposedRung)
		return AP_ITEMSANITY_NO_ITEM;

	return rolled;
}

#endif
