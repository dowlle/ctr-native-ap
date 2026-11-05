"""Run the actual trap effect and engine fruit helper against the full counter."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
traps = (root / "ap/ap_traps.c").read_text()
start = traps.index("\tcase AP_TRAP_WUMPA_WIPEOUT:", traps.index("static void AP_TrapApplyInstant"))
effect = traps[start:traps.index("\n\t\tbreak;", start)].split(":", 1)[1]
player = (root / "game/231/RB_Player.c").read_text()
start = player.index("void RB_Player_ModifyWumpa(")
modify = player[start:player.index("\n}\n", start) + 3]
fixture = r'''
#define main shipped_counter_main
#define RB_Player_ModifyWumpa counter_fixture_modify
#include "tools/test-wumpa-race-counter.c"
#undef RB_Player_ModifyWumpa
#undef main
int OtherFX_Play(u32 id, int flags) { (void)id; (void)flags; return 0; }
void AP_WumpaReachedTen(struct Driver *driver) { (void)driver; }
'''
fixture += modify
fixture += "\n#define AP_TrapSchedEffectDone(a,b) ((void)0)\n"
fixture += "static void wipeout(struct Driver *local) {\n" + effect + "\n}\n"
fixture += r'''
#include <assert.h>
int main(void) {
    struct GameTracker gt = {0};
    struct Driver d = {0};
    sdata->gGT = &gt; sdata->Loading.stage = LOAD_IDLE;
    gt.drivers[0] = &d; gt.gameMode1 = ADVENTURE_MODE;
    onTrack = 1; g_countdown_seen = 1;
    const int totals[] = {0, 1, 9, 10, 11, 37, 129, 2147483647};
    for (unsigned i = 0; i < sizeof totals / sizeof totals[0]; ++i) {
        g_race_wumpa = totals[i]; d.numWumpas = totals[i] > 10 ? 10 : totals[i];
        credited = 80; wipeout(&d);
        assert(AP_WumpaRaceCount(&d) == 0 && d.numWumpas == 0);
        assert(credited == 80); /* Already banked currency is never removed. */
    }
    g_race_wumpa = 37; d.numWumpas = 10;
    d.actionsFlagSet = ACTION_MASK_WEAPON;
    wipeout(&d); assert(g_race_wumpa == 37 && d.numWumpas == 10);
    d.actionsFlagSet = 0; gt.gameMode2 = CHEAT_WUMPA;
    wipeout(&d); assert(g_race_wumpa == 37 && d.numWumpas == 10);
    gt.gameMode2 = 0;
    wipeout(&d);
    ctr_wumpa_shop.state = AP_WUMPA_SHOP_VALID;
    AP_WumpaFinishArm(&g_finish_latch);
    AP_WumpaOnRaceFinish(&d, 1, d.numWumpas);
    assert(credited == 80); /* A finish cannot bank the wiped fruit. */
    gt.gameMode1 = ARCADE_MODE; d.numWumpas = 10;
    wipeout(&d); assert(d.numWumpas == 0); /* Legacy capped helper still works. */
    puts("PASS: production Wipeout clears the full race count and respects protection and banking");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "wipeout.c").write_text(fixture)
    subprocess.run(["cc", "-m32", "-std=c99", "-DCTR_AP", "-DCTR_NATIVE", "-DBUILD=926",
                    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                    "-I", str(root), "-I", str(root / "include"), "-I", str(root / "ap"),
                    str(path / "wipeout.c"), "-o", str(path / "wipeout")], check=True)
    subprocess.run([str(path / "wipeout")], check=True)
