#ifndef AP_SPOILER_ITEMS_LOGIC_H
#define AP_SPOILER_ITEMS_LOGIC_H
// Spoiler Items (#459): which held-back previews show. Freestanding: no engine
// types, no IO. ap/ap_spoiler_items.c feeds it the seed option and the two
// receipts; tools/test-spoiler-items.py pins every combination on the host.
//
// ctr_options.spoiler_items (schema 18, absent = off) hides two presentations
// until the slot receives the item that restores it:
//   Warp Pad Preview (35010204)  the floating reward previews above warp pads.
//   AP Box Colours   (35010205)  the scouted-item colours of AP item boxes.
// Off, or the item received: the display is exactly what it was before #459.
// Neither item touches a gate: AP_PadState, the pad counters and every check
// keep working while a preview is hidden.

// Whether warp pads show their reward previews.
static inline int AP_Spoiler_PadPreviewShown(int spoilerOn, int previewReceived)
{
	return !spoilerOn || previewReceived;
}

// Whether AP item boxes may wear their item colours. This only lifts the
// spoiler lock; the seed's color_boxes_by_item and the player's own Options row
// still decide afterwards (AP_BoxColour_Enabled), so the item can never turn on
// colours a seed or a player turned off.
static inline int AP_Spoiler_BoxColoursUnlocked(int spoilerOn, int boxColoursReceived)
{
	return !spoilerOn || boxColoursReceived;
}

// How many display bits the warp pad glow pass may lay out (AH_WarpPad.c): n
// while previews are shown, 0 while held back. -1 (not an AP glow destination,
// vanilla glow) passes through unchanged. Both layouts select from this count,
// so one_pile and by_reward_type hide alike.
static inline int AP_Spoiler_PadDisplayCount(int previewShown, int n)
{
	return (previewShown || n < 0) ? n : 0;
}

// Whether a held-back pad shows one neutral Archipelago marker in slot 0
// instead (#459 ruling, 2026-10-28): yes while the pad still has
// any open location behind it, n > 0, which counts every glow family (rewards,
// podium rungs, item boxes, CTR letters and Wumpa). A finished pad, n == 0,
// shows nothing, and so does a pad with previews shown or a vanilla pad.
// markerReady: the Archipelago marker model is parked; without it the pad
// stays hidden rather than showing a placeholder model.
static inline int AP_Spoiler_PadNeutralMarker(int previewShown, int n, int markerReady)
{
	return !previewShown && n > 0 && markerReady;
}

#endif // AP_SPOILER_ITEMS_LOGIC_H
