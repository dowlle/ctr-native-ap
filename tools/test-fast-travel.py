"""A3-16 hub fast travel: pin the reachability rule, the save-screen landing
math against the real retail hub data, the trip lifecycle, and the production
wiring.

The hub fixtures are the STATIC_SAVEOBJ InstDef position and rotation of each
adventure hub, extracted from the retail hub levels on 2026-10-27 (the hub
scan behind the Wumpa Shop save-object maps). The approach direction is the
trigger instance's local X axis, (cos(rotY), -sin(rotY)) in 12-bit fixed
point, the same vector AH_SaveObj_ThTick aims the save camera at.
"""
from pathlib import Path
import math
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

HUBS = [
    ("Gem Stone Valley", (-17451, 45, 5095), -810),
    ("N. Sanity Beach", (-14003, 5, -6582), 0),
    ("The Lost Ruins", (3072, 0, 17664), 2042),
    ("Glacier Park", (2751, 0, -7895), -1685),
    ("Citadel City", (5127, 768, -21107), -1029),
]

failures = []


def check(cond, why):
    if not cond:
        failures.append(why)
        print(f"FAIL {why}")


# ── the C fixture: the shipped logic header under test ──────────────────────
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include "ap_fasttravel_logic.h"

int main(void) {
    ap_ft_request r; int wantLoad, drop;

    /* reachability: the hub-door Key rule, one row per hub */
    for (int hub = 0; hub < 5; hub++)
        for (int keys = 0; keys <= 4; keys++) {
            static const int need[5] = {1, 0, 1, 2, 3};
            assert(AP_FT_HubOpen(hub, keys) == (keys >= need[hub]));
        }
    assert(!AP_FT_HubOpen(-1, 99) && !AP_FT_HubOpen(5, 99));

    /* begin: refused out of range, short on Keys, another trip pending, busy */
    AP_FTReset(&r);
    assert(!AP_FTBegin(&r, 5, 9, 0));
    assert(!AP_FTBegin(&r, 4, 2, 0));
    assert(!AP_FTBegin(&r, 3, 2, 1));
    assert(AP_FTBegin(&r, 3, 2, 0));
    assert(!AP_FTBegin(&r, 1, 0, 0)); /* a trip in flight takes no second */
    assert(AP_FTFrame(&r, 1, 1, 2, 0, &wantLoad) == AP_FT_DROP_NONE && !wantLoad);

    /* safe hub: the load is asked exactly once */
    assert(AP_FTFrame(&r, 1, 0, 2, 0, &wantLoad) == AP_FT_DROP_NONE && wantLoad);
    assert(AP_FTFrame(&r, 1, 1, 2, 0, &wantLoad) == AP_FT_DROP_NONE && !wantLoad);
    /* the destination's birth placed the kart: the trip is done */
    assert(AP_FTFrame(&r, 3, 1, 2, 1, &wantLoad) == AP_FT_DROP_NONE && r.stage == AP_FT_IDLE);

    /* a gate that closes on the way cancels the trip */
    AP_FTReset(&r);
    assert(AP_FTBegin(&r, 2, 1, 0));
    assert(AP_FTFrame(&r, 1, 0, 1, 0, &wantLoad) == AP_FT_DROP_NONE && wantLoad);
    assert(AP_FTFrame(&r, 1, 1, 0, 0, &wantLoad) == AP_FT_DROP_GATE && r.stage == AP_FT_IDLE);

    /* the destination loaded but never spawned: dropped, normal spawn kept */
    AP_FTReset(&r);
    assert(AP_FTBegin(&r, 1, 0, 0));
    assert(AP_FTFrame(&r, 1, 0, 0, 0, &wantLoad) == AP_FT_DROP_NONE && wantLoad);
    drop = AP_FT_DROP_NONE;
    for (int i = 0; i < AP_FT_NOSPAWN_FRAMES + 2 && !drop; i++)
        drop = AP_FTFrame(&r, 1, 0, 0, 0, &wantLoad);
    assert(drop == AP_FT_DROP_NOSPAWN && r.stage == AP_FT_IDLE);

    /* a trip that never gets a safe hub times out instead of waiting forever */
    AP_FTReset(&r);
    assert(AP_FTBegin(&r, 0, 1, 0));
    drop = AP_FT_DROP_NONE;
    for (int i = 0; i < AP_FT_TIMEOUT_FRAMES + 2 && !drop; i++)
        drop = AP_FTFrame(&r, -1, 1, 1, 0, &wantLoad);
    assert(drop == AP_FT_DROP_TIMEOUT);

    /* idle: no opinion, no load */
    AP_FTReset(&r);
    assert(AP_FTFrame(&r, 1, 0, 9, 0, &wantLoad) == AP_FT_DROP_NONE && !wantLoad);

    puts("fixture OK");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "ft.c").write_text(fixture)
    subprocess.run(["cc", "-m32", "-I", str(root / "ap"), "-I", str(root / "include"),
                    str(path / "ft.c"), "-o", str(path / "ft")], check=True)
    subprocess.run([str(path / "ft")], check=True)

