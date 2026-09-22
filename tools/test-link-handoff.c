// Behavioral harness for the confirmed one-click-connect handoff (issue #334,
// implementation slice 3). It includes the production units directly, so these
// assertions run against the code main.c compiles:
//
//   * platform/native_link_pending.c  record format and the locked publish /
//                                     claim protocol, driven against a
//                                     simulated filesystem with fault and
//                                     crash injection
//   * platform/native_link_handoff.c  the switch decision: coalescing,
//                                     latest-distinct-wins, main-menu deferral,
//                                     prompt arming, deny, accept and expiry
//   * platform/native_link_route.c    startup routing (hand off or run)
//   * platform/native_link_host.c     the real store and primary lock, driven
//                                     from several processes at once
//
//   cc -Wall -Wextra -I . -I include -o /tmp/test-link-handoff tools/test-link-handoff.c
//
// Exit 0 = every assertion held; failures are printed otherwise.
//
// Actual prompt rendering and the pad path through RECTMENU_ProcessState are
// Steam acceptance checks; this harness proves the state machine and the
// concurrency behaviour independently of the game.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "platform/native_launch_request.c"
#include "platform/native_link_pending.c"
#include "platform/native_link_handoff.c"
#include "platform/native_link_route.c"
#include "platform/native_fs_utf8.c"
#include "platform/native_link_host.c"

static int g_checks;
static int g_failures;

static void expect(int condition, const char *name)
{
	g_checks++;
	if (!condition)
	{
		g_failures++;
		printf("FAIL: %s\n", name);
	}
}

#define T0 1760000000000LL // a fixed wall clock, ms
#define MINUTE (60LL * 1000LL)

static NativeLaunchRequest Req(const char *host, unsigned port, const char *slot, const char *room)
{
	NativeLaunchRequest r;
	memset(&r, 0, sizeof r);
	snprintf(r.host, sizeof r.host, "%s", host);
	r.port = port;
	snprintf(r.slot, sizeof r.slot, "%s", slot);
	snprintf(r.room, sizeof r.room, "%s", room);
	return r;
}

static NativeLinkRecord Rec(const char *host, unsigned port, const char *slot, const char *room,
                            unsigned long long token, long long created)
{
	NativeLinkRecord rec;
	memset(&rec, 0, sizeof rec);
	rec.request = Req(host, port, slot, room);
	rec.token = token;
	rec.createdUnixMs = created;
	return rec;
}

static int SameRequest(const NativeLaunchRequest *a, const NativeLaunchRequest *b)
{
	return strcmp(a->host, b->host) == 0 && a->port == b->port && strcmp(a->slot, b->slot) == 0 &&
	       strcmp(a->room, b->room) == 0;
}

// ---------------------------------------------------------------------------
// Simulated filesystem: three named files, a lock and injected faults.

enum { SIM_FILES = 3 };
static const char *const s_simNames[SIM_FILES] = {NATIVE_LINK_PENDING_FILE, NATIVE_LINK_CONSUMING_FILE,
                                                  NATIVE_LINK_TEMP_FILE};

typedef struct
{
	int present[SIM_FILES];
	char data[SIM_FILES][NATIVE_LINK_RECORD_MAX + 64];
	size_t len[SIM_FILES];
	int locked;          // held by this simulated process
	int heldElsewhere;   // held by another process
	int failWrite;
	int failReplace;
	int crashAfterWrite; // publisher dies after writing the temp file
	int crashAfterRename;// claimer dies after renaming to consuming
	int crashed;
} SimFs;

static int SimIndex(const char *name)
{
	int i;
	for (i = 0; i < SIM_FILES; i++)
		if (strcmp(s_simNames[i], name) == 0)
			return i;
	return -1;
}

static int SimLock(void *ctx, int blocking)
{
	SimFs *fs = (SimFs *)ctx;
	(void)blocking;
	if (fs->heldElsewhere || fs->locked)
		return 0;
	fs->locked = 1;
	return 1;
}

static void SimUnlock(void *ctx)
{
	((SimFs *)ctx)->locked = 0;
}

static int SimRead(void *ctx, const char *name, char *buf, size_t cap, size_t *len)
{
	SimFs *fs = (SimFs *)ctx;
	const int i = SimIndex(name);
	if (i < 0 || fs->crashed || !fs->present[i])
		return 0;
	if (fs->len[i] > cap)
		return -1;
	memcpy(buf, fs->data[i], fs->len[i]);
	*len = fs->len[i];
	return 1;
}

static int SimWriteNew(void *ctx, const char *name, const char *data, size_t len)
{
	SimFs *fs = (SimFs *)ctx;
	const int i = SimIndex(name);
	if (i < 0 || fs->crashed || fs->present[i] || fs->failWrite || len > sizeof fs->data[i])
		return 0;
	memcpy(fs->data[i], data, len);
	fs->len[i] = len;
	fs->present[i] = 1;
	if (fs->crashAfterWrite)
		fs->crashed = 1; // every later operation of this process fails
	return 1;
}

static int SimReplace(void *ctx, const char *from, const char *to)
{
	SimFs *fs = (SimFs *)ctx;
	const int a = SimIndex(from);
	const int b = SimIndex(to);
	if (a < 0 || b < 0 || fs->crashed || fs->failReplace || !fs->present[a])
		return 0;
	memcpy(fs->data[b], fs->data[a], fs->len[a]);
	fs->len[b] = fs->len[a];
	fs->present[b] = 1;
	fs->present[a] = 0;
	if (fs->crashAfterRename && b == SimIndex(NATIVE_LINK_CONSUMING_FILE))
		fs->crashed = 1;
	return 1;
}

static int SimRemove(void *ctx, const char *name)
{
	SimFs *fs = (SimFs *)ctx;
	const int i = SimIndex(name);
	if (i < 0 || fs->crashed)
		return 0;
	fs->present[i] = 0;
	return 1;
}

static int SimExists(void *ctx, const char *name)
{
	SimFs *fs = (SimFs *)ctx;
	const int i = SimIndex(name);
	return i >= 0 && !fs->crashed && fs->present[i];
}

