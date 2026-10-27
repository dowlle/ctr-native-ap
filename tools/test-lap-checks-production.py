"""Run the production lap crossing dispatch (#344) against deterministic engine seams.

Compiles ap/ap_hooks_laps.c as written with stand-ins for the engine, the
content-plan race contexts and the network, then drives AP_LapOnCrossing
through retail, trial, Cortex Vortex, legacy custom, content-plan standalone
and content-plan cup races, and every case that must not send. Also checks
that game/PlayLevel.c calls it right after the forward crossing advances
lapIndex, and that the unity build includes the part.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
modes = (root / 'include/namespace_Main.h').read_text()
start = modes.index('enum GameMode1\n')
mode_enum = modes[start:modes.index('};', start) + 2]

# The call sits right after the increment, inside the not-yet-finished branch,
# so it also runs on a one-lap race's final crossing and never on the
# behind-the-start-line crossing that skips it.
play = (root / 'game/PlayLevel.c').read_text()
call = re.search(r'currDriver->lapIndex\+\+;\n#ifdef CTR_AP\n[^\n]*\n\t*AP_LapOnCrossing\(gGT, currDriver, iVar10\);\n#endif\n', play)
assert call, 'AP_LapOnCrossing must follow currDriver->lapIndex++ in PlayLevel_UpdateLapStats'
assert play.count('AP_LapOnCrossing(') == 1
assert play.index('ACTION_BEHIND_START_LINE') < call.start() < play.index('If did not just finish race')
unity = (root / 'game/game_unity.h').read_text()
assert unity.index('ap_hooks_content_cups.c') < unity.index('ap_hooks_laps.c'), 'unity order'

source = (root / 'ap/ap_hooks_laps.c').read_text()
fixture = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ap_lap_checks.h"
#include "ap_content_plan.h"
#define CTR_AP 1
#define CTR_CUSTOM_TRACKS 1
#define CTR_CUSTOM_PACKAGES 1
#define ADVENTURE_BOSS 0x80000000u
#define ACTION_BOT 0x100000
struct Driver {int actionsFlagSet, lapIndex;};
struct GameTracker {unsigned gameMode1; int levelID; char boolDemoMode; struct {int cupID;} cup;};
struct CustomTrackRaceContext {int cup, retail;};
static struct {int custom_tracks_ok; struct {int slot;} custom_track;} ctr_cfg;
AP_LapChecks ctr_lap_checks;
static int active = 1, plan, offline, cortex, serving, exists = 1, checked, sent, podium = -1, cup_leg = -1;
static long last;
static struct CustomTrackRaceContext context, *context_on;
static ctr_content_pad pad, cup_pad, *route_pad, *cup_on;
int ctr_cfg_active(void) {return active;}
int ap_content_plan_active(void) {return plan;}
int MainRaceTrack_OfflineCustomLoad(void) {return offline;}
int AP_CortexTrackActive(void) {return cortex;}
int CustomTrack_ServingLoad(int a, int b, int c) {(void)a; (void)b; (void)c; return serving;}
int CustomTrack_RetailPodiumLevelID(int level, int cup, int id) {(void)cup; (void)id; return podium == -2 ? level : podium;}
const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int level) {(void)level; return context_on;}
const ctr_content_pad *AP_ContentCupPad(void) {return cup_on;}
int AP_ContentCupLeg(void) {return cup_leg;}
static const ctr_content_pad *AP_StandaloneRoutePad(const struct CustomTrackRaceContext *c) {(void)c; return route_pad;}
int ap_net_location_exists(long code) {(void)code; return exists;}
int ap_net_location_checked(long code) {(void)code; return checked;}
static int AP_EmitClassCheck(long code, int a, int b, int c, int d, const char *fmt, ...)
{
    (void)a; (void)b; (void)c; (void)d; (void)fmt;
    sent++; last = code; return 1;
}
"""
tests = r"""
static struct Driver driver;
static struct GameTracker g;
static void reset(void)
{
    active = exists = 1; plan = offline = cortex = serving = checked = 0; podium = -2; cup_leg = -1;
    context_on = NULL; route_pad = cup_on = NULL;
    memset(&context, 0, sizeof context); memset(&pad, 0, sizeof pad); memset(&cup_pad, 0, sizeof cup_pad);
    ctr_cfg.custom_tracks_ok = 0; ctr_cfg.custom_track.slot = 0;
    AP_LapChecksReset(&ctr_lap_checks); ctr_lap_checks.enabled = 1;
    for (int lid = 0; lid < AP_LAP_RETAIL_TRACKS; ++lid) ctr_lap_checks.tracks[lid] = AP_LapRetailCode(lid);
    ctr_lap_checks.cortex = AP_LAP_CORTEX_CODE;
    driver.actionsFlagSet = 0; driver.lapIndex = 1;
    g.gameMode1 = ADVENTURE_MODE; g.levelID = 3; g.boolDemoMode = 0; g.cup.cupID = 0;
}
static long cross(int slot)
{
    int before = sent;
    last = 0;
    AP_LapOnCrossing(&g, &driver, slot);
    return sent == before ? -1 : last;
}
int main(void)
{
    // Retail Trophy Race, CTR Challenge, Relic Race and Gem Cup leg.
    reset(); assert(cross(0) == AP_LapRetailCode(3));
    reset(); g.gameMode1 |= RELIC_RACE; assert(cross(0) == AP_LapRetailCode(3));
    reset(); g.gameMode1 |= ADVENTURE_CUP; g.levelID = 9; assert(cross(0) == AP_LapRetailCode(9));
    reset(); driver.lapIndex = 3; assert(cross(0) == AP_LapRetailCode(3)); // a later lap, still unchecked
    // The trial tracks keep their own LevelIDs.
    reset(); g.levelID = 16; podium = -1; assert(cross(0) == AP_LapRetailCode(16));
    reset(); g.levelID = 17; podium = -1; assert(cross(0) == AP_LapRetailCode(17));
    // Cortex Vortex on its Oxide Station host.
    reset(); g.levelID = 13; cortex = 1; assert(cross(0) == AP_LAP_CORTEX_CODE);
    reset(); g.levelID = 13; cortex = 1; ctr_lap_checks.cortex = -1; assert(cross(0) == -1);
    // The legacy custom track on its borrowed host is its slot, never the host.
    reset(); g.levelID = 6; serving = 1; ctr_cfg.custom_tracks_ok = 1; ctr_cfg.custom_track.slot = 1;
    ctr_lap_checks.custom_slot = 1; ctr_lap_checks.custom = 35028100; assert(cross(0) == 35028100);
    reset(); g.levelID = 6; serving = 1; ctr_cfg.custom_tracks_ok = 1; ctr_cfg.custom_track.slot = 1;
    assert(cross(0) == -1); // the seed has no lap check for it: no retail fallback
    // A retail host the custom policy does not name as retail sends nothing.
    reset(); podium = -1; assert(cross(0) == -1);
    // The seed's subset only.
    reset(); ctr_lap_checks.tracks[3] = -1; assert(cross(0) == -1);
    reset(); ctr_lap_checks.enabled = 0; assert(cross(0) == -1);
    // Not this kart, not this race.
    reset(); assert(cross(1) == -1);
    reset(); driver.actionsFlagSet = ACTION_BOT; assert(cross(0) == -1);
    reset(); driver.lapIndex = 0; assert(cross(0) == -1);
    reset(); g.gameMode1 |= ADVENTURE_BOSS; assert(cross(0) == -1);
    reset(); g.gameMode1 = ARCADE_MODE; assert(cross(0) == -1);
    reset(); g.gameMode1 = ADVENTURE_MODE | CRYSTAL_CHALLENGE; assert(cross(0) == -1);
    reset(); g.gameMode1 = ADVENTURE_MODE | ADVENTURE_ARENA; assert(cross(0) == -1);
    reset(); g.gameMode1 = ADVENTURE_MODE | BATTLE_MODE; assert(cross(0) == -1);
    reset(); g.boolDemoMode = 1; assert(cross(0) == -1);
    reset(); active = 0; assert(cross(0) == -1);
    reset(); offline = 1; assert(cross(0) == -1);
    // Membership and checked state.
    reset(); exists = 0; assert(cross(0) == -1);
    reset(); checked = 1; assert(cross(0) == -1);
    // Content plans: the pad's own code for a standalone race in any mode, the
    // cup leg's code in a cup, nothing from the normal room's table.
    reset(); plan = 1; context_on = &context; context.retail = 1; route_pad = &pad; pad.lap_location = 35201616;
    assert(cross(0) == 35201616);
    reset(); plan = 1; context_on = &context; context.retail = 0; route_pad = &pad; pad.lap_location = 35028104;
    assert(cross(0) == -1); // custom bytes not served
    serving = 1; assert(cross(0) == 35028104);
    reset(); plan = 1; context_on = &context; context.retail = 1; route_pad = NULL; assert(cross(0) == -1);
    reset(); plan = 1; context_on = &context; context.retail = 1; route_pad = &pad; pad.lap_location = 0; assert(cross(0) == -1);
    reset(); plan = 1; assert(cross(0) == -1); // no context: never the retail table
    reset(); plan = 1; cup_on = &cup_pad; cup_leg = 2; context_on = &context; context.cup = 1;
    cup_pad.legs[2].lap = 35028003; cup_pad.legs[1].lap = 35028004; assert(cross(0) == 35028003);
    cup_leg = 1; assert(cross(0) == 35028004);
    cup_leg = 3; assert(cross(0) == -1); // a leg without a lap code (a boss cup leg)
    reset(); plan = 1; context_on = &context; context.cup = 1; route_pad = &pad; pad.lap_location = 35028001;
    assert(cross(0) == -1); // a stale cup context is no standalone route
    // Outside a content plan a standalone context has no identity.
    reset(); context_on = &context; assert(cross(0) == -1);
    puts("Production lap dispatch: retail, trial, Cortex Vortex, legacy custom, content-plan pads and cup legs, "
         "and every refusal passed");
    return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / 'laps.c', Path(tmp) / 'laps'
    src.write_text(mode_enum + fixture + source + tests)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-fsanitize=undefined', '-I', str(root / 'ap'), str(src),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
