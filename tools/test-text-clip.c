// cc -std=c99 -Wall -Wextra -Werror -I ap -o /tmp/test-text-clip tools/test-text-clip.c
//
// ap/ap_text_clip.h: the Wumpa Shop's name column. A name is clipped to the
// width left of the price column with at least one character cell of gap, the
// selected row scrolls a name that does not fit (hold, scroll, hold, loop),
// and no window ever passes the column. Widths come from a model of the
// engine's DecalFont_GetLineWidthStrlen with the FONT_SMALL table of
// game/zGlobal_DATA.c (character 13, colon and period 7, button glyphs
// @ [ ^ * 2 more than a character), so a change in how widths are summed here
// has to match the engine function it models.
#include <stdio.h>
#include <string.h>

#include "ap_text_clip.h"

static int failures;
#define CHECK(label, expr) do { int ok_ = !!(expr); printf("%s  %s\n", ok_ ? "ok  " : "FAIL", label); failures += !ok_; } while (0)

#define CHAR_PX 13
#define PUNC_PX 7
#define BUTTON_PX 2

static int ModelWidth(const char *s, int len, void *ctx)
{
	int w = 0;
	(void)ctx;
	for (; *s != '\0' && len != 0; s++, len--)
	{
		unsigned char c = (unsigned char)*s;
		if (c == '@' || c == '[' || c == '^' || c == '*')
			w += BUTTON_PX;
		if (c == ':' || c == '.')
			w += PUNC_PX - CHAR_PX;
		if (c > 2)
			w += CHAR_PX;
	}
	return w;
}

// The page's geometry (ap_wumpa_shop_ui.c): name at x 0x44, price right edge 0x1d0.
static int Avail(const char *right)
{
	return 0x1d0 - 0x44 - ModelWidth(right, -1, 0) - ModelWidth(" ", 1, 0);
}

static void Window(const char *name, int avail, int frame, int *start, int *shown)
{
	AP_TextMarquee(ModelWidth, 0, name, (int)strlen(name), avail, frame, start, shown);
}

int main(void)
{
	const char *longName = "LETTER C CORTEX VORTEX CUSTOM (SOMEONE)";
	const char *rights[] = {"SOLD", "...", "AT 1000", "100", "7", "AT 999999"};
	int ri, marked, n, f, start, shown, avail, last, cycle;

	CHECK("model: ordinary glyph is 13 px", ModelWidth("A", 1, 0) == 13);
	CHECK("model: period is 7 px", ModelWidth(".", 1, 0) == 7);
	CHECK("model: a button glyph is 15 px", ModelWidth("^", 1, 0) == 15);

	// Clip: the kept text plus its mark and a gap cell never reaches the price column.
	for (ri = 0; ri < 6; ri++)
	{
		const char *r = rights[ri];
		char label[96];
		avail = Avail(r);
		n = AP_TextClipLen(ModelWidth, 0, longName, (int)strlen(longName), avail, &marked);
		snprintf(label, sizeof label, "clip beside \"%s\": fits with the mark and a one-cell gap", r);
		CHECK(label, marked && ModelWidth(longName, n, 0) + ModelWidth(".", 1, 0) <= avail);
		snprintf(label, sizeof label, "clip beside \"%s\": one more character would not fit", r);
		CHECK(label, n >= (int)strlen(longName) || ModelWidth(longName, n + 1, 0) + ModelWidth(".", 1, 0) > avail);
		snprintf(label, sizeof label, "clip beside \"%s\": text ends a full cell before the price", r);
		CHECK(label, 0x44 + ModelWidth(longName, n, 0) + ModelWidth(".", 1, 0) + CHAR_PX <= 0x1d0 - ModelWidth(r, -1, 0));
	}
	avail = Avail("SOLD");
	n = AP_TextClipLen(ModelWidth, 0, "SHORT", 5, avail, &marked);
	CHECK("a short name is kept whole and unmarked", n == 5 && !marked);
	n = AP_TextClipLen(ModelWidth, 0, "EXACTLYFITTING", 14, ModelWidth("EXACTLYFITTING", 14, 0), &marked);
	CHECK("a name that exactly fills the column is not marked", n == 14 && !marked);
	n = AP_TextClipLen(ModelWidth, 0, "ANYTHING", 8, 5, &marked);
	CHECK("a column narrower than one glyph keeps nothing", n == 0 && marked);
	n = AP_TextClipLen(ModelWidth, 0, "", 0, avail, &marked);
	CHECK("an empty name stays empty", n == 0 && !marked);
	// Names and owner both count: the same text length but a wide owner part clips earlier.
	CHECK("the owner part counts toward the width",
	      AP_TextClipLen(ModelWidth, 0, "SMALL WUMPA BUNDLE (A VERY LONG PLAYER NAME)", 44, avail, &marked) < 44 && marked);

	// Marquee over a long name beside SOLD.
	avail = Avail("SOLD");
	Window(longName, avail, 0, &start, &shown);
	CHECK("marquee: starts at the first character", start == 0 && shown > 0 && shown < (int)strlen(longName));
	Window(longName, avail, AP_MARQUEE_HOLD_FRAMES - 1, &start, &shown);
	CHECK("marquee: holds at the start", start == 0);
	Window(longName, avail, AP_MARQUEE_HOLD_FRAMES + AP_MARQUEE_STEP_FRAMES, &start, &shown);
	CHECK("marquee: moves one character per step after the hold", start == 1);
	last = AP_TextLastStart(ModelWidth, 0, longName, (int)strlen(longName), avail);
	cycle = AP_MARQUEE_HOLD_FRAMES * 2 + last * AP_MARQUEE_STEP_FRAMES;
	Window(longName, avail, AP_MARQUEE_HOLD_FRAMES + last * AP_MARQUEE_STEP_FRAMES + 3, &start, &shown);
	CHECK("marquee: reaches the end of the name", start == last && start + shown == (int)strlen(longName));
	Window(longName, avail, cycle - 1, &start, &shown);
	CHECK("marquee: holds at the end", start == last);
	Window(longName, avail, cycle, &start, &shown);
	CHECK("marquee: loops back to the start", start == 0);
	Window(longName, avail, cycle * 3 + 5, &start, &shown);
	CHECK("marquee: stays looping", start == 0);
	{
		int widest = 0, monotone = 1, prev = 0;
		for (f = 0; f < cycle * 2; f++)
		{
			Window(longName, avail, f, &start, &shown);
			if (ModelWidth(longName + start, shown, 0) > widest)
				widest = ModelWidth(longName + start, shown, 0);
			if (f < cycle && start < prev)
				monotone = 0;
			prev = start;
			if (start + shown > (int)strlen(longName))
				monotone = 0;
		}
		CHECK("marquee: no window is wider than the column", widest <= avail);
		CHECK("marquee: the window only moves forward within one pass and stays in the name", monotone);
	}
	Window("SHORT", avail, 500, &start, &shown);
	CHECK("marquee: a name that fits does not move", start == 0 && shown == 5);
	Window(longName, avail, -5, &start, &shown);
	CHECK("marquee: a negative frame counts as the start", start == 0);
	Window("", avail, 10, &start, &shown);
	CHECK("marquee: an empty name is fine", start == 0 && shown == 0);

	printf("%s\n", failures ? "FAILED" : "all passed");
	return failures != 0;
}
