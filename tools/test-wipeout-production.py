"""Run the actual trap effect and engine fruit helper against the full counter."""
from pathlib import Path
import subprocess
import tempfile
from coop_fixture import function

root = Path(__file__).resolve().parents[1]
traps = (root / "ap/ap_traps.c").read_text()
start = traps.index("\tcase AP_TRAP_WUMPA_WIPEOUT:", traps.index("static void AP_TrapApplyInstant"))
effect = traps[start:traps.index("\n\t\tbreak;", start) + len("\n\t\tbreak;")]
player = (root / "game/231/RB_Player.c").read_text()
start = player.index("void RB_Player_ModifyWumpa(")
modify = player[start:player.index("\n}\n", start) + 3]
fixture = r'''
#include <assert.h>
#define main shipped_counter_main
#define RB_Player_ModifyWumpa counter_fixture_modify
#include "tools/test-wumpa-race-counter.c"
#undef RB_Player_ModifyWumpa
#undef main
#include "ap/ap_trap_sched_logic.h"
#include "ap/ap_trap_observe_logic.h"
#include "ap/ap_trap_alpha3_logic.h"
#define AP_TRAP_JUICED_WUMPA 10
int OtherFX_Play(u32 id, int flags) { (void)id; (void)flags; return 0; }
void AP_WumpaReachedTen(struct Driver *driver) { (void)driver; }
static int recorder_effects;
void AP_NavRec_NoteKartEffect(void) { recorder_effects++; }
static AP_TrapSched g_sched;
static unsigned char g_trap_spawn_pending[AP_TRAP_EFFECT_COUNT];
static struct Driver *AP_TrapCrystalPartner(struct GameTracker *gt)
{ return AP_CoopCrystal(gt) ? gt->drivers[1] : NULL; }
static int AP_TrapCanActNow(struct GameTracker *gt, struct Driver *driver)
{ return driver && sdata->Loading.stage == LOAD_IDLE && !(gt->gameMode1 & PAUSE_ALL); }
static int AP_TrapFireWarpball(struct GameTracker *gt, struct Driver *driver) { return 0; }
static int AP_TrapApplyHazardHit(struct Driver *driver, int effect) { return 0; }
'''
fixture += modify
for signature in ("static int AP_TrapFruitProtected(", "static int AP_TrapTakeFruit(",
                  "static void AP_TrapPollPendingInstants("):
    fixture += function("ap/ap_traps.c", signature)
fixture += r'''
static void fire(struct Driver *local, int effect) {
    struct GameTracker *gGT = sdata->gGT;
    unsigned seats = AP_CoopCrystal(gGT) ? 3 : 1;
    AP_TrapSchedReset(&g_sched);
    int slot = AP_TrapSchedReceive(&g_sched, effect);
    assert(slot >= 0);
    g_sched.slots[slot].state = AP_TRAP_SLOT_ACTIVE;
    switch (effect) {
''' + effect + r'''
    }
}
static void wipeout(struct Driver *local) { fire(local, AP_TRAP_WUMPA_WIPEOUT); }
'''
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
        credited = 80; recorder_effects = 0; wipeout(&d);
        assert(AP_WumpaRaceCount(&d) == 0 && d.numWumpas == 0);
        assert(credited == 80); /* Already banked currency is never removed. */
        assert(recorder_effects == 1);
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
    struct Driver partner = {0};
    gt.drivers[1] = &partner;
    gt.numPlyrCurrGame = 2;
    gt.gameMode1 = ADVENTURE_MODE | CRYSTAL_CHALLENGE;
    g_race_wumpa = 11;
    d.numWumpas = 10;
    partner.numWumpas = 10;
    wipeout(&d);
    assert(d.numWumpas == 0 && partner.numWumpas == 0);
    assert(AP_TrapSchedPollGate(&g_sched, AP_TRAP_WUMPA_WIPEOUT) == AP_TRAP_POLL_GONE);
    g_race_wumpa = d.numWumpas = 6;
    partner.numWumpas = 1;
    fire(&d, AP_TRAP_WUMPA_LEAK);
    assert(d.numWumpas == 3 && partner.numWumpas == 1);
    assert(AP_TrapSchedPollGate(&g_sched, AP_TRAP_WUMPA_LEAK) == AP_TRAP_POLL_RUN);
    partner.numWumpas = 8;
    partner.actionsFlagSet = ACTION_MASK_WEAPON;
    AP_TrapPollPendingInstants(&gt, &d);
    assert(d.numWumpas == 3 && partner.numWumpas == 8);
    partner.actionsFlagSet = 0;
    AP_TrapPollPendingInstants(&gt, &d);
    assert(d.numWumpas == 3 && partner.numWumpas == 4);
    assert(AP_TrapSchedPollGate(&g_sched, AP_TRAP_WUMPA_LEAK) == AP_TRAP_POLL_GONE);
    g_race_wumpa = d.numWumpas = 10;
    partner.numWumpas = 9;
    wipeout(&d);
    assert(d.numWumpas == 0 && partner.numWumpas == 9);
    assert(AP_TrapSchedPollGate(&g_sched, AP_TRAP_WUMPA_WIPEOUT) == AP_TRAP_POLL_RUN);
    g_race_wumpa = d.numWumpas = 10;
    partner.numWumpas = 10;
    AP_TrapPollPendingInstants(&gt, &d);
    assert(d.numWumpas == 10 && partner.numWumpas == 0);
    assert(AP_TrapSchedPollGate(&g_sched, AP_TRAP_WUMPA_WIPEOUT) == AP_TRAP_POLL_GONE);
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