static NativeLinkFsOps SimOps(SimFs *fs)
{
	NativeLinkFsOps ops;
	ops.ctx = fs;
	ops.lock = SimLock;
	ops.unlock = SimUnlock;
	ops.read = SimRead;
	ops.write_new = SimWriteNew;
	ops.replace = SimReplace;
	ops.remove = SimRemove;
	ops.exists = SimExists;
	return ops;
}

// The process that crashed is gone; the next one sees the files it left and a
// free lock (the OS dropped it).
static void SimRestart(SimFs *fs)
{
	fs->crashed = 0;
	fs->crashAfterWrite = 0;
	fs->crashAfterRename = 0;
	fs->locked = 0;
}

static int SimPresent(SimFs *fs, const char *name)
{
	return fs->present[SimIndex(name)];
}

// ---------------------------------------------------------------------------

static void TestRecordFormat(void)
{
	NativeLinkRecord rec = Rec("archipelago.gg", 38281, "Player One&=?#%/", "AbC-12_x", 0x0123456789ABCDEFULL, T0);
	NativeLinkRecord back;
	char text[NATIVE_LINK_RECORD_MAX];
	size_t n = NativeLinkRecord_Format(&rec, text, sizeof text);

	expect(n > 0, "record formats");
	expect(NativeLinkRecord_Parse(text, n, &back) && SameRequest(&back.request, &rec.request) &&
	           back.token == rec.token && back.createdUnixMs == rec.createdUnixMs,
	       "record round-trips, special slot characters included");
	expect(strstr(text, "password") == NULL, "record carries no password key");

	{
		NativeLinkRecord ipv6 = Rec("[2001:db8::1]", 1, "caf\xC3\xA9", "r", 1, T0);
		n = NativeLinkRecord_Format(&ipv6, text, sizeof text);
		expect(n > 0 && NativeLinkRecord_Parse(text, n, &back) && SameRequest(&back.request, &ipv6.request),
		       "IPv6 host and UTF-8 slot round-trip");
	}

	{
		char bad[NATIVE_LINK_RECORD_MAX];
		NativeLinkRecord untouched;
		memset(&untouched, 0x5A, sizeof untouched);
		back = untouched;

		n = NativeLinkRecord_Format(&rec, text, sizeof text);
		memcpy(bad, text, n);
		bad[0] = 'x';
		expect(!NativeLinkRecord_Parse(bad, n, &back), "wrong schema refused");
		expect(memcmp(&back, &untouched, sizeof back) == 0, "refused record leaves output untouched");

		expect(!NativeLinkRecord_Parse(text, n - 1, &back), "record without final newline refused");

		memcpy(bad, text, n);
		bad[n] = 'x';
		expect(!NativeLinkRecord_Parse(bad, n + 1, &back), "trailing bytes refused");

		memcpy(bad, text, n);
		bad[5] = '\0';
		expect(!NativeLinkRecord_Parse(bad, n, &back), "embedded NUL refused");

		{
			const char *withPassword = "ctr-ap-link 1\ntoken 0000000000000001\ncreated 1760000000000\n"
			                           "request ctr-ap://connect?host=a&port=1&slot=b&room=c&password=x\n";
			expect(!NativeLinkRecord_Parse(withPassword, strlen(withPassword), &back),
			       "a request with a password key is refused");
		}
		{
			const char *upperHex = "ctr-ap-link 1\ntoken 000000000000000A\ncreated 1760000000000\n"
			                       "request ctr-ap://connect?host=a&port=1&slot=b&room=c\n";
			expect(!NativeLinkRecord_Parse(upperHex, strlen(upperHex), &back), "non-canonical token refused");
		}
		{
			const char *negative = "ctr-ap-link 1\ntoken 0000000000000001\ncreated -5\n"
			                       "request ctr-ap://connect?host=a&port=1&slot=b&room=c\n";
			expect(!NativeLinkRecord_Parse(negative, strlen(negative), &back), "negative creation time refused");
		}
	}
}

static void TestIdentity(void)
{
	NativeLaunchRequest a = Req("Archipelago.GG", 38281, "Player", "room1");
	NativeLaunchRequest b = Req("archipelago.gg", 38281, "Player", "room1");
	NativeLaunchRequest saved;

	expect(NativeLinkRequest_SameIdentity(&a, &b), "host compares case-insensitively");
	b.port = 38282;
	expect(!NativeLinkRequest_SameIdentity(&a, &b), "different port is a different session");
	b = a;
	snprintf(b.slot, sizeof b.slot, "player");
	expect(!NativeLinkRequest_SameIdentity(&a, &b), "slot compares exactly");
	b = a;
	snprintf(b.room, sizeof b.room, "room2");
	expect(!NativeLinkRequest_SameIdentity(&a, &b), "different room is a different session");
	b = a;
	b.room[0] = '\0';
	expect(NativeLinkRequest_SameIdentity(&a, &b), "unknown room (saved connection) matches by endpoint and slot");

	expect(NativeLinkIdentity_FromConnection("archipelago.gg:38281", "Player", &saved) &&
	           strcmp(saved.host, "archipelago.gg") == 0 && saved.port == 38281 && saved.room[0] == '\0',
	       "saved host:port parses");
	expect(NativeLinkIdentity_FromConnection("wss://archipelago.gg:38281", "Player", &saved) && saved.port == 38281,
	       "saved wss:// uri parses");
	expect(NativeLinkIdentity_FromConnection("ws://[::1]:38281/", "Player", &saved) &&
	           strcmp(saved.host, "[::1]") == 0,
	       "saved bracketed IPv6 with trailing slash parses");
	expect(!NativeLinkIdentity_FromConnection("http://a:1", "P", &saved), "foreign scheme refused");
	expect(!NativeLinkIdentity_FromConnection("archipelago.gg", "P", &saved), "missing port refused");
	expect(!NativeLinkIdentity_FromConnection("a:0", "P", &saved), "port 0 refused");
	expect(!NativeLinkIdentity_FromConnection("a:65536", "P", &saved), "port overflow refused");
	expect(!NativeLinkIdentity_FromConnection("a:1/path", "P", &saved), "path refused");

	{
		char uri[128];
		NativeLaunchRequest v6 = Req("[::1]", 38281, "P", "r");
		expect(NativeLinkIdentity_ToConnectionUri(&a, uri, sizeof uri) && strcmp(uri, "Archipelago.GG:38281") == 0,
		       "accepted request saves as host:port");
		expect(NativeLinkIdentity_ToConnectionUri(&v6, uri, sizeof uri) && strcmp(uri, "[::1]:38281") == 0,
		       "IPv6 request saves bracketed");
		expect(NativeLinkIdentity_FromConnection(uri, "P", &saved) && NativeLinkRequest_SameIdentity(&saved, &v6),
		       "saved uri reads back as the same session");
	}
}

