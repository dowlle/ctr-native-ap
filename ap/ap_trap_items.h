#ifndef AP_TRAP_ITEMS_H
#define AP_TRAP_ITEMS_H

// Apworld item identity for every trap, as an explicit table (#280).
//
// WHY A TABLE AND NOT ARITHMETIC. The shipped client computed
// `effect = idx - AP_TRAP_ITEM_FIRST_INDEX` and range-checked 16..20, which was
// correct only while the five traps were the whole roster and sat in one
// contiguous block right after Wumpa Fruit. The 0.2.0 apworld has 20 trap
// identities in three separate runs (16..20, 106..116, 190..193) with unrelated
// item families in between, so that arithmetic now maps weapon unlocks and letters
// onto trap effects. There is no expression that describes this layout; the
// apworld owns it, and native mirrors it as data.
//
// This header is freestanding by design: no engine, no network, no config, so
// tools/test-trap-scheduler.c pins the same table the client uses rather than a
// copy of it.
//
// ITEM IDS ARE FROZEN. Renaming an item in the apworld does not move its id, and
// native matches by id. "No Brakes" became "Forced USF" as a display name only;
// its id is still 35010018.

#include "ap_trap_sched_logic.h"

// Mirrors AP_ITEM_BASE (ap_items.h). Repeated here so this header stays
// freestanding; ap_hooks.c pins the two together with a static assert at the one
// place both are visible, the same way ap_reward_policy.h's model ids are pinned.
#define AP_TRAP_ITEM_ID_BASE 35010000

// Highest registered trap item index. The verifier's
// inventory projection is sized from this, so the table growing at the top does
// not need a second edit somewhere else.
#define AP_TRAP_ITEM_INDEX_MAX 206

typedef struct
{
	short index;          // apworld item index (id - AP_TRAP_ITEM_ID_BASE)
	unsigned char effect; // AP_TrapEffectId
} AP_TrapItemRow;

// Append-only trap identities, including the 0.3.0 Clocked Out entry.
static const AP_TrapItemRow AP_TRAP_ITEM_MAP[] = {
	// Implemented (ids 35010016..35010020).
	{16, AP_TRAP_ICY},
	{17, AP_TRAP_LOWGRAV},
	{18, AP_TRAP_USF_NOBRAKE}, // display name "Forced USF", formerly "No Brakes"
	{19, AP_TRAP_BOOST},
	{20, AP_TRAP_FIRSTPERSON},
	// Reserved expansion block (ids 35010106..35010116).
	{106, AP_TRAP_WUMPA_WIPEOUT},
	{107, AP_TRAP_FLATTEN},
	{108, AP_TRAP_ITEM_REROLL},
	{109, AP_TRAP_FORCED_USE},
	{110, AP_TRAP_EMPTY_CRATES},
	{111, AP_TRAP_WEAKENED_KART},
	{112, AP_TRAP_BOOST_BLOCKER},
	{113, AP_TRAP_WIREFRAME},
	{114, AP_TRAP_NITRO},
	{115, AP_TRAP_REVERSE_STEERING},
	{116, AP_TRAP_RED_POTION},
	// Rework identities added at the top of the table (ids 35010190..35010193).
	{190, AP_TRAP_UPSIDE_DOWN},
	{191, AP_TRAP_MIRROR_MODE},
	{192, AP_TRAP_WARPBALL_AMBUSH},
	{193, AP_TRAP_DEMO_CAMERA},
	{206, AP_TRAP_CLOCKED_OUT}, // #455, already minted in the 0.3.0 datapackage
};

#define AP_TRAP_ITEM_MAP_COUNT \
	((int)(sizeof AP_TRAP_ITEM_MAP / sizeof AP_TRAP_ITEM_MAP[0]))

// The roster size is a fact about the apworld, not something to rediscover by
// counting rows. If the two ever disagree the build stops here.
typedef char ap_trap_item_map_matches_roster[AP_TRAP_ITEM_MAP_COUNT == AP_TRAP_EFFECT_COUNT ? 1 : -1];

