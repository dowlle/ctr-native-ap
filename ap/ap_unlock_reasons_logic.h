#ifndef AP_UNLOCK_REASONS_LOGIC_H
#define AP_UNLOCK_REASONS_LOGIC_H

// A3-17: unlock reasons in the in-game AP log feed.
//
// When a received item opens something (a pad, a pad's stage 2, a hub door, a
// Wumpa Shop shelf, a boss garage), the feed says what opened in short plain
// words, for example "Crash Cove OPEN" or "Ripper Roo GARAGE OPEN". The feed's
// own receipt line ("TROPHY", "TROPHY KEY FROM <PLAYER>") lands in the queue
// immediately before, so the pairing carries the cause; the reason line stays
// free of the item name.
//
// Spam rules (the 2026-11-06 work order): keep the five-line feed and the
// A3-21 one-line trap rule; at most ONE reason line per drained item, and only
// when something actually opened. One line names the first new opening in a
// fixed priority order (garage, door, pad, stage 2, shop) and summarizes the
// rest as " (+N MORE)".
//
// This header is the pure half: the watch layout, the pad-slot mapping, the
// before/after diff and the line wording. The engine half lives in
// ap/ap_hooks_gates.c (capture from the live gates, feed hand-off) so that
// tools/test-unlock-reasons.c pins every behaviour here out of engine.
//
// Header-only and free of engine types, like ap_pad_state.h.

#include <stdio.h>

// Watched pads: the 22 dense warp pads (0..19, 21, 23) plus the five gem cup
// pads (100..104), mapped to bits 0..26. These are the only engine LevelIDs
// that carry a warp pad.
#define AP_UNLOCK_PAD_COUNT 27
// Hub doors: N. Sanity Beach -> Gem Stone Valley, the two-key Glacier Park
// doors, Glacier Park -> Citadel City, Gem Stone Valley -> the cup room. One
// bit each, in AP_DoorBitPure bit order (1, 2, 4, 8).
#define AP_UNLOCK_DOOR_COUNT 4
// The four boss garages plus Oxide's garage (bit 4).
#define AP_UNLOCK_GARAGE_COUNT 5
// Wumpa Shop hubs, in the apworld's hub order (ap_wumpa_shop.h).
#define AP_UNLOCK_SHOP_HUBS 5

typedef struct AP_UnlockWatch
{
	unsigned padOpen;     // bit per pad slot: stage 1 + racer lock met
	unsigned padStage2;   // bit per pad slot: stage-2 requirement met
	unsigned doorsReady;  // bit per door: received Keys reach its requirement
	unsigned garagesOpen; // bit per garage: requirement met
	unsigned char shopOpen[AP_UNLOCK_SHOP_HUBS]; // unlocked (on-sale) slots per hub
	unsigned char shopKnown; // the lifetime storage has resolved; 0 -> shop diff silent
} AP_UnlockWatch;

enum AP_UnlockKind
{
	AP_UNLOCK_PAD = 0,
	AP_UNLOCK_STAGE2,
	AP_UNLOCK_DOOR,
	AP_UNLOCK_GARAGE,
	AP_UNLOCK_SHOP
};

typedef struct AP_UnlockEvent
{
	int kind;  // enum AP_UnlockKind
	int index; // pad slot, door slot, garage index or shop hub
} AP_UnlockEvent;

// Physical pad LevelID -> watch bit slot, or -1 when the id is no warp pad.
static inline int AP_UnlockPadSlot(int physLevelID)
{
	if (physLevelID >= 0 && physLevelID <= 19)
		return physLevelID;
	if (physLevelID == 21)
		return 20;
	if (physLevelID == 23)
		return 21;
	if (physLevelID >= 100 && physLevelID <= 104)
		return 22 + (physLevelID - 100);
	return -1;
}

// Watch bit slot -> physical pad LevelID, or -1 out of range.
static inline int AP_UnlockSlotPad(int slot)
{
	if (slot >= 0 && slot <= 19)
		return slot;
	if (slot == 20)
		return 21;
	if (slot == 21)
		return 23;
	if (slot >= 22 && slot <= 26)
		return 100 + (slot - 22);
	return -1;
}

