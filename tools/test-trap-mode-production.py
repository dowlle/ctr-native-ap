"""Execute trap physics/render guards with effects already active on mode exit."""
from pathlib import Path
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
#include "ap/ap_traps.h"
#include "ap/ap_capability.h"
struct sData sdata_static;
static struct Driver local, other;
static int adventure, g_fp_applied = 1, g_trap_paused, g_trap_reroll_excluded = 5;
static unsigned char g_active[AP_TRAP_EFFECT_COUNT];
static AP_TrapSched g_sched;
static struct Driver *AP_TrapLocalDriver(void) { return &local; }
int AP_CapabilityRoomStatsRace(void) { return adventure; }
'''
fixture += next(line for line in source.splitlines() if line.startswith("#define AP_TRAP_GRAV_SCALE")) + "\n"
for signature in ("static int AP_TrapIsLocal(", "int AP_TrapSuppressCrateReward(",
                  "int AP_TrapAllowBoostGrant(", "int AP_TrapWeakenedActive(",
                  "int AP_TrapWeakenBoostTier(", "int AP_TrapRerollExcludedItem(",
                  "int AP_TrapOwnsCamera(", "void AP_TrapRenderTransform(",
                  "int AP_TrapMirrorCullFlip(", "int AP_TrapGravity("):
    fixture += function(signature)
fixture += r'''
int main(void) {
    struct GameTracker gt = {0}; sdata->gGT = &gt;
    struct PushBuffer *pb = &gt.pushBuffer[0];
    memset(g_active, 1, sizeof g_active);
    adventure = 0;
    assert(!AP_TrapIsLocal(&local));
    assert(!AP_TrapSuppressCrateReward(&local));
    assert(AP_TrapAllowBoostGrant(&local));
    assert(!AP_TrapWeakenedActive(&local));
    assert(AP_TrapWeakenBoostTier(-1) == -1);
    assert(AP_TrapWeakenBoostTier(2) == 2);
    assert(AP_TrapRerollExcludedItem() == -1);
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
    assert(AP_TrapRerollExcludedItem() == 5);
    assert(AP_TrapOwnsCamera() && AP_TrapMirrorCullFlip());
    assert(AP_TrapGravity(&local, 1000) < 1000);
    puts("PASS: active trap consumers cannot affect a non-Adventure race");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "trap-mode.c").write_text(fixture)
    subprocess.run(["cc", "-m32", "-std=c99", "-DCTR_AP", "-DCTR_NATIVE", "-DBUILD=926",
                    "-I", str(root), "-I", str(root / "include"), "-I", str(root / "ap"),
                    str(path / "trap-mode.c"), "-o", str(path / "trap-mode")], check=True)
    subprocess.run([str(path / "trap-mode")], check=True)
