#ifndef AP_PAD_GLOW_ITEMS_H
#define AP_PAD_GLOW_ITEMS_H

// Hub pad display identities for item boxes, CTR letters and per-track Wumpa
// locations (ruling of 2026-09-30: the warp pad glow shows the placed item of EVERY
// open location behind the pad).
//
// Those three families have no AdvProgress bit, so the bit-keyed glow pipeline
// could not address them; the pad only COUNTED them (AP_PadUncollectedBoxCount,
// ...LetterCount, ...WumpaCount) so the pad state could not strand them. These
// are process-local pseudo-bits, never wire values, in the same spirit as the
// trial (#343, 0x210), Cortex Vortex (0x220) and Relic Perfect (#439, 0x240)
// identities. AP_LookupLocationCode turns them into the seed's location codes,
// so the reward model, tint, ghost and checked-state helpers work unchanged.
//
//   boxes   0x300 + (box code - AP_BOX_CODE_BASE)          0x300..0x40d (270)
//   letters 0x420 + level * 3 + letter                     0x420..0x455 (54)
//   Wumpa   0x460 + level                                  0x460..0x471 (18)
//   custom letters 0x456 + letter (the custom CTR Challenge) 0x456..0x458 (3)
//
// Freestanding and header-only so tools/test-pad-glow-items.c exercises the
// exact gather production uses.

#include "ap_box_map.h"
#include "ap_seedcfg.h"

#define AP_PADGLOW_BOX_BASE    0x300
#define AP_PADGLOW_LETTER_BASE 0x420
#define AP_PADGLOW_WUMPA_BASE  0x460
// Custom-track letters sit right after the 18 per-track letter rows, below the
// Wumpa range (2026-10-09).
#define AP_PADGLOW_CUSTOM_LETTER_BASE \
	(AP_PADGLOW_LETTER_BASE + CTR_CFG_LETTER_TRACK_COUNT * CTR_CFG_LETTER_COUNT)

// Buffer size for a pad's DISPLAY enumeration is AP_PAD_DISPLAY_BITS_MAX in
// ap_hooks.h; AP_PADGLOW_WORST_CASE_BITS below is the worst case it must cover,
// pinned by a static assert in ap_hooks.c and by tools/test-pad-glow-items.c.
// Worst case is a Gem Cup: its gem, plus per leg track up to 5 podium rungs,
// 15 boxes and 1 Wumpa (a Cortex Vortex leg has 5 rungs and 1 Wumpa) = 1 + 4 * 21
// = 85. A single race or trial pad tops out at 5 tiers + 1 perfect + 2 trial
// checks + 5 rungs + 15 boxes + 3 letters + 1 Wumpa = 32.
#define AP_PADGLOW_WORST_CASE_BITS \
	(1 + 4 * (CTR_CFG_PODIUM_RUNG_COUNT + AP_BOX_SLOTS_PER_TRACK + 1))

typedef int (*ap_pad_glow_query)(long code, void *ctx);

// ── boxes ────────────────────────────────────────────────────────────────────
static inline int AP_PadGlowBoxBit(long code)
{
	long off = code - AP_BOX_CODE_BASE;

	if (off < 0 || off >= AP_BOX_LOCATION_COUNT)
		return -1;
	return AP_PADGLOW_BOX_BASE + (int)off;
}

// Wire code of a box pseudo-bit, or -1.
static inline long AP_PadGlowBoxCode(int bit)
{
	int off = bit - AP_PADGLOW_BOX_BASE;

	if (off < 0 || off >= AP_BOX_LOCATION_COUNT)
		return -1;
	return AP_BOX_CODE_BASE + off;
}

// ── letters ──────────────────────────────────────────────────────────────────
static inline int AP_PadGlowLetterBit(int level, int letter)
{
	return AP_PADGLOW_LETTER_BASE + level * CTR_CFG_LETTER_COUNT + letter;
}

// 1 when `bit` is a letter pseudo-bit, with level / letter filled in.
static inline int AP_PadGlowLetterDecode(int bit, int *outLevel, int *outLetter)
{
	int off = bit - AP_PADGLOW_LETTER_BASE;

	if (off < 0 || off >= CTR_CFG_LETTER_TRACK_COUNT * CTR_CFG_LETTER_COUNT)
		return 0;
	if (outLevel)
		*outLevel = off / CTR_CFG_LETTER_COUNT;
	if (outLetter)
		*outLetter = off % CTR_CFG_LETTER_COUNT;
	return 1;
}

// Custom-track letter pseudo-bit, and its letter (0..2) or -1.
static inline int AP_PadGlowCustomLetterBit(int letter)
{
	return AP_PADGLOW_CUSTOM_LETTER_BASE + letter;
}

static inline int AP_PadGlowCustomLetter(int bit)
{
	int off = bit - AP_PADGLOW_CUSTOM_LETTER_BASE;

	return (off >= 0 && off < CTR_CFG_LETTER_COUNT) ? off : -1;
}

// ── Wumpa ────────────────────────────────────────────────────────────────────
static inline int AP_PadGlowWumpaBit(int level)
{
	return AP_PADGLOW_WUMPA_BASE + level;
}

