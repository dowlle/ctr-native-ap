#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ap/ap_goal_line.h"
#include "../ap/ap_text_clip.h"

static int small_font_width(const char *text, int len, void *ctx)
{
	(void)ctx;
	int width = 0;
	for (int i = 0; i < len; ++i)
		width += text[i] == ' ' ? 6 : 13;
	return width;
}

int main(void)
{
	const int oxideModes[] = {AP_OXIDE_GOAL_ANY, AP_OXIDE_GOAL_FINAL,
	                          AP_OXIDE_GOAL_OPTIONAL, AP_OXIDE_GOAL_DISABLED};
	int cases = 0;
	for (int mode = 0; mode < 4; ++mode)
	for (int bosses = 0; bosses <= 4; ++bosses)
	for (int gems = 0; gems <= 5; ++gems)
	for (int wumpa = 0; wumpa <= 5000; ++wumpa)
	{
		AP_GoalLines lines;
		int oxide = oxideModes[mode];
		int count = AP_GoalFormatLines(lines, oxide, 1, 0, bosses, 3, gems, 4, wumpa, 2866);
		assert(count == (mode < 2) + (bosses > 0) + (gems > 0) + (wumpa > 0));
		int row = 0;
		char expected[AP_GOAL_LINE_CAP];
		if (mode < 2)
			assert(strcmp(lines[row++], mode == 0 ? "Oxide Challenge 1/1" : "Oxide Final 0/1") == 0);
		if (bosses) {
			snprintf(expected, sizeof expected, "Boss wins %d/%d", bosses < 3 ? bosses : 3, bosses);
			assert(strcmp(lines[row++], expected) == 0);
		}
		if (gems) {
			snprintf(expected, sizeof expected, "Gem colours %d/%d", gems < 4 ? gems : 4, gems);
			assert(strcmp(lines[row++], expected) == 0);
		}
		if (wumpa) {
			snprintf(expected, sizeof expected, "Wumpa %d/%d", wumpa < 2866 ? wumpa : 2866, wumpa);
			assert(strcmp(lines[row++], expected) == 0);
		}
		for (int i = 0; i < count; ++i) {
			int len = (int)strlen(lines[i]);
			assert(small_font_width(lines[i], len, NULL) <= 448);
			assert(111 + i * 9 + 8 <= 146);
			assert(668 - (count - 1 - i) * 19 >= 611);
		}
		++cases;
	}

	/* An overflow in one row scrolls independently of the adjacent counter. */
	const char *longText = "An individual goal condition whose label exceeds the available width";
	const char *shortText = "Wumpa 5000/5000";
	int start, shown;
	int len = (int)strlen(longText);
	AP_TextMarquee(small_font_width, NULL, longText, len, 448, 72, &start, &shown);
	assert(start > 0 && shown < len && small_font_width(longText + start, shown, NULL) <= 448);
	len = (int)strlen(shortText);
	AP_TextMarquee(small_font_width, NULL, shortText, len, 448, 72, &start, &shown);
	assert(start == 0 && shown == len);
	printf("goal lines: %d condition combinations and independent overflow passed\n", cases);
	return 0;
}
