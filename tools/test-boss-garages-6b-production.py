"""Boss garages on content plans, #133 PR 6b: custom-bytes venues and boss cups.

The parser and its refusals are covered on apworld-emitted rooms by
tools/test-content-plan.cpp (bosses-custom.json, bosses-cups.json) and the
parity tool. This harness checks the production decisions and wiring around
them:

1. ap/ap_boss_venue.h, compiled: a custom venue on the garage's home LevelID
   takes the inherited-attack adapter (a retail home venue does not), and its
   certified restart point count is the stated one, not the retail table's.
2. The garage venue serving predicate is consulted wherever the Oxide 2 venue
   is (loader override, arena sizing, recorded lanes, item boxes, Wumpa), and
   the loader override checks it before the standalone path.
3. Boss cups: the garage stages the cup before its venue logic and keeps boss
   mode, the boss row wins the difficulty over the cup arm, the cup's points
   win calls AP_NotifyAdvReward with the garage's boss bit, and every hub
   return path (cup end, a leg that cannot be served, the pause menu) returns
   to the garage.

Engine state is not run; this does not claim a Steam race.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def read(rel):
    return (root / rel).read_text(encoding="utf-8")


cases = 0


def check(ok, why):
    global cases
    assert ok, why
    cases += 1


# 1. The venue record decisions.
program = r'''
#include <assert.h>
#include <stdio.h>
#include "ap/ap_boss_venue.h"
int main(void) {
    AP_BossEncounters e = {0};
    int g;
    e.state = AP_BOSS_ENC_VALID;
    for (g = 0; g < AP_BOSS_VENUE_GARAGES; ++g) e.track[g] = (signed char)AP_BOSS_VENUE_GARAGE[g].homeLevel;
    /* Retail home venues keep the exact retail path. */
    assert(AP_BossEncounterShuffledGarage(&e, 1, 0, 3, AP_BOSS_VENUE_GARAGE[3].homeLevel) == -1);
    /* A custom venue on the home LevelID takes the adapter, by its own count. */
    e.custom[3] = 1; e.points[3] = 35;
    assert(AP_BossEncounterShuffledGarage(&e, 1, 0, 3, AP_BOSS_VENUE_GARAGE[3].homeLevel) == 3);
    assert(AP_BossEncounterCertifiedPoints(&e, 3, AP_BOSS_VENUE_GARAGE[3].homeLevel) == 35);
    assert(AP_BossEncounterCertifiedPoints(&e, 0, AP_BOSS_VENUE_GARAGE[0].homeLevel) ==
           AP_BOSS_VENUE_RESTART_POINTS[AP_BOSS_VENUE_GARAGE[0].homeLevel]);
    /* Not a boss race, or a cup leg: no adapter from the record. */
    assert(AP_BossEncounterShuffledGarage(&e, 0, 0, 3, AP_BOSS_VENUE_GARAGE[3].homeLevel) == -1);
    assert(AP_BossEncounterShuffledGarage(&e, 1, 1, 3, AP_BOSS_VENUE_GARAGE[3].homeLevel) == -1);
    /* Counts: Baby T Park's 35 and Cortex Vortex's 255 carry every garage. */
    for (g = 0; g < AP_BOSS_VENUE_GARAGES; ++g)
        assert(AP_BossVenueScheduleFits(g, 35) && AP_BossVenueScheduleFits(g, 255) && !AP_BossVenueScheduleFits(g, 3));
    puts("ok");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / "venue.c"
    exe = Path(tmp) / "venue"
    src.write_text(program, encoding="utf-8")
    subprocess.run(["cc", "-Wall", "-Wextra", "-I", str(root), "-o", str(exe), str(src)], check=True)
    out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
    check(out.strip() == "ok", "venue record decisions")

# 2. Garage venue serving at every Oxide 2 venue site.
tracks = read("platform/native_custom_tracks.c")
override = tracks[tracks.index("int CustomTrack_GetOverride("):]
check(override.index("CustomTrack_GarageVenueServing(") < override.index("CustomTrack_OxideFinalServing("),
      "the loader override checks the garage venue before the Oxide 2 venue and the standalone path")
for rel, count in (("game/MAIN/MainInit.c", 2), ("game/BOTS.c", 1), ("ap/ap_navrec.c", 1),
                   ("ap/ap_boxes.c", 1), ("ap/ap_hooks_race.c", 1)):
    check(read(rel).count("CustomTrack_GarageVenueServing(") >= count, rel + " consults the garage venue")
gates = read("ap/ap_hooks_gates.c")
venue = gates[gates.index("int AP_BossGarageVenue(int bossIdx)"):]
venue = venue[:venue.index("\n}\n")]
check("CustomTrack_GarageVenueReady(bossIdx)" in venue, "a custom venue without verified files keeps the garage shut")

# 3. Boss cups.
garage = read("game/232/AH_Garage.c")
check(garage.index("AP_ContentGarageCupPrepare(hubID - 1)") < garage.index("int venue = AP_BossGarageVenue(hubID - 1)"),
      "the garage stages its boss cup before its venue")
block = garage[garage.index("AP_ContentGarageCupPrepare(hubID - 1)"):garage.index("int venue = AP_BossGarageVenue(hubID - 1)")]
check("AddBitsConfig0 |= ADVENTURE_CUP" in block and "RemBitsConfig0 |= ADVENTURE_BOSS" not in block
      and "AddBitsConfig0 |= ADVENTURE_BOSS" in garage[:garage.index("AP_ContentGarageCupPrepare(hubID - 1)")],
      "the boss cup keeps boss mode and adds cup mode")
bots = read("game/BOTS.c")
check("(gameMode1 & ADVENTURE_CUP) != 0 && (gameMode1 & ADVENTURE_BOSS) == 0" in bots,
      "a boss cup leg takes the boss difficulty row")
cups = read("ap/ap_hooks_content_cups.c")
finish = cups[cups.index("int AP_ContentCupFinish("):]
check("AP_NotifyAdvReward(garage + ADV_REWARD_FIRST_BOSS_KEY)" in finish[:finish.index("\n}\n")],
      "a boss cup's points win completes the garage")
check("AP_ContentGarageCupRunning() < 0" in cups[cups.index("int AP_ContentCupHitDestination"):],
      "a boss cup seats no Hit field")
standings = read("game/UI/UI_CupStandings.c")
check(standings.count("AP_ContentGarageCupReturn(gGT, apGarageCup)") == 2, "both cup-end paths return to the garage")
check("AP_ContentGarageCupReturn(gGT, apGarageCup)" in read("game/MAIN/MainFreeze.c"), "the pause menu returns to the garage")
check("AP_ContentGarageCupReturn(gGT, AP_ContentGarageCupRunning())" in read("game/UI/UI_RaceFlow.c"),
      "the end-of-race abandon path returns to the garage")
check("(id >= 1000 && id <= 1003)" in read("include/platform/native_custom_race_context.h"),
      "a boss cup's race context is a valid pad context")
check("AP_ContentGarageCupRunning() >= 0 && (pad = AP_ContentCupPad()) != NULL" in read("ap/ap_hooks_race.c"),
      "race-loss stakes use the garage's own check")

print(f"PASS {cases} boss garage custom-venue and boss cup cases")
