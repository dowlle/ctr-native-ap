#ifndef AP_FEED_LINES_LOGIC_H
#define AP_FEED_LINES_LOGIC_H

// Pure line store of the AP item feed (ap/ap_hooks_feed.c): the pending ring, the
// visible lines, the promotion stagger and the per-line lifetime. No engine, no
// network and no config dependency, so the feed and the host harness
// (tools/test-feed-trap-lines.c) drive the same code.
//
// TRAP LINES (A3-21, ruled 2026-11-06: trap notices were too spammy).
// A trap owns ONE line that changes in place: NAME ARMED, then NAME INCOMING, then
// NAME. The line carries the trap's scheduler slot as its identity (trapSlot, or
// AP_FEED_NO_TRAP for every other line). A later state of the same trap replaces
// the text of that trap's line and restarts its display time instead of appending.
// A line that is still waiting in the ring is replaced there, so it never shows an
// older state. If the line already scrolled out, the new state adds a fresh line.
//
// Slot reuse: a slot only empties after its trap fired, so an ARMED notice for a
// slot whose line is already in the FIRED stage belongs to a NEW trap and starts a
// new line. An ARMED notice over an ARMED or INCOMING line is the scheduler
// announcing the same waiting trap again (a race restart re-arms it), and replaces
// in place.

#include <stdio.h>
#include <string.h>

#define AP_FEED_LIFETIME_FRAMES    240 // per-line visible dwell (~4s), tunable
#define AP_FEED_MAX_VISIBLE        5   // lines on screen at once
#define AP_FEED_QUEUE_MAX          24  // pending (not-yet-visible) ring capacity
#define AP_FEED_STAGGER_FRAMES     12  // min frames between promotions (cascade)
#define AP_FEED_TEXT_CAP           96

#define AP_FEED_NO_TRAP            (-1)

enum
{
	AP_FEED_TRAP_ARMED = 0,
	AP_FEED_TRAP_INCOMING = 1,
	AP_FEED_TRAP_FIRED = 2
};

typedef struct
{
	char text[AP_FEED_TEXT_CAP];
	int  color;      // DecalFontStyle index resolved from the item's AP class (#195)
	int  framesLeft; // visible lifetime remaining
	int  playCue;    // received-item cue, consumed only when promoted in a race
	int  trapSlot;   // scheduler slot of the trap this line belongs to, or AP_FEED_NO_TRAP
	int  trapStage;  // AP_FEED_TRAP_* while trapSlot is set
} AP_FeedLine;

typedef struct
{
	AP_FeedLine queue[AP_FEED_QUEUE_MAX]; // FIFO of pending lines
	int qhead;
	int qcount;
	AP_FeedLine vis[AP_FEED_MAX_VISIBLE]; // [0]=oldest(top)..newest(bottom)
	int vcount;
	int stagger;
} AP_FeedLines;

static inline void AP_FeedLinesReset(AP_FeedLines *f)
{
	f->qhead = 0;
	f->qcount = 0;
	f->vcount = 0;
	f->stagger = 0;
}

// Append a ready-formatted line to the pending ring. Oldest is dropped if full.
static inline AP_FeedLine *AP_FeedLinesEnqueue(AP_FeedLines *f, const char *text,
                                               int color, int playCue)
{
	AP_FeedLine *q;
	int slot;
	if (f->qcount >= AP_FEED_QUEUE_MAX)
	{
		f->qhead = (f->qhead + 1) % AP_FEED_QUEUE_MAX;
		f->qcount--;
	}
	slot = (f->qhead + f->qcount) % AP_FEED_QUEUE_MAX;
	q = &f->queue[slot];
	snprintf(q->text, AP_FEED_TEXT_CAP, "%s", text);
	q->color = color;
	q->framesLeft = 0;
	q->playCue = playCue;
	q->trapSlot = AP_FEED_NO_TRAP;
	q->trapStage = 0;
	f->qcount++;
	return q;
}

// Newest line (queued first, then visible) that belongs to this trap slot.
static inline AP_FeedLine *AP_FeedLinesFindTrap(AP_FeedLines *f, int trapSlot)
{
	int i;
	for (i = f->qcount - 1; i >= 0; i--)
	{
		AP_FeedLine *q = &f->queue[(f->qhead + i) % AP_FEED_QUEUE_MAX];
		if (q->trapSlot == trapSlot)
			return q;
	}
	for (i = f->vcount - 1; i >= 0; i--)
		if (f->vis[i].trapSlot == trapSlot)
			return &f->vis[i];
	return 0;
}

// One trap state for one trap slot. Returns 1 when an existing line was updated in
// place, 0 when a new line was added.
static inline int AP_FeedLinesTrapState(AP_FeedLines *f, int trapSlot, int stage,
                                        const char *text, int color)
{
	AP_FeedLine *l = AP_FeedLinesFindTrap(f, trapSlot);
	if (l != 0 && stage == AP_FEED_TRAP_ARMED && l->trapStage == AP_FEED_TRAP_FIRED)
	{
		l->trapSlot = AP_FEED_NO_TRAP; // that trap is over; the slot now holds a new one
		l = 0;
	}
	if (l != 0)
	{
		snprintf(l->text, AP_FEED_TEXT_CAP, "%s", text);
		l->color = color;
		l->trapStage = stage;
		if (l->framesLeft > 0) // visible: restart the display time
			l->framesLeft = AP_FEED_LIFETIME_FRAMES;
		return 1;
	}
	l = AP_FeedLinesEnqueue(f, text, color, 1);
	l->trapSlot = trapSlot;
	l->trapStage = stage;
	return 0;
}

// Promote one pending line into a free visible slot, rate-limited by the stagger.
// Returns 1 when the promoted line asked for the received-item cue.
static inline int AP_FeedLinesPromote(AP_FeedLines *f)
{
	AP_FeedLine *q, *v;
	int cue;
	if (f->vcount >= AP_FEED_MAX_VISIBLE || f->qcount <= 0)
		return 0;
	if (f->stagger > 0)
	{
		f->stagger--;
		return 0;
	}
	q = &f->queue[f->qhead];
	f->qhead = (f->qhead + 1) % AP_FEED_QUEUE_MAX;
	f->qcount--;
	v = &f->vis[f->vcount];
	*v = *q;
	v->framesLeft = AP_FEED_LIFETIME_FRAMES; // lifetime starts now (visible)
	cue = q->playCue;
	v->playCue = 0;
	f->vcount++;
	f->stagger = AP_FEED_STAGGER_FRAMES;
	return cue;
}

// Age the visible lines one frame and compact out the expired ones.
static inline void AP_FeedLinesAge(AP_FeedLines *f)
{
	int i, w = 0;
	for (i = 0; i < f->vcount; i++)
	{
		if (f->vis[i].framesLeft > 0)
			f->vis[i].framesLeft--;
		if (f->vis[i].framesLeft > 0)
		{
			if (w != i)
				f->vis[w] = f->vis[i];
			w++;
		}
	}
	f->vcount = w;
}

#endif