// The effect an apworld item index maps to, or -1 when the index is not a trap.
// Linear over the small roster on a receive path that runs a handful of times per session;
// a binary search would buy nothing and could go stale against the table order.
static inline int AP_TrapEffectForItemIndex(long long idx)
{
	int i;
	for (i = 0; i < AP_TRAP_ITEM_MAP_COUNT; i++)
		if (AP_TRAP_ITEM_MAP[i].index == idx)
			return (int)AP_TRAP_ITEM_MAP[i].effect;
	return -1;
}

static inline int AP_TrapItemIndexIsTrap(long long idx)
{
	return AP_TrapEffectForItemIndex(idx) >= 0;
}

// Same lookup from a raw apworld item id. Ids below the base, or far above the
// table, are not traps and must not wrap into one.
static inline int AP_TrapEffectForItemId(long long id)
{
	long long idx = id - AP_TRAP_ITEM_ID_BASE;
	if (idx < 0 || idx > AP_TRAP_ITEM_INDEX_MAX)
		return -1;
	return AP_TrapEffectForItemIndex((int)idx);
}

// ── Trap identities on the wire (#307, slot_data schema 18) ───────────────
//
// The stable identity of a trap EFFECT is its key: the same lowercase string the
// apworld uses as the `trap_weights` YAML key, never renamed. From schema 18 the
// apworld sends `trap_identities`, a block whose `items` map each trap item id
// to that key, and native applies a received trap only when the seed's key for
// the item is the key this build has for it. An entry this build cannot honour
// (unknown item id, unknown key, or a known key on the wrong item) is refused
// with one log line at parse, and that trap never fires. The enum value of an
// effect is a native detail that never travels, so neither side can re-point a
// trap by reordering a list.
//
// One row per AP_TrapEffect, indexed by the enum. tools/test-trap-identities.cpp
// checks this table, the item table above and AP_TRAP_DESC names against the
// shared fixture tools/fixtures/trap_identities.json (a byte-identical copy of the
// apworld's worlds/ctr/test/fixtures/trap_identities.json). A new trap (#455,
// #248) appends one enum value, one key here, one item row above and one fixture
// row in both repositories; nothing existing moves.
static const char *const AP_TRAP_EFFECT_KEY[AP_TRAP_EFFECT_COUNT] = {
	"icy_road",
	"low_gravity",
	"forced_usf",
	"forced_boost",
	"first_person",
	"wumpa_wipeout",
	"flatten",
	"item_reroll",
	"forced_use",
	"empty_crates",
	"weakened_kart",
	"boost_blocker",
	"wireframe",
	"nitro",
	"reverse_steering",
	"red_potion",
	"upside_down",
	"mirror_mode",
	"warpball_ambush",
	"demo_camera",
	"clocked_out",
};

// First slot_data schema whose seeds carry `trap_identities` and must be read
// through it. An older seed keeps the frozen item table above, which is what a
// 0.2.x client applied, so its traps behave exactly as before.
#define AP_TRAP_IDENTITIES_SCHEMA 18
// The block's own shape version.
#define AP_TRAP_IDENTITIES_VERSION 1

// The effect this build has for a key, or -1 when the key is unknown here.
static inline int AP_TrapEffectForKey(const char *key)
{
	int e;
	if (key == 0)
		return -1;
	for (e = 0; e < AP_TRAP_EFFECT_COUNT; e++)
	{
		const char *a = AP_TRAP_EFFECT_KEY[e];
		const char *b = key;
		while (*a && *a == *b)
		{
			a++;
			b++;
		}
		if (*a == 0 && *b == 0)
			return e;
	}
	return -1;
}

