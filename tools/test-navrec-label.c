#include <stdio.h>

#include "../ap/ap_navrec_label_logic.h"

static int failures;

static void check(int condition, const char *name)
{
	printf("%s  %s\n", condition ? "ok " : "BAD", name);
	if (!condition)
		failures++;
}

int main(void)
{
	check(AP_NavRec_LabelWidthForDepth(-1) == 0, "behind-camera depth is hidden");
	check(AP_NavRec_LabelWidthForDepth(149) == 0, "near clip ends below 150");
	check(AP_NavRec_LabelWidthForDepth(150) == 13, "nearest visible band starts at 150");
	check(AP_NavRec_LabelWidthForDepth(189) == 13, "nearest visible band ends below 190");
	check(AP_NavRec_LabelWidthForDepth(190) == 12, "second band starts at 190");
	check(AP_NavRec_LabelWidthForDepth(234) == 11, "third band starts at 234");
	check(AP_NavRec_LabelWidthForDepth(331) == 10, "fourth band starts at 331");
	check(AP_NavRec_LabelWidthForDepth(475) == 9, "fifth band starts at 475");
	check(AP_NavRec_LabelWidthForDepth(905) == 9, "fifth band ends below 906");
	check(AP_NavRec_LabelWidthForDepth(906) == 7, "furthest band starts at 906");
	check(AP_NavRec_LabelWidthForDepth(100000) == 7, "far labels retain the minimum width");

	printf("\n%d failure(s)\n", failures);
	return failures != 0;
}
