#ifndef AP_PAD_TITLE_WRAP_H
#define AP_PAD_TITLE_WRAP_H

// The custom track name on a warp pad (CN1).
//
// A custom track name can be longer than the room the hub map leaves beside
// the name, up to 105 bytes for a generated room title with a player suffix.
// It used to scroll in a clipped window, so the first and last letters were
// always cut off. Now the whole name is drawn at once: on one line when it
// fits, else wrapped at word boundaries into at most AP_PAD_TITLE_BIG_LINES
// lines in the title font, else in the small font (a name too long for three
// big lines). A word wider than the box is split between letters. The lines
// are balanced (the narrowest box that keeps the line count), so the name
// reads as a block and not as one long line and a stub.
//
// The block grows upward from the line the name always had: the last line
// stays where the single line was, and everything above the name (racer line,
// portrait, feed) moves up by ap_pad_title_extra_lift.
//
// Freestanding on purpose: tools/test-pad-title-wrap.py runs it on the host
// with the production font width routine.

#define AP_PAD_TITLE_MAX_LINES 10
#define AP_PAD_TITLE_BIG_LINES 3

typedef struct
{
	int font;                       // the font id passed in as big or small
	int count;                      // lines, 1..AP_PAD_TITLE_MAX_LINES
	int start[AP_PAD_TITLE_MAX_LINES]; // byte offsets into the name
	int len[AP_PAD_TITLE_MAX_LINES];   // bytes per line, no edge spaces
	int overflow;                   // 1 when the last line still is wider than the box
} ap_pad_title_layout;

// Width in pixels of the first `len` bytes of `text` in `font`.
typedef int (*ap_pad_title_width_fn)(const char *text, int len, int font);

// Greedy wrap into at most `max` lines of at most `width` pixels; returns the
// number of lines it needed (it keeps counting past `max` without storing).
// A single word wider than the box is cut between letters.
static inline int ap_pad_title_wrap(const char *text, int width, int font,
                                    ap_pad_title_width_fn fn, int max, ap_pad_title_layout *out)
{
	int n = 0, i = 0;
	int total = 0;
	while (text[total])
		total++;
	while (i < total)
	{
		int end, lastBreak = -1;
		while (i < total && text[i] == ' ')
			i++;
		if (i >= total)
			break;
		end = i;
		// extend by whole characters while the line still fits
		while (end < total && fn(text + i, end + 1 - i, font) <= width)
		{
			if (text[end] == ' ')
				lastBreak = end;
			end++;
		}
		if (end >= total)
		{
			// the rest fits
		}
		else if (end == i)
		{
			end = i + 1; // one letter wider than the box: still make progress
		}
		else if (text[end] == ' ')
		{
			// the word ended exactly at the edge
		}
		else if (lastBreak > i)
		{
			end = lastBreak; // break at the last space that fit
		}
		// else: one word wider than the box, cut between letters at `end`
		{
			int e = end;
			while (e > i && text[e - 1] == ' ')
				e--;
			if (out && n < max)
			{
				out->start[n] = i;
				out->len[n] = e - i;
			}
			n++;
		}
		i = end;
	}
	return n ? n : 1;
}

static inline void ap_pad_title_fill(const char *text, int width, int font,
                                     ap_pad_title_width_fn fn, int max, ap_pad_title_layout *out)
{
	int lo = 1, hi = width, count, w;
	ap_pad_title_layout scratch;
	out->font = font;
	count = ap_pad_title_wrap(text, width, font, fn, max, out);
	out->count = count < max ? count : max;
	out->overflow = count > max;
	if (count > 1 && count <= max)
	{
		// balance: the narrowest box that still gives the same line count
		while (lo < hi)
		{
			const int mid = (lo + hi) / 2;
			if (ap_pad_title_wrap(text, mid, font, fn, max, &scratch) <= count)
				hi = mid;
			else
				lo = mid + 1;
		}
		w = hi;
		ap_pad_title_wrap(text, w, font, fn, max, out);
	}
	if (!out->overflow)
	{
		int i;
		for (i = 0; i < out->count; i++)
			if (fn(text + out->start[i], out->len[i], font) > width)
				out->overflow = 1;
	}
}

// Choose the layout: the whole name on one big line, else up to
// AP_PAD_TITLE_BIG_LINES big lines, else small lines. `width` is the box.
static inline void ap_pad_title_layout_for(const char *text, int width, int bigFont, int smallFont,
                                           ap_pad_title_width_fn fn, ap_pad_title_layout *out)
{
	ap_pad_title_fill(text, width, bigFont, fn, AP_PAD_TITLE_BIG_LINES, out);
	if (!out->overflow)
		return;
	ap_pad_title_fill(text, width, smallFont, fn, AP_PAD_TITLE_MAX_LINES, out);
}

// Vertical pitch between wrapped lines: the glyph height and two pixels.
static inline int ap_pad_title_pitch(int glyphHeight)
{
	return glyphHeight + 2;
}

// Height of the block in pixels.
static inline int ap_pad_title_block_height(const ap_pad_title_layout *layout, int glyphHeight)
{
	return (layout->count - 1) * ap_pad_title_pitch(glyphHeight) + glyphHeight;
}

// How far the lines above the name move up, in pixels, beyond the single big
// line the layout always had (`bigHeight` pixels tall).
static inline int ap_pad_title_extra_lift(const ap_pad_title_layout *layout, int glyphHeight, int bigHeight)
{
	const int h = ap_pad_title_block_height(layout, glyphHeight);
	return h > bigHeight ? h - bigHeight : 0;
}

#endif