// The item-table row for an apworld item index, or -1 when it is not a trap here.
static inline int AP_TrapItemRowForIndex(long long idx)
{
	int i;
	for (i = 0; i < AP_TRAP_ITEM_MAP_COUNT; i++)
		if (AP_TRAP_ITEM_MAP[i].index == idx)
			return i;
	return -1;
}

// What a seed's trap_identities block resolved to, per item-table row.
enum
{
	AP_TRAP_IDS_ABSENT = 0, // no usable block yet
	AP_TRAP_IDS_VALID,      // block accepted; rows hold the agreed effects
	AP_TRAP_IDS_INVALID     // block refused as a whole: no trap fires
};

typedef struct
{
	// 0 = the seed predates schema 18 (or no slot_data was parsed): the frozen
	// item table above decides, exactly as a 0.2.x client did. 1 = the seed must
	// carry trap_identities and only what it agreed fires. Zero-initialised is
	// therefore the legacy behaviour.
	int required;
	int state; // AP_TRAP_IDS_*
	// Effect agreed for AP_TRAP_ITEM_MAP[i], or -1 when the seed does not map
	// that item to the key this build has for it (missing, unknown, contradictory).
	signed char effect[AP_TRAP_ITEM_MAP_COUNT];
} AP_TrapIdentityTable;

// The current seed's table. Written by the slot_data parser (ap_seedcfg.cpp)
// at connect, read by the received-item path.
#ifdef __cplusplus
extern "C"
#else
extern
#endif
    AP_TrapIdentityTable ctr_trap_identities;

// Outcome of checking one seed entry against this build.
enum
{
	AP_TRAP_ENTRY_OK = 0,
	AP_TRAP_ENTRY_NOT_A_TRAP,    // the item id is not a trap identity in this build
	AP_TRAP_ENTRY_UNKNOWN_KEY,   // the key names an effect this build does not have
	AP_TRAP_ENTRY_CONTRADICTION  // known key, but not the one this build has for the item
};

// Check one (item id, key) entry. On OK, *rowOut and *effectOut are set.
static inline int AP_TrapCheckEntry(long long itemId, const char *key, int *rowOut,
                                    int *effectOut)
{
	long long idx = itemId - AP_TRAP_ITEM_ID_BASE;
	int row = (idx < 0 || idx > AP_TRAP_ITEM_INDEX_MAX) ? -1 : AP_TrapItemRowForIndex(idx);
	int effect = AP_TrapEffectForKey(key);
	if (row < 0)
		return AP_TRAP_ENTRY_NOT_A_TRAP;
	if (effect < 0)
		return AP_TRAP_ENTRY_UNKNOWN_KEY;
	if (effect != (int)AP_TRAP_ITEM_MAP[row].effect)
		return AP_TRAP_ENTRY_CONTRADICTION;
	if (rowOut)
		*rowOut = row;
	if (effectOut)
		*effectOut = effect;
	return AP_TRAP_ENTRY_OK;
}

// The effect to apply for a received item index, or -1 to refuse it.
//   not required (seed schema < 18, or no slot_data): the frozen item table.
//   required: only what the seed's block agreed. An absent or refused block
//   refuses every trap, because a schema-18 seed is required to carry it.
static inline int AP_TrapResolveEffect(const AP_TrapIdentityTable *t, long long idx)
{
	int row;
	if (t == 0 || !t->required)
		return AP_TrapEffectForItemIndex(idx);
	if (t->state != AP_TRAP_IDS_VALID)
		return -1;
	row = AP_TrapItemRowForIndex(idx);
	if (row < 0)
		return -1;
	return (int)t->effect[row];
}

// Does this build have a native effect for the trap, or does it only know the
// identity? A received identity with no effect is retained armed, never consumed.
static inline int AP_TrapItemIsBuildable(int effect)
{
	if (effect < 0 || effect >= AP_TRAP_EFFECT_COUNT)
		return 0;
	return AP_TRAP_DESC[effect].active != 0;
}

#endif // AP_TRAP_ITEMS_H
