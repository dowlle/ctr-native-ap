"""Execute the actual confirm handler with the production co-op option module."""
from pathlib import Path
import subprocess
import tempfile
from coop_fixture import function

root = Path(__file__).resolve().parents[1]
fixture = (root / 'tools/test-coop.c').read_text().split('int main(void)')[0]
fixture = fixture.replace('#include "../ap/ap_coop.c"', '#include "ap/ap_coop.c"')
fixture = fixture.replace('#include "../ap/ap_capability.c"', '#include "ap/ap_capability.c"')
fixture += '''
#include "include/ctr_menu_pages.h"
const ConfigEntry g_configEntries[] = {
    {.valuePtr = &g_config.coopAdventure},
    {.valuePtr = &g_config.coopRacer},
    {.valuePtr = &g_config.volFx},
};
const int g_numConfigEntries = sizeof g_configEntries / sizeof g_configEntries[0];
static int s_numSections = 2, s_currentSection, s_configTop;
static const char *s_sectionName[CTR_MENU_PAGE_CAP + 1];
static int s_sectionCount[CTR_MENU_PAGE_CAP + 1];
static int s_sectionRows[CTR_MENU_PAGE_CAP + 1][CTR_MENU_PAGE_ROW_CAP];
static struct RectMenu g_configMenu;
static void BuildSectionMap(void) { assert(0); }
static int legacySteps;
static void Enum_Step(const ConfigEntry *entry, int direction)
{ (void)entry; (void)direction; legacySteps++; }
'''
fixture += function("game/230/MM_ConfigMenu.c", "static void MM_ConfigOpenHubCoop(")
menu = (root / 'game/230/MM_ConfigMenu.c').read_text()
start = menu.index('static void Enum_Advance(')
fixture += menu[start:menu.index('\n}\n', start) + 3]
fixture += r'''
int main(void) {
    static struct GameTracker gt;
    static struct GamepadSystem pads;
    sdata->gGT = &gt; sdata->gGamepads = &pads;
    sdata->Loading.stage = LOAD_IDLE;
    gt.gameMode1 = ADVENTURE_MODE | ADVENTURE_ARENA;
    assert(AP_CoopHubAvailable(&gt));
    MM_ConfigOpenHubCoop();
    assert(sdata->ptrDesiredMenu == &g_configMenu && s_currentSection == CTR_MENU_PAGE_CAP);
    assert(g_configMenu.rowSelected == 0 && s_sectionCount[s_currentSection] == 2);
    assert(s_sectionRows[s_currentSection][0] == 0 && s_sectionRows[s_currentSection][1] == 1);
    ctr_cfg.character_phase_present = 1;
    ctr_cfg.character_unlocks = 1;
    ConfigEntry players = {.valuePtr = &g_config.coopAdventure};
    ConfigEntry racer = {.valuePtr = &g_config.coopRacer};
    pads.gamepadsConnectedByFlag = 1;
    Enum_Advance(&players);
    assert(!g_config.coopAdventure && !writes && !legacySteps);
    assert(strstr(AP_CoopHelp(), "second controller"));
    pads.gamepadsConnectedByFlag = 3;
    Enum_Advance(&players); assert(g_config.coopAdventure && writes == 1);
    Enum_Advance(&players); assert(!g_config.coopAdventure && writes == 2);
    unlockRacer(10);
    Enum_Advance(&racer); assert(g_config.coopRacer == 10);
    Enum_Advance(&racer); assert(g_config.coopRacer == 0);
    int saved = writes;
    transportReady = 0;
    Enum_Advance(&players); Enum_Advance(&racer);
    assert(!g_config.coopAdventure && g_config.coopRacer == 0 && writes == saved);
    assert(strstr(AP_CoopHelp(), "Reconnect"));
    transportReady = 1;
    gt.gameMode1 |= ADVENTURE_CUP;
    int before = writes;
    Enum_Advance(&players); Enum_Advance(&racer);
    assert(writes == before && !legacySteps && strstr(AP_CoopHelp(), "cup"));
    gt.gameMode1 = ADVENTURE_MODE;
    Enum_Advance(&players); assert(writes == before && strstr(AP_CoopHelp(), "hub"));
    int ordinary = 0; ConfigEntry other = {.valuePtr = &ordinary};
    Enum_Advance(&other); assert(legacySteps == 2);
    puts("PASS co-op menu: confirm toggles, refusals survive wrap fallback, unlocked P2 cycling, unrelated enums unchanged");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, binary = Path(tmp) / 'menu.c', Path(tmp) / 'menu'
    src.write_text(fixture)
    subprocess.run(['cc', '-m32', '-std=c99', '-DCTR_AP', '-DCTR_NATIVE', '-DBUILD=926',
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-I', str(root), '-I', str(root / 'include'), '-I', str(root / 'ap'),
                    str(src), '-o', str(binary), '-lm'], check=True)
    subprocess.run([str(binary)], check=True)
