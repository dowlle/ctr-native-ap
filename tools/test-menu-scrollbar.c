// Focused checks of the menu scroll bar geometry (A4-36).
//
//   cc -Wall -Wextra -Werror -I include -o /tmp/test-menu-scrollbar tools/test-menu-scrollbar.c && /tmp/test-menu-scrollbar

#include <stdio.h>
#include <ctr_menu_scrollbar.h>

static int failures;

#define CHECK(name, expr) do { if (!(expr)) { printf("FAIL %s\n", name); failures++; } } while (0)

int main(void)
{
	int y, h, total, visible, top;

	// A list that fits, or an empty one, draws no bar.
	CHECK("fits exactly", !CTR_ScrollBarThumb(9, 9, 0, 126, 8, &y, &h));
	CHECK("fewer rows", !CTR_ScrollBarThumb(3, 9, 0, 126, 8, &y, &h));
	CHECK("empty", !CTR_ScrollBarThumb(0, 9, 0, 126, 8, &y, &h));
	CHECK("no window", !CTR_ScrollBarThumb(12, 0, 0, 126, 8, &y, &h));
	CHECK("no track", !CTR_ScrollBarThumb(12, 9, 0, 0, 8, &y, &h));

	// Twelve rows, nine shown: thumb is three quarters of the track.
	CHECK("12 rows draws", CTR_ScrollBarThumb(12, 9, 0, 120, 8, &y, &h));
	CHECK("12 rows thumb height", h == 90);
	CHECK("top window at the top", y == 0);
	CHECK("bottom window at the bottom", CTR_ScrollBarThumb(12, 9, 3, 120, 8, &y, &h) && y + h == 120);
	CHECK("middle window between", CTR_ScrollBarThumb(12, 9, 1, 120, 8, &y, &h) && y > 0 && y + h < 120);

	// Out-of-range tops are clamped into the track.
	CHECK("negative top", CTR_ScrollBarThumb(12, 9, -5, 120, 8, &y, &h) && y == 0);
	CHECK("past the end", CTR_ScrollBarThumb(12, 9, 99, 120, 8, &y, &h) && y + h == 120);

	// A long list keeps a thumb that can be seen.
	CHECK("minimum thumb", CTR_ScrollBarThumb(2000, 9, 0, 126, 8, &y, &h) && h == 8);

	// Large lists and tracks do not overflow.
	CHECK("huge list", CTR_ScrollBarThumb(2000000000, 1000, 1999999000, 100000, 8, &y, &h) && h == 8 && y == 100000 - 8);
	CHECK("huge track", CTR_ScrollBarThumb(2000000000, 1000000000, 1000000000, 2000000000, 8, &y, &h) && h == 1000000000 && y == 1000000000);

	// Every shape: inside the track, monotone in top, ends exact.
	for (total = 2; total <= 200; total++)
		for (visible = 1; visible < total && visible <= 20; visible++)
		{
			int prev = -1;
			for (top = 0; top <= total - visible; top++)
			{
				if (!CTR_ScrollBarThumb(total, visible, top, 126, 8, &y, &h)) { printf("FAIL draws %d/%d\n", total, visible); failures++; break; }
				if (y < 0 || h < 8 || y + h > 126 || y < prev) { printf("FAIL bounds %d/%d/%d\n", total, visible, top); failures++; break; }
				prev = y;
			}
			CTR_ScrollBarThumb(total, visible, 0, 126, 8, &y, &h);
			if (y != 0) { printf("FAIL start %d/%d\n", total, visible); failures++; }
			CTR_ScrollBarThumb(total, visible, total - visible, 126, 8, &y, &h);
			if (y + h != 126) { printf("FAIL end %d/%d\n", total, visible); failures++; }
		}

	if (failures)
		return 1;
	puts("PASS: scroll bar geometry");
	return 0;
}
