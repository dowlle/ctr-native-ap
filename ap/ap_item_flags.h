#ifndef AP_ITEM_FLAGS_H
#define AP_ITEM_FLAGS_H

// AP item classification flags + the precedence that resolves them (issue #195).
//
// Freestanding by design, so an out-of-engine harness
// (tools/test-feed-class.c) can pin the precedence deterministically without
// pulling in the game. The hub item feed resolves a font colour from the class
// ordinal returned here, and the reward-glow marker tint (AP_ClassTint in
// ap_hooks.c) resolves from the same ordinal, so the two presentations share
// ONE precedence and can never disagree about it.

// AP item flags, as carried by NetworkItem.flags and the scout cache: bit0 =
// progression, bit1 = useful, bit2 = trap, 0 = filler. Any other bits are
// ignored by the precedence below (they do not change the class).
#define AP_ITEM_FLAG_PROGRESSION 1
#define AP_ITEM_FLAG_USEFUL      2
#define AP_ITEM_FLAG_TRAP        4

// Winning class for a set of flags, ordered so progression beats useful (an
// item carrying both flags presents as progression, matching how AP's own
// clients show it), useful beats trap, and trap beats the filler default.
typedef enum
{
	AP_ITEM_CLASS_PROGRESSION = 0,
	AP_ITEM_CLASS_USEFUL = 1,
	AP_ITEM_CLASS_TRAP = 2,
	AP_ITEM_CLASS_FILLER = 3
} AP_ItemClass;

static inline AP_ItemClass AP_ItemFlagsClass(unsigned flags)
{
	if (flags & AP_ITEM_FLAG_PROGRESSION)
		return AP_ITEM_CLASS_PROGRESSION;
	if (flags & AP_ITEM_FLAG_USEFUL)
		return AP_ITEM_CLASS_USEFUL;
	if (flags & AP_ITEM_FLAG_TRAP)
		return AP_ITEM_CLASS_TRAP;
	return AP_ITEM_CLASS_FILLER; // unknown / missing flags -> filler default
}

#endif // AP_ITEM_FLAGS_H
