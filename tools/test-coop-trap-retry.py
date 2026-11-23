"""Run the production reroll boundary and boost clamp with two seated racers."""
from pathlib import Path
import re
import subprocess
import tempfile

from coop_fixture import ROOT, function, trap_inventory


traps = (ROOT / "ap/ap_traps.c").read_text()
boundary = re.search(
    r"\t\t\tif \(AP_TrapHeldItemWaitOwed\(inventory->rerollWait.*?"
    r"owed\[AP_TRAP_ITEM_REROLL\] \|= seat;",
    traps,
    re.S,
)[0]
fixture = r'''
#include <assert.h>
#include <common.h>
#include "ap/ap_capability.h"
#include "ap/ap_coop.h"
#include "ap/ap_trap_alpha3_logic.h"
#include "ap/ap_trap_sched_logic.h"
#include "ap/ap_trap_observe_logic.h"
struct sData sdata_static;
struct Data data;
static struct GameTracker gt;
static AP_TrapSched g_sched;
static unsigned char g_active[AP_TRAP_EFFECT_COUNT];
static void AP_NavRec_NoteKartEffect(void) {}
static int AP_TrapCanActNow(struct GameTracker *gt, struct Driver *driver)
{ return driver && sdata->Loading.stage == LOAD_IDLE; }
static int AP_TrapHeldItemResolved(struct Driver *driver)
{ return AP_TrapHeldItemIsResolved(driver->heldItemID, driver->itemRollTimer, driver->noItemTimer); }
int AP_CapabilityRoomStatsRace(void) { return 1; }
int AP_CapabilityAppliedRankForCharacter(int chain, int racer)
{
    assert(chain == AP_CAP_CHAIN_BOOST);
    return racer == CRASH_BANDICOOT ? AP_CAP_BOOST_NONE : AP_CAP_BOOST_USF;
}
int AP_CapabilityAppliedBoostTier(void) { return AP_CAP_BOOST_NONE; }
'''
fixture += next(line + "\n" for line in traps.splitlines() if line.startswith("#define AP_TRAP_ROLL_FRAMES "))
fixture += trap_inventory()
for signature in (
    "int AP_TrapWeakenBoostTier(",
    "static void AP_TrapClampWeakenedFire(",
    "static void AP_TrapReleaseHeldItemClaims(",
    "static void AP_TrapStartReroll(",
    "static void AP_TrapPollReroll(",
):
    fixture += function("ap/ap_traps.c", signature)
fixture += function("ap/ap_capability.c", "int AP_CapabilityAppliedBoostTierForDriver(")
fixture += r'''
static unsigned reroll_owed(unsigned nextSeats) {
    unsigned owed[AP_TRAP_EFFECT_COUNT] = {0};
    for (int player = 0; player < 2; player++) {
        struct AP_TrapInventory *inventory = &g_trap_inventory[player];
        unsigned seat = 1u << player;
        BOUNDARY
    }
    return owed[AP_TRAP_ITEM_REROLL] & nextSeats;
}
int main(int argc, char **argv) {
    struct Driver drivers[2] = {0};
    sdata->gGT = &gt;
    sdata->Loading.stage = LOAD_IDLE;
    gt.gameMode1 = ADVENTURE_MODE | CRYSTAL_CHALLENGE;
    gt.numPlyrCurrGame = 2;
    for (int i = 0; i < 2; i++) {
        gt.drivers[i] = &drivers[i];
        drivers[i].driverID = i;
    }
    if (argc > 1) {
        data.characterIDs[0] = CRASH_BANDICOOT;
        data.characterIDs[1] = COCO_BANDICOOT;
        g_active[AP_TRAP_WEAKENED_KART] = 1;
        for (int i = 0; i < 2; i++) {
            drivers[i].reserves = 32;
            drivers[i].fireSpeedCap = 1000;
            drivers[i].const_SacredFireSpeed = 700;
            AP_TrapClampWeakenedFire(&drivers[i]);
        }
        assert(drivers[0].fireSpeedCap == 0 && drivers[1].fireSpeedCap == 700);
        assert(drivers[1].reserves == 32);
        return 0;
    }
    AP_TrapSchedReset(&g_sched);
    int slot = AP_TrapSchedReceive(&g_sched, AP_TRAP_ITEM_REROLL);
    g_sched.slots[slot].state = AP_TRAP_SLOT_ACTIVE;
    drivers[1].heldItemID = 2;
    g_trap_inventory[1].rerollWait = 1;
    AP_TrapPollReroll(&gt, &drivers[1], &g_trap_inventory[1]);
    assert(!g_trap_inventory[1].rerollWait && g_trap_inventory[1].rerollExcluded == 2);
    assert(drivers[1].itemRollTimer > 0);
    sdata->Loading.stage = LOAD_RESTART;
    assert(reroll_owed(3) == 2);
    assert(reroll_owed(1) == 0);
    drivers[1].heldItemID = 3;
    drivers[1].itemRollTimer = 0;
    AP_TrapPollReroll(&gt, &drivers[1], &g_trap_inventory[1]);
    assert(reroll_owed(3) == 0);
}
'''.replace("BOUNDARY", boundary)

with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "test.c", Path(tmp) / "test"
    src.write_text(fixture)
    subprocess.run(
        ["cc", "-m32", "-std=c99", "-DCTR_AP", "-DCTR_NATIVE", "-DBUILD=926",
         "-I", ".", "-I", "include", str(src), "-o", str(exe)], cwd=ROOT, check=True
    )
    subprocess.run([str(exe)], check=True)
    subprocess.run([str(exe), "boost"], check=True)
print("PASS: pending P2 reroll at Retry and per-driver Weakened Kart boost clamp")
