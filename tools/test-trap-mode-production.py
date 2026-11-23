"""Execute trap physics/render guards with effects already active on mode exit."""
from pathlib import Path
from coop_fixture import trap_inventory
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_traps.c").read_text()
def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]

fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <common.h>
#include "ap/ap_coop.h"
#include "ap/ap_traps.h"
#include "ap/ap_capability.h"
#include "ap/ap_navrec.h"
struct sData sdata_static;
static struct Driver local, other;
static int adventure, g_fp_applied = 1, g_trap_paused;
static int g_trap_boost_tainted, physics_effects;
static int forced_grants;
static unsigned char g_active[AP_TRAP_EFFECT_COUNT];
static unsigned char g_trap_spawn_pending[AP_TRAP_EFFECT_COUNT];
static int hazardHits[2], shielded[2], inRescue[2];
static AP_TrapSched g_sched;
static struct Driver *AP_TrapLocalDriver(void) { return &local; }
int AP_CoopP2Available(void) { return 1; }
int AP_CapabilityRoomStatsRace(void) { return adventure; }
void AP_NavRec_NotePhysicsEffect(void) { physics_effects++; }
void AP_NavRec_NoteBoostGrant(enum AP_NavRecBoostGrant grant) { forced_grants |= grant; }
static int AP_TrapCanActNow(struct GameTracker *gt, struct Driver *driver)
{
    return !(gt->gameMode1 & PAUSE_ALL) && driver && !inRescue[driver == &other];
}
static int AP_TrapApplyHazardHit(struct Driver *driver, int effect)
{
    assert(effect == AP_TRAP_NITRO || effect == AP_TRAP_RED_POTION);
    int seat = driver == &other;
    if (shielded[seat]) return 0;
    hazardHits[seat]++;
    return 1;
}
static int AP_TrapFireWarpball(struct GameTracker *gt, struct Driver *driver)
{
    (void)gt; (void)driver; assert(0); return 0;
}
static int AP_TrapTakeFruit(struct GameTracker *gt, struct Driver *driver, int effect)
{
    (void)gt; (void)driver; (void)effect; assert(0); return 0;
}
'''
fixture += trap_inventory()
for constant in ("AP_TRAP_GRAV_SCALE", "AP_TRAP_BOOST_RESERVES", "AP_TRAP_USF_FIRELEVEL"):
    fixture += next(line for line in source.splitlines() if line.startswith(f"#define {constant} ")) + "\n"
for signature in ("static int AP_TrapIsLocal(", "int AP_TrapSuppressCrateReward(",
                  "int AP_TrapAllowBoostGrant(", "int AP_TrapWeakenedActive(",
                  "int AP_TrapWeakenBoostTier(", "int AP_TrapRerollExcludedItem(",
                  "int AP_TrapOwnsCamera(", "void AP_TrapRenderTransform(",
                  "int AP_TrapMirrorCullFlip(", "int AP_TrapGravity(",
                  "int AP_TrapAffectsRecordedLap(", "static void AP_TrapFloorReserves(", "void AP_TrapForceBoost(",
                  "void AP_TrapNoteWeaponRequest(", "void AP_TrapNoteWeaponFire(",
                  "static struct Driver *AP_TrapCrystalPartner(",
                  "static void AP_TrapPollPendingInstants("):
    fixture += function(signature)
fixture += r'''
int main(void) {
    struct GameTracker gt = {0}; sdata->gGT = &gt; gt.drivers[0] = &local;
    g_trap_inventory[0].rerollExcluded = 5;
    struct PushBuffer *pb = &gt.pushBuffer[0];
    memset(g_active, 1, sizeof g_active);
    adventure = 0;
    assert(!AP_TrapIsLocal(&local));
    assert(!AP_TrapSuppressCrateReward(&local));
    assert(AP_TrapAllowBoostGrant(&local));
    assert(!AP_TrapWeakenedActive(&local));
    assert(AP_TrapWeakenBoostTier(-1) == -1);
    assert(AP_TrapWeakenBoostTier(2) == 2);
    assert(AP_TrapRerollExcludedItem(&local) == -1);
    assert(!AP_TrapOwnsCamera() && !AP_TrapMirrorCullFlip());
    assert(AP_TrapGravity(&local, 1000) == 1000);
    pb->matrix_ViewProj.m[0][0] = 123;
    pb->matrix_ViewProj.m[1][0] = 234;
    AP_TrapRenderTransform(pb);
    assert(pb->matrix_ViewProj.m[0][0] == 123 && pb->matrix_ViewProj.m[1][0] == 234);
    adventure = 1;
    assert(AP_TrapIsLocal(&local) && !AP_TrapIsLocal(&other));
    assert(AP_TrapSuppressCrateReward(&local) && !AP_TrapAllowBoostGrant(&local));
    assert(AP_TrapWeakenedActive(&local) && AP_TrapWeakenBoostTier(2) == 1);
    assert(AP_TrapRerollExcludedItem(&local) == 5);
    assert(AP_TrapOwnsCamera() && AP_TrapMirrorCullFlip());
    assert(AP_TrapGravity(&local, 1000) < 1000);
    for (int effect = AP_TRAP_USF_NOBRAKE; effect <= AP_TRAP_BOOST; effect++) {
        memset(g_active, 0, sizeof g_active);
        local.const_SingleTurboSpeed = 1200;
        local.const_SacredFireSpeed = 1300;
        local.reserves = local.fireSpeedCap = 0;
        physics_effects = 0;
        g_active[effect] = 1;
        AP_TrapForceBoost(&local);
        assert(physics_effects == 1 && local.reserves > 0);
        g_active[effect] = 0;
        assert(AP_TrapAffectsRecordedLap(&local));
        AP_TrapForceBoost(&local);
        assert(physics_effects == 2);
        local.reserves = 0;
        AP_TrapForceBoost(&local);
        assert(physics_effects == 2 && !AP_TrapAffectsRecordedLap(&local));
    }
    physics_effects = 0;
    g_trap_inventory[0].forcedPress = 1;
    AP_TrapNoteWeaponRequest(&local);
    g_trap_inventory[0].forcedPress = 0;
    g_trap_inventory[0].usePending = 0; /* Scheduler has credited the original request. */
    AP_TrapNoteWeaponFire(&other, 0);
    assert(physics_effects == 0 && forced_grants == 0);
    AP_TrapNoteWeaponFire(&local, 0);
    assert(physics_effects == 1 && forced_grants == AP_NAVREC_GRANT_RESERVES);
    AP_TrapNoteWeaponRequest(&local);
    AP_TrapNoteWeaponFire(&local, 0);
    assert(physics_effects == 1);

    memset(g_active, 1, sizeof g_active);
    gt.gameMode1 = ADVENTURE_MODE; gt.numPlyrCurrGame = 2; gt.drivers[1] = &other;
    assert(AP_TrapIsLocal(&other) && AP_TrapWeakenedActive(&other));
    assert(AP_TrapGravity(&other, 1000) < 1000 && !AP_TrapAllowBoostGrant(&other));
    g_trap_inventory[0].forcedPress = 1;
    AP_TrapNoteWeaponRequest(&local);
    g_trap_inventory[0].forcedPress = 0;
    AP_TrapNoteWeaponRequest(&other);
    AP_TrapNoteWeaponFire(&other, 7);
    assert(physics_effects == 1);
    AP_TrapNoteWeaponFire(&local, 7);
    assert(physics_effects == 2 && (forced_grants & AP_NAVREC_GRANT_MASK));
    assert(AP_TrapRerollExcludedItem(&other) == -1);
    gt.gameMode1 |= CRYSTAL_CHALLENGE;
    struct PushBuffer *partnerView = &gt.pushBuffer[1];
    partnerView->matrix_ViewProj.m[0][0] = 145;
    partnerView->matrix_ViewProj.m[1][0] = 245;
    AP_TrapRenderTransform(partnerView);
    assert(partnerView->matrix_ViewProj.m[0][0] == 145 && partnerView->matrix_ViewProj.m[1][0] == -245);
    assert(AP_TrapRerollExcludedItem(&local) == 5);
    assert(AP_TrapRerollExcludedItem(&other) == -1);
    g_trap_inventory[1].rerollExcluded = 7;
    assert(AP_TrapRerollExcludedItem(&other) == 7 && AP_TrapRerollExcludedItem(&local) == 5);
    g_trap_inventory[1].forcedPress = 1;
    AP_TrapNoteWeaponRequest(&other);
    AP_TrapNoteWeaponFire(&local, 7);
    assert(physics_effects == 2);
    AP_TrapNoteWeaponFire(&other, 7);
    assert(physics_effects == 3);
    AP_TrapSchedReset(&g_sched);
    int slot = AP_TrapSchedReceive(&g_sched, AP_TRAP_NITRO);
    g_sched.slots[slot].state = AP_TRAP_SLOT_ACTIVE;
    g_trap_spawn_pending[AP_TRAP_NITRO] = 3;
    gt.gameMode1 |= PAUSE_ALL;
    AP_TrapPollPendingInstants(&gt, &local);
    assert(!hazardHits[0] && !hazardHits[1] && g_trap_spawn_pending[AP_TRAP_NITRO] == 3);
    gt.gameMode1 &= ~PAUSE_ALL;
    inRescue[0] = 1;
    AP_TrapPollPendingInstants(&gt, &local);
    assert(!hazardHits[0] && hazardHits[1] == 1 && g_trap_spawn_pending[AP_TRAP_NITRO] == 1);
    inRescue[0] = 0;
    shielded[0] = 1;
    AP_TrapPollPendingInstants(&gt, &local);
    assert(!hazardHits[0] && hazardHits[1] == 1 && g_trap_spawn_pending[AP_TRAP_NITRO] == 1);
    shielded[0] = 0;
    AP_TrapPollPendingInstants(&gt, &local);
    assert(hazardHits[0] == 1 && hazardHits[1] == 1 && !g_trap_spawn_pending[AP_TRAP_NITRO]);
    gt.gameMode1 |= ARCADE_MODE;
    assert(!AP_TrapIsLocal(&other));
    puts("PASS: trap guards, both Crystal views, and per-kart hazard and weapon targeting");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "trap-mode.c").write_text(fixture)
    subprocess.run(["cc", "-m32", "-std=c99", "-DCTR_AP", "-DCTR_NATIVE", "-DBUILD=926",
                    "-I", str(root), "-I", str(root / "include"), "-I", str(root / "ap"),
                    str(path / "trap-mode.c"), "-o", str(path / "trap-mode")], check=True)
    subprocess.run([str(path / "trap-mode")], check=True)
