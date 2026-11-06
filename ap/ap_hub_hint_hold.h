#ifndef AP_HUB_HINT_HOLD_H
#define AP_HUB_HINT_HOLD_H

// Hub streaming waits for a mask hint (A3-12, the Aku Aku mask-hint glitch).
//
// An Adventure hub keeps the next hub in its second mempack: walking onto a
// door corridor's trigger quads reads the neighbour hub into the pack the
// player is NOT on (LOAD_Hub_Main -> LOAD_Hub_ReadFile), and a later trigger
// swaps the packs (LOAD_Hub_SwapNow). A mask hint loads its talking mask into
// that same pack (AH_MaskHint_Start -> LOAD_TalkingMask). The two never wait
// for each other:
//
// - A hub read while the hint runs wipes the pack under the mask: the read
//   clears modelMaskHints3D and loads the next hub's textures over the
//   mask's. The mask turns to a smear and the hint never finishes (seen in
//   the headless client, 2026-11-07). This is the reported glitch at the
//   N. Sanity Beach to Gem Stone Valley door, where the new-area greeting
//   used to start beside the corridor (once per save, hence "save-slot
//   dependent"); AP has pre-latched that greeting since issue #17.
// - A hint that starts after the next hub was read evicts it from the pack.
//   The pack is marked empty, but the hub's level pointer stays, so a swap
//   without a fresh read swaps in the mask's memory (the #17 door crash).
//
// The hold: while a hint runs, a hub read is remembered instead of done, and
// a hub the mask evicts is remembered for a re-read. A swap waits while a
// hint runs or a remembered read is still to be issued. Once the hint has
// ended, the remembered read is issued if the player is still in the hub it
// was meant for, and the swap follows once that read is queued (the swap
// itself stalls until the queued read lands, as retail's does). Without a
// hint nothing changes.
//
// Freestanding on purpose: tools/test-hub-hint-hold.c drives it on the host.

typedef struct
{
	int lev;  // hub to read into the second pack once the hint ends, -1 none
	int from; // the hub the player was in when it was remembered
} ap_hub_hint_hold;

// Adventure hubs, Gem Stone Valley (25) to Citadel City (29).
static inline int ap_hub_hint_hold_is_hub(int levelID)
{
	return levelID >= 25 && levelID <= 29;
}

// A hub read is about to start. Returns 1 when it must wait for the hint;
// it is remembered then.
static inline int ap_hub_hint_hold_read(ap_hub_hint_hold *h, int hintState, int levID, int currLevel)
{
	if (hintState == 0)
		return 0;
	h->lev = levID;
	h->from = currLevel;
	return 1;
}

// The talking mask is about to evict `packLev` from the second pack.
static inline void ap_hub_hint_hold_evict(ap_hub_hint_hold *h, int packLev, int currLevel)
{
	if (ap_hub_hint_hold_is_hub(packLev) && ap_hub_hint_hold_is_hub(currLevel))
	{
		h->lev = packLev;
		h->from = currLevel;
	}
}

// Once per hub stream tick. Returns the hub to read now, or -1. A read meant
// for a hub the player has left is dropped.
static inline int ap_hub_hint_hold_release(ap_hub_hint_hold *h, int hintState, int currLevel)
{
	int lev = h->lev;
	if (lev < 0 || hintState != 0)
		return -1;
	h->lev = -1;
	return h->from == currLevel ? lev : -1;
}

// Must a requested pack swap wait?
static inline int ap_hub_hint_hold_swap_waits(const ap_hub_hint_hold *h, int hintState)
{
	return hintState != 0 || h->lev >= 0;
}

#endif