static void TestPendingProtocol(void)
{
	SimFs fs;
	NativeLinkFsOps ops;
	NativeLinkRecord out;
	NativeLinkRecord a = Rec("h", 1, "A", "r", 1, T0);
	NativeLinkRecord a2 = Rec("h", 1, "A", "r", 2, T0 + MINUTE);
	NativeLinkRecord b = Rec("h", 1, "B", "r", 3, T0 + 2 * MINUTE);
	NativeLinkRecord c = Rec("h", 2, "C", "r2", 4, T0 + 3 * MINUTE);

	memset(&fs, 0, sizeof fs);
	ops = SimOps(&fs);

	expect(NativeLinkPending_Claim(&ops, &out, T0) == NATIVE_LINK_PENDING_NONE, "empty store claims nothing");
	expect(NativeLinkPending_Publish(&ops, &a, T0) == NATIVE_LINK_PENDING_PUBLISHED, "first publish");
	expect(NativeLinkPending_Publish(&ops, &a2, T0 + MINUTE) == NATIVE_LINK_PENDING_COALESCED,
	       "same request repeated before claim coalesces");
	expect(NativeLinkPending_Claim(&ops, &out, T0 + MINUTE) == NATIVE_LINK_PENDING_CLAIMED &&
	           out.token == a.token && out.createdUnixMs == T0,
	       "coalesced request keeps the oldest creation time");
	expect(!SimPresent(&fs, NATIVE_LINK_PENDING_FILE) && !SimPresent(&fs, NATIVE_LINK_CONSUMING_FILE),
	       "claim leaves no pending or consuming file");
	expect(!fs.locked, "claim releases the lock");

	// Latest distinct request wins before confirmation.
	expect(NativeLinkPending_Publish(&ops, &a, T0) == NATIVE_LINK_PENDING_PUBLISHED, "A published");
	expect(NativeLinkPending_Publish(&ops, &b, T0 + 2 * MINUTE) == NATIVE_LINK_PENDING_REPLACED, "B replaces A");
	expect(NativeLinkPending_Publish(&ops, &c, T0 + 3 * MINUTE) == NATIVE_LINK_PENDING_REPLACED, "C replaces B");
	expect(NativeLinkPending_Claim(&ops, &out, T0 + 3 * MINUTE) == NATIVE_LINK_PENDING_CLAIMED &&
	           SameRequest(&out.request, &c.request),
	       "only the newest of A, B, C is claimed");
	expect(NativeLinkPending_Claim(&ops, &out, T0 + 3 * MINUTE) == NATIVE_LINK_PENDING_NONE, "nothing left after claim");

	// An expired pending request of the same identity is replaced, not kept.
	expect(NativeLinkPending_Publish(&ops, &a, T0) == NATIVE_LINK_PENDING_PUBLISHED, "old A published");
	expect(NativeLinkPending_Publish(&ops, &a2, T0 + 16 * MINUTE) == NATIVE_LINK_PENDING_REPLACED,
	       "same identity after expiry replaces the stale one");
	expect(NativeLinkPending_Claim(&ops, &out, T0 + 16 * MINUTE) == NATIVE_LINK_PENDING_CLAIMED && out.token == a2.token,
	       "fresh copy claimed");

	// Expiry at claim; a clock that went backwards is treated as expired.
	expect(NativeLinkPending_Publish(&ops, &a, T0) == NATIVE_LINK_PENDING_PUBLISHED, "A published for expiry");
	expect(NativeLinkPending_Claim(&ops, &out, T0 + NATIVE_LINK_PENDING_TTL_MS + 1) == NATIVE_LINK_PENDING_EXPIRED,
	       "expired at claim");
	expect(!SimPresent(&fs, NATIVE_LINK_PENDING_FILE), "expired request deleted");
	expect(NativeLinkPending_Publish(&ops, &a, T0) == NATIVE_LINK_PENDING_PUBLISHED, "A published for skew");
	expect(NativeLinkPending_Claim(&ops, &out, T0 - NATIVE_LINK_PENDING_FUTURE_SKEW_MS - 1) ==
	           NATIVE_LINK_PENDING_EXPIRED,
	       "creation time in the future is expired, not fresh forever");
	expect(NativeLinkPending_Claim(&ops, &out, T0 + NATIVE_LINK_PENDING_TTL_MS) == NATIVE_LINK_PENDING_NONE,
	       "store empty after skew drop");
	expect(!NativeLinkRecord_Expired(T0, T0 + NATIVE_LINK_PENDING_TTL_MS), "exactly 15 minutes is still valid");

	// A lock held elsewhere: the publisher reports it, the claimer retries later.
	expect(NativeLinkPending_Publish(&ops, &a, T0) == NATIVE_LINK_PENDING_PUBLISHED, "A published for busy lock");
	fs.heldElsewhere = 1;
	expect(NativeLinkPending_Publish(&ops, &b, T0) == NATIVE_LINK_PENDING_ERR_LOCK, "publisher reports a held lock");
	expect(NativeLinkPending_Claim(&ops, &out, T0) == NATIVE_LINK_PENDING_BUSY, "claimer backs off from a held lock");
	expect(SimPresent(&fs, NATIVE_LINK_PENDING_FILE), "busy claim leaves the pending file");
	fs.heldElsewhere = 0;
	expect(NativeLinkPending_Claim(&ops, &out, T0) == NATIVE_LINK_PENDING_CLAIMED && SameRequest(&out.request, &a.request),
	       "claim succeeds once the lock is free");

	// A damaged file is dropped, never half-used.
	{
		const int i = SimIndex(NATIVE_LINK_PENDING_FILE);
		memcpy(fs.data[i], "garbage\n", 8);
		fs.len[i] = 8;
		fs.present[i] = 1;
		expect(NativeLinkPending_Claim(&ops, &out, T0) == NATIVE_LINK_PENDING_INVALID, "unreadable file dropped");
		expect(!SimPresent(&fs, NATIVE_LINK_PENDING_FILE) && !SimPresent(&fs, NATIVE_LINK_CONSUMING_FILE),
		       "unreadable file removed");
		memcpy(fs.data[i], "garbage\n", 8);
		fs.present[i] = 1;
		expect(NativeLinkPending_Publish(&ops, &a, T0) == NATIVE_LINK_PENDING_REPLACED,
		       "publisher replaces an unreadable file");
		expect(NativeLinkPending_Claim(&ops, &out, T0) == NATIVE_LINK_PENDING_CLAIMED, "replacement claimable");
	}

	// Write or rename failures keep the previous request intact.
	expect(NativeLinkPending_Publish(&ops, &a, T0) == NATIVE_LINK_PENDING_PUBLISHED, "A published before faults");
	fs.failWrite = 1;
	expect(NativeLinkPending_Publish(&ops, &b, T0) == NATIVE_LINK_PENDING_ERR_IO, "write failure reported");
	fs.failWrite = 0;
	fs.failReplace = 1;
	expect(NativeLinkPending_Publish(&ops, &b, T0) == NATIVE_LINK_PENDING_ERR_IO, "rename failure reported");
	fs.failReplace = 0;
	expect(!SimPresent(&fs, NATIVE_LINK_TEMP_FILE), "failed publish leaves no temp file");
	expect(!fs.locked, "failed publish releases the lock");
	expect(NativeLinkPending_Claim(&ops, &out, T0) == NATIVE_LINK_PENDING_CLAIMED && SameRequest(&out.request, &a.request),
	       "previous request survives failed publishes");

	// From here every request was created by T0 + 3 minutes.
	{
		const long long NOW = T0 + 3 * MINUTE;
		// Crash between write and replace: the temp file is nobody's, the next
		// publisher clears it and the claimer ignores it.
		fs.crashAfterWrite = 1;
		NativeLinkPending_Publish(&ops, &b, NOW);
		SimRestart(&fs);
		expect(SimPresent(&fs, NATIVE_LINK_TEMP_FILE) && !SimPresent(&fs, NATIVE_LINK_PENDING_FILE),
		       "crash after write leaves only a temp file");
		expect(NativeLinkPending_Claim(&ops, &out, NOW) == NATIVE_LINK_PENDING_NONE, "claimer ignores a stray temp file");
		expect(NativeLinkPending_Publish(&ops, &c, NOW) == NATIVE_LINK_PENDING_PUBLISHED, "next publish after crash");
		expect(!SimPresent(&fs, NATIVE_LINK_TEMP_FILE), "next publisher cleared the stray temp file");
		expect(NativeLinkPending_Claim(&ops, &out, NOW) == NATIVE_LINK_PENDING_CLAIMED && SameRequest(&out.request, &c.request),
		       "request published after the crash claimed");

		// Crash between replace and claim: the published request simply waits.
		expect(NativeLinkPending_Publish(&ops, &a, NOW) == NATIVE_LINK_PENDING_PUBLISHED, "published, then publisher gone");
		expect(NativeLinkPending_Claim(&ops, &out, NOW) == NATIVE_LINK_PENDING_CLAIMED, "claimed by the next primary");

		// Crash between the claim's rename and its delete: recovered.
		expect(NativeLinkPending_Publish(&ops, &b, NOW) == NATIVE_LINK_PENDING_PUBLISHED, "B published before claim crash");
		fs.crashAfterRename = 1;
		NativeLinkPending_Claim(&ops, &out, NOW);
		SimRestart(&fs);
		expect(SimPresent(&fs, NATIVE_LINK_CONSUMING_FILE) && !SimPresent(&fs, NATIVE_LINK_PENDING_FILE),
		       "claim crash leaves a consuming file");
		expect(NativeLinkPending_Claim(&ops, &out, NOW) == NATIVE_LINK_PENDING_CLAIMED && SameRequest(&out.request, &b.request),
		       "leftover consuming file recovered");
		expect(!SimPresent(&fs, NATIVE_LINK_CONSUMING_FILE), "recovered consuming file deleted");

		// ...unless a newer request was published since: the newer one wins.
		expect(NativeLinkPending_Publish(&ops, &b, NOW) == NATIVE_LINK_PENDING_PUBLISHED, "B published before second crash");
		fs.crashAfterRename = 1;
		NativeLinkPending_Claim(&ops, &out, NOW);
		SimRestart(&fs);
		expect(NativeLinkPending_Publish(&ops, &c, NOW) == NATIVE_LINK_PENDING_PUBLISHED, "C published after the crash");
		expect(NativeLinkPending_Claim(&ops, &out, NOW) == NATIVE_LINK_PENDING_CLAIMED && SameRequest(&out.request, &c.request),
		       "newer pending request beats a leftover consuming file");
		expect(!SimPresent(&fs, NATIVE_LINK_CONSUMING_FILE) && !SimPresent(&fs, NATIVE_LINK_PENDING_FILE),
		       "store clean after recovery");
	}
}

