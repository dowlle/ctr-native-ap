#ifndef AP_DEATHLINK_INBOX_H
#define AP_DEATHLINK_INBOX_H

// The DeathLink bounces received since the game thread last looked (pure logic,
// shared by ap_net.cpp and the host harness; C and C++).
//
// This is not a queue of deaths. AP_DeathLinkTick drains the whole inbox in the
// frame it runs, oldest first: only the newest death stays pending, and every
// older one is shown as ignored ("DEATHLINK IGNORED", naming who died). Before
// this inbox the network layer kept a single latch, so a second death arriving
// before the game thread drained the first overwrote it and the older death got
// no popup at all.
//
// The inbox is bounded. ap_net_poll runs once per frame (AP_NetTick) and the tick
// drains the inbox later in the same frame, so it only fills if more than
// AP_DL_INBOX_CAP deaths arrive in one poll. The cap matches what the feed can show
// at once: each popup is two lines and the feed's pending ring holds 24
// (AP_FEED_QUEUE_MAX in ap_hooks_feed.c), so beyond 12 deaths the feed would drop
// the oldest lines anyway. Past the cap the oldest death is discarded without a
// popup of its own; it is counted instead (AP_DeathLinkInboxTakeDropped) and the
// game thread shows one ignored popup for the count, so no death vanishes
// without a trace. AP_DeathLinkInboxPush also reports it so the caller can log it.

#include <string.h>

#define AP_DL_INBOX_CAP        12
#define AP_DL_INBOX_CAUSE_MAX  128
#define AP_DL_INBOX_SOURCE_MAX 64

typedef struct
{
	char cause[AP_DL_INBOX_CAUSE_MAX];
	char source[AP_DL_INBOX_SOURCE_MAX];
} APDeathLinkInboxEntry;

typedef struct
{
	APDeathLinkInboxEntry entry[AP_DL_INBOX_CAP];
	int head;  // index of the oldest entry
	int count; // entries held
	int dropped; // deaths discarded because the inbox was full, since the last TakeDropped
} APDeathLinkInbox;

// Copy s into dst (size n > 0), truncating; NULL reads as "".
static inline void AP_DeathLinkInboxCopy(char *dst, int n, const char *s)
{
	size_t len = s ? strlen(s) : 0;

	if (dst == 0 || n <= 0)
		return;
	if (len > (size_t)(n - 1))
		len = (size_t)(n - 1);
	if (len > 0)
		memcpy(dst, s, len);
	dst[len] = '\0';
}

static inline void AP_DeathLinkInboxClear(APDeathLinkInbox *in)
{
	in->head = 0;
	in->count = 0;
	in->dropped = 0;
}

// Add a received death. Returns 1 if a full inbox discarded its oldest entry.
static inline int AP_DeathLinkInboxPush(APDeathLinkInbox *in, const char *cause,
                                        const char *source)
{
	int full = in->count == AP_DL_INBOX_CAP;
	APDeathLinkInboxEntry *e;

	if (full)
	{
		in->head = (in->head + 1) % AP_DL_INBOX_CAP;
		in->count--;
		in->dropped++;
	}
	e = &in->entry[(in->head + in->count) % AP_DL_INBOX_CAP];
	AP_DeathLinkInboxCopy(e->cause, (int)sizeof e->cause, cause);
	AP_DeathLinkInboxCopy(e->source, (int)sizeof e->source, source);
	in->count++;
	return full;
}

// Take the oldest death: returns 1 and copies its cause and source (truncated;
// either buffer may be NULL), or 0 when the inbox is empty.
static inline int AP_DeathLinkInboxTake(APDeathLinkInbox *in, char *cause_buf,
                                        int cause_n, char *source_buf, int source_n)
{
	const APDeathLinkInboxEntry *e;

	if (in->count <= 0)
		return 0;
	e = &in->entry[in->head];
	AP_DeathLinkInboxCopy(cause_buf, cause_n, e->cause);
	AP_DeathLinkInboxCopy(source_buf, source_n, e->source);
	in->head = (in->head + 1) % AP_DL_INBOX_CAP;
	in->count--;
	return 1;
}

// How many deaths a full inbox discarded since the last call; resets the count.
static inline int AP_DeathLinkInboxTakeDropped(APDeathLinkInbox *in)
{
	int n = in->dropped;

	in->dropped = 0;
	return n;
}

#endif // AP_DEATHLINK_INBOX_H
