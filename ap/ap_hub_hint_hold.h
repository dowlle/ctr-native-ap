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
// The hold: while a hint runs, a corridor read is remembered instead of done,
// a hub the mask evicts is remembered for a re-read, and a corridor swap is
// remembered with the hub it was asked in. Once the hint has ended the
// remembered read goes first (only while the player is still in the hub it was
// meant for), and the remembered swap follows on a later tick, again only in
// that hub and only when the second pack holds a hub read or being read, so
// LOAD_Hub_SwapNow never waits on an empty pack. Anything stale is dropped.
// The hold keeps its own swap request instead of retail's
// bool_AdvHub_NeedToSwapLEV, which the cutscene thread owns and which nothing
// would ever clear after a full level load. Without a hint nothing changes.
//
// Freestanding on purpose: tools/test-hub-hint-hold.c drives it on the host.

typedef struct
{
	int lev;      // hub to read into the second pack once the hint ends, -1 none
	int from;     // the hub the player was in when it was remembered
	int swap;     // a corridor swap was asked during the hint
	int swapFrom; // the hub it was asked in
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

// Once per hub stream tick, before the corridor. Returns the hub to read now,
// or -1. A read meant for a hub the player has left is dropped.
static inline int ap_hub_hint_hold_release(ap_hub_hint_hold *h, int hintState, int currLevel)
{
	int lev = h->lev;
	if (lev < 0 || hintState != 0)
		return -1;
	h->lev = -1;
	return h->from == currLevel ? lev : -1;
}

// A corridor swap is asked; `packLev` is levID_in_each_mempack of the second
// pack. Returns 1 when it must not run now: during a hint or with a read still
// held it is remembered; with no hub in the second pack it is skipped, because
// LOAD_Hub_SwapNow would wait forever on it (only a hint can lead there).
static inline int ap_hub_hint_hold_swap(ap_hub_hint_hold *h, int hintState, int currLevel, int packLev)
{
	if (hintState == 0 && h->lev < 0)
		return !ap_hub_hint_hold_is_hub(packLev);
	h->swap = 1;
	h->swapFrom = currLevel;
	return 1;
}

// Once per hub stream tick, after a held read was issued on an earlier tick.
// Returns 1 when the remembered swap is to be done now: the hint is over, no
// read is still held, the player is in the hub it was asked in, and the second
// pack holds a hub (`packLev`, levID_in_each_mempack of that pack). A stale
// swap is dropped.
static inline int ap_hub_hint_hold_swap_release(ap_hub_hint_hold *h, int hintState, int currLevel, int packLev)
{
	if (!h->swap || hintState != 0 || h->lev >= 0)
		return 0;
	h->swap = 0;
	return h->swapFrom == currLevel && ap_hub_hint_hold_is_hub(packLev);
}

#endif
