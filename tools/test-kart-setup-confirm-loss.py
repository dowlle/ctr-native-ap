"""In-race Kart Setup: a confirm on the frame the row disappears is swallowed.

Compiles the PRODUCTION race pause menu (game/MAIN/MainFreeze.c, whole), the
production menu engine (game/RECTMENU.c, whole) and the production page
(ap/ap_kartsetup.c) together, with the retail Adventure race pause menu and
row table taken from game/zGlobal_DATA.c. Only engine, audio, drawing and
network calls are stubbed; the connection is one flag behind
AP_CharSwap_PauseRowLive.

Each frame follows the game's order: MainFrame_GameLogic's Start unpause (it
reads AnyPlayerTap as the previous menu pass left it, game/MAIN/MainFrame.c),
then MainFrame_RenderFrame's RECTMENU_CollectInput and RECTMENU_ProcessState,
which runs AP_KartSetupMenuFrame (and with it MainFreeze_APKartSetupSync)
before the menu's own RECTMENU_ProcessInput.

The case under test: KART SETUP is highlighted, the player presses Cross (or
Start), and on that same frame a disconnect removes the row. The sync moves
the highlight to RESUME. The press must be consumed: the race stays paused,
the pause menu stays on screen, ordinary RESUME still works, and a fresh press
on the very next frame is accepted.

The harness builds twice. The second build removes the swallow from
AP_KartSetupMenuFrame (the code before this fix) and must show the old
failure: the press confirms RESUME and the race unpauses.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]


def initializer(source, name):
    """Text of `.name = { ... }` from the retail data table, braces balanced."""
    start = source.index("." + name + " =")
    brace = source.index("{", start)
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise AssertionError(name)


globals_src = (root / "game/zGlobal_DATA.c").read_text()
retail_rows = initializer(globals_src, "rowsAdvRace")
retail_menu = initializer(globals_src, "menuAdvRace")
assert "MainFreeze_MenuPtrDefault" in retail_menu and "rowsAdvRace" in retail_menu

# The Start unpause the harness replays, as MainFrame_GameLogic has it.
mainframe = (root / "game/MAIN/MainFrame.c").read_text()
assert re.search(r"\(\(sdata->AnyPlayerTap & BTN_START\) != 0\)\)\s*\{\s*RECTMENU_ClearInput\(\);\s*"
                 r"gGT->gameMode1 &= ~PAUSE_1;", mainframe), "MainFrame Start unpause changed; update the harness"
render = (root / "game/MAIN/MainFrame_RenderFrame.c").read_text()
assert render.index("RECTMENU_CollectInput();") < render.index("RECTMENU_ProcessState();")

page = (root / "ap/ap_kartsetup.c").read_text()
FIX = re.compile(r"\t// If that removed the highlighted KART SETUP row.*?\n\tif \(MainFreeze_APKartSetupSync\(\) && !ap_ks\.open && !ap_ks\.request\)\n\t\{\n.*?\n\t\}\n", re.S)
assert len(FIX.findall(page)) == 1, "swallow block not found in AP_KartSetupMenuFrame"
old_page = FIX.sub("\tMainFreeze_APKartSetupSync();\n", page)

fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <common.h>
#include "ap/ap_wumpa_shop_menu.h"
// The game build declares these through its own include chain; a newer GCC
// refuses implicit declarations, so the harness names them up front.
#include "ap/ap_hooks.h"
#include "ap/ap_charswap.h"
#include "ap/ap_deathlink.h"
#include "ap/ap_fasttravel.h"
#include "ap/ap_tracker.h"
#include "ap/ap_link.h"
#include "ap/ap_update.h"
#include "game/MAIN/MainFreeze.c"
#include "game/RECTMENU.c"
#include "PAGE_SOURCE"

struct sData sdata_static;
struct Data data = { RETAIL_ROWS, RETAIL_MENU };
struct OverlayDATA_232 D232;
NativeConfig g_config;

// ---- the connection: KART SETUP is live while `live` is set ----------------
static int live = 1;
int AP_CharSwap_PauseRowLive(void) { return live; }
int AP_CapabilityRoomStatsRace(void) { return 1; }
unsigned AP_CharSwap_KartSetupRows(void) { return 0x7; }
int AP_CharSwap_KartSetupAdjust(int row, int dir) { (void)dir; return row; }
void AP_CharSwap_KartSetupDrawPanel(int panelY, int focusRow) { (void)panelY; (void)focusRow; }
void AP_CharSwap_KartSetupResetBars(void) {}

// ---- observed engine effects --------------------------------------------------
static int resume_audio, elim_off, confirm_sfx;
int OtherFX_Play(u32 id, int flags) { (void)flags; if (id == 1) confirm_sfx++; return 0; }
void MainFrame_TogglePauseAudio(b32 pause) { if (!pause) resume_audio++; }
void ElimBG_Deactivate(struct GameTracker *g) { (void)g; elim_off++; }

// ---- everything else the two files call --------------------------------------
void AddPrim(void *ot, void *pri) { (void)ot; (void)pri; }
void AH_Pause_Destroy(void) {}
void AH_Pause_Update(void) {}
void AP_AiDifficultyCommit(void) {}
int AP_CharSwap_PickerOpen(void) { return 0; }
void AP_CharSwap_RequestPickerFromPause(void) {}
int AP_CupReturnHub(void) { return 0; }
void AP_DeathLinkOnPauseLeave(int quit) { (void)quit; }
int AP_FastTravelAvailable(void) { return 0; }
void AP_FastTravelShowMenu(void) {}
int AP_LinkMenuFrame(void) { return 0; }
void AP_LogLine(const char *msg) { (void)msg; }
int AP_TrackerAvailable(void) { return 0; }
int AP_TrackerMenuFrame(void) { return 0; }
void AP_TrackerRequestOpen(void) {}
int AP_UpdateMenuFrame(void) { return 0; }
int AP_WumpaShopMenuFrame(void) { return 0; }
void CTR_Box_DrawClearBox(const RECT *r, const Color *c, int t, uint32_t *ot) { (void)r; (void)c; (void)t; (void)ot; }
void CTR_Box_DrawSolidBox(RECT *r, Color c, uint32_t *ot) { (void)r; (void)c; (void)ot; }
void CTR_Box_DrawWirePrims(Point a, Point b, Color c, void *ot) { (void)a; (void)b; (void)c; (void)ot; }
void DecalFont_DrawLine(char *s, int x, int y, s16 f, int fl) { (void)s; (void)x; (void)y; (void)f; (void)fl; }
int DecalFont_DrawMultiLine(char *s, int x, int y, int w, s16 f, int fl) { (void)s; (void)x; (void)y; (void)w; (void)f; (void)fl; return 0; }
int DecalFont_GetLineWidth(char *s, s16 f) { (void)f; return (int)strlen(s) * 8; }
void DecalHUD_Arrow2D(struct Icon *i, s16 x, s16 y, struct PrimMem *p, uint32_t *ot, u32 a, u32 b, u32 c, u32 d, char t, int s, u16 r)
{ (void)i; (void)x; (void)y; (void)p; (void)ot; (void)a; (void)b; (void)c; (void)d; (void)t; (void)s; (void)r; }
void DecalHUD_DrawPolyGT4(struct Icon *i, s16 x, s16 y, struct PrimMem *p, uint32_t *ot, u32 a, u32 b, u32 c, u32 d, char t, s16 s)
{ (void)i; (void)x; (void)y; (void)p; (void)ot; (void)a; (void)b; (void)c; (void)d; (void)t; (void)s; }
void ElimBG_Activate(struct GameTracker *g) { (void)g; }
void GhostTape_Destroy(void) {}
int howl_ModeGet(void) { return 0; }
void howl_ModeSet(int m) { (void)m; }
int howl_VolumeGet(int t) { (void)t; return 0; }
void howl_VolumeSet(int t, u8 v) { (void)t; (void)v; }
int LOAD_IsOpen_AdvHub(void) { return 0; }
void MainRaceTrack_RequestLoad(s16 l) { (void)l; }
int MATH_Cos(u32 a) { (void)a; return 0x1000; }
int MATH_Sin(u32 a) { (void)a; return 0; }
void NativeConfig_Save(void) {}
uint32_t NativeGpuLinks_FromHostPointer(const void *p) { (void)p; return 0; }
char *NativeMenu_Labels(const char *t) { return (char *)t; }
void NativeRenderer_CaptureWindowGeometry(void) {}
void OptionsMenu_TestSound(int r, int p) { (void)r; (void)p; }
void RaceFlag_BeginTransition(int d) { (void)d; }
int RaceFlag_GetCanDraw(void) { return 1; }
int RaceFlag_IsFullyOffScreen(void) { return 0; }
int RaceFlag_IsFullyOnScreen(void) { return 0; }
void RaceFlag_SetCanDraw(s16 v) { (void)v; }
int RngDeadCoed(struct RngDeadCoedState *s) { (void)s; return 0; }
int VehPickupItem_MaskBoolGoodGuy(struct Driver *d) { (void)d; return 0; }

// ---- the frame loop -------------------------------------------------------------
static struct GameTracker gt;
static struct GamepadSystem pads;
static struct DB db;
static u32 prims[0x4000], ot[0x40];
static char *lng[0x400];

// MainFrame_GameLogic's Start unpause (game/MAIN/MainFrame.c), the part that
// matters here: it reads the aggregate the previous menu pass left behind.
static void game_logic(void)
{
	if ((gt.gameMode1 & PAUSE_ALL) == 0 || gt.cooldownfromPauseUntilUnpause != 0)
		return;
	if (sdata->ptrActiveMenu != &data.menuRacingWheelConfig && sdata->ptrActiveMenu != &D232.menuHintMenu &&
	    (sdata->AnyPlayerTap & BTN_START) != 0)
	{
		RECTMENU_ClearInput();
		gt.gameMode1 &= ~PAUSE_1;
		MainFrame_TogglePauseAudio(0);
		if (sdata->ptrActiveMenu != NULL)
			RECTMENU_Hide(sdata->ptrActiveMenu);
	}
}

// One frame: `pad` is controller 0's tap, `pad2` controller 1's.
static void frame2(int pad, int pad2)
{
	game_logic();
	pads.gamepad[0].buttonsTapped = pad;
	pads.gamepad[1].buttonsTapped = pad2;
	db.primMem.cursor = db.primMem.start;
	if (sdata->ptrActiveMenu != NULL || (gt.gameMode1 & END_OF_RACE) != 0)
		RECTMENU_CollectInput();
	if (sdata->ptrActiveMenu != NULL)
		RECTMENU_ProcessState();
}
static void frame(int pad) { frame2(pad, 0); }

static struct RectMenu *const menu = &data.menuAdvRace;
static int paused(void) { return (gt.gameMode1 & PAUSE_1) != 0; }
static int row_is(int string) { return menu->rows[menu->rowSelected].stringIndex == string; }

// A fresh Adventure race pause with the room connected: the first pass syncs
// KART SETUP in below RESUME.
static void pause_race(void)
{
	memset(&gt, 0, sizeof gt);
	gt.gameMode1 = ADVENTURE_MODE | PAUSE_1;
	gt.numPlyrCurrGame = 1;
	gt.numPlyrNextGame = 2; // two pads plugged in: both listen to the menu
	db.primMem.start = db.primMem.cursor = prims;
	db.primMem.end = prims + 0x4000;
	db.primMem.capacityBytes = sizeof prims;
	db.otMem.uiOT = ot;
	gt.backBuffer = &db;
	for (int i = 0; i < 0x400; i++)
		lng[i] = "ROW";
	sdata->lngStrings = lng;
	sdata->gGT = &gt;
	sdata->gGamepads = &pads;
	menu->rows = &data.rowsAdvRace[0];
	menu->rowSelected = 0;
	menu->state &= ~NEEDS_TO_CLOSE;
	sdata->ptrActiveMenu = menu;
	sdata->ptrDesiredMenu = NULL;
	sdata->activeSubMenu = NULL;
	live = 1;
	resume_audio = elim_off = confirm_sfx = 0;
	frame(0);
	assert(menu->rows != &data.rowsAdvRace[0] && row_is(LNG_RESUME));
	assert(menu->rows[1].stringIndex == AP_LNG_KART_SETUP);
}

static void highlight_kart_setup(void)
{
	frame(BTN_DOWN);
	assert(row_is(AP_LNG_KART_SETUP) && paused());
}

// The race is still paused with the pause menu up and nothing resumed.
static void still_paused(void)
{
	assert(paused());
	assert(sdata->ptrActiveMenu == menu && (menu->state & NEEDS_TO_CLOSE) == 0);
	assert(resume_audio == 0 && elim_off == 0);
	assert(!AP_KartSetupOpen());
}

// Ordinary RESUME, confirmed with Cross.
static void resume_with_cross(void)
{
	assert(row_is(LNG_RESUME));
	frame(BTN_CROSS_one);
	assert(!paused() && resume_audio == 1 && elim_off == 1);
	assert((menu->state & NEEDS_TO_CLOSE) != 0 && sdata->ptrActiveMenu == NULL);
}

int main(void)
{
#ifdef OLD_BEHAVIOUR
	// Before the fix: the confirm meant for KART SETUP selects RESUME.
	pause_race();
	highlight_kart_setup();
	live = 0;
	frame(BTN_CROSS_one);
	assert(!paused() && resume_audio == 1);
	puts("old behaviour reproduced: the disappearing row's confirm resumed the race");
	return 0;
#else
	// 1. Cross on KART SETUP, row removed by the same frame's sync.
	pause_race();
	highlight_kart_setup();
	live = 0;
	frame(BTN_CROSS_one);
	still_paused();
	assert(menu->rows == &data.rowsAdvRace[0] && row_is(LNG_RESUME));
	assert(sdata->AnyPlayerTap == 0 && sdata->buttonTapPerPlayer[0] == 0);
	frame(0); // MainFrame sees nothing left over, an idle frame changes nothing
	still_paused();
	resume_with_cross(); // ordinary RESUME works afterwards
	puts("ok    Cross on a disappearing KART SETUP row is swallowed; RESUME still works");

	// 2. Fresh input on the very next frame is accepted.
	pause_race();
	highlight_kart_setup();
	live = 0;
	frame(BTN_CROSS_one);
	still_paused();
	resume_with_cross();
	puts("ok    a fresh press on the next frame is accepted");

	// 3. Start on the loss frame does not reach MainFrame's unpause next frame;
	//    a later Start still unpauses the way retail does.
	pause_race();
	highlight_kart_setup();
	live = 0;
	frame(BTN_START);
	frame(0);
	still_paused();
	frame(BTN_START);
	frame(0);
	assert(!paused() && resume_audio == 1);
	puts("ok    Start on the loss frame is swallowed; the next Start unpauses");

	// 4. The second controller (the pause menu listens to every pad) is
	//    swallowed the same way.
	pause_race();
	highlight_kart_setup();
	live = 0;
	frame2(0, BTN_CROSS_one);
	still_paused();
	frame2(0, BTN_CROSS_one);
	assert(!paused() && resume_audio == 1);
	puts("ok    a second controller's Cross on the loss frame is swallowed");

	// 5. Only the highlighted KART SETUP row: with OPTIONS highlighted, the
	//    disconnect keeps OPTIONS under the cursor and its press acts.
	pause_race();
	frame(BTN_UP);
	assert(row_is(0xE));
	live = 0;
	frame(BTN_CROSS_one);
	assert(menu->rows == &data.rowsAdvRace[0]);
	assert(sdata->ptrDesiredMenu == &data.menuRacingWheelConfig && paused());
	puts("ok    a press on another row during the disconnect is not eaten");

	// 6. Ordinary RESUME with the row live, and the existing open, back and
	//    pending paths.
	pause_race();
	resume_with_cross();
	pause_race();
	highlight_kart_setup();
	frame(BTN_CROSS_one); // confirm: request
	assert(paused() && !AP_KartSetupOpen());
	frame(0); // pending open: the page takes the frame
	assert(AP_KartSetupOpen() && paused());
	frame(BTN_START); // Back, never unpause
	frame(0);
	assert(!AP_KartSetupOpen() && paused() && resume_audio == 0);
	assert(row_is(AP_LNG_KART_SETUP));
	frame(BTN_UP);
	resume_with_cross();
	puts("ok    RESUME with the row live, page open and Start-back unchanged");

	// 7. Existing: row removed while the open is pending, and while the page is
	//    open, each swallows that frame's press.
	pause_race();
	highlight_kart_setup();
	frame(BTN_CROSS_one); // request
	live = 0;
	frame(BTN_CROSS_one);
	still_paused();
	resume_with_cross();
	pause_race();
	highlight_kart_setup();
	frame(BTN_CROSS_one);
	frame(0);
	assert(AP_KartSetupOpen());
	live = 0;
	frame(BTN_CROSS_one);
	still_paused();
	resume_with_cross();
	puts("ok    pending and open pages still swallow the press when the row goes");

	// 8. A junk highlight index (a restored savestate) is bounded by the
	//    table's terminator before the swap clamps it to RESUME.
	pause_race();
	menu->rowSelected = 99;
	live = 0;
	frame(0);
	assert(menu->rows == &data.rowsAdvRace[0] && row_is(LNG_RESUME));
	still_paused();
	puts("ok    a junk restored row index is read within the table");

	puts("kart setup confirm on a disappearing row: PASS");
	return 0;
#endif
}
'''


def build_and_run(tmp, name, page_source, extra):
    page_path = Path(tmp) / (name + "_page.c")
    page_path.write_text(page_source)
    src = Path(tmp) / (name + ".c")
    exe = Path(tmp) / name
    text = fixture.replace('"PAGE_SOURCE"', '"' + str(page_path) + '"')
    text = text.replace("RETAIL_ROWS", retail_rows).replace("RETAIL_MENU", retail_menu)
    src.write_text(text)
    subprocess.run(["cc", "-m32", "-std=gnu99", "-Werror=implicit-function-declaration",
                    "-Werror=incompatible-pointer-types", "-Werror=int-conversion",
                    "-DCTR_AP", "-DCTR_NATIVE", "-DBUILD=926", *extra,
                    "-I", str(root / "include"), "-I", str(root), "-I", str(root / "ap"),
                    "-I", str(root / "externals/SDL/include"), str(src), "-o", str(exe), "-lm"], check=True)
    return subprocess.run([str(exe)], check=False)


with tempfile.TemporaryDirectory() as tmp:
    fixed = build_and_run(tmp, "fixed", page, [])
    if fixed.returncode != 0:
        sys.exit("FAIL  fixed build")
    old = build_and_run(tmp, "old", old_page, ["-DOLD_BEHAVIOUR"])
    if old.returncode != 0:
        sys.exit("FAIL  the pre-fix build no longer shows the old failure; the regression lost its teeth")
print("kart setup confirm loss (fixed and pre-fix builds): PASS")
