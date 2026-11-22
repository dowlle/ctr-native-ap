"""Drive the shared Options entry, child return and close helpers."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "game/230/MM_ConfigMenu.c").read_text()
freeze = (root / "game/MAIN/MainFreeze.c").read_text()
frame = (root / "game/MAIN/MainFrame.c").read_text()
hints = (root / "game/232/AH_HintMenu.c").read_text()
assert "if (menu == &data.menuAdvHub && ctr_cfg_active())" in freeze
assert "MM_ConfigOpen(menu);" in freeze
assert "!MM_ConfigOwnsPauseInput()" in frame
assert "Config_OpenChild(&data.menuRacingWheelConfig);" in source
assert "Config_OpenChild(&D232.menuHintMenu);" in source
assert "const int fromOptions = MM_ConfigReturnFromChild(menu)" in freeze
assert "if (!MM_ConfigReturnFromChild(menu))" in hints
assert "if (!Config_FromHub() && (pad->buttonsTapped & (BTN_CROSS | BTN_CIRCLE))" in source
assert "if (!Config_HubSectionLocked(menu->rowSelected) && (pad->buttonsTapped" in source
assert "if (!Config_FromHub())\n\t\t\t\t\tAP_LinkRegAction();" in source
assert "!Config_RowLockedBySeed(e) && !Config_RowLockedByHub(e)" in source

start = source.index("static struct RectMenu *s_configParent;")
end = source.index("static void Config_UpdateSlider", start)
fixture = r'''
#include <assert.h>
#include <stdio.h>
struct RectMenu { int rowSelected; };
static struct { struct RectMenu menuAdvHub, menuRacingWheelConfig; } data;
static struct { struct RectMenu menuMainMenu; } D230;
static struct { struct RectMenu *ptrDesiredMenu, *ptrActiveMenu; } state, *sdata = &state;
static struct RectMenu g_configMenu, hintMenu, retailHubMenu;
static int s_customContentSection = 7;
static int s_currentSection, s_configTop, saved, committed, captured, controlsSaved, cleared;
void NativeConfig_Save(void) { saved++; }
void NativeRenderer_CaptureWindowGeometry(void) { captured++; }
void NativeControls_Save(void) { controlsSaved++; }
void AP_AiDifficultyCommit(void) { committed++; }
void RECTMENU_ClearInput(void) { cleared++; }
struct RectMenu *MainFreeze_GetMenuPtr(void) { return &data.menuAdvHub; }
'''
fixture += source[start:end]
fixture += r'''
int main(void) {
    assert(!MM_ConfigOwnsPauseInput());
    s_currentSection = 2; s_configTop = 3; g_configMenu.rowSelected = 4;
    data.menuAdvHub.rowSelected = 4;
    MM_ConfigOpen(&data.menuAdvHub);
    assert(Config_FromHub() && sdata->ptrDesiredMenu == &g_configMenu);
    assert(s_currentSection == -1 && s_configTop == 0 && g_configMenu.rowSelected == 0);
    assert(MM_ConfigOwnsPauseInput());
    assert(Config_HubSectionLocked(7) && !Config_HubSectionLocked(1));

    g_configMenu.rowSelected = 2;
    Config_OpenChild(&data.menuRacingWheelConfig);
    assert(sdata->ptrDesiredMenu == &data.menuRacingWheelConfig);
    assert(!MM_ConfigReturnFromChild(&hintMenu));
    assert(MM_ConfigReturnFromChild(&data.menuRacingWheelConfig));
    assert(sdata->ptrDesiredMenu == &g_configMenu && g_configMenu.rowSelected == 2);
    assert(!MM_ConfigReturnFromChild(&data.menuRacingWheelConfig));

    s_currentSection = 1; g_configMenu.rowSelected = 10;
    Config_OpenChild(&hintMenu);
    assert(MM_ConfigReturnFromChild(&hintMenu));
    assert(s_currentSection == 1 && g_configMenu.rowSelected == 10);
    sdata->ptrActiveMenu = &g_configMenu; sdata->ptrDesiredMenu = NULL;
    assert(MM_ConfigOwnsPauseInput());
    Config_Close();
    assert(sdata->ptrDesiredMenu == &data.menuAdvHub && data.menuAdvHub.rowSelected == 4);
    assert(saved == 1 && committed == 0);

    MM_ConfigOpen(&data.menuAdvHub);
    s_configDifficultyEdited = 1;
    Config_Close();
    assert(saved == 2 && committed == 1);

    MM_ConfigOpen(&D230.menuMainMenu);
    assert(!Config_FromHub() && s_currentSection == -1 && !Config_HubSectionLocked(7));
    Config_OpenChild(&data.menuRacingWheelConfig);
    assert(MM_ConfigReturnFromChild(&data.menuRacingWheelConfig));
    Config_Close();
    assert(sdata->ptrDesiredMenu == &D230.menuMainMenu);
    assert(saved == 3 && committed == 2 && captured == 3 && controlsSaved == 3 && cleared == 3);
    sdata->ptrActiveMenu = &retailHubMenu;
    assert(!MM_ConfigOwnsPauseInput());
    puts("PASS: hub and title Options, retail children, saved settings and pause input");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "menu.c").write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-DCTR_AP", "-DCTR_CUSTOM_TRACKS",
                    str(path / "menu.c"), "-o", str(path / "menu")], check=True)
    subprocess.run([str(path / "menu")], check=True)
