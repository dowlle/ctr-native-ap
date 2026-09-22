#include "platform/native_link_pending.h"

#include <stdio.h>
#include <string.h>

// Pending one-click-connect request store (issue #334, slice 3). See the header
// for the protocol. Freestanding: no engine headers, no SDL and no direct
// filesystem calls, so the host harness compiles this unit as it is.

#define NATIVE_LINK_RECORD_SCHEMA "ctr-ap-link 1"

static int linkAsciiLower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

static int linkHostEqual(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0')
	{
		if (linkAsciiLower((unsigned char)*a) != linkAsciiLower((unsigned char)*b))
			return 0;
		a++;
		b++;
	}
	return *a == *b;
}

int NativeLinkRequest_SameIdentity(const NativeLaunchRequest *a, const NativeLaunchRequest *b)
{
	if (a == NULL || b == NULL)
		return 0;
	if (a->port != b->port)
		return 0;
	if (!linkHostEqual(a->host, b->host))
		return 0;
	if (strcmp(a->slot, b->slot) != 0)
		return 0;
	if (a->room[0] == '\0' || b->room[0] == '\0')
		return 1;
	return strcmp(a->room, b->room) == 0;
}

static int linkIsUnreserved(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
	       c == '-' || c == '.' || c == '_' || c == '~';
}

// Append src percent-encoded. Returns the new length, or 0 when it does not fit.
static size_t linkAppendEncoded(char *out, size_t cap, size_t at, const char *src)
{
	static const char hex[] = "0123456789ABCDEF";

	for (; *src != '\0'; src++)
	{
		const unsigned char c = (unsigned char)*src;
		if (linkIsUnreserved(c))
		{
			if (at + 1 >= cap)
				return 0;
			out[at++] = (char)c;
		}
		else
		{
			if (at + 3 >= cap)
				return 0;
			out[at++] = '%';
			out[at++] = hex[c >> 4];
			out[at++] = hex[c & 0x0F];
		}
	}
	out[at] = '\0';
	return at;
}

static size_t linkAppendRaw(char *out, size_t cap, size_t at, const char *src)
{
	const size_t n = strlen(src);
	if (at + n + 1 > cap)
		return 0;
	memcpy(out + at, src, n + 1);
	return at + n;
}

int NativeLinkRequest_Encode(const NativeLaunchRequest *request, char *out, size_t cap)
{
	char port[16];
	size_t at;

	if (request == NULL || out == NULL || cap == 0)
		return 0;
	out[0] = '\0';
	snprintf(port, sizeof port, "%u", request->port);

	at = linkAppendRaw(out, cap, 0, NATIVE_LAUNCH_REQUEST_URI_PREFIX NATIVE_LAUNCH_REQUEST_AUTHORITY "?host=");
	if (at == 0 || (at = linkAppendEncoded(out, cap, at, request->host)) == 0)
		return 0;
	if ((at = linkAppendRaw(out, cap, at, "&port=")) == 0 || (at = linkAppendRaw(out, cap, at, port)) == 0)
		return 0;
	if ((at = linkAppendRaw(out, cap, at, "&slot=")) == 0 || (at = linkAppendEncoded(out, cap, at, request->slot)) == 0)
		return 0;
	if ((at = linkAppendRaw(out, cap, at, "&room=")) == 0 || (at = linkAppendEncoded(out, cap, at, request->room)) == 0)
		return 0;
	return 1;
}

size_t NativeLinkRecord_Format(const NativeLinkRecord *record, char *out, size_t cap)
{
	char uri[NATIVE_LAUNCH_REQUEST_URI_MAX + 1];
	int n;

	if (record == NULL || out == NULL || cap == 0 || record->createdUnixMs < 0)
		return 0;
	if (!NativeLinkRequest_Encode(&record->request, uri, sizeof uri))
		return 0;

	n = snprintf(out, cap, NATIVE_LINK_RECORD_SCHEMA "\ntoken %016llx\ncreated %lld\nrequest %s\n",
	             record->token, record->createdUnixMs, uri);
	if (n <= 0 || (size_t)n >= cap)
		return 0;
	return (size_t)n;
}

// Take one "<prefix><value>\n" line from text; value is copied into buf.
static int linkTakeLine(const char **cursor, const char *end, const char *prefix, char *buf, size_t cap)
{
	const size_t prefixLen = strlen(prefix);
	const char *p = *cursor;
	const char *nl;
	size_t n;

	if ((size_t)(end - p) < prefixLen || memcmp(p, prefix, prefixLen) != 0)
		return 0;
	p += prefixLen;
	nl = (const char *)memchr(p, '\n', (size_t)(end - p));
	if (nl == NULL)
		return 0;
	n = (size_t)(nl - p);
	if (n == 0 || n >= cap)
		return 0;
	memcpy(buf, p, n);
	buf[n] = '\0';
	*cursor = nl + 1;
	return 1;
}