// ---------------------------------------------------------------------------

typedef struct
{
	NativeLinkHandoff h;
	long long mono;
	long long wall;
} Client;

static void ClientInit(Client *c)
{
	NativeLinkHandoff_Init(&c->h);
	c->mono = 100000;
	c->wall = T0;
}

static NativeLinkFrameAction Step(Client *c, int safe, int live, int accept, int decline, NativeLinkRecord *taken,
                                  NativeLinkRecord *shown)
{
	NativeLinkFrameInput in;
	memset(&in, 0, sizeof in);
	in.safe = safe;
	in.sessionLive = live;
	in.nowMonoMs = c->mono;
	in.acceptTapped = accept;
	in.declineTapped = decline;
	c->mono += 33;
	c->wall += 33;
	return NativeLinkHandoff_Frame(&c->h, &in, taken, shown);
}

// Show the prompt until it is armed.
static void ArmPrompt(Client *c, int live)
{
	int i;
	for (i = 0; i < NATIVE_LINK_PROMPT_ARM_FRAMES; i++)
		Step(c, 1, live, 0, 0, NULL, NULL);
}

static void TestHandoffCoalescing(void)
{
	Client c;
	NativeLaunchRequest active = Req("host", 38281, "Me", "");
	NativeLinkRecord same = Rec("HOST", 38281, "Me", "room", 1, T0);
	NativeLinkRecord other = Rec("host", 38282, "Me", "room2", 2, T0);
	NativeLinkRecord shown;

	ClientInit(&c);
	NativeLinkHandoff_SetActive(&c.h, &active);

	expect(NativeLinkHandoff_Offer(&c.h, &same, c.wall, c.mono, 1) == NATIVE_LINK_OFFER_COALESCED_ACTIVE,
	       "link for the connected session coalesces");
	expect(!c.h.haveOffer, "coalesced link leaves nothing waiting");
	expect(Step(&c, 1, 1, 0, 0, NULL, NULL) == NATIVE_LINK_FRAME_IDLE, "no prompt for the connected session");

	// Repeated while displayed: one copy, oldest time, deadline never extended.
	expect(NativeLinkHandoff_Offer(&c.h, &other, c.wall, c.mono, 1) == NATIVE_LINK_OFFER_WAITING, "other room waits");
	ArmPrompt(&c, 1);
	{
		const long long deadline = c.h.offerDeadlineMonoMs;
		const int frames = c.h.promptFrames;
		NativeLinkRecord again = other;
		again.token = 9;
		again.createdUnixMs = c.wall;
		expect(NativeLinkHandoff_Offer(&c.h, &again, c.wall, c.mono, 1) == NATIVE_LINK_OFFER_COALESCED_WAITING,
		       "same link repeated while displayed coalesces");
		expect(c.h.offer.createdUnixMs == T0 && c.h.offer.token == 2, "displayed copy keeps its oldest creation");
		expect(c.h.offerDeadlineMonoMs == deadline, "repeat does not extend the expiry");
		expect(c.h.promptFrames == frames, "repeat does not re-arm the prompt");
	}
	expect(Step(&c, 1, 1, 0, 0, NULL, &shown) == NATIVE_LINK_FRAME_PROMPT && SameRequest(&shown.request, &other.request),
	       "prompt still shows the one copy");

	// The newest request is the current session: the older wait ends.
	expect(NativeLinkHandoff_Offer(&c.h, &same, c.wall, c.mono, 1) == NATIVE_LINK_OFFER_COALESCED_ACTIVE,
	       "newest link is the current session");
	expect(!c.h.haveOffer, "older different-room link stops waiting");

	// Connecting counts as a live session too.
	expect(NativeLinkHandoff_Offer(&c.h, &same, c.wall, c.mono, 1) == NATIVE_LINK_OFFER_COALESCED_ACTIVE,
	       "same link while connecting coalesces");
}