// Level of a Wumpa pseudo-bit, or -1.
static inline int AP_PadGlowWumpaLevel(int bit)
{
	int level = bit - AP_PADGLOW_WUMPA_BASE;

	return (level >= 0 && level < CTR_CFG_WUMPA_TRACK_COUNT) ? level : -1;
}

// Wire code of any of the three families, or -1 when `bit` is none of them or
// the seed carries no such location. `cfg` supplies the letter and Wumpa rows.
static inline long AP_PadGlowItemCode(const ctr_seed_config *cfg, int bit)
{
	int level, letter;
	long code;

	code = AP_PadGlowBoxCode(bit);
	if (code >= 0)
		return code;
	if (cfg == 0)
		return -1;
	if (AP_PadGlowLetterDecode(bit, &level, &letter))
	{
		code = cfg->lettersanity_locations[level][letter];
		return code > 0 ? code : -1;
	}
	level = AP_PadGlowWumpaLevel(bit);
	if (level >= 0)
	{
		code = cfg->wumpa.tracks[level];
		return code > 0 ? code : -1;
	}
	letter = AP_PadGlowCustomLetter(bit);
	if (letter >= 0)
	{
		code = cfg->custom_letter_locations[letter];
		return code > 0 ? code : -1;
	}
	return -1;
}

// 1 when `bit` belongs to one of the three families (used to order the group
// test ahead of the podium range, which is open-ended upwards).
static inline int AP_PadGlowIsItemBit(int bit)
{
	int level, letter;

	return AP_PadGlowBoxCode(bit) >= 0 ||
	       AP_PadGlowLetterDecode(bit, &level, &letter) ||
	       AP_PadGlowWumpaLevel(bit) >= 0 ||
	       AP_PadGlowCustomLetter(bit) >= 0;
}

// Prize slot under by_reward_type (ruling of 2026-09-30): item boxes and Wumpa in
// the race slot (0), CTR letters in the token slot (2). -1 when `bit` is none
// of the three families.
static inline int AP_PadGlowItemRewardGroup(int bit)
{
	int level, letter;

	if (AP_PadGlowBoxCode(bit) >= 0 || AP_PadGlowWumpaLevel(bit) >= 0)
		return 0;
	if (AP_PadGlowLetterDecode(bit, &level, &letter) || AP_PadGlowCustomLetter(bit) >= 0)
		return 2;
	return -1;
}

// ── gathers ──────────────────────────────────────────────────────────────────
// Every gather mirrors the predicate its pad-state count uses, and none of them
// touch AP_PadState: they only add display entries.

// Boxes still standing on one track. `slots` is how many placements the level
// holds (the spawn set), so a box counts only if it has geometry, is a live
// location and is unchecked, exactly like AP_BoxMap_CountStanding.
static inline int AP_PadGlowAppendBoxes(int levelID, int slots, int *outBits,
                                        int cap, int count,
                                        ap_pad_glow_query exists,
                                        ap_pad_glow_query checked, void *ctx)
{
	int slot;

	if (outBits == 0 || exists == 0 || checked == 0 || count < 0)
		return count;
	if (AP_BoxMap_ApTrack(levelID) < 0)
		return count;
	if (slots > AP_BOX_SLOTS_PER_TRACK)
		slots = AP_BOX_SLOTS_PER_TRACK;
	for (slot = 0; slot < slots && count < cap; slot++)
	{
		long code = AP_BoxMap_Code(levelID, slot);
		int bit;

		if (code < 0 || !exists(code, ctx) || checked(code, ctx))
			continue;
		bit = AP_PadGlowBoxBit(code);
		if (bit >= 0)
			outBits[count++] = bit;
	}
	return count;
}

// Unchecked CTR letters of one track (`codes` = its lettersanity row).
static inline int AP_PadGlowAppendLetters(const long *codes, int levelID,
                                          int *outBits, int cap, int count,
                                          ap_pad_glow_query exists,
                                          ap_pad_glow_query checked, void *ctx)
{
	int letter;

	if (codes == 0 || outBits == 0 || exists == 0 || checked == 0 || count < 0 ||
	    levelID < 0 || levelID >= CTR_CFG_LETTER_TRACK_COUNT)
		return count;
	for (letter = 0; letter < CTR_CFG_LETTER_COUNT && count < cap; letter++)
		if (codes[letter] >= 0 && exists(codes[letter], ctx) &&
		    !checked(codes[letter], ctx))
			outBits[count++] = AP_PadGlowLetterBit(levelID, letter);
	return count;
}

// The per-track Wumpa location of one level, when open.
static inline int AP_PadGlowAppendWumpa(long code, int levelID, int *outBits,
                                        int cap, int count,
                                        ap_pad_glow_query exists,
                                        ap_pad_glow_query checked, void *ctx)
{
	if (outBits == 0 || exists == 0 || checked == 0 || count < 0 || count >= cap ||
	    levelID < 0 || levelID >= CTR_CFG_WUMPA_TRACK_COUNT)
		return count;
	if (code >= 0 && exists(code, ctx) && !checked(code, ctx))
		outBits[count++] = AP_PadGlowWumpaBit(levelID);
	return count;
}

#endif // AP_PAD_GLOW_ITEMS_H
