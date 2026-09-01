#ifndef AP_ITEMS_H
#define AP_ITEMS_H
#ifdef CTR_AP

// AP item-id -> AdvProgress category mapping for CTR-Native.
//
// Icebound's items (data/items.json) are GENERIC category counters, not
// track-specific: Trophy x16, Sapphire/Gold/Platinum Relic x18, CTR Token
// (5 colours x4), Gem (5 colours), Key x4, Wumpa Fruit (filler). The apworld
// assigns explicit stable codes in data/items.json. The currently shipped base
// block remains contiguous from AP_ITEM_BASE, which is what the native category
// switch consumes; JSON array order is no longer the source of those ids.
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
// early tracks first. GEMS and CTR TOKENS are the exceptions: both are
// per-colour items with per-colour requirements AND a visible colour identity
// on the pause screen, so AP_ApplyItems mirrors each colour's own bits from
// ap_recv_count instead of pooled high-end-first (gems: issue #35, where the
// pooled fill set the Purple bit on the FIRST received gem of any colour;
// tokens: issue #142). The grant-guard half of the collision is closed
// separately: every check-sending grant site now gates on the AP location
// CHECKED-state, not adv->rewards (221.c crystal arena, 222.c trophy + boss
// key, 223.c relic, ThTick, UI_CupStandings gem cup). The clean fix (redirect
// the gate counters to a separate received-item counter so item application
// never touches location bits) is a larger change, deferred -- on the roadmap.

#define AP_ITEM_BASE 35010000

// ── Per-item-TYPE indices (Option B) ──
// item id = AP_ITEM_BASE + index. 15 indices (0..14), Wumpa (15) is filler.
// Colour order 0=Red 1=Green 2=Blue 3=Yellow 4=Purple (matches the apworld).
// Also declared in ap_hooks.h (the gate API header); guard against redefinition.
#ifndef AP_IDX_TROPHY
#define AP_IDX_TROPHY        0
#define AP_IDX_SAPPHIRE      1
#define AP_IDX_GOLD          2
#define AP_IDX_PLATINUM      3
#define AP_IDX_TOKEN_RED     4  // tokens: 4..8 = R,G,B,Y,P
#define AP_IDX_GEM_RED       9  // gems:   9..13 = R,G,B,Y,P
#define AP_IDX_KEY          14
#define AP_ITEM_INDEX_COUNT 15
#endif

// ── Constants owned elsewhere that the classification below needs ──────────
//
// ap_capability.h carries the capability ladder and the character unlocks;
// ap_itemsanity_logic.h carries the weapon unlocks. Both are freestanding
// headers with include guards, so pulling them in here keeps this file
// compilable outside the engine (tools/test-reward-policy.c depends on that).
#include "ap_capability.h"       // AP_CAPABILITY_* and AP_CHARACTER_* item blocks
#include "ap_itemsanity_logic.h" // AP_ITEMSANITY_ITEM_FIRST_INDEX / _WEAPON_COUNT
#include "ap_trap_items.h"       // the 20 trap identities, which are not one range

// The lettersanity block is owned by ap_seedcfg.h, next to the rest of the
// per-seed letter config. This header cannot reach for that one and stay
// freestanding, so the range is mirrored, guarded. The values are plain integer
// literals in both places, so whichever header a translation unit sees second is
// an identical redefinition. When these are ever consolidated, keep the
// ap_seedcfg.h block and drop this one.
#ifndef CTR_LETTER_ITEM_FIRST_INDEX
#define CTR_CFG_LETTER_TRACK_COUNT 16
#define CTR_CFG_LETTER_COUNT 3
#define CTR_LETTER_ITEM_FIRST_INDEX 139
#endif

// Gas Pedal (item index 187) sits alone between the letter block and the Tizi
// Helper. Unlike every other block here it has no upstream owner to defer to --
// no client receives it yet -- so whoever builds its receive path should take
// the index from here rather than minting a second one.
#define AP_GAS_PEDAL_ITEM_INDEX 187

// 0.2.0 Wumpa family, frozen immediately before character unlocks.
#define AP_WUMPA_SMALL_BUNDLE_ITEM_INDEX 120
#define AP_WUMPA_BIG_BUNDLE_ITEM_INDEX 121
#define AP_WUMPA_PROGRESSIVE_ITEM_INDEX 122

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
	// Every CTR progression item with NO vanilla model of its own: the capability
	// ladder (#12/#13), the weapon unlocks (#145), the character unlocks
	// (#54/#209), the letters (#148) and Gas Pedal. No bit pool. The name is the
	// MODEL these present as, not the feature they arrive from -- the ruling
	// (#219) is that all of them read as the purple crystal, so a player never has
	// to tell CTR's own progression apart from a foreign multiworld item by
	// squinting at a marker's tint.
	AP_CAT_CRYSTAL,
	AP_CAT_NONE
} AP_ItemCat;

