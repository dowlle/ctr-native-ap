#ifndef AP_ITEMS_H
#define AP_ITEMS_H
#ifdef CTR_AP

// AP item-id -> AdvProgress category mapping for CTR-Native.
//
// Icebound's items (data/items.json) are GENERIC category counters, not
// track-specific: Trophy x16, Sapphire/Gold/Platinum Relic x18, CTR Token
// (5 colours x4), Gem (5 colours), Key x4, Wumpa Fruit (filler). Item id =
// AP_ITEM_BASE + index_in_items.json.
//
// On the rando ROM these increment SaveSlot-4 byte-packed counters and the
// patched gates read those counters. ctr-native has no such patch -- the gates
// count AdvProgress bits (GAMEPROG_AdvPercent). So "having N of a category" is
// expressed by setting N bits of that category's bit pool. The pools ARE the
// same AdvProgress bits as the category's locations (there is no separate item
// storage on native).
//
// COLLISION NOTE: because item bits == location bits, an item that pre-sets a
// track's bit makes the game's grant guard skip when the player later wins that
// track, so the option-A location hook (inside that guard) won't fire -> a
// missed check for that one track. To minimise this during play/testing we fill
// each pool from the HIGH end (the bit choice is arbitrary for generic items),
// so early item grants land on late-game tracks while a tester typically wins
// early tracks first. The clean fix (redirect the gate counters to a separate
// received-item counter so item application never touches location bits) is a
// larger change, deferred. See board 2026-06-22 15:50 / 17:40 threads.

#define AP_ITEM_BASE 35010000

typedef enum
{
	AP_CAT_TROPHY = 0,
	AP_CAT_SAPPHIRE,
	AP_CAT_GOLD,
	AP_CAT_PLATINUM,
	AP_CAT_TOKEN,
	AP_CAT_GEM,
	AP_CAT_KEY,
	AP_CAT_COUNT, // number of bit-pool categories
	AP_CAT_WUMPA, // filler, no bit pool
	AP_CAT_NONE
} AP_ItemCat;

static AP_ItemCat AP_ItemCategory(long long id)
{
	switch (id - AP_ITEM_BASE)
	{
	case 0:  return AP_CAT_TROPHY;   // Trophy
	case 1:  return AP_CAT_SAPPHIRE; // Sapphire Relic
	case 2:  return AP_CAT_GOLD;     // Gold Relic
	case 3:  return AP_CAT_PLATINUM; // Platinum Relic
	case 4: case 5: case 6: case 7: case 8:
		return AP_CAT_TOKEN; // Red/Green/Blue/Yellow/Purple CTR Token
	case 9: case 10: case 11: case 12: case 13:
		return AP_CAT_GEM;   // Red/Green/Blue/Yellow/Purple Gem
	case 14: return AP_CAT_KEY;   // Key
	case 15: return AP_CAT_WUMPA; // Wumpa Fruit (filler)
	default: return AP_CAT_NONE;
	}
}

// Bit pools (AdvProgress global bit indices = word*32 + bit), matching the
// category ranges in ap_locations.h.
static const int AP_POOL_TROPHY[16]   = {6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21};
static const int AP_POOL_SAPPHIRE[18] = {22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39};
static const int AP_POOL_GOLD[18]     = {40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57};
static const int AP_POOL_PLATINUM[18] = {58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75};
static const int AP_POOL_TOKEN[16]    = {76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91};
static const int AP_POOL_KEY[4]       = {94,95,96,97};
static const int AP_POOL_GEM[5]       = {106,107,108,109,110};

typedef struct
{
	const int *bits;
	int        size;
	const char *name;
} AP_CatPool;

static const AP_CatPool AP_CATEGORY_POOLS[AP_CAT_COUNT] = {
	{AP_POOL_TROPHY,   16, "Trophy"},
	{AP_POOL_SAPPHIRE, 18, "Sapphire Relic"},
	{AP_POOL_GOLD,     18, "Gold Relic"},
	{AP_POOL_PLATINUM, 18, "Platinum Relic"},
	{AP_POOL_TOKEN,    16, "CTR Token"},
	{AP_POOL_GEM,       5, "Gem"},
	{AP_POOL_KEY,       4, "Key"},
};

#endif // CTR_AP
#endif // AP_ITEMS_H
