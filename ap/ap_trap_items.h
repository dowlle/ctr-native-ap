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

// Highest item index in the 0.2.0 table, which is Demo Camera. The verifier's
// inventory projection is sized from this, so the table growing at the top does
// not need a second edit somewhere else.
#define AP_TRAP_ITEM_INDEX_MAX 193

typedef struct
{
	short index;          // apworld item index (id - AP_TRAP_ITEM_ID_BASE)
	unsigned char effect; // AP_TrapEffectId
} AP_TrapItemRow;

// All 20 trap identities in the 0.2.0 datapackage.
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
};

#define AP_TRAP_ITEM_MAP_COUNT \
	((int)(sizeof AP_TRAP_ITEM_MAP / sizeof AP_TRAP_ITEM_MAP[0]))

// The roster size is a fact about the apworld, not something to rediscover by
// counting rows. If the two ever disagree the build stops here.
typedef char ap_trap_item_map_is_twenty[AP_TRAP_ITEM_MAP_COUNT == 20 ? 1 : -1];

// The effect an apworld item index maps to, or -1 when the index is not a trap.
// Linear over 20 rows on a receive path that runs a handful of times per session;
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

// Does this build have a native effect for the trap, or does it only know the
// identity? A received identity with no effect is retained armed, never consumed.
static inline int AP_TrapItemIsBuildable(int effect)
{
	if (effect < 0 || effect >= AP_TRAP_EFFECT_COUNT)
		return 0;
	return AP_TRAP_DESC[effect].active != 0;
}

#endif // AP_TRAP_ITEMS_H
