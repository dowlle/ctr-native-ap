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
    {"Archipelago", "coop_rules", "Co-op Rules", CFG_ENUM, &g_config.coopRules},
    {"Game", "boss_music", "Boss Music", CFG_ENUM, &g_config.bossMusic},
};
const int g_numConfigEntries = sizeof g_configEntries / sizeof g_configEntries[0];
static int s_numSections, s_currentSection, s_configTop, s_configFromHub;
static const char *s_sectionName[CTR_MENU_PAGE_CAP + 1];
static int s_sectionCount[CTR_MENU_PAGE_CAP + 1];
static int s_sectionRows[CTR_MENU_PAGE_CAP + 1][CTR_MENU_PAGE_ROW_CAP];
static struct RectMenu g_configMenu;
static void BuildSectionMap(void)
{
    s_numSections = CTR_MenuBuildPages(g_configEntries, g_numConfigEntries,
                                     s_sectionName, s_sectionRows, s_sectionCount);
}
static int legacySteps;
static void Enum_Step(const ConfigEntry *entry, int direction)
{ (void)entry; (void)direction; legacySteps++; }
'''
fixture += function("game/230/MM_ConfigMenu.c", "static void MM_ConfigOpenHubGameplay(")
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
    MM_ConfigOpenHubGameplay();
    assert(sdata->ptrDesiredMenu == &g_configMenu && s_configFromHub);
    assert(!strcmp(s_sectionName[s_currentSection], "Gameplay"));
    assert(g_configMenu.rowSelected == 0 && s_sectionCount[s_currentSection] == 2);
    assert(s_sectionRows[s_currentSection][1] == 0);
    const ConfigEntry *rules = &g_configEntries[0];
    g_config.coopRacer = -1;
    Enum_Advance(rules);
    assert(g_config.coopRules && !g_config.coopAdventure && packedValue == 96 && writes == 1);
    Enum_Advance(rules);
    assert(!g_config.coopRules && packedValue == 64 && writes == 2);
    int saved = writes;
    transportReady = 0;
    Enum_Advance(rules);
    assert(!g_config.coopRules && writes == saved);
    assert(!strcmp(AP_CoopHelp(), "Reconnect to save."));
    assert(strlen(AP_CoopHelp()) * 13 < 432);
    transportReady = 1;
    gt.gameMode1 |= ADVENTURE_CUP;
    int before = writes;
    Enum_Advance(rules);
    assert(writes == before && !legacySteps && strstr(AP_CoopHelp(), "cup"));
    gt.gameMode1 = ADVENTURE_MODE;
    Enum_Advance(rules); assert(writes == before && strstr(AP_CoopHelp(), "hub"));
    gt.gameMode1 |= ADVENTURE_ARENA | LOADING;
    Enum_Advance(rules); assert(writes == before);
    gt.gameMode1 &= ~LOADING;
    sdata->Loading.stage = LOAD_RESTART;
    Enum_Advance(rules); assert(writes == before);
    int ordinary = 0; ConfigEntry other = {.valuePtr = &ordinary};
    Enum_Advance(&other); assert(legacySteps == 2);
    puts("PASS Gameplay Co-op Rules: per-slot save, offline prompt fits, race/cup/load refusals, unrelated enums unchanged");
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