static void TestHandoffLatestWins(void)
{
	Client c;
	NativeLaunchRequest active = Req("host", 1, "Me", "");
	NativeLinkRecord a = Rec("host", 2, "A", "ra", 1, T0);
	NativeLinkRecord b = Rec("host", 3, "B", "rb", 2, T0);
	NativeLinkRecord cc = Rec("host", 4, "C", "rc", 3, T0);
	NativeLinkRecord shown;
	NativeLinkRecord taken;
	int i;

	ClientInit(&c);
	NativeLinkHandoff_SetActive(&c.h, &active);
	expect(NativeLinkHandoff_Offer(&c.h, &a, c.wall, c.mono, 1) == NATIVE_LINK_OFFER_WAITING, "A waits");
	expect(NativeLinkHandoff_Offer(&c.h, &b, c.wall, c.mono, 1) == NATIVE_LINK_OFFER_REPLACED, "B replaces A");
	expect(NativeLinkHandoff_Offer(&c.h, &cc, c.wall, c.mono, 1) == NATIVE_LINK_OFFER_REPLACED, "C replaces B");
	expect(Step(&c, 1, 1, 0, 0, NULL, &shown) == NATIVE_LINK_FRAME_PROMPT && SameRequest(&shown.request, &cc.request),
	       "only C is offered");

	// A newer request while C is displayed re-arms: a press meant for C cannot
	// accept B.
	ArmPrompt(&c, 1);
	expect(NativeLinkHandoff_Offer(&c.h, &b, c.wall, c.mono, 1) == NATIVE_LINK_OFFER_REPLACED, "B replaces displayed C");
	expect(Step(&c, 1, 1, 1, 0, &taken, &shown) == NATIVE_LINK_FRAME_PROMPT && SameRequest(&shown.request, &b.request),
	       "press right after a replacement does not accept");
	for (i = 0; i < NATIVE_LINK_PROMPT_ARM_FRAMES; i++)
		Step(&c, 1, 1, 0, 0, NULL, NULL);
	expect(Step(&c, 1, 1, 1, 0, &taken, NULL) == NATIVE_LINK_FRAME_ACCEPT && SameRequest(&taken.request, &b.request),
	       "armed press accepts the request on screen");
	expect(c.h.haveActive && SameRequest(&c.h.active, &b.request), "accepted request is the new session");

	// Once accepted and reconnecting, a later request waits as a new request.
	expect(NativeLinkHandoff_Offer(&c.h, &a, c.wall, c.mono, 1) == NATIVE_LINK_OFFER_WAITING,
	       "request during the accepted reconnect waits");
	expect(Step(&c, 1, 1, 0, 0, NULL, &shown) == NATIVE_LINK_FRAME_PROMPT && SameRequest(&c.h.active, &b.request),
	       "it is asked about, not dialed over the reconnect");
}

