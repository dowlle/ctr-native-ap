// ap_hint_filters_logic.h -- hint filters (A2-23), the pure decisions.
//
// Three client settings, all off by default (absent = off), that only narrow
// which locations the auto hints name; none changes a location, an item or
// logic:
//   pad_hints_wait_for_spoiler  with spoiler_items on, Pad Approach Hints wait
//                               for Warp Pad Preview (the receipt that also
//                               shows the pad glow).
//   pad_hints_progression_only  Pad Approach Hints name only locations whose
//                               item is progression.
//   shop_hints_progression_only a Wumpa Shop page hints only slots whose item
//                               is progression.
// A filter runs BEFORE the room latch is written, so a location it skipped is
// still hintable after the preview arrives or a setting changes.
//
// Header-only and engine-free so tools/test-hint-filters.cpp can run it.
#ifndef AP_HINT_FILTERS_LOGIC_H
#define AP_HINT_FILTERS_LOGIC_H

#define AP_HINT_FLAG_PROGRESSION 1u // Archipelago NetworkItem flags bit 0: advancement

// Whether a pad must hold its hints back now. wait: the seed's
// pad_hints_wait_for_spoiler. spoilerOn: the seed's spoiler_items. previewShown:
// Warp Pad Preview received. Seeds without Spoiler Items never wait.
static inline int AP_HintFilter_PadWaits(int wait, int spoilerOn, int previewShown)
{
	return wait && spoilerOn && !previewShown;
}

// Result of asking whether one location passes a progression-only filter.
enum
{
	AP_HINT_DROP = 0,  // its item is not progression: never hint it
	AP_HINT_KEEP = 1,  // hint it
	AP_HINT_LATER = 2  // its item is not known yet (scouts not in): try again
};

// only: the filter's setting. known: the scout cache holds this location.
// flags: the scouted item's NetworkItem flags.
static inline int AP_HintFilter_Progression(int only, int known, unsigned flags)
{
	if (!only) return AP_HINT_KEEP;
	if (!known) return AP_HINT_LATER;
	return (flags & AP_HINT_FLAG_PROGRESSION) ? AP_HINT_KEEP : AP_HINT_DROP;
}

#endif