# ── the landing, per real hub ───────────────────────────────────────────────
for name, pos, rot in HUBS:
    a = 2 * math.pi * rot / 4096
    dirx, dirz = round(math.cos(a) * 4096), round(-math.sin(a) * 4096)
    # the shipped math, mirrored: out = pos + dir * 1024 >> 12
    lx = pos[0] + (dirx * 1024 >> 12)
    lz = pos[2] + (dirz * 1024 >> 12)
    dx, dz = lx - pos[0], lz - pos[2]
    dist2 = dx * dx + dz * dz
    check(dist2 > 0x8FFFF, f"{name}: the landing clears the save screen's wake range (dist^2 {dist2})")
    check(1000 * 1000 <= dist2 <= 1050 * 1050, f"{name}: the landing sits about 1024 out ({math.isqrt(dist2)})")
    # facing: kart forward (sin r, cos r) points at the screen
    face = (rot - 0x400) & 0xFFF
    fa = 2 * math.pi * face / 4096
    fwd = (math.sin(fa), math.cos(fa))
    to_screen = (-dx / math.sqrt(dist2), -dz / math.sqrt(dist2))
    dot = fwd[0] * to_screen[0] + fwd[1] * to_screen[1]
    check(dot > 0.999, f"{name}: the kart faces the save screen on arrival (dot {dot:.4f})")

# ── production wiring ───────────────────────────────────────────────────────
freeze = (root / "game/MAIN/MainFreeze.c").read_text()
menu = freeze[freeze.index("void MainFreeze_MenuPtrDefault(struct RectMenu *menu)"):]
check("kinds[r] == AP_PAUSEROW_TRAVEL" in freeze, "the pause row set builds the TRAVEL row")
check('AP_LNG_TRAVEL' in freeze and 'AP_FastTravelShowMenu();' in menu,
      "the TRAVEL row opens the hub selector")
check('int travel = AP_FastTravelAvailable();' in freeze, "the TRAVEL row is gated on an AP seed in a hub")

rect = (root / "game/RECTMENU.c").read_text()
check('if (index == AP_LNG_TRAVEL)' in rect, "RECTMENU resolves the TRAVEL label")

frame = (root / "ap/ap_hooks_frame.c").read_text()
onframe = frame[frame.index("void AP_OnFrame(struct GameTracker *gGT)"):]
check("AP_FastTravelFrame(gGT);" in onframe, "the trip runs from AP_OnFrame")

birth = (root / "game/Vehicle/VehBirth.c").read_text()
tele = birth[birth.index("void VehBirth_TeleportSelf("):]
check("AP_FastTravelSpawnAt(level1, (int)gGT->levelID, &apTravelPos, &apTravelRot)" in tele,
      "the birth asks the trip for its save-screen spawn")
check(tele.index("AP_FastTravelSpawnAt(level1") < tele.index("VehBirth_FindDoor5(level1)"),
      "the travel spawn wins over the door and podium rules")
check("d->rotCurr.y = apTravelRot;" in tele, "the landing faces the save screen")
check("ctr_cfg_active() && d == gGT->drivers[0]" in tele, "only the player, only in a seed")

launch = (root / "ap/ap_tracker_launch.c").read_text()
begin = launch[launch.index("int AP_TrackerLaunchBegin("):]
check("AP_FastTravelPending()" in begin, "a travel in flight refuses a tracker start")

travel = (root / "ap/ap_fasttravel.c").read_text()
check("AP_TrackerLaunchPending()" in travel, "a tracker start in flight refuses a travel")
check("MainFreeze_APResumeFromTracker();" in travel, "a confirmed hub resumes like a tracker start")
check("AP_FT_Landing(pos, MATH_Cos(a), -MATH_Sin(a), out);" in travel,
      "the landing walks the save camera's approach direction")
check("instDef->modelID == STATIC_SAVEOBJ" in travel, "the anchor is the hub's save object instance")
check("ap_ft_spawnHub = -1;" in travel, "one trip arms one spawn")
check("a dropped trip keeps no armed spawn" in travel and travel.count("ap_ft_spawnHub = -1;") == 3,
      "a dropped trip disarms its spawn too")
check("MainRaceTrack_RequestLoad((s16)(GEM_STONE_VALLEY + r->hub));" in travel,
      "every destination takes the hub's own reload")

unity = (root / "game/game_unity.h").read_text()
check('../ap/ap_fasttravel.c' in unity, "the module is in the unity build")

harnesses = (root / "tools/ci/harnesses.txt").read_text()
check("tools/test-fast-travel.py" in harnesses, "this harness is registered for CI")

if failures:
    raise SystemExit(f"{len(failures)} fast-travel checks failed")
print("PASS: hub fast travel reachability, landing, lifecycle and wiring")
