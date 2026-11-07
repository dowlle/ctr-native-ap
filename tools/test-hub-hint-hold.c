// cc -std=c99 -Wall -Wextra -Werror -I ap tools/test-hub-hint-hold.c -o /tmp/test-hub-hint-hold
// A3-12: hub streaming waits for a mask hint (ap/ap_hub_hint_hold.h).
//
// A small model of the hub stream and the talking mask: the second pack holds
// either a hub, the mask, or nothing; a hub read wipes the pack and clears the
// mask model; the mask load evicts the hub but leaves its level pointer, and a
// swap takes whatever the second pack holds once its queued read has landed.
// Each scenario runs with and without the hold. Without it the model shows the
// two failures the hold exists for (a hint whose mask is wiped, a swap into
// the mask's memory); with it neither happens and the swap gets the right hub.
// The model follows game/LOAD/LOAD_Hub.c and LOAD_Level.c; the hook points
// themselves are pinned in tools/test-hub-hint-hold-production.py.

#include <assert.h>
#include <stdio.h>

#include "ap_hub_hint_hold.h"

enum { NSB = 26, GSV = 25, MASK = 1000 };

typedef struct
{
	int useHold;
	ap_hub_hint_hold hold;
	int level;        // hub the player is in
	int packLev;      // levID_in_each_mempack of the second pack (-1 empty)
	int packContent;  // what the second pack's memory holds: a hub, MASK or -1
	int level2;       // level pointer of the second pack, 0 = none (a hub id here)
	int queued;       // hub read queued and not landed yet (-1 none)
	int hint;         // AkuAkuHintState
	int maskModel;    // modelMaskHints3D set
	int needSwap;     // bool_AdvHub_NeedToSwapLEV
	int swappedTo;    // hub the last swap brought in (-1 none), MASK if broken
	int swaps;        // swaps done
	int stalled;      // LOAD_Hub_SwapNow would wait forever (level2 0, nothing queued)
	int maskWiped;    // a read cleared the mask while the hint ran
} Model;

static void Init(Model *m, int useHold)
{
	m->useHold = useHold;
	m->hold.lev = -1;
	m->hold.from = -1;
	m->hold.swap = 0;
	m->hold.swapFrom = -1;
	m->level = NSB;
	m->packLev = -1;
	m->packContent = -1;
	m->level2 = 0;
	m->queued = -1;
	m->hint = 0;
	m->maskModel = 0;
	m->needSwap = 0;
	m->swappedTo = -1;
	m->swaps = 0;
	m->stalled = 0;
	m->maskWiped = 0;
}

// LOAD_Hub_ReadFile
static void ReadFile(Model *m, int lev)
{
	if (m->packLev == lev)
		return;
	if (m->hint != 0 && m->maskModel)
		m->maskWiped = 1;
	m->maskModel = 0;
	m->packContent = -1;
	m->level2 = 0;
	m->packLev = lev;
	m->queued = lev;
}

// The load queue lands one file per call.
static void LandQueue(Model *m)
{
	if (m->queued >= 0)
	{
		m->packContent = m->queued;
		m->level2 = m->queued;
		m->queued = -1;
	}
}

// LOAD_Hub_SwapNow: stalls until level2 is set, loading queued files; with
// nothing queued it would wait forever. Then the packs trade places: the hub
// left behind becomes the second pack, as LOAD_HubSwapPtrs does.
static void SwapNow(Model *m)
{
	int out = m->level;
	while (m->level2 == 0 && m->queued >= 0)
		LandQueue(m);
	if (m->level2 == 0)
	{
		m->stalled = 1;
		return;
	}
	m->swappedTo = m->packContent == m->level2 ? m->level2 : MASK;
	m->swaps++;
	m->level = m->level2;
	m->packLev = out;
	m->packContent = out;
	m->level2 = out;
}

// LOAD_TalkingMask, at AH_MaskHint_Start
static void MaskStart(Model *m)
{
	if (m->useHold)
		ap_hub_hint_hold_evict(&m->hold, m->packLev, m->level);
	m->packLev = -1;
	m->packContent = MASK;
	m->maskModel = 1;
}

// LOAD_Hub_Main with the driver on a read quad (readLev) or a swap quad.
static void HubMain(Model *m, int readLev, int swapQuad)
{
	if (m->useHold)
	{
		int held = ap_hub_hint_hold_release(&m->hold, m->hint, m->level);
		if (held >= 0)
		{
			ReadFile(m, held);
			return;
		}
		if (ap_hub_hint_hold_swap_release(&m->hold, m->hint, m->level, m->packLev))
		{
			m->needSwap = 0;
			SwapNow(m);
			return;
		}
	}
	if (readLev < 0)
	{
		if (swapQuad || m->needSwap)
		{
			if (m->useHold && ap_hub_hint_hold_swap(&m->hold, m->hint, m->level, m->packLev))
				return;
			m->needSwap = 0;
			SwapNow(m);
		}
		return;
	}
	if (m->useHold && ap_hub_hint_hold_read(&m->hold, m->hint, readLev, m->level))
		return;
	ReadFile(m, readLev);
}

