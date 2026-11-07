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

#endif // AP_TRAP_DISPLAY_LOGIC_H
