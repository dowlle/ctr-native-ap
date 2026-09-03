#ifndef AP_ITEM_ALIASES_H
#define AP_ITEM_ALIASES_H
#ifdef CTR_AP

// Native display aliases for long CTR item names (issue #324).
//
// The transient feed, the race-end ceremony and the podium-rung line all clamp
// the item portion of a notification to 31 visible characters. 41 CTR item
// names exceed that -- every one of them a per-character Progressive Boost or
// Progressive Stats item -- so the racer or capability was getting cut off
// before "FROM", "TO" or the rung reason was appended.
//
// DISPLAY ONLY. This header never touches the canonical Archipelago name, the
// item id, the datapackage, logic or the wire protocol -- it only offers a
// shorter native string a caller may choose to draw instead. Keyed by item id
// (idx = id - AP_ITEM_BASE), the same frozen-id discipline as ap_trap_items.h:
// renaming an item in the apworld does not move its id, and this resolves by id.
//
// Computed, not tabulated: the alias families are systematic (a racer
// qualifier plus a capability suffix, or a letter plus a track name), so the
// same two small lookup tables serve all 64 per-character capability items and
// all 48 letter items instead of hand-writing over a hundred literal rows,
// which is where a copy/paste slip would hide.

#include "ap_capability.h" // AP_CAPABILITY_*, AP_CAP_CHAIN_*, AP_CAP_ROSTER_COUNT
#include "ap_items.h"      // AP_ITEM_BASE, AP_WUMPA_PROGRESSIVE_ITEM_INDEX,
                            // CTR_LETTER_ITEM_FIRST_INDEX, CTR_CFG_LETTER_*
#include "ap_lettersanity.h" // AP_LetterItemRowToLevelIDPure

#include <stdio.h> // snprintf

// Racer qualifiers, indexed by WIRE roster slot -- the SAME order as
// ap_capability.c's AP_CAP_ROSTER_CHARACTER (Crash, Coco, Polar, Pura, Cortex,
// Tropy, Roo, Papu, Joe, Pinstripe, Dingodile, Tiny, N. Gin, Fake Crash, Oxide,
// Penta), not an engine character id. Approved wording, issue #324.
static const char *const AP_ALIAS_RACER[AP_CAP_ROSTER_COUNT] = {
	"CRASH", "COCO", "POLAR", "PURA", "CORTEX", "N. TROPY",
	"ROO", "PAPU", "JOE", "PINSTRIPE", "DINGODILE", "TINY",
	"N. GIN", "FAKE CRASH", "OXIDE", "PENTA",
};

// Capability suffix by chain (AP_CAP_CHAIN_BOOST..AP_CAP_CHAIN_TURNING).
// Approved wording, issue #324.
static const char *const AP_ALIAS_CAPABILITY[AP_CAP_CHAIN_COUNT] = {
	"BOOST+", "SPEED+", "ACCEL+", "TURN+",
};

// 16 track display names, LevelID-indexed 0..15 -- the same order as
// ap_author.c's s_levelNames / AP_TrophyName's globalBit table, spelled the
// way the pause/ceremony surfaces already spell them (AP_TrophyName).
static const char *const AP_ALIAS_TRACK[16] = {
	"DINGO CANYON",   "DRAGON MINES",   "BLIZZARD BLUFF", "CRASH COVE",
	"TIGER TEMPLE",   "PAPU'S PYRAMID", "ROO'S TUBES",    "HOT AIR SKYWAY",
	"SEWER SPEEDWAY", "MYSTERY CAVES",  "CORTEX CASTLE",  "N. GIN LABS",
	"POLAR PASS",     "OXIDE STATION",  "COCO PARK",      "TINY ARENA",
};

// Letter-slot -> display char, matching ap_hooks.c's AP_LetterUnavailableTouched
// `names[CTR_CFG_LETTER_COUNT] = {'C', 'T', 'R'}` table exactly.
static const char AP_ALIAS_LETTER_CHAR[CTR_CFG_LETTER_COUNT] = {'C', 'T', 'R'};

// Resolve a raw AP item id to its native display alias (issue #324).
//
// Returns 1 and fills `out` (NUL-terminated, truncated to `cap`) when `item_id`
// is a known aliased CTR identity: a shared-global or per-character
// Progressive capability item, Progressive Starting Wumpa, or a Lettersanity
// letter. Returns 0 for everything else -- character unlocks, ordinary CTR
// items, traps, an id this build does not recognise, or a cross-game item --
// in which case the caller must keep its existing name-based display path.
static inline int AP_ItemDisplayAlias(long long item_id, char *out, int cap)
{
	long long idx;

	if (!out || cap <= 0)
		return 0;

	idx = item_id - AP_ITEM_BASE;
	if (idx < 0)
		return 0; // below native's item space entirely (e.g. a cross-game id)

	if (idx >= AP_CAPABILITY_ITEM_FIRST_INDEX &&
	    idx < AP_CAPABILITY_ITEM_FIRST_INDEX + AP_CAPABILITY_ITEM_COUNT)
	{
		int chain = (int)(idx - AP_CAPABILITY_ITEM_FIRST_INDEX);
		snprintf(out, (size_t)cap, "%s", AP_ALIAS_CAPABILITY[chain]);
		return 1;
	}

	if (idx >= AP_CAPABILITY_PC_ITEM_FIRST_INDEX &&
	    idx < AP_CAPABILITY_PC_ITEM_FIRST_INDEX + AP_CAPABILITY_PC_ITEM_COUNT)
	{
		int block = (int)(idx - AP_CAPABILITY_PC_ITEM_FIRST_INDEX);
		int slot  = block / AP_CAP_CHAIN_COUNT;   // racer-first: slot before chain
		int chain = block % AP_CAP_CHAIN_COUNT;
		snprintf(out, (size_t)cap, "%s %s", AP_ALIAS_RACER[slot], AP_ALIAS_CAPABILITY[chain]);
		return 1;
	}

	if (idx == AP_WUMPA_PROGRESSIVE_ITEM_INDEX)
	{
		snprintf(out, (size_t)cap, "START WUMPA+");
		return 1;
	}

	if (idx >= CTR_LETTER_ITEM_FIRST_INDEX &&
	    idx < CTR_LETTER_ITEM_FIRST_INDEX + CTR_CFG_LETTER_TRACK_COUNT * CTR_CFG_LETTER_COUNT)
	{
		int li     = (int)(idx - CTR_LETTER_ITEM_FIRST_INDEX);
		int row    = li / CTR_CFG_LETTER_COUNT;
		int letter = li % CTR_CFG_LETTER_COUNT;
		int level  = AP_LetterItemRowToLevelIDPure(row);
		if (level < 0 || level >= 16)
			return 0; // unreachable with the frozen 16-track table; fail closed
		snprintf(out, (size_t)cap, "%c: %s", AP_ALIAS_LETTER_CHAR[letter], AP_ALIAS_TRACK[level]);
		return 1;
	}

	return 0; // character unlocks (123..138), ordinary items, traps, unknown ids
}

#endif // CTR_AP
#endif // AP_ITEM_ALIASES_H
