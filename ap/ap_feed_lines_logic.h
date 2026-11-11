#ifndef AP_FEED_LINES_LOGIC_H
#define AP_FEED_LINES_LOGIC_H

// Pure line store of the AP item feed (ap/ap_hooks_feed.c): the pending ring, the
// visible lines, the promotion stagger and the per-line lifetime. No engine, no
// network and no config dependency, so the feed and the host harness
// (tools/test-feed-trap-lines.c) drive the same code.
//
// A4-54: identical effects share a counted line, with receipt sequences keeping
// state transitions from adding copies. ARMED is hidden in race contexts; copies
// join on their own warning/fire/refresh event. Filler receipts group only
// within one consecutive run and one fixed four-second receipt window.
//
// Historical per-copy helper below is retained for the A3-21 regression fixture
// and before/after render comparison. Production uses AP_FeedLinesTrapGrouped.
// TRAP LINES (A3-21, ruled 2026-11-06: trap notices were too spammy).
// A trap owns ONE line that changes in place: NAME ARMED, then NAME INCOMING, then
// NAME. The line carries the trap copy's identity (scheduler slot plus its receipt
// sequence, or AP_FEED_NO_TRAP for every other line). A later state of the same
// trap replaces the text of that trap's line and restarts its display time instead
// of appending. A line that is still waiting in the ring is replaced there, so it
// never shows an older state. If the line already scrolled out, the new state adds
// a fresh line.
//
// Slot reuse: a slot only empties after its trap is over, and a refreshed
// duplicate's slot empties without its line ever reaching the fired state, so the
// slot alone cannot tell a new copy from the old one's still-visible line. The
// receipt sequence can: a reused slot's next copy announces with a newer seq, the
// old line no longer matches and the notice starts a new line. A re-announced
// ARMED over an ARMED or INCOMING line (a race restart re-arms the same waiting
// copy, seq unchanged) replaces in place.

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
	unsigned trapSeq; // receipt sequence of the trap copy, while trapSlot is set
	int trapEffect, copies;
	unsigned copySeq[32]; // one latest receipt identity per scheduler slot
	unsigned receiptID;
	long long receiptItem;
	int receiptPlayer;
	char baseText[AP_FEED_TEXT_CAP];
} AP_FeedLine;

typedef struct
{
	AP_FeedLine queue[AP_FEED_QUEUE_MAX]; // FIFO of pending lines
	int qhead;
	int qcount;
	AP_FeedLine vis[AP_FEED_MAX_VISIBLE]; // [0]=oldest(top)..newest(bottom)
	int vcount;
	int stagger;
	unsigned receiptID;
	int receiptWindow;
} AP_FeedLines;

static inline void AP_FeedLinesReset(AP_FeedLines *f)
{
	f->qhead = 0;
	f->qcount = 0;
	f->vcount = 0;
	f->stagger = 0;
	f->receiptID = 0;
	f->receiptWindow = 0;
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
	memset(q, 0, sizeof *q);
	q->trapEffect = -1;
	snprintf(q->text, AP_FEED_TEXT_CAP, "%s", text);
	q->color = color;
	q->framesLeft = 0;
	q->playCue = playCue;
	q->trapSlot = AP_FEED_NO_TRAP;
	q->trapStage = 0;
	q->trapSeq = 0;
	f->qcount++;
	return q;
}

// Newest line (queued first, then visible) that belongs to this trap copy.
static inline AP_FeedLine *AP_FeedLinesFindTrap(AP_FeedLines *f, int trapSlot,
                                                unsigned trapSeq)
{
	int i;
	for (i = f->qcount - 1; i >= 0; i--)
	{
		AP_FeedLine *q = &f->queue[(f->qhead + i) % AP_FEED_QUEUE_MAX];
		if (q->trapSlot == trapSlot && q->trapSeq == trapSeq)
			return q;
	}
	for (i = f->vcount - 1; i >= 0; i--)
		if (f->vis[i].trapSlot == trapSlot && f->vis[i].trapSeq == trapSeq)
			return &f->vis[i];
	return 0;
}

// One trap state for one trap copy. Returns 1 when an existing line was updated in
// place, 0 when a new line was added.
static inline int AP_FeedLinesTrapState(AP_FeedLines *f, int trapSlot,
                                        unsigned trapSeq, int stage,
                                        const char *text, int color)
{
	AP_FeedLine *l = AP_FeedLinesFindTrap(f, trapSlot, trapSeq);
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
	l->trapSeq = trapSeq;
	l->trapStage = stage;
	return 0;
}