// A full level load (a warp pad race, a boss, a menu) leaves the hub stream.
static void FullLoad(Model *m, int level)
{
	m->level = level;
	m->packLev = -1;
	m->packContent = -1;
	m->level2 = 0;
	m->queued = -1;
}

int main(void)
{
	Model m;
	int useHold, checks = 0;

	for (useHold = 0; useHold < 2; useHold++)
	{
		// No hint: read on the corridor, land, swap. Identical with and without the hold.
		Init(&m, useHold);
		HubMain(&m, GSV, 0);
		LandQueue(&m);
		HubMain(&m, -1, 1);
		assert(m.swappedTo == GSV && m.level == GSV && !m.maskWiped);
		checks++;

		// The glitch: a hint is on screen when the player rolls onto the read quad.
		Init(&m, useHold);
		m.hint = 4;
		MaskStart(&m);
		HubMain(&m, GSV, 0);
		LandQueue(&m);
		if (!useHold)
			assert(m.maskWiped && !m.maskModel); // retail: the mask is wiped under the hint
		else
		{
			assert(!m.maskWiped && m.maskModel && m.packContent == MASK);
			m.hint = 0; // the hint ends
			HubMain(&m, -1, 0);
			assert(m.queued == GSV); // the held read is issued now
			LandQueue(&m);
			HubMain(&m, -1, 1);
			assert(m.swappedTo == GSV && m.level == GSV);
		}
		checks++;

		// The #17 crash: the next hub was read, then a hint loads the mask over it,
		// and the player reaches the swap quad without a fresh read.
		Init(&m, useHold);
		HubMain(&m, GSV, 0);
		LandQueue(&m);
		m.hint = 4;
		MaskStart(&m);
		HubMain(&m, -1, 1); // swap quad during the hint
		if (!useHold)
			assert(m.swappedTo == MASK); // retail: the swap takes the mask's memory
		else
		{
			assert(m.swappedTo == -1 && m.needSwap == 0 && m.hold.swap && m.level == NSB);
			HubMain(&m, -1, 0); // still running: nothing moves
			assert(m.swappedTo == -1 && m.queued == -1);
			m.hint = 0;
			HubMain(&m, -1, 0); // held re-read first
			assert(m.queued == GSV && m.swappedTo == -1);
			HubMain(&m, -1, 0); // then the held swap, which waits for the read to land
			assert(m.swappedTo == GSV && m.level == GSV && m.swaps == 1 && !m.hold.swap);
			HubMain(&m, -1, 0); // and only once
			assert(m.swaps == 1 && m.packLev == NSB);
		}
		checks++;
	}

	// A held read for a hub the player has left is dropped.
	Init(&m, 1);
	m.hint = 4;
	MaskStart(&m);
	HubMain(&m, GSV, 0);
	m.hint = 0;
	m.level = 27; // a full level load took the player elsewhere
	HubMain(&m, -1, 0);
	assert(m.queued == -1 && m.hold.lev == -1);
	checks++;

	// The mask evicting no hub (empty pack, or a race level) holds nothing.
	Init(&m, 1);
	m.packLev = -1;
	MaskStart(&m);
	assert(m.hold.lev == -1);
	m.packLev = 3;
	MaskStart(&m);
	assert(m.hold.lev == -1);
	checks++;

	// A swap asked during a hint with nothing read: retail would wait forever in
	// LOAD_Hub_SwapNow; the hold drops it once the hint is over.
	for (useHold = 0; useHold < 2; useHold++)
	{
		// The kart is still on the swap quad when the hint ends.
		Init(&m, useHold);
		m.hint = 4;
		MaskStart(&m);
		HubMain(&m, -1, 1);
		m.hint = 0;
		HubMain(&m, -1, 1);
		HubMain(&m, -1, 1);
		HubMain(&m, -1, 1);
		if (!useHold)
			assert(m.stalled);
		else
			assert(!m.stalled && m.swaps == 0 && !m.hold.swap && m.level == NSB);
		checks++;
	}

	// A held swap and a held read, then a full level load before the hint ends
	// (or a hint that ends in another level): both are dropped, nothing swaps or
	// stalls later.
	Init(&m, 1);
	HubMain(&m, GSV, 0);
	LandQueue(&m);
	m.hint = 4;
	MaskStart(&m);
	HubMain(&m, -1, 1);
	assert(m.hold.swap && m.hold.lev == GSV);
	FullLoad(&m, 3);
	m.hint = 0;
	HubMain(&m, -1, 0);
	HubMain(&m, -1, 0);
	HubMain(&m, -1, 0);
	assert(m.swaps == 0 && !m.stalled && !m.hold.swap && m.hold.lev == -1 && m.queued == -1);
	FullLoad(&m, NSB); // back in a hub later: an idle tick does nothing
	HubMain(&m, -1, 0);
	assert(m.swaps == 0 && !m.stalled);
	checks++;

	printf("PASS: hub streaming waits for a mask hint (%d scenarios)\n", checks);
	return 0;
}