static AP_ItemCat AP_ItemCategory(long long id)
{
	long long idx = id - AP_ITEM_BASE;

	switch (idx)
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
	case AP_WUMPA_SMALL_BUNDLE_ITEM_INDEX:
	case AP_WUMPA_BIG_BUNDLE_ITEM_INDEX:
		return AP_CAT_WUMPA;
	default: break;
	}

	// ── AP_CAT_CRYSTAL: CTR progression with no vanilla model of its own ──
	//
	// DISPLAY ONLY, and structurally so: AP_CAT_CRYSTAL sits past AP_CAT_COUNT, so
	// AP_CATEGORY_POOLS cannot index it, it owns no AdvProgress bits, and its only
	// consumers are ap_reward_policy.h and the pad resolver. Widening it grants
	// nothing, gates nothing and counts nothing -- every receive path below is
	// untouched by what this function answers.

	// The capability ladder: the shared-global block (Progressive Boost / Top
	// Speed / Acceleration / Turning, indices 27..30) and the per-character block
	// (31..94), which are contiguous.
	if (idx >= AP_CAPABILITY_ITEM_FIRST_INDEX &&
	    idx < AP_CAPABILITY_PC_ITEM_FIRST_INDEX + AP_CAPABILITY_PC_ITEM_COUNT)
		return AP_CAT_CRYSTAL;

	// The itemsanity weapon unlocks (95..105).
	if (idx >= AP_ITEMSANITY_ITEM_FIRST_INDEX &&
	    idx < AP_ITEMSANITY_ITEM_FIRST_INDEX + AP_ITEMSANITY_WEAPON_COUNT)
		return AP_CAT_CRYSTAL;

	// The character unlocks (123..138). They join the category rather than getting
	// one of their own because every answer the display policy would give them is
	// the ladder's -- same model, same purple, same scale -- and a second category
	// whose every arm duplicated the first is a seam that can only ever drift.
	if (idx >= AP_CHARACTER_ITEM_FIRST_INDEX &&
	    idx < AP_CHARACTER_ITEM_FIRST_INDEX + AP_CHARACTER_ITEM_COUNT)
		return AP_CAT_CRYSTAL;
	if (idx == AP_WUMPA_PROGRESSIVE_ITEM_INDEX)
		return AP_CAT_CRYSTAL;

	// The letters (139..186) and Gas Pedal (187). Gas Pedal has no receive path in
	// any client, and is classified here anyway so the display cannot be the thing
	// still outstanding when one lands.
	if (idx >= CTR_LETTER_ITEM_FIRST_INDEX &&
	    idx < CTR_LETTER_ITEM_FIRST_INDEX + CTR_CFG_LETTER_TRACK_COUNT * CTR_CFG_LETTER_COUNT)
		return AP_CAT_CRYSTAL;
	if (idx == AP_GAS_PEDAL_ITEM_INDEX)
		return AP_CAT_CRYSTAL;

	// Traps. Stated as its own branch rather than left to the fall-through: the 19
	// identities are scattered across 16..20, 106..116 and 190..193, so "everything
	// else" is no longer a description a reader can check, and a future family
	// dropped into one of the gaps must not silently inherit the trap answer.
	// The answer itself is unchanged, AP_CAT_NONE, which is the Archipelago logo.
	if (AP_TrapItemIndexIsTrap((int)idx))
		return AP_CAT_NONE;

	// Everything else is Archipelago-logo material too: the comfort items (21..25),
	// the Tizi Helper (188), the Turbo Grant (189) and anything an apworld invents
	// later. The Tizi Helper is deliberately NOT a crystal: it is a per-track
	// assist, not a progression step, and nothing has ruled it into the family.
	return AP_CAT_NONE;
}

// Bit pools (AdvProgress global bit indices = word*32 + bit), matching the
// category ranges in ap_locations.h.
static const int AP_POOL_TROPHY[16]   = {6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21};
static const int AP_POOL_SAPPHIRE[18] = {22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39};
static const int AP_POOL_GOLD[18]     = {40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57};
static const int AP_POOL_PLATINUM[18] = {58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75};
static const int AP_POOL_KEY[4]       = {94,95,96,97};
static const int AP_POOL_GEM[5]       = {106,107,108,109,110};

// CTR Tokens are per-colour items (4 of each colour) and the pause screen shows
// WHICH colours you hold, so this pool is COLOUR-MAJOR, not bit-ascending:
// entries [c*4 .. c*4+3] are colour c's four bits, c = 0 Red, 1 Green, 2 Blue,
// 3 Yellow, 4 Purple -- the same keying as AP_IDX_TOKEN_RED + c,
// AP_GateCountTokenColour and data.AdvCups[]. Colours 0-3 are the 16 race-track
// challenge bits 76-91 regrouped by each track's ctrTokenGroupID (metaDataLEV in
// game/zGlobal_DATA.c); colour 4 is the four crystal-arena bits 111-114
// (ADV_REWARD_FIRST_PURPLE_TOKEN), which NO pool covered before issue #142 --
// so nothing ever cleared the bit a locally won arena set and its pause icon
// stayed lit across the tick, the level load and the save.
static const int AP_POOL_TOKEN[20]    = {
	 78, 79, 81, 85, // Red:    Blizzard Bluff, Crash Cove, Papu's Pyramid, Mystery Caves
	 82, 86, 88, 90, // Green:  Roo's Tubes, Cortex Castle, Polar Pass, Coco Park
	 77, 80, 84, 87, // Blue:   Dragon Mines, Tiger Temple, Sewer Speedway, N. Gin Labs
	 76, 83, 89, 91, // Yellow: Dingo Canyon, Hot Air Skyway, Oxide Station, Tiny Arena
	111,112,113,114, // Purple: Skull Rock, Rampage Ruins, Rocky Road, Nitro Court
};

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
	{AP_POOL_TOKEN,    20, "CTR Token"},
	{AP_POOL_GEM,       5, "Gem"},
	{AP_POOL_KEY,       4, "Key"},
};

#endif // CTR_AP
#endif // AP_ITEMS_H
