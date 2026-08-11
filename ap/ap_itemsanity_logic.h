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

// Preserve the original weighted roll when it is unlocked. On a locked result,
// select from the same rank-dependent weighted table after removing locked
// entries. Reusing the original roll value makes the substitution deterministic
// and consumes no extra RNG draw. Returns the no-item sentinel when exhausted.
static int AP_ItemsanitySubstituteRoll(int rolled, unsigned roll,
	const unsigned char *table, int tableCount,
	const unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT])
{
	int canonical = AP_ItemsanityCanonicalWeapon(rolled);
	int index = AP_ItemsanityWeaponIndex(canonical);
	int eligible = 0;
	int i;

	if (index >= 0 && owned[index])
		return rolled;

	for (i = 0; i < tableCount; i++)
	{
		canonical = AP_ItemsanityCanonicalWeapon(table[i]);
		index = AP_ItemsanityWeaponIndex(canonical);
		if (index >= 0 && owned[index])
			eligible++;
	}
	if (eligible == 0)
		return AP_ITEMSANITY_NO_ITEM;

	int selected = (int)(roll % (unsigned)eligible);
	for (i = 0; i < tableCount; i++)
	{
		canonical = AP_ItemsanityCanonicalWeapon(table[i]);
		index = AP_ItemsanityWeaponIndex(canonical);
		if (index >= 0 && owned[index] && selected-- == 0)
			return table[i];
	}
	return AP_ITEMSANITY_NO_ITEM;
}

#endif