static void TestHandoffDeferral(void)
{
	Client c;
	NativeLaunchRequest active = Req("host", 1, "Me", "");
	NativeLinkRecord a = Rec("host", 2, "A", "ra", 1, T0);
	NativeLinkRecord taken;
	int i;
	int notices = 0;
	int prompts = 0;

	ClientInit(&c);
	NativeLinkHandoff_SetActive(&c.h, &active);
	NativeLinkHandoff_Offer(&c.h, &a, c.wall, c.mono, 1);

	// Race, load, save, cutscene: all not safe. The request waits, one notice.
	for (i = 0; i < 600; i++)
	{
		expect(Step(&c, 0, 1, (i & 1), (i & 2) != 0, &taken, NULL) == NATIVE_LINK_FRAME_WAITING,
		       "request waits while not at the main menu");
		notices += NativeLinkHandoff_TakeNotice(&c.h);
	}
	expect(notices == 1, "exactly one waiting notice");
	expect(SameRequest(&c.h.active, &active), "waiting changes nothing");

	// Menu entry without any press only shows the question.
	for (i = 0; i < 300; i++)
		prompts += Step(&c, 1, 1, 0, 0, &taken, NULL) == NATIVE_LINK_FRAME_PROMPT;
	expect(prompts == 300, "menu entry without input only prompts");
	expect(SameRequest(&c.h.active, &active) && c.h.haveOffer, "no switch without a press");

	// A press held on arrival at the menu does not answer.
	Step(&c, 0, 1, 0, 0, NULL, NULL); // left the menu (load)
	expect(Step(&c, 1, 1, 1, 0, &taken, NULL) == NATIVE_LINK_FRAME_PROMPT, "press on the first menu frame ignored");
	expect(Step(&c, 1, 1, 0, 1, &taken, NULL) == NATIVE_LINK_FRAME_PROMPT, "decline before arming ignored");

	// Decline keeps the current session and drops the request.
	ArmPrompt(&c, 1);
	expect(Step(&c, 1, 1, 0, 1, &taken, NULL) == NATIVE_LINK_FRAME_DECLINED, "decline");
	expect(!c.h.haveOffer && SameRequest(&c.h.active, &active), "decline preserves the session identity");
	expect(Step(&c, 1, 1, 1, 0, &taken, NULL) == NATIVE_LINK_FRAME_IDLE, "nothing left to accept after decline");
}

static void TestHandoffNoSession(void)
{
	Client c;
	NativeLinkRecord a = Rec("host", 2, "A", "ra", 1, T0);
	NativeLinkRecord b = Rec("host", 3, "B", "rb", 2, T0);
	NativeLinkRecord taken;

	// Not connected: admitted at the safe menu without a prompt...
	ClientInit(&c);
	NativeLinkHandoff_Offer(&c.h, &a, c.wall, c.mono, 0);
	expect(Step(&c, 0, 0, 0, 0, &taken, NULL) == NATIVE_LINK_FRAME_WAITING, "offline request still waits for safety");
	expect(Step(&c, 1, 0, 0, 0, &taken, NULL) == NATIVE_LINK_FRAME_ADMIT && SameRequest(&taken.request, &a.request),
	       "offline request admitted at the main menu without a prompt");
	expect(SameRequest(&c.h.active, &a.request), "admitted request becomes the session");

	// ...and at boot before the first dial (the glue passes safe, no session).
	ClientInit(&c);
	NativeLinkHandoff_Offer(&c.h, &a, c.wall, c.mono, 0);
	expect(Step(&c, 1, 0, 0, 0, &taken, NULL) == NATIVE_LINK_FRAME_ADMIT, "boot request admitted");

	// A question already on screen is not taken away when the session drops.
	ClientInit(&c);
	NativeLinkHandoff_SetActive(&c.h, &a.request);
	NativeLinkHandoff_Offer(&c.h, &b, c.wall, c.mono, 1);
	Step(&c, 1, 1, 0, 0, NULL, NULL);
	expect(Step(&c, 1, 0, 0, 0, &taken, NULL) == NATIVE_LINK_FRAME_PROMPT, "open prompt survives a session drop");
}

static void TestHandoffExpiry(void)
{
	Client c;
	NativeLaunchRequest active = Req("host", 1, "Me", "");
	NativeLinkRecord a = Rec("host", 2, "A", "ra", 1, T0);
	NativeLinkRecord taken;

	// Before display: already too old when offered.
	ClientInit(&c);
	NativeLinkHandoff_SetActive(&c.h, &active);
	expect(NativeLinkHandoff_Offer(&c.h, &a, T0 + NATIVE_LINK_PENDING_TTL_MS + 1, c.mono, 1) ==
	           NATIVE_LINK_OFFER_EXPIRED,
	       "expired before display");
	expect(!c.h.haveOffer, "expired offer never waits");

	// While waiting off the menu.
	ClientInit(&c);
	NativeLinkHandoff_SetActive(&c.h, &active);
	NativeLinkHandoff_Offer(&c.h, &a, T0 + 14 * MINUTE, c.mono, 1);
	c.mono += MINUTE + 1;
	expect(Step(&c, 0, 1, 0, 0, NULL, NULL) == NATIVE_LINK_FRAME_EXPIRED, "expired while waiting in a race");

	// During display.
	ClientInit(&c);
	NativeLinkHandoff_SetActive(&c.h, &active);
	NativeLinkHandoff_Offer(&c.h, &a, T0 + 14 * MINUTE, c.mono, 1);
	ArmPrompt(&c, 1);
	c.mono = c.h.offerDeadlineMonoMs;
	expect(Step(&c, 1, 1, 0, 0, NULL, NULL) == NATIVE_LINK_FRAME_EXPIRED, "expired while displayed");
	expect(!c.h.haveOffer && SameRequest(&c.h.active, &active), "expiry during display keeps the session");

	// On the frame of acceptance.
	ClientInit(&c);
	NativeLinkHandoff_SetActive(&c.h, &active);
	NativeLinkHandoff_Offer(&c.h, &a, T0 + 14 * MINUTE, c.mono, 1);
	ArmPrompt(&c, 1);
	c.mono = c.h.offerDeadlineMonoMs;
	expect(Step(&c, 1, 1, 1, 0, &taken, NULL) == NATIVE_LINK_FRAME_EXPIRED, "press on the expiry frame does not accept");
	expect(SameRequest(&c.h.active, &active), "late press keeps the session");

	// Just before expiry still accepts; the wall clock is not consulted after
	// the offer, so changing it cannot extend or cut the lifetime.
	ClientInit(&c);
	NativeLinkHandoff_SetActive(&c.h, &active);
	NativeLinkHandoff_Offer(&c.h, &a, T0 + 14 * MINUTE, c.mono, 1);
	ArmPrompt(&c, 1);
	c.mono = c.h.offerDeadlineMonoMs - 1;
	expect(Step(&c, 1, 1, 1, 0, &taken, NULL) == NATIVE_LINK_FRAME_ACCEPT, "press just before expiry accepts");
	{
		Client d;
		ClientInit(&d);
		NativeLinkHandoff_Offer(&d.h, &a, T0, d.mono, 1);
		expect(d.h.offerDeadlineMonoMs == d.mono + NATIVE_LINK_PENDING_TTL_MS, "fresh offer gets the full lifetime");
		ClientInit(&d);
		NativeLinkHandoff_Offer(&d.h, &a, T0 - 2000, d.mono, 1); // publisher clock slightly ahead
		expect(d.h.offerDeadlineMonoMs == d.mono + NATIVE_LINK_PENDING_TTL_MS, "small future skew never extends");
	}
}

