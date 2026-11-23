"""Exercise production split-hub menu input and a contested tunnel crossing."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
rectmenu = (root / "game/RECTMENU.c").read_text()
hub = (root / "game/LOAD/LOAD_Hub.c").read_text()
frame = (root / "game/MAIN/MainFrame.c").read_text()
pad = (root / "game/232/AH_WarpPad.c").read_text()


def function(source, signature):
    start = source.index(signature + "\n{")
    return source[start:source.index("\n}\n", start) + 3]


fixture = r'''
#include <assert.h>
#include <common.h>
#include "ap/ap_pauserow.h"
#include "ap/ap_coop.h"
struct sData sdata_static;
struct OverlayDATA_232 D232;
struct RectMenu menu221;
static struct GameTracker tracker;
static struct GamepadSystem pads;
static struct RectMenu p2Menu;
static int requestedLevel, swaps, leader = -1;
static int warpPlayer = -1;
static const int connected[5][3] = {
    {26, 27, 29}, {25, 27, 28}, {25, 26, 28}, {26, 27, 29}, {25, 27, 28}
};
#define LOAD_HUB_CONNECTED_LEV(hub, index) connected[hub][index]

int RngDeadCoed(struct RngDeadCoedState *state) { return 0; }
int OtherFX_Play(u32 sound, int flags) { return 0; }
int AP_SkipHints(void) { return 0; }
int AP_CoopMenuOpen(void) { return 0; }
int AP_CoopP2Available(void) { return 1; }
int AP_CoopHubWarpDriver(const struct GameTracker *gt) { return warpPlayer; }
void VehPhysProc_FreezeEndEvent_Init(struct Thread *t, struct Driver *d) {}

int AP_CoopHubHeldDriver(const struct GameTracker *gt) { return -1; }
void LOAD_Hub_ReadFile(struct BigHeader *file, int level, int pack)
{
    requestedLevel = level;
    assert(pack == 2);
}
void LOAD_Hub_SwapNow(void)
{
    assert(requestedLevel == GEM_STONE_VALLEY);
    swaps++;
}
void AP_CoopPlaceBeside(struct GameTracker *gt, int enteringPlayer)
{
    leader = enteringPlayer;
}
'''
fixture += function(pad, "int AH_WarpPad_CoopMenuPlayer(const struct RectMenu *menu)")
fixture += function(pad, "static int AH_WarpPad_CoopSelectorInView(const struct GameTracker *gt, int player)")
for name in ("RECTMENU_ClearInput", "RECTMENU_CollectInput"):
    fixture += function(rectmenu, f"void {name}()")
fixture += function(rectmenu, "int RECTMENU_ProcessInput(struct RectMenu *m)")
fixture += function(frame, "void MainFrame_RequestMaskHint(s16 hintId, char interruptWarpPad)")
fixture += hub[hub.index("static int coopCrossingDriver"):
               hub.index("\n#endif", hub.index("static int coopCrossingDriver"))]
fixture += r'''

int main(void)
{
    sdata->gGT = &tracker;
    sdata->gGamepads = &pads;
    tracker.numPlyrNextGame = 1;
    struct MenuRow rows[2] = {{.stringIndex = 1, .rowOnPressDown = 1}, {.stringIndex = 2}};
    p2Menu.rows = rows;
    p2Menu.stringIndexTitle = LNG_CHOOSE_RACE_TYPE;
    D232.menuTokenRelic = p2Menu;
    struct RectMenu *selector = &D232.menuTokenRelic;
    tracker.numPlyrCurrGame = 2;
    tracker.gameMode1 = ADVENTURE_MODE | ADVENTURE_ARENA;
    sdata->ptrActiveMenu = sdata->activeSubMenu = selector;
    for (warpPlayer = 0; warpPlayer < 2; warpPlayer++) {
        sdata->ptrActiveMenu = NULL;
        assert(AH_WarpPad_CoopSelectorInView(&tracker, warpPlayer));
        assert(!AH_WarpPad_CoopSelectorInView(&tracker, 1 - warpPlayer));
        sdata->ptrActiveMenu = selector;
        assert(AH_WarpPad_CoopSelectorInView(&tracker, warpPlayer));
        assert(!AH_WarpPad_CoopSelectorInView(&tracker, 1 - warpPlayer));
    }
    warpPlayer = 1;

    pads.gamepad[0].buttonsTapped = BTN_CROSS_one;
    RECTMENU_CollectInput();
    assert(RECTMENU_ProcessInput(selector) == 0);
    pads.gamepad[0].buttonsTapped = 0;
    pads.gamepad[0].buttonsHeldCurrFrame = BTN_L1;
    pads.gamepad[1].buttonsTapped = BTN_DOWN;
    RECTMENU_CollectInput();
    assert(sdata->AnyPlayerTap == 0);
    assert(RECTMENU_ProcessInput(selector) == 0 && selector->rowSelected == 1);
    pads.gamepad[1].buttonsTapped = BTN_CROSS_one;
    RECTMENU_CollectInput();
    assert(RECTMENU_ProcessInput(selector) == 1);
    pads.gamepad[1].buttonsTapped = BTN_TRIANGLE;
    RECTMENU_CollectInput();
    assert(RECTMENU_ProcessInput(selector) == -1);

    tracker.gameMode1 = ADVENTURE_MODE | CRYSTAL_CHALLENGE | END_OF_RACE;
    menu221.rows = rows;
    sdata->ptrActiveMenu = sdata->activeSubMenu = &menu221;
    pads.gamepad[0].buttonsHeldCurrFrame = 0;
    pads.gamepad[1].buttonsTapped = BTN_DOWN;
    RECTMENU_CollectInput();
    assert(RECTMENU_ProcessInput(&menu221) == 0 && menu221.rowSelected == 1);
    pads.gamepad[1].buttonsTapped = BTN_CROSS_one;
    RECTMENU_CollectInput();
    assert(RECTMENU_ProcessInput(&menu221) == 1);

    struct Driver drivers[2] = {0};
    tracker.drivers[0] = &drivers[0];
    tracker.drivers[1] = &drivers[1];
    tracker.levelID = N_SANITY_BEACH;
    tracker.activeMempackIndex = 1;
    tracker.levID_in_each_mempack[2] = THE_LOST_RUINS;
    tracker.framesInThisLEV = 1;
    sdata->queueLength = 2;
    drivers[1].stepFlagSet = 1 << COLL_STEP_TRIGGER_HUB_LEVEL_ID_SHIFT;
    LOAD_Hub_Coop(NULL, &tracker);
    drivers[1].stepFlagSet = COLL_STEP_TRIGGER_HUB_SWAP_NOW_MASK;
    LOAD_Hub_Coop(NULL, &tracker);
    assert(LOAD_Hub_CoopCrossing() && swaps == 0);

    drivers[1].stepFlagSet = 0;
    drivers[0].stepFlagSet = COLL_STEP_TRIGGER_HUB_SWAP_NOW_MASK;
    sdata->queueLength = 0;
    LOAD_Hub_Coop(NULL, &tracker);
    assert(LOAD_Hub_CoopCrossing() && swaps == 0);
    sdata->queueReady = 1;
    LOAD_Hub_Coop(NULL, &tracker);
    assert(!LOAD_Hub_CoopCrossing() && swaps == 1 && leader == 1);
    assert(drivers[0].stepFlagSet == 0 && drivers[1].stepFlagSet == 0);

    tracker.numPlyrCurrGame = 2;
    tracker.gameMode1 = ADVENTURE_MODE | ADVENTURE_ARENA;
    sdata->AkuHint_RequestedHint = -1;
    warpPlayer = 1;
    MainFrame_RequestMaskHint(ADV_MASK_HINT_ID_NEED_MORE_TROPHIES, 0);
    assert(sdata->AkuHint_RequestedHint == -1 && !sdata->AkuAkuHintState);
    warpPlayer = 0;
    MainFrame_RequestMaskHint(ADV_MASK_HINT_ID_RELIC_CHALLENGE, 1);
    assert(sdata->AkuHint_RequestedHint == ADV_MASK_HINT_ID_RELIC_CHALLENGE &&
           sdata->AkuHint_boolInterruptWarppad);
}
'''

with tempfile.TemporaryDirectory() as temp:
    src = Path(temp) / "probe.c"
    executable = Path(temp) / "probe"
    src.write_text(fixture)
    subprocess.run(["cc", "-m32", "-DCTR_NATIVE", "-DCTR_AP", "-DBUILD=926", "-I", "include", "-I", ".",
                    str(src), "-o", str(executable)], cwd=root, check=True)
    subprocess.run([str(executable)], check=True)
print("Split-hub menu ownership, contested crossing and mask hint admission pass")
