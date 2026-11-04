#ifndef AP_TEXT_CLIP_H
#define AP_TEXT_CLIP_H

// Fit a line of text into a pixel width, and scroll it when it does not fit.
// Used by the Wumpa Shop page so an item name never prints over the price or
// SOLD column. Pure and free of engine types: the caller supplies the width of
// a run of text, which production takes from DecalFont_GetLineWidthStrlen (the
// font's real widths) and tools/test-text-clip.c takes from a model of it.
//
// CLIP. AP_TextClipLen gives how many leading characters fit in `avail` pixels
// together with a trailing "." that marks the cut. A text that fits whole is
// never marked.
//
// MARQUEE. AP_TextMarquee gives the window of a text that is too long: it
// waits at the start, moves one character at a time to the end, waits there,
// and loops. A text that fits does not move. The window never exceeds `avail`.

typedef int (*AP_TextWidthFn)(const char *s, int len, void *ctx);

#define AP_MARQUEE_HOLD_FRAMES 60 // pause at the start and at the end
#define AP_MARQUEE_STEP_FRAMES 6  // frames per character of movement

static inline int AP_TextLen(const char *s)
{
	int n = 0;
	while (s[n] != '\0')
		n++;
	return n;
}

// Leading characters of `s` (length `len`) that fit in `avail` pixels, with
// the cut marked by a "." when the whole text does not fit. Returns `len`
// when everything fits (no mark), otherwise the kept length; *marked says
// whether the caller should draw a "." after the kept prefix.
static inline int AP_TextClipLen(AP_TextWidthFn width, void *ctx, const char *s, int len,
                                 int avail, int *marked)
{
	int n;
	*marked = 0;
	if (avail <= 0)
		return 0;
	if (width(s, len, ctx) <= avail)
		return len;
	*marked = 1;
	for (n = len; n > 0; n--)
		if (width(s, n, ctx) + width(".", 1, ctx) <= avail)
			return n;
	return 0;
}

// First character of the last window: the smallest start such that the rest
// of the text fits in `avail`.
static inline int AP_TextLastStart(AP_TextWidthFn width, void *ctx, const char *s, int len,
                                   int avail)
{
	int st;
	for (st = 0; st < len; st++)
		if (width(s + st, len - st, ctx) <= avail)
			return st;
	return len > 0 ? len - 1 : 0;
}

// Length of the longest window starting at `start` that fits in `avail`.
static inline int AP_TextWindowLen(AP_TextWidthFn width, void *ctx, const char *s, int len,
                                   int start, int avail)
{
	int n = len - start;
	while (n > 0 && width(s + start, n, ctx) > avail)
		n--;
	return n;
}

// The marquee window at `frame` frames since the row was selected.
static inline void AP_TextMarquee(AP_TextWidthFn width, void *ctx, const char *s, int len,
                                  int avail, int frame, int *start, int *shown)
{
	int last, cycle, f;
	*start = 0;
	if (width(s, len, ctx) <= avail)
	{
		*shown = len;
		return;
	}
	last = AP_TextLastStart(width, ctx, s, len, avail);
	cycle = AP_MARQUEE_HOLD_FRAMES + last * AP_MARQUEE_STEP_FRAMES + AP_MARQUEE_HOLD_FRAMES;
	f = frame < 0 ? 0 : frame % cycle;
	if (f >= AP_MARQUEE_HOLD_FRAMES)
	{
		f -= AP_MARQUEE_HOLD_FRAMES;
		*start = f / AP_MARQUEE_STEP_FRAMES;
		if (*start > last)
			*start = last;
	}
	*shown = AP_TextWindowLen(width, ctx, s, len, *start, avail);
}

#endif