// New openings in `after` that were not open in `before`, in the fixed
// priority order garage, door, pad, stage 2, shop. Only ever 0 -> 1
// transitions: every watched condition is monotone in the received-item
// counts (counts never go down within a session), and the shop's lifetime
// total never goes down either. Returns the event count (<= cap).
static inline int AP_UnlockDiff(const AP_UnlockWatch *before, const AP_UnlockWatch *after,
                                AP_UnlockEvent *out, int cap)
{
	int n = 0;
	int i;
	unsigned rose;

	rose = after->garagesOpen & ~before->garagesOpen;
	for (i = 0; i < AP_UNLOCK_GARAGE_COUNT && n < cap; i++)
		if (rose & (1u << i))
		{
			out[n].kind = AP_UNLOCK_GARAGE;
			out[n].index = i;
			n++;
		}

	rose = after->doorsReady & ~before->doorsReady;
	for (i = 0; i < AP_UNLOCK_DOOR_COUNT && n < cap; i++)
		if (rose & (1u << i))
		{
			out[n].kind = AP_UNLOCK_DOOR;
			out[n].index = i;
			n++;
		}

	rose = after->padOpen & ~before->padOpen;
	for (i = 0; i < AP_UNLOCK_PAD_COUNT && n < cap; i++)
		if (rose & (1u << i))
		{
			out[n].kind = AP_UNLOCK_PAD;
			out[n].index = i;
			n++;
		}

	rose = after->padStage2 & ~before->padStage2;
	for (i = 0; i < AP_UNLOCK_PAD_COUNT && n < cap; i++)
		if (rose & (1u << i))
		{
			out[n].kind = AP_UNLOCK_STAGE2;
			out[n].index = i;
			n++;
		}

	// While either side predates the lifetime storage reply, shop counts are
	// a baseline, not an opening: a late Get reply must not toast shelves that
	// were on sale all along.
	if (before->shopKnown && after->shopKnown)
		for (i = 0; i < AP_UNLOCK_SHOP_HUBS && n < cap; i++)
			if (after->shopOpen[i] > before->shopOpen[i])
			{
				out[n].kind = AP_UNLOCK_SHOP;
				out[n].index = i;
				n++;
			}

	return n;
}

// Keep only the events of one kind, compacting in place; returns the new
// count. The quiet per-frame call (before the drain, and after it when
// nothing was drained) announces shop shelves only: everything else the
// watch reads is a received-item tally whose transitions are caught by the
// per-item call, so a quiet-frame diff of those kinds can only be a non-item
// change (a boss win, a venue download finishing), which is adopted silently
// rather than announced behind an unrelated receipt.
static inline int AP_UnlockEventsOnly(AP_UnlockEvent *ev, int n, int kind)
{
	int w = 0;
	int i;
	for (i = 0; i < n; i++)
		if (ev[i].kind == kind)
			ev[w++] = ev[i];
	return w;
}

// The one feed line for an event, naming what opened. `name` is the
// destination track, hub, boss or shop hub, trimmed to 24 characters so the
// kind suffix and the " (+N MORE)" summary always fit the feed's small font.
// `extra` is how many further openings the same item caused (events - 1).
static inline int AP_UnlockReasonLine(char *out, int cap, int kind, const char *name, int extra)
{
	const char *suffix;
	int n;

	switch (kind)
	{
	case AP_UNLOCK_PAD:
		suffix = "OPEN";
		break;
	case AP_UNLOCK_STAGE2:
		suffix = "STAGE 2 OPEN";
		break;
	case AP_UNLOCK_DOOR:
		suffix = "DOOR OPEN";
		break;
	case AP_UNLOCK_GARAGE:
		suffix = "GARAGE OPEN";
		break;
	case AP_UNLOCK_SHOP:
		suffix = "SHOP STOCK OPEN";
		break;
	default:
		suffix = "OPEN";
		break;
	}

	n = snprintf(out, (size_t)cap, "%.*s %s", 24, name != 0 ? name : "?", suffix);
	if (extra > 0 && n > 0 && n < cap)
		n += snprintf(out + n, (size_t)(cap - n), " (+%d MORE)", extra);
	return n;
}

#endif // AP_UNLOCK_REASONS_LOGIC_H
