"""The Current Seed page cannot trap the player (#133 PR 7).

Compiles game/230/MM_CurrentSeed.c, the page Options > Custom Content shows
in a content-plan room, with stub drawing and a stub room behind the real
decisions in ap/ap_seed_content_logic.h, and drives it frame by frame.

Cases:
  1. a room without a content plan (legacy or none) never sees the page: it
     returns 0 every frame, so the old Custom Content page runs unchanged,
  2. Back (Triangle) leaves Custom Content except while the install overview
     is open; then it only closes the overview,
  3. Cross opens the overview only when there is something to download;
     otherwise the reason is shown and Back still leaves,
  4. Cross in the overview starts the install once and closes the overview,
  5. L1 / R1 switch to the package page (which then runs) and close the
     overview; Back is never held there,
  6. the overview closes itself when the room changes under it,
  7. Up / Down wrap through the rows; leaving and coming back starts clean,
  8. Square rescans, except while an install runs.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
page = (root / "game/230/MM_CurrentSeed.c").read_text(encoding="utf-8")

prelude = r'''
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ap/ap_seed_content_logic.h"
typedef struct { short x, y, w, h; } RECT;
enum { FONT_BIG = 1, FONT_SMALL = 2 };
enum { ORANGE = 0, WHITE = 4, RED = 3, JUSTIFY_CENTER = 0x8000, JUSTIFY_RIGHT = 0x4000, TRANS_50_DECAL = 1 };
enum { BTN_UP = 0x1, BTN_DOWN = 0x2, BTN_CROSS = 0x10 | 0x4000, BTN_SQUARE = 0x20 | 0x8000, BTN_CIRCLE = 0x40,
       BTN_R1 = 0x400, BTN_L1 = 0x800, BTN_START = 0x1000, BTN_TRIANGLE = 0x40000 };
struct GamepadBuffer { int buttonsTapped; };
struct GameTracker { unsigned int timer; };
struct { struct GameTracker *gGT; int menuRowHighlight_Normal; } sdata_s;
#define sdata (&sdata_s)
static struct GameTracker gt;
static int draws;
int DecalFont_GetLineWidth(char *s, short font) { (void)font; return (int)strlen(s) * 8; }
void DecalFont_DrawLineOT(char *s, int x, int y, int font, int flags, uint32_t *ot) { (void)s; (void)x; (void)y; (void)font; (void)flags; (void)ot; draws++; }
void OtherFX_Play(int a, int b) { (void)a; (void)b; }
void CTR_Box_DrawClearBox(RECT *r, int *c, int t, uint32_t *ot) { (void)r; (void)c; (void)t; (void)ot; }
static int s_customContentSection = 5;

/* The stub room. */
static int active = 1, busy, starts, rescans, plan_result = AP_SEED_PLAN_OK;
static struct AP_SeedContentRow room[3];
static int room_count = 3;
int AP_SeedContentActive(void) { return active; }
int AP_SeedContentRows(struct AP_SeedContentRow *rows, int max)
{
	int i;
	for (i = 0; i < room_count && i < max; i++) rows[i] = room[i];
	return i;
}
int AP_SeedContentInstallPreview(int *plan, int *count, uint64_t *bytes, char *reason, size_t size)
{
	if (plan_result != AP_SEED_PLAN_OK) { snprintf(reason, size, "refused %d", plan_result); return plan_result; }
	plan[0] = 0; *count = 1; *bytes = 2586568; reason[0] = 0;
	return AP_SEED_PLAN_OK;
}
int AP_SeedContentInstallStart(char *m, size_t n) { starts++; busy = 1; snprintf(m, n, "Downloading"); return 1; }
int AP_SeedContentInstallTick(char *m, size_t n) { (void)m; (void)n; return busy; }
int AP_SeedContentInstallBusy(void) { return busy; }
void AP_CustomContentRescan(void) { rescans++; }
'''

tests = r'''
static int failures, checks;
static void expect(int ok, const char *what)
{
	checks++;
	if (!ok) { failures++; printf("FAIL: %s\n", what); }
}
static int frame(int buttons)
{
	struct GamepadBuffer pad = {buttons};
	gt.timer++;
	return MM_CurrentSeed_Page(NULL, &pad);
}
/* What MM_MenuProc_Config does with Back this frame, before the page runs. */
static int back_leaves(void) { return !MM_CurrentSeed_ConsumesBack(s_customContentSection); }
int main(void)
{
	int i;
	sdata_s.gGT = &gt;
	for (i = 0; i < 3; i++)
	{
		memset(&room[i], 0, sizeof room[i]);
		snprintf(room[i].title, sizeof room[i].title, "Track %d", i);
		snprintf(room[i].folder, sizeof room[i].folder, "t%d", i);
		room[i].scan = i == 0 ? AP_SEED_SCAN_NOT_INSTALLED : AP_SEED_SCAN_READY;
		room[i].source = AP_SEED_SOURCE_SAPHI;
		room[i].locator.bigfile_index = -1;
	}

	/* 1. No content plan. */
	active = 0;
	expect(frame(0) == 0 && frame(BTN_CROSS) == 0 && frame(BTN_L1) == 0 && back_leaves(), "no plan: old page every frame, Back leaves");
	active = 1;

	/* 2-4. The overview. */
	expect(frame(0) == 1 && back_leaves(), "Current Seed draws; Back leaves");
	expect(frame(BTN_CROSS) == 1 && s_seedOverview && !back_leaves(), "Cross opens the overview; Back is held for it");
	expect(frame(BTN_TRIANGLE) == 1 && !s_seedOverview && back_leaves() && starts == 0, "Triangle closes it, installs nothing");
	frame(BTN_CROSS);
	expect(frame(BTN_CROSS) == 1 && starts == 1 && !s_seedOverview && back_leaves(), "Cross in the overview installs once and closes");
	expect(frame(BTN_CROSS) == 1 && starts == 1, "while installing, Cross only previews");
	busy = 0; s_seedOverview = 0;
	plan_result = AP_SEED_PLAN_MANUAL_ONLY;
	expect(frame(BTN_CROSS) == 1 && !s_seedOverview && back_leaves() && strstr(s_seedMessage, "refused") != NULL,
	       "nothing to download: the reason, no overview, Back leaves");
	plan_result = AP_SEED_PLAN_OK;

	/* 5. Tabs. */
	frame(BTN_CROSS);
	expect(s_seedOverview && frame(BTN_R1) == 0 && !s_seedOverview && back_leaves(), "R1 goes to the package page and closes the overview");
	expect(frame(BTN_CROSS) == 0 && !s_seedOverview && back_leaves() && starts == 1, "the package page owns Cross; Back leaves");
	expect(frame(BTN_L1) == 1 && s_seedTab == 0, "L1 comes back to Current Seed");

	/* 6. The room changes under the overview. */
	frame(BTN_CROSS);
	plan_result = AP_SEED_PLAN_ALL_READY;
	expect(frame(0) == 1 && !s_seedOverview && back_leaves(), "the overview closes itself when nothing is left to install");
	plan_result = AP_SEED_PLAN_OK;
	frame(BTN_CROSS);
	active = 0;
	expect(back_leaves() && frame(0) == 0 && !s_seedOverview, "a room that drops its plan never holds Back");
	active = 1;

	/* 7. Selection and fresh visits. */
	s_seedSelected = 0;
	frame(BTN_UP);
	expect(s_seedSelected == 2, "Up wraps to the last row");
	frame(BTN_DOWN);
	expect(s_seedSelected == 0, "Down wraps to the first row");
	room_count = 1;
	s_seedSelected = 2;
	frame(0);
	expect(s_seedSelected == 0, "a shrinking room clamps the selection");
	room_count = 0;
	expect(frame(BTN_DOWN) == 1 && s_seedSelected == 0, "an empty list draws and does not move");
	room_count = 3;
	frame(BTN_CROSS);
	gt.timer += 100; /* the player left the page */
	expect(frame(0) == 1 && !s_seedOverview && s_seedMessage[0] == 0, "a fresh visit starts clean");

	/* 8. Rescan. */
	frame(BTN_SQUARE);
	expect(rescans == 1 && strstr(s_seedMessage, "Rescanned") != NULL, "Square rescans");
	busy = 1;
	frame(BTN_SQUARE);
	expect(rescans == 1 && strstr(s_seedMessage, "Wait") != NULL, "no rescan while an install runs");
	gt.timer += 100;
	frame(0);
	expect(strstr(s_seedMessage, "Wait") != NULL, "a running install keeps its message across visits");
	busy = 0;

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "seed_page.c", Path(tmp) / "seed_page"
    src.write_text(prelude + page + tests, encoding="utf-8")
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-fsanitize=undefined,address", "-fno-sanitize-recover=all",
                    "-I" + str(root), str(src), "-o", str(exe)], check=True, cwd=root)
    subprocess.run([str(exe)], check=True, cwd=root)
print("PASS: Current Seed page: off without a plan, Back held only by the open overview, one install per confirm, "
      "tabs, self-closing overview, selection bounds, fresh visits, rescan")