// Copies of an effect share one queued or visible line. The latest event owns
// the displayed state; repeat state updates never add to the receipt count.
static inline int AP_FeedLinesTrapGrouped(AP_FeedLines *f, int effect, int slot,
        unsigned seq, int stage, const char *text, int color, int racing)
{
	AP_FeedLine *l = 0;
	int i;
	if (effect < 0 || slot < 0 || slot >= 32 || seq == 0) return 0;
	if (stage == AP_FEED_TRAP_ARMED && racing) return 0;
	for (i = f->qcount - 1; i >= 0 && !l; --i) {
		AP_FeedLine *q = &f->queue[(f->qhead + i) % AP_FEED_QUEUE_MAX];
		if (q->trapEffect == effect) l = q;
	}
	for (i = f->vcount - 1; i >= 0 && !l; --i)
		if (f->vis[i].trapEffect == effect) l = &f->vis[i];
	int updated = l != 0;
	if (!l) l = AP_FeedLinesEnqueue(f, text, color, 1);
	if (slot >= 0 && slot < 32 && l->copySeq[slot] != seq) {
		l->copySeq[slot] = seq;
		++l->copies;
	}
	l->trapEffect = effect;
	l->trapStage = stage;
	snprintf(l->baseText, sizeof l->baseText, "%s", text);
	l->color = color;
	if (l->copies > 1) snprintf(l->text, sizeof l->text, "%d %.*s", l->copies,
	    AP_FEED_TEXT_CAP - 13, l->baseText);
	else snprintf(l->text, sizeof l->text, "%s", l->baseText);
	if (l->framesLeft > 0) l->framesLeft = AP_FEED_LIFETIME_FRAMES;
	return updated;
}

// Receipt grouping uses a fixed four-second window from its first receipt,
// independent of promotion delays. A different receipt ends the run.
static inline void AP_FeedLinesReceipt(AP_FeedLines *f, long long item, int player,
        int filler, const char *text, int color)
{
	AP_FeedLine *l = 0;
	int i;
	if (filler && f->receiptWindow > 0) {
		for (i = 0; i < f->qcount; ++i) {
			AP_FeedLine *q = &f->queue[(f->qhead + i) % AP_FEED_QUEUE_MAX];
			if (q->receiptID == f->receiptID) l = q;
		}
		for (i = 0; i < f->vcount; ++i)
			if (f->vis[i].receiptID == f->receiptID) l = &f->vis[i];
		if (l && (l->receiptItem != item || l->receiptPlayer != player ||
		          strcmp(l->baseText, text))) l = 0;
	}
	if (l) {
		++l->copies;
		snprintf(l->text, sizeof l->text, "%d %.*s", l->copies,
		         AP_FEED_TEXT_CAP - 13, l->baseText);
		if (l->framesLeft > 0) l->framesLeft = AP_FEED_LIFETIME_FRAMES;
		return;
	}
	l = AP_FeedLinesEnqueue(f, text, color, 1);
	if (++f->receiptID == 0) ++f->receiptID;
	f->receiptWindow = filler ? AP_FEED_LIFETIME_FRAMES : 0;
	l->receiptID = f->receiptID;
	l->receiptItem = item;
	l->receiptPlayer = player;
	l->copies = 1;
	snprintf(l->baseText, sizeof l->baseText, "%s", text);
}

static inline void AP_FeedLinesReceiptTick(AP_FeedLines *f)
{
	if (f->receiptWindow > 0) --f->receiptWindow;
}

// Runtime reissues an undelivered effect after a load using a fresh scheduler
// slot/sequence (and therefore unchanged receipt-order priority). Transfer only
// the display membership: this is the same received copy, not another item.
static inline void AP_FeedLinesReissue(AP_FeedLines *f, int effect, int oldSlot,
        unsigned oldSeq, int newSlot, unsigned newSeq)
{
	int i;
	if (oldSlot < 0 || oldSlot >= 32 || newSlot < 0 || newSlot >= 32 || !oldSeq || !newSeq) return;
	for (i = 0; i < f->qcount + f->vcount; ++i) {
		AP_FeedLine *l = i < f->qcount ? &f->queue[(f->qhead + i) % AP_FEED_QUEUE_MAX]
		                                  : &f->vis[i - f->qcount];
		if (l->trapEffect != effect || l->copySeq[oldSlot] != oldSeq) continue;
		l->copySeq[oldSlot] = 0;
		l->copySeq[newSlot] = newSeq;
	}
}

// Hub ARMED notices must not travel into the race surface. Retire them before
// promotion so hidden setup notices cannot occupy the race's visible budget.
// Return the first moved visible index for the renderer's per-line clocks.
static inline int AP_FeedLinesRetireArmed(AP_FeedLines *f)
{
	int i, w = 0, moved = -1;
	for (i = 0; i < f->vcount; ++i) {
		if (f->vis[i].trapEffect >= 0 && f->vis[i].trapStage == AP_FEED_TRAP_ARMED) {
			if (moved < 0) moved = i;
			continue;
		}
		if (w != i) f->vis[w] = f->vis[i];
		++w;
	}
	f->vcount = w;
	w = 0;
	for (i = 0; i < f->qcount; ++i) {
		AP_FeedLine *q = &f->queue[(f->qhead + i) % AP_FEED_QUEUE_MAX];
		if (q->trapEffect >= 0 && q->trapStage == AP_FEED_TRAP_ARMED) continue;
		if (w != i) f->queue[(f->qhead + w) % AP_FEED_QUEUE_MAX] = *q;
		++w;
	}
	f->qcount = w;
	return moved;
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
