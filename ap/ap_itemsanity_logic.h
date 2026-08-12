#ifndef AP_ITEMSANITY_LOGIC_H
#define AP_ITEMSANITY_LOGIC_H

// Pure, engine-independent itemsanity rules. Kept in a header so the runtime
// hook and the focused host harness exercise the exact same classification and
// roll-then-substitute algorithm.

#define AP_ITEMSANITY_WEAPON_COUNT 11
#define AP_ITEMSANITY_NO_ITEM 0xF

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

// Roulette draw filter: preserve the original weighted roll when it is received,
// otherwise substitute from the same table. Nothing is excluded here: this runs
// upstream of every vanilla cap, so a substituted Warpball or Missile x3 still
// passes through the caps' own bookkeeping exactly like a natural roll.
static int AP_ItemsanitySubstituteRoll(int rolled, unsigned roll,
	const unsigned char *table, int tableCount,
	const unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT])
{
	if (AP_ItemsanityRollAllowed(rolled, owned))
		return rolled;
	return AP_ItemsanitySelectOwned(roll, 0, table, tableCount, owned);
}

// Downstream substitution filter. Vanilla's single-warpball rule and its
// two-holders-of-3-missiles cap rewrite the settled item AFTER the draw filter
// has run, so on their own they can hand out a weapon that was never received.
// Every ordinary substitution goes through here; only the boss-race rewrite and
// the Crystal Challenge hardcode stay ruled bypasses. Both capped ids are always
// excluded, per AP_ITEMSANITY_CAPPED_IDS.
static int AP_ItemsanitySubstituteDownstream(int proposed, unsigned roll,
	const unsigned char *table, int tableCount,
	const unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT])
{
	if (AP_ItemsanityRollAllowed(proposed, owned))
		return proposed;
	return AP_ItemsanitySelectOwned(roll, AP_ITEMSANITY_CAPPED_IDS, table,
	                                tableCount, owned);
}

#endif