static int linkParseHex64(const char *s, unsigned long long *out)
{
	unsigned long long v = 0;
	int i;

	if (strlen(s) != 16)
		return 0;
	for (i = 0; i < 16; i++)
	{
		const char c = s[i];
		int d;
		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else
			return 0;
		v = (v << 4) | (unsigned long long)d;
	}
	*out = v;
	return 1;
}

static int linkParseDec63(const char *s, long long *out)
{
	long long v = 0;
	size_t i;
	const size_t n = strlen(s);

	if (n == 0 || n > 18 || (n > 1 && s[0] == '0'))
		return 0;
	for (i = 0; i < n; i++)
	{
		if (s[i] < '0' || s[i] > '9')
			return 0;
		v = v * 10 + (s[i] - '0');
	}
	*out = v;
	return 1;
}

int NativeLinkRecord_Parse(const char *text, size_t len, NativeLinkRecord *out)
{
	const char *cursor = text;
	const char *end;
	char line[NATIVE_LAUNCH_REQUEST_URI_MAX + 2];
	NativeLinkRecord record;

	if (text == NULL || out == NULL || len == 0 || len > NATIVE_LINK_RECORD_MAX)
		return 0;
	if (memchr(text, '\0', len) != NULL)
		return 0;
	end = text + len;

	if ((size_t)(end - cursor) < sizeof(NATIVE_LINK_RECORD_SCHEMA) ||
	    memcmp(cursor, NATIVE_LINK_RECORD_SCHEMA "\n", sizeof(NATIVE_LINK_RECORD_SCHEMA)) != 0)
		return 0;
	cursor += sizeof(NATIVE_LINK_RECORD_SCHEMA);

	if (!linkTakeLine(&cursor, end, "token ", line, sizeof line) || !linkParseHex64(line, &record.token))
		return 0;
	if (!linkTakeLine(&cursor, end, "created ", line, sizeof line) ||
	    !linkParseDec63(line, &record.createdUnixMs))
		return 0;
	if (!linkTakeLine(&cursor, end, "request ", line, sizeof line))
		return 0;
	if (cursor != end)
		return 0; // trailing bytes: not a record this build wrote
	if (NativeLaunchRequest_Parse(line, &record.request) != NATIVE_LAUNCH_REQUEST_OK)
		return 0;

	*out = record;
	return 1;
}

int NativeLinkRecord_Expired(long long createdUnixMs, long long nowUnixMs)
{
	const long long age = nowUnixMs - createdUnixMs;
	if (age < -NATIVE_LINK_PENDING_FUTURE_SKEW_MS)
		return 1;
	return age > NATIVE_LINK_PENDING_TTL_MS;
}

NativeLinkPendingStatus NativeLinkPending_Publish(const NativeLinkFsOps *ops, const NativeLinkRecord *record,
                                                  long long nowUnixMs)
{
	char text[NATIVE_LINK_RECORD_MAX];
	char existingText[NATIVE_LINK_RECORD_MAX];
	size_t textLen;
	size_t existingLen = 0;
	NativeLinkRecord existing;
	NativeLinkPendingStatus result = NATIVE_LINK_PENDING_PUBLISHED;
	int readResult;

	if (ops == NULL || record == NULL)
		return NATIVE_LINK_PENDING_ERR_ARG;
	textLen = NativeLinkRecord_Format(record, text, sizeof text);
	if (textLen == 0)
		return NATIVE_LINK_PENDING_ERR_ARG;

	if (!ops->lock(ops->ctx, 1))
		return NATIVE_LINK_PENDING_ERR_LOCK;

	// A temp file can only be left behind by a publisher that crashed while it
	// held this lock; nobody owns it now.
	ops->remove(ops->ctx, NATIVE_LINK_TEMP_FILE);

	readResult = ops->read(ops->ctx, NATIVE_LINK_PENDING_FILE, existingText, sizeof existingText, &existingLen);
	if (readResult == 1)
	{
		if (NativeLinkRecord_Parse(existingText, existingLen, &existing) &&
		    NativeLinkRequest_SameIdentity(&existing.request, &record->request) &&
		    !NativeLinkRecord_Expired(existing.createdUnixMs, nowUnixMs))
		{
			ops->unlock(ops->ctx);
			return NATIVE_LINK_PENDING_COALESCED;
		}
		result = NATIVE_LINK_PENDING_REPLACED;
	}
	else if (readResult < 0)
	{
		result = NATIVE_LINK_PENDING_REPLACED;
	}

	if (!ops->write_new(ops->ctx, NATIVE_LINK_TEMP_FILE, text, textLen))
	{
		ops->remove(ops->ctx, NATIVE_LINK_TEMP_FILE);
		ops->unlock(ops->ctx);
		return NATIVE_LINK_PENDING_ERR_IO;
	}
	if (!ops->replace(ops->ctx, NATIVE_LINK_TEMP_FILE, NATIVE_LINK_PENDING_FILE))
	{
		ops->remove(ops->ctx, NATIVE_LINK_TEMP_FILE);
		ops->unlock(ops->ctx);
		return NATIVE_LINK_PENDING_ERR_IO;
	}

	ops->unlock(ops->ctx);
	return result;
}

