#ifndef AP_BOSS_KIT_TARGET_LOGIC_H
#define AP_BOSS_KIT_TARGET_LOGIC_H

// ============================================================================
// Who a boss-kit missile or bomb locks onto when retail finds nobody (A4-48a2).
//
// FREESTANDING: no engine headers. ap/ap_hooks_gates.c calls it from the boss
// weapon fire path; tools/test-boss-kit-target.c drives the same functions.
//
// Retail boss targeting (VehPickupItem_MissileGetTargetDriver plus the
// fallback in VehPickupItem_ShootNow) never picks a target for a boss in first
// place, and for a trailing boss only on odd frames. No retail boss table
// carries a missile, so this never showed in clean native. A composed boss kit
// can add one (ruled 2026-11-10 22:43 CET: "Only for kit missiles"): a weapon
// the kit adds, which the garage's retail table does not have, targets the
// human player when the retail rules find no target. A weapon the retail
// table has keeps retail behaviour.
//
// Homing weapons are the two that share the ShootNow Missile branch and its
// tracker thread: the missile (item 2) and the bomb (item 1, fired as a
// tracker with a model swap). The warp orb has its own targeting and is not
// part of this rule; potion and TNT are placed, not tracked.
// ============================================================================

#define AP_BKT_ITEM_BOMB 1
#define AP_BKT_ITEM_MISSILE 2
#define AP_BKT_ITEM_TNT 3
#define AP_BKT_ITEM_POTION 4

// The boss table's weaponType encodes TNT, bomb and potion as 0x64..0x66;
// every other value is a direct item id (0xf is none).
static inline int AP_BossKitTarget_ItemOfType(int weaponType)
{
	switch (weaponType)
	{
	case 0x64:
		return AP_BKT_ITEM_TNT;
	case 0x65:
		return AP_BKT_ITEM_BOMB;
	case 0x66:
		return AP_BKT_ITEM_POTION;
	default:
		return weaponType;
	}
}

// Does the shared Missile branch track a target for this item?
static inline int AP_BossKitTarget_Homing(int item)
{
	return item == AP_BKT_ITEM_MISSILE || item == AP_BKT_ITEM_BOMB;
}

// Is `item` one of the weapons of a retail table? `types` holds the weaponType
// column of its rows, `rows` of them (the table ends at its zero throwFlag).
static inline int AP_BossKitTarget_InTable(int item, const unsigned char *types, int rows)
{
	int i;

	for (i = 0; i < rows; i++)
		if (AP_BossKitTarget_ItemOfType(types[i]) == item)
			return 1;
	return 0;
}

// Does the weapon about to fire qualify for the player fallback? kitRace: the
// race runs a composed boss kit. A retail-table weapon never qualifies.
static inline int AP_BossKitTarget_Qualifies(int kitRace, int weaponType, const unsigned char *retailTypes,
                                             int retailRows)
{
	int item = AP_BossKitTarget_ItemOfType(weaponType);

	return kitRace && AP_BossKitTarget_Homing(item) && !AP_BossKitTarget_InTable(item, retailTypes, retailRows);
}

// The target a shot ends with: retail's own pick wins; only when retail found
// nobody and the shot qualifies does the player take it, and only a player who
// could be locked in retail (present, not invisible, not the shooter).
static inline int AP_BossKitTarget_UsePlayer(int retailFound, int qualifies, int playerPresent,
                                             int playerIsShooter, int playerInvisible)
{
	return !retailFound && qualifies && playerPresent && !playerIsShooter && !playerInvisible;
}

#endif
