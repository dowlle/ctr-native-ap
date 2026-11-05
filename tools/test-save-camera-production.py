"""Execute pause and hub-request gates during a real save camera round trip."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
pause = (root / "game/MAIN/MainFreeze.c").read_text()
swap = (root / "ap/ap_charswap.c").read_text()

def function(source, signature):
    start = source.index(signature + "\n{")
    return source[start:source.index("\n}\n", start) + 3]

fixture = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <common.h>
struct sData sdata_static;
struct Data data;
static struct GameTracker gt;
static struct Driver driver;
static struct RectMenu menu;
static int hub = 1, shown, picker;
int LOAD_IsOpen_AdvHub(void) { return hub; }
int RaceFlag_IsFullyOnScreen(void) { return 0; }
int AP_CharSwap_PickerOpen(void) { return picker; }
struct RectMenu *MainFreeze_GetMenuPtr(void) { return &menu; }
void RECTMENU_Show(struct RectMenu *m) { assert(m == &menu); shown++; }
void MainFrame_TogglePauseAudio(b32 p) { assert(p); }
int OtherFX_Play(u32 s, int flags) { return 0; }
void ElimBG_Activate(struct GameTracker *g) { assert(g == &gt); }
'''
for signature in ("static int ap_cs_hubReady(struct GameTracker *gGT)",
                  "static int ap_cs_hubTransitionIdle(struct GameTracker *gGT)",
                  "static int ap_cs_safeToOpen(struct GameTracker *gGT)",
                  "int AP_HubSafeForRequest(struct GameTracker *gGT)"):
    fixture += function(swap, signature)
fixture += function(pause, "void MainFreeze_IfPressStart(void)")
fixture += r'''
int main(void) {
    sdata->gGT = &gt; sdata->Loading.stage = LOAD_IDLE;
    sdata->queueReady = 1; gt.drivers[0] = &driver;
    gt.levelID = GEM_STONE_VALLEY; gt.gameMode1 = ADVENTURE_ARENA | ADVENTURE_MODE;
    unsigned modes = gt.gameMode1;
    unsigned transitions[] = {CAMERA_FLAG_TRANSITION_AWAY,
        CAMERA_FLAG_TRANSITION_AWAY | CAMERA_FLAG_TRANSITION_HOLD,
        CAMERA_FLAG_TRANSITION_AWAY | CAMERA_FLAG_TRANSITION_HOLD | CAMERA_FLAG_TRANSITION_BACK};
    for (unsigned i = 0; i < sizeof transitions / sizeof *transitions; i++) {
        gt.cameraDC[0].flags = transitions[i];
        assert(!AP_HubSafeForRequest(&gt));
        MainFreeze_IfPressStart();
        assert(!shown && gt.gameMode1 == modes);
    }
    // CAM clears the flags after the last return frame: both gates reopen.
    gt.cameraDC[0].flags = 0;
    assert(AP_HubSafeForRequest(&gt));
    MainFreeze_IfPressStart();
    assert(shown == 1 && (gt.gameMode1 & PAUSE_1));
    assert(!AP_HubSafeForRequest(&gt));
    gt.gameMode1 = modes;
    sdata->ptrActiveMenu = &menu;
    MainFreeze_IfPressStart();
    assert(shown == 1 && !AP_HubSafeForRequest(&gt));
    sdata->ptrActiveMenu = NULL;
    // The new pause guard is hub-only, preserving race auto/manual pause.
    hub = 0; gt.levelID = CRASH_COVE; gt.gameMode1 = ARCADE_MODE;
    gt.cameraDC[0].flags = CAMERA_FLAG_TRANSITION_AWAY;
    MainFreeze_IfPressStart();
    assert(shown == 2 && (gt.gameMode1 & PAUSE_1));
    puts("PASS: save approach/hold/return reject overlapping pause and tracker requests; normal pause survives");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "save.c").write_text(fixture)
    subprocess.run(["cc", "-m32", "-DCTR_AP", "-DCTR_NATIVE", "-DBUILD=926", "-I", str(root / "include"),
                    "-I", str(root), str(path / "save.c"), "-o", str(path / "save")], check=True)
    subprocess.run([str(path / "save")], check=True)
