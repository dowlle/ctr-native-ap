#ifndef AP_RUNG_FEED_REASON_LOGIC_H
#define AP_RUNG_FEED_REASON_LOGIC_H

// Freestanding pure form of ap_hooks.c's AP_RungFeedReason (podium-rung feed
// qualifier text), split out so tools/test-item-aliases.c can pin the exact
// wording out of engine instead of a copy of it (issue #324: the held-position
// reasons shortened from "BE IN 1ST/3RD/5TH" to "IN 1ST/3RD/5TH"; the finish
// rungs are unchanged). rungTag values match the AP_RUNG_* tags ap_hooks.c
// defines (0 held_1st, 1 held_3rd, 2 held_5th, 3 finish_podium, 4 finish_any);
// this header intentionally uses the same literal ints AP_CeremonyPrefix
// already does rather than pulling in the enum's owning translation unit.
static inline const char *AP_RungFeedReasonPure(int rungTag)
{
	switch (rungTag)
	{
	case 0: return "IN 1ST";
	case 1: return "IN 3RD";
	case 2: return "IN 5TH";
	case 3: return "FINISH ON PODIUM";
	case 4: return "FINISH";
	default: return "PODIUM RUNG";
	}
}

#endif // AP_RUNG_FEED_REASON_LOGIC_H