static void TestRoute(void)
{
	NativeLinkRouteInput in;

	memset(&in, 0, sizeof in);
	in.storeUsable = 1;
	in.isPrimary = 1;
	expect(NativeLinkRoute_Decide(&in) == NATIVE_LINK_ROUTE_RUN, "no request: normal start");
	in.isPrimary = 0;
	expect(NativeLinkRoute_Decide(&in) == NATIVE_LINK_ROUTE_RUN, "no request, second client: unchanged behaviour");
	in.haveRequest = 1;
	expect(NativeLinkRoute_Decide(&in) == NATIVE_LINK_ROUTE_HAND_OFF,
	       "request with a running client: hand off, no second game process");
	in.isPrimary = 1;
	expect(NativeLinkRoute_Decide(&in) == NATIVE_LINK_ROUTE_RUN_WITH_REQUEST, "request, no client: start with it");
	in.storeUsable = 0;
	in.isPrimary = 0;
	expect(NativeLinkRoute_Decide(&in) == NATIVE_LINK_ROUTE_RUN, "unusable store: start with the request in memory");
	expect(NativeLinkRoute_Decide(NULL) == NATIVE_LINK_ROUTE_RUN, "NULL input");
}

static void TestDiagnostics(void)
{
	int s;
	for (s = NATIVE_LINK_PENDING_OK; s <= NATIVE_LINK_PENDING_ERR_IO + 1; s++)
	{
		const char *t = NativeLinkPending_StatusText((NativeLinkPendingStatus)s);
		expect(t != NULL && strstr(t, "ctr-ap") == NULL && strstr(t, "Player") == NULL &&
		           strstr(t, "archipelago") == NULL && strchr(t, '%') == NULL,
		       "status text is a fixed string without request fields");
	}
}

// ---------------------------------------------------------------------------
// Real store, several processes.

static char g_dir[256];

static void RealOps(NativeLinkHost *host, NativeLinkFsOps *ops)
{
	expect(NativeLinkHost_Init(host, g_dir), "state directory usable");
	NativeLinkHost_Ops(host, ops);
}

static void TestPrimaryLock(void)
{
	NativeLinkHost host;
	NativeLinkHost other;
	int pipefd[2];
	pid_t pid;
	char ready;
	int status;

	expect(NativeLinkHost_Init(&host, g_dir) && NativeLinkHost_Init(&other, g_dir), "hosts open");
	expect(NativeLinkHost_TryPrimary(&host), "first client becomes primary");
	expect(!NativeLinkHost_TryPrimary(&other), "second client in the same install is not primary");
	NativeLinkHost_ReleasePrimary(&host);
	expect(NativeLinkHost_TryPrimary(&other), "primary lock free after release");
	NativeLinkHost_ReleasePrimary(&other);

	// A primary that dies without releasing leaves no stale lock.
	if (pipe(pipefd) != 0)
	{
		expect(0, "pipe");
		return;
	}
	pid = fork();
	if (pid == 0)
	{
		NativeLinkHost child;
		close(pipefd[0]);
		if (!NativeLinkHost_Init(&child, g_dir) || !NativeLinkHost_TryPrimary(&child))
			_exit(2);
		if (write(pipefd[1], "r", 1) != 1)
			_exit(3);
		sleep(1);
		_exit(0); // no release: the process just ends
	}
	close(pipefd[1]);
	expect(read(pipefd[0], &ready, 1) == 1, "child took the primary lock");
	expect(!NativeLinkHost_TryPrimary(&host), "running primary in another process blocks a second one");
	waitpid(pid, &status, 0);
	close(pipefd[0]);
	expect(WIFEXITED(status) && WEXITSTATUS(status) == 0, "child exited");
	expect(NativeLinkHost_TryPrimary(&host), "lock of a dead primary is free again");
	NativeLinkHost_ReleasePrimary(&host);
}

static NativeLinkRecord ChildRecord(int child, int n)
{
	char slot[32];
	snprintf(slot, sizeof slot, "P%d_%d", child, n);
	return Rec("archipelago.gg", 38000 + (unsigned)child, slot, "room", (unsigned long long)(child * 1000 + n + 1),
	           NativeLinkHost_NowUnixMs());
}

