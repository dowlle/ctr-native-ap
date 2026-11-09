"""Execute the production hub selector with engine persistence stubs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "game/MAIN/MainFreeze.c").read_text()
start = source.index("static struct MenuRow ap_difficultyRows")
end = source.index("\n#endif", start)
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <common.h>
#include "ap/ap_ai_difficulty.h"
#include "ap/ap_pauserow.h"
struct sData sdata_static;
NativeConfig g_config;
static struct RectMenu pause_menu;
static int live = 1, saved, committed, destroyed;
int AP_FastTravelAvailable(void) { return live; }
void NativeConfig_Save(void) { saved++; }
void AP_AiDifficultyCommit(void) { committed++; }
void MainFreeze_SafeAdvDestroy(void) { destroyed++; }
struct RectMenu *MainFreeze_GetMenuPtr(void) { return &pause_menu; }
void DecalFont_DrawLine(char *text, int x, int y, s16 font, int flags) {}
'''
fixture += source[start:end]
fixture += r'''
int main(void) {
    for (int i = 0; i < AI_DIFF_COUNT; i++) {
        g_config.aiDifficulty = s_aiDiffValues[i];
        MainFreeze_ShowAiDifficulty();
        assert(sdata->ptrDesiredMenu == &ap_difficultyMenu);
        assert(ap_difficultyMenu.rowSelected == i);
        assert(ap_difficultyRows[AI_DIFF_COUNT].stringIndex == -1);
        for (int j = 0; j < AI_DIFF_COUNT; j++) {
            assert(ap_difficultyRows[j].stringIndex == AP_LNG_AI_PRESET + j);
            assert(ap_difficultyRows[j].rowOnPressUp == (j + AI_DIFF_COUNT - 1) % AI_DIFF_COUNT);
            assert(ap_difficultyRows[j].rowOnPressDown == (j + 1) % AI_DIFF_COUNT);
        }
        ap_difficultyMenu.unk1e = 1;
        MainFreeze_AiDifficultyProc(&ap_difficultyMenu);
        assert(saved == i && committed == i && destroyed == i + 1);
        ap_difficultyMenu.unk1e = 0;
        g_config.aiDifficulty = -1;
        MainFreeze_AiDifficultyProc(&ap_difficultyMenu);
        assert(g_config.aiDifficulty == s_aiDiffValues[i]);
        assert(saved == i + 1 && committed == i + 1);
        assert(sdata->ptrDesiredMenu == &pause_menu);
    }
    int before = saved;
    g_config.aiDifficulty = 123;
    MainFreeze_ShowAiDifficulty();
    assert(g_config.aiDifficulty == 123);
    ap_difficultyMenu.rowSelected = -1;
    MainFreeze_AiDifficultyProc(&ap_difficultyMenu);
    assert(saved == before && committed == before && g_config.aiDifficulty == 123);
    ap_difficultyMenu.rowSelected = AI_DIFF_COUNT;
    MainFreeze_AiDifficultyProc(&ap_difficultyMenu);
    live = 0;
    ap_difficultyMenu.rowSelected = 1;
    MainFreeze_AiDifficultyProc(&ap_difficultyMenu);
    assert(saved == before && committed == before && g_config.aiDifficulty == 123);
    puts("PASS: six presets, navigation, draw, confirm, cancel and stale hub");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "menu.c").write_text(fixture)
    subprocess.run(["cc", "-m32", "-DCTR_AP", "-DCTR_NATIVE", "-DBUILD=926",
                    "-I", str(root / "include"), "-I", str(root),
                    str(path / "menu.c"), "-o", str(path / "menu")], check=True)
    subprocess.run([str(path / "menu")], check=True)