NativeLinkPendingStatus NativeLinkPending_Claim(const NativeLinkFsOps *ops, NativeLinkRecord *out,
                                                long long nowUnixMs)
{
	char text[NATIVE_LINK_RECORD_MAX];
	size_t textLen = 0;
	NativeLinkRecord record;
	int readResult;

	if (ops == NULL || out == NULL)
		return NATIVE_LINK_PENDING_ERR_ARG;

	// Cheap pre-check so an idle primary does not take the lock every poll.
	if (!ops->exists(ops->ctx, NATIVE_LINK_PENDING_FILE) && !ops->exists(ops->ctx, NATIVE_LINK_CONSUMING_FILE))
		return NATIVE_LINK_PENDING_NONE;

	if (!ops->lock(ops->ctx, 0))
		return NATIVE_LINK_PENDING_BUSY;

	if (ops->exists(ops->ctx, NATIVE_LINK_CONSUMING_FILE))
	{
		// A claim crashed between its rename and its delete. A pending file
		// published since then is newer and wins; otherwise the leftover is
		// recovered and goes through the same checks as a fresh claim.
		if (ops->exists(ops->ctx, NATIVE_LINK_PENDING_FILE))
		{
			if (!ops->remove(ops->ctx, NATIVE_LINK_CONSUMING_FILE))
			{
				ops->unlock(ops->ctx);
				return NATIVE_LINK_PENDING_ERR_IO;
			}
		}
	}

	if (!ops->exists(ops->ctx, NATIVE_LINK_CONSUMING_FILE))
	{
		if (!ops->exists(ops->ctx, NATIVE_LINK_PENDING_FILE))
		{
			ops->unlock(ops->ctx);
			return NATIVE_LINK_PENDING_NONE;
		}
		if (!ops->replace(ops->ctx, NATIVE_LINK_PENDING_FILE, NATIVE_LINK_CONSUMING_FILE))
		{
			ops->unlock(ops->ctx);
			return NATIVE_LINK_PENDING_ERR_IO;
		}
	}

	readResult = ops->read(ops->ctx, NATIVE_LINK_CONSUMING_FILE, text, sizeof text, &textLen);
	// Only the lock holder can create the consuming file, so this deletes the
	// file this claim owns and never a pending file published meanwhile.
	ops->remove(ops->ctx, NATIVE_LINK_CONSUMING_FILE);
	ops->unlock(ops->ctx);

	if (readResult != 1 || !NativeLinkRecord_Parse(text, textLen, &record))
		return NATIVE_LINK_PENDING_INVALID;
	if (NativeLinkRecord_Expired(record.createdUnixMs, nowUnixMs))
		return NATIVE_LINK_PENDING_EXPIRED;

	*out = record;
	return NATIVE_LINK_PENDING_CLAIMED;
}

const char *NativeLinkPending_StatusText(NativeLinkPendingStatus status)
{
	switch (status)
	{
	case NATIVE_LINK_PENDING_OK:
		return "ok";
	case NATIVE_LINK_PENDING_PUBLISHED:
		return "request handed to the running client";
	case NATIVE_LINK_PENDING_REPLACED:
		return "request handed to the running client (replaced an older request)";
	case NATIVE_LINK_PENDING_COALESCED:
		return "the same request is already waiting";
	case NATIVE_LINK_PENDING_CLAIMED:
		return "request received";
	case NATIVE_LINK_PENDING_NONE:
		return "no request waiting";
	case NATIVE_LINK_PENDING_BUSY:
		return "request store busy";
	case NATIVE_LINK_PENDING_EXPIRED:
		return "request expired";
	case NATIVE_LINK_PENDING_INVALID:
		return "unreadable request dropped";
	case NATIVE_LINK_PENDING_ERR_ARG:
		return "invalid request";
	case NATIVE_LINK_PENDING_ERR_LOCK:
		return "request store locked";
	case NATIVE_LINK_PENDING_ERR_IO:
		return "request store write failed";
	default:
		return "request store error";
	}
}
