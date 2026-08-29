// Out-of-engine assertions for the overhead contributor-name label bands.
//
// Compiles the REAL code: ap/ap_navrec_label_logic.h is freestanding, so the
// band boundaries asserted here are the ones the client draws with.
//
//   cc -Wall -Wextra -o /tmp/test-navrec-label tools/test-navrec-label.c && /tmp/test-navrec-label
//
// Exit 0 = every assertion held; the failing case is printed otherwise.

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

	// Draw-distance cap. The ladder's last measured band is the width 9 that
	// ends at 906; beyond it the old code drew every bot at one fixed 7-pixel
	// width, out to the SZ3 clamp, so a name stayed readable past the depth at
	// which the engine has already coarsened the world itself. A label is now
	// hidden past the cap the same way it is hidden behind the camera, which is
	// the path the draw loop already honours.
	check(AP_NavRec_LabelWidthForDepth(AP_NAVREC_LABEL_MAX_DEPTH - 1) != 0, "a label inside the cap is drawn");
	check(AP_NavRec_LabelWidthForDepth(AP_NAVREC_LABEL_MAX_DEPTH - 1) == 9, "the last band inside the cap keeps its measured width");
	check(AP_NavRec_LabelWidthForDepth(AP_NAVREC_LABEL_MAX_DEPTH) == 0, "the cap boundary itself is hidden");
	check(AP_NavRec_LabelWidthForDepth(906) == 0, "no label is drawn at the old catch-all band");
	check(AP_NavRec_LabelWidthForDepth(1575) == 0, "no label at the level's recursive-near depth");
	check(AP_NavRec_LabelWidthForDepth(11700) == 0, "no label at the BSP LOD switch depth");
	check(AP_NavRec_LabelWidthForDepth(0xFFFF) == 0, "no label at the SZ3 clamp");
	check(AP_NAVREC_LABEL_MAX_DEPTH == 906, "the cap is the ladder's own top boundary, not a new literal");

	printf("\n%d failure(s)\n", failures);
	return failures != 0;
}