static void TestSimultaneousPublishers(void)
{
	enum { KIDS = 8 };
	pid_t pids[KIDS];
	int i;
	int ok = 1;
	NativeLinkHost host;
	NativeLinkFsOps ops;
	NativeLinkRecord out;

	for (i = 0; i < KIDS; i++)
	{
		pids[i] = fork();
		if (pids[i] == 0)
		{
			NativeLinkHost h;
			NativeLinkFsOps o;
			NativeLinkRecord r = ChildRecord(i, 0);
			NativeLinkPendingStatus s;
			if (!NativeLinkHost_Init(&h, g_dir))
				_exit(2);
			NativeLinkHost_Ops(&h, &o);
			s = NativeLinkPending_Publish(&o, &r, r.createdUnixMs);
			_exit((s == NATIVE_LINK_PENDING_PUBLISHED || s == NATIVE_LINK_PENDING_REPLACED) ? 0 : 1);
		}
	}
	for (i = 0; i < KIDS; i++)
	{
		int status;
		waitpid(pids[i], &status, 0);
		ok = ok && WIFEXITED(status) && WEXITSTATUS(status) == 0;
	}
	expect(ok, "every simultaneous publisher succeeded");

	RealOps(&host, &ops);
	expect(NativeLinkPending_Claim(&ops, &out, NativeLinkHost_NowUnixMs()) == NATIVE_LINK_PENDING_CLAIMED &&
	           strncmp(out.request.slot, "P", 1) == 0,
	       "exactly one intact request remains");
	expect(NativeLinkPending_Claim(&ops, &out, NativeLinkHost_NowUnixMs()) == NATIVE_LINK_PENDING_NONE,
	       "and only one");
	expect(!ops.exists(ops.ctx, NATIVE_LINK_TEMP_FILE) && !ops.exists(ops.ctx, NATIVE_LINK_CONSUMING_FILE),
	       "no temp or consuming file left");
}

static void TestPublisherRacingConsumer(void)
{
	enum { KIDS = 4, EACH = 60 };
	pid_t pids[KIDS];
	int i;
	int ok = 1;
	int claimed = 0;
	int invalid = 0;
	int running = KIDS;
	NativeLinkHost host;
	NativeLinkFsOps ops;
	NativeLinkRecord out;

	RealOps(&host, &ops);
	for (i = 0; i < KIDS; i++)
	{
		pids[i] = fork();
		if (pids[i] == 0)
		{
			NativeLinkHost h;
			NativeLinkFsOps o;
			int n;
			if (!NativeLinkHost_Init(&h, g_dir))
				_exit(2);
			NativeLinkHost_Ops(&h, &o);
			for (n = 0; n < EACH; n++)
			{
				NativeLinkRecord r = ChildRecord(i, n);
				NativeLinkPendingStatus s = NativeLinkPending_Publish(&o, &r, r.createdUnixMs);
				if (s != NATIVE_LINK_PENDING_PUBLISHED && s != NATIVE_LINK_PENDING_REPLACED)
					_exit(1);
			}
			_exit(0);
		}
	}

	// The primary polls without waiting for the lock, as the game loop does.
	while (running > 0)
	{
		NativeLinkPendingStatus s = NativeLinkPending_Claim(&ops, &out, NativeLinkHost_NowUnixMs());
		if (s == NATIVE_LINK_PENDING_CLAIMED)
			claimed++;
		else if (s != NATIVE_LINK_PENDING_NONE && s != NATIVE_LINK_PENDING_BUSY)
			invalid++;
		for (i = 0; i < KIDS; i++)
		{
			int status;
			if (pids[i] > 0 && waitpid(pids[i], &status, WNOHANG) == pids[i])
			{
				ok = ok && WIFEXITED(status) && WEXITSTATUS(status) == 0;
				pids[i] = 0;
				running--;
			}
		}
	}
	// Drain what the last publisher left.
	for (i = 0; i < 100; i++)
	{
		NativeLinkPendingStatus s = NativeLinkPending_Claim(&ops, &out, NativeLinkHost_NowUnixMs());
		if (s == NATIVE_LINK_PENDING_CLAIMED)
			claimed++;
		else if (s == NATIVE_LINK_PENDING_NONE)
			break;
		else if (s != NATIVE_LINK_PENDING_BUSY)
			invalid++;
	}

	expect(ok, "every racing publisher succeeded");
	expect(invalid == 0, "no claim ever read a torn or invalid request");
	expect(claimed >= 1, "the primary claimed requests while publishers ran");
	expect(!ops.exists(ops.ctx, NATIVE_LINK_PENDING_FILE) && !ops.exists(ops.ctx, NATIVE_LINK_CONSUMING_FILE) &&
	           !ops.exists(ops.ctx, NATIVE_LINK_TEMP_FILE),
	       "store empty after the race");
}

static void TestRealFileHasNoSecrets(void)
{
	NativeLinkHost host;
	NativeLinkFsOps ops;
	NativeLinkRecord r = Rec("archipelago.gg", 38281, "Player", "room", 7, NativeLinkHost_NowUnixMs());
	char buf[NATIVE_LINK_RECORD_MAX + 1];
	size_t len = 0;

	RealOps(&host, &ops);
	expect(NativeLinkPending_Publish(&ops, &r, r.createdUnixMs) == NATIVE_LINK_PENDING_PUBLISHED, "real publish");
	expect(ops.read(ops.ctx, NATIVE_LINK_PENDING_FILE, buf, sizeof buf - 1, &len) == 1, "real file readable");
	buf[len] = '\0';
	expect(strstr(buf, "password") == NULL && strstr(buf, "ctr-ap-link 1\n") == buf, "real file has no password");
	{
		NativeLinkRecord out;
		expect(NativeLinkPending_Claim(&ops, &out, NativeLinkHost_NowUnixMs()) == NATIVE_LINK_PENDING_CLAIMED,
		       "real claim");
	}
}

int main(void)
{
	snprintf(g_dir, sizeof g_dir, "/tmp/ctr-link-harness-%d", (int)getpid());

	TestRecordFormat();
	TestIdentity();
	TestPendingProtocol();
	TestHandoffCoalescing();
	TestHandoffLatestWins();
	TestHandoffDeferral();
	TestHandoffNoSession();
	TestHandoffExpiry();
	TestRoute();
	TestDiagnostics();

	TestPrimaryLock();
	TestSimultaneousPublishers();
	TestPublisherRacingConsumer();
	TestRealFileHasNoSecrets();

	{
		char cmd[320];
		snprintf(cmd, sizeof cmd, "rm -rf %s", g_dir);
		if (system(cmd) != 0)
			printf("note: could not remove %s\n", g_dir);
	}

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
