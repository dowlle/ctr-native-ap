#ifndef AP_PICKERLINE_H
#define AP_PICKERLINE_H

// ---------------------------------------------------------------------------
// The one small line under the highlighted racer's name in the hub picker.
//
// It exists as a header, apart from AP_CharPicker_Draw, because the thing that
// went wrong with it is a WIDTH, and a width is only checkable if a harness can
// compose the string. The line is drawn JUSTIFY_CENTER at x=256 in a 512-wide
// frame, so anything wider than the frame loses characters off BOTH ends and
// the player cannot even tell what was cut. That is what shipped: the line also
// carried the ownership mode and the editor state, giving
// "PER-CHARACTER  BOOST 0/3 NONE  (READ-ONLY)  LOCKED" -- 50 characters, which
// the Deck rendered as "...HARACTER  BOOST 0/3 NONE  READ-ONLY  L..."
// (field session 2026-08-20).
//
// The content was the real fault, not the font. Ownership mode and editor
// availability are SEED-WIDE facts: identical on all 16 tiles, unchanged by
// moving the cursor, and therefore not per-character information at all. They
// are dropped (ruling 2026-08-20). What is left is what actually varies per
// racer: the boost tier it would race at, and whether it is locked.
//
// AP_PICKERLINE_MAX is the budget the harness holds this to. The worst case is
// a locked racer at a Blue Fire ceiling with the longest tier name, and it must
// stay comfortably inside the frame at FONT_SMALL rather than merely fit.
// ---------------------------------------------------------------------------

#include <stdio.h>

// Characters, excluding the terminator. Held by tools/test-picker-infoline.c.
#define AP_PICKERLINE_MAX 30

// Compose the line. `tier` is the tier this racer DRIVES (the rank selector's
// applied tier, ruling 2026-09-11 extended to Boost 2026-10-26) and `tierName`
// the caller's name for it (the picker's ap_cs_boostTierName); `ceiling` is the
// top of the seed's boost chain. `tier < 0` means the seed gives this racer no
// boost capability at all, in which case there is no BOOST clause to show and
// the line is either "NOT UNLOCKED" or empty.
//
// `maxName` is the name of the tier the received items unlock, passed only when
// the player has LOWERED the selection below it (NULL otherwise); the line then
// says "MAX <name>" so the choice is visible. A locked racer shows NOT UNLOCKED
// instead: that is the fact the player needs on that tile, and both clauses
// together would break the width budget.
//
// Returns the length written, so a caller or harness can check it.
static int AP_PickerLine_Compose(char *out, unsigned long outSize, int tier, int ceiling, const char *tierName,
                                 const char *maxName, int unlocked)
{
	int n;

	if (out == 0 || outSize == 0)
		return 0;

	if (tier >= 0)
	{
		const char *tail = "";
		const char *tailName = "";
		if (!unlocked)
			tail = "  NOT UNLOCKED";
		else if (maxName != 0 && maxName[0] != '\0')
		{
			tail = "  MAX ";
			tailName = maxName;
		}
		n = snprintf(out, (size_t)outSize, "BOOST %d/%d %s%s%s", tier, ceiling, (tierName != 0) ? tierName : "?",
		             tail, tailName);
	}
	else
	{
		n = snprintf(out, (size_t)outSize, "%s", unlocked ? "" : "NOT UNLOCKED");
	}

	// snprintf returns what it WOULD have written. Report what is there.
	if (n < 0)
	{
		out[0] = '\0';
		return 0;
	}
	if ((unsigned long)n >= outSize)
		return (int)(outSize - 1);

	return n;
}

#endif // AP_PICKERLINE_H
