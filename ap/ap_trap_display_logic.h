#ifndef AP_TRAP_DISPLAY_LOGIC_H
#define AP_TRAP_DISPLAY_LOGIC_H

// Trap Display (ruled 2026-11-06, decision D4 A): how a location holding a trap
// looks before it is collected. ctr_options.trap_display is absent for `shown`,
// 1 for `as_progression` and 2 for `mixed`; `mixed` also carries
// ctr_options.trap_display_salt. Freestanding, so tools/test-trap-display.c
// pins the same mapping the box colour, pad glow and Wumpa Shop sites use.
//
// Only the scouted flags a display reads are mapped. Hints, the feed after a
// check and the AP server keep the real flags.

#include "ap_item_flags.h"

#define AP_TRAP_DISPLAY_SHOWN 0
#define AP_TRAP_DISPLAY_AS_PROGRESSION 1
#define AP_TRAP_DISPLAY_MIXED 2

// `mixed`: does this location's trap look like progression (1) or filler (0)?
// A fixed hash of the seed's salt and the location id, so every look at one
// location in one room agrees.
static inline int AP_TrapDisplayMixedLooksProgression(unsigned salt, long long location)
{
	unsigned long long x = (unsigned long long)location ^ ((unsigned long long)salt << 32);
	x += 0x9e3779b97f4a7c15ull;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
	x ^= x >> 31;
	return (int)(x & 1u);
}

// The flags a display should resolve for one scouted location. An item that is
// also progression or useful already shows as that, so only a plain trap moves.
static inline unsigned AP_TrapDisplayFlags(int mode, unsigned salt, long long location,
                                           unsigned flags)
{
	if ((flags & AP_ITEM_FLAG_TRAP) == 0 ||
	    (flags & (AP_ITEM_FLAG_PROGRESSION | AP_ITEM_FLAG_USEFUL)) != 0)
		return flags;
	switch (mode)
	{
	case AP_TRAP_DISPLAY_AS_PROGRESSION:
		return (flags & ~AP_ITEM_FLAG_TRAP) | AP_ITEM_FLAG_PROGRESSION;
	case AP_TRAP_DISPLAY_MIXED:
		return AP_TrapDisplayMixedLooksProgression(salt, location)
		           ? ((flags & ~AP_ITEM_FLAG_TRAP) | AP_ITEM_FLAG_PROGRESSION)
		           : (flags & ~AP_ITEM_FLAG_TRAP);
	default:
		return flags;
	}
}

// A pre-collection name must agree with the disguised colour. Keep the real
// scout and item id intact for hints, receipt messages and collected rewards.
// NULL means the caller should use the item's ordinary name and aliases.
static inline const char *AP_TrapDisplayName(int mode, unsigned salt, long long location,
                                            unsigned flags, int collected)
{
	unsigned display = AP_TrapDisplayFlags(mode, salt, location, flags);
	if (collected || display == flags)
		return 0;
	return (display & AP_ITEM_FLAG_PROGRESSION) ? "PROGRESSION ITEM" : "FILLER ITEM";
}

// Wumpa Shop disguise (ruled 2026-11-10, GR-03 / A4-50): in the shop a
// disguised trap picks PROGRESSION ITEM or FILLER ITEM per location from the
// same salt hash `mixed` uses, in either disguise mode, so the label no
// longer tracks the mode. Boxes and pad glow keep the mode's own mapping.
// `as_progression` carries no salt envelope, so its split comes from the
// location id alone and is still fixed per slot within one seed.
static inline unsigned AP_TrapDisplayShopFlags(int mode, unsigned salt, long long location,
                                               unsigned flags)
{
	if ((flags & AP_ITEM_FLAG_TRAP) == 0 ||
	    (flags & (AP_ITEM_FLAG_PROGRESSION | AP_ITEM_FLAG_USEFUL)) != 0)
		return flags;
	if (mode != AP_TRAP_DISPLAY_AS_PROGRESSION && mode != AP_TRAP_DISPLAY_MIXED)
		return flags;
	return AP_TrapDisplayMixedLooksProgression(salt, location)
	           ? ((flags & ~AP_ITEM_FLAG_TRAP) | AP_ITEM_FLAG_PROGRESSION)
	           : (flags & ~AP_ITEM_FLAG_TRAP);
}

// The shop's pre-collection name for the same display flags, so name and
// colour always come from one mapping. NULL as in AP_TrapDisplayName.
static inline const char *AP_TrapDisplayShopName(int mode, unsigned salt, long long location,
                                                 unsigned flags, int collected)
{
	unsigned display = AP_TrapDisplayShopFlags(mode, salt, location, flags);
	if (collected || display == flags)
		return 0;
	return (display & AP_ITEM_FLAG_PROGRESSION) ? "PROGRESSION ITEM" : "FILLER ITEM";
}

#endif // AP_TRAP_DISPLAY_LOGIC_H
