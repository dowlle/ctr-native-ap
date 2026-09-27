#ifndef AP_TEXT_BUF_LOGIC_H
#define AP_TEXT_BUF_LOGIC_H

// Freestanding growable text buffer for files the AP layer rewrites whole
// (ap-state.json). The game thread formats the complete content into memory,
// which costs microseconds, compares it with the last content it handed to the
// background writer (ap/ap_file_writer.h) and only hands over a copy when the
// bytes changed. The file system is never touched on the game thread.
//
// Pinned by tools/test-text-buf.c. No engine, no SDL.
//
// Growth: the buffer doubles until the text fits, capped at AP_TEXT_BUF_MAX.
// vsnprintf is called with a copy of the argument list per attempt and a return
// value of -1 is treated as "did not fit" as well, so a C runtime that answers
// -1 on truncation instead of the needed length still grows correctly. A failed
// allocation or the cap sets `failed`; a failed buffer is never written out.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AP_TEXT_BUF_INITIAL (16 * 1024)
#define AP_TEXT_BUF_MAX     (1024 * 1024)

typedef struct AP_TextBuf
{
	char  *data; // NUL-terminated while not failed
	size_t len;  // bytes of text, excluding the NUL
	size_t cap;  // allocated bytes, including room for the NUL
	int    failed;
} AP_TextBuf;

// Start a new text, keeping the allocation.
static inline void AP_TextBufReset(AP_TextBuf *b)
{
	b->len = 0;
	b->failed = 0;
	if (b->data != NULL)
		b->data[0] = '\0';
}

static inline void AP_TextBufFree(AP_TextBuf *b)
{
	free(b->data);
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
	b->failed = 0;
}

// Make room for `extra` more bytes plus the NUL. Returns 0 (and sets failed) when
// that would pass the cap or the allocation fails.
static inline int AP_TextBufReserve(AP_TextBuf *b, size_t extra)
{
	size_t need, cap;
	char *p;

	if (b->failed)
		return 0;
	need = b->len + extra + 1;
	if (need <= b->cap)
		return 1;
	if (need > AP_TEXT_BUF_MAX)
	{
		b->failed = 1;
		return 0;
	}
	cap = b->cap ? b->cap : AP_TEXT_BUF_INITIAL;
	while (cap < need)
		cap *= 2;
	if (cap > AP_TEXT_BUF_MAX)
		cap = AP_TEXT_BUF_MAX;
	p = (char *)realloc(b->data, cap);
	if (p == NULL)
	{
		b->failed = 1;
		return 0;
	}
	b->data = p;
	b->cap = cap;
	return 1;
}

static inline void AP_TextBufPuts(AP_TextBuf *b, const char *s)
{
	size_t n = strlen(s);
	if (!AP_TextBufReserve(b, n))
		return;
	memcpy(b->data + b->len, s, n + 1);
	b->len += n;
}

static inline void AP_TextBufVPrintf(AP_TextBuf *b, const char *fmt, va_list ap)
{
	if (!AP_TextBufReserve(b, 0))
		return;
	for (;;)
	{
		size_t room = b->cap - b->len;
		va_list cp;
		int n;

		va_copy(cp, ap);
		n = vsnprintf(b->data + b->len, room, fmt, cp);
		va_end(cp);
		if (n >= 0 && (size_t)n < room)
		{
			b->len += (size_t)n;
			return;
		}
		// Did not fit: grow to the reported size, or double when the runtime
		// only said "too small".
		if (!AP_TextBufReserve(b, n >= 0 ? (size_t)n : room * 2))
		{
			if (b->data != NULL && b->cap > 0)
				b->data[b->len < b->cap ? b->len : b->cap - 1] = '\0';
			return;
		}
	}
}

#if defined(__GNUC__)
static inline void AP_TextBufPrintf(AP_TextBuf *b, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
#endif
static inline void AP_TextBufPrintf(AP_TextBuf *b, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	AP_TextBufVPrintf(b, fmt, ap);
	va_end(ap);
}

static inline int AP_TextBufEquals(const AP_TextBuf *a, const AP_TextBuf *b)
{
	if (a->failed || b->failed || a->len != b->len)
		return 0;
	return a->len == 0 || memcmp(a->data, b->data, a->len) == 0;
}

// dst = src (same bytes). Returns 0 when dst could not grow; dst is then marked
// failed so it never compares equal.
static inline int AP_TextBufCopy(AP_TextBuf *dst, const AP_TextBuf *src)
{
	AP_TextBufReset(dst);
	if (src->failed)
	{
		dst->failed = 1;
		return 0;
	}
	if (!AP_TextBufReserve(dst, src->len))
		return 0;
	if (src->len > 0)
		memcpy(dst->data, src->data, src->len);
	dst->data[src->len] = '\0';
	dst->len = src->len;
	return 1;
}

// Should the freshly built text go to disk? Not when building it failed, and not
// when it is byte-identical to the last text handed to the writer AND that write
// succeeded (a failed write is retried with the next dump even if nothing
// changed).
static inline int AP_TextBufShouldWrite(const AP_TextBuf *now, const AP_TextBuf *lastQueued,
                                        int lastWriteOk)
{
	if (now->failed)
		return 0;
	return !(lastWriteOk && AP_TextBufEquals(now, lastQueued));
}

#endif // AP_TEXT_BUF_LOGIC_H
