"""Run the production lap crossing dispatch (#344) against deterministic engine seams.

Compiles ap/ap_hooks_laps.c as written with stand-ins for the engine, the
plan's race contexts and the network, then drives AP_LapOnCrossing
through plan pads, cup legs, boss races and boss cup legs, and every case that
must not send. Also checks
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
#define ACTION_BOT 0x100000
#define ADVENTURE_BOSS 0x80000000u
#define IS_BOSS_RACE(gm) ((int)(gm) < 0)
struct Driver {int actionsFlagSet, lapIndex;};
struct GameTracker {unsigned gameMode1; int levelID; int bossID; char boolDemoMode; int numPlyrCurrGame;};
struct CustomTrackRaceContext {int cup, retail;};
static int active = 1, plan = 1, offline, serving, exists = 1, checked, sent, cup_leg = -1;
static long last;
static struct CustomTrackRaceContext context, *context_on;
static ctr_content_pad pad, cup_pad, *route_pad, *cup_on;
static ctr_content_bosses bosses;
int ctr_cfg_active(void) {return active;}
int ap_content_plan_active(void) {return plan;}
const ctr_content_bosses *ap_content_plan_bosses(void) {return &bosses;}
int MainRaceTrack_OfflineCustomLoad(void) {return offline;}
int CustomTrack_ServingLoad(int a, int b, int c) {(void)a; (void)b; (void)c; return serving;}
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
    active = exists = plan = 1; offline = serving = checked = 0; cup_leg = -1;
    context_on = NULL; route_pad = cup_on = NULL;
    memset(&context, 0, sizeof context); memset(&pad, 0, sizeof pad); memset(&cup_pad, 0, sizeof cup_pad);
    memset(&bosses, 0, sizeof bosses);
    driver.actionsFlagSet = 0; driver.lapIndex = 1;
    g.gameMode1 = ADVENTURE_MODE; g.levelID = 3; g.bossID = 0; g.boolDemoMode = 0; g.numPlyrCurrGame = 1;
    context.retail = 1; pad.lap_location = 35028003;
}
static void standalone(void) { context_on = &context; route_pad = &pad; }
static long cross(int slot)
{
    int before = sent;
    last = 0;
    AP_LapOnCrossing(&g, &driver, slot);
    return sent == before ? -1 : last;
}
int main(void)
{
    // A standalone race in any mode sends its pad's code, a later lap too.
    reset(); standalone(); assert(cross(0) == 35028003);
    reset(); standalone(); g.gameMode1 |= RELIC_RACE; assert(cross(0) == 35028003);
    reset(); standalone(); driver.lapIndex = 3; assert(cross(0) == 35028003);
    // Custom bytes count only while they are served.
    reset(); standalone(); context.retail = 0; assert(cross(0) == -1);
    serving = 1; assert(cross(0) == 35028003);
    reset(); context_on = &context; assert(cross(0) == -1); // no route pad
    reset(); standalone(); pad.lap_location = 0; assert(cross(0) == -1);
    reset(); assert(cross(0) == -1); // no context: never a retail table
    // A cup leg sends its own track's code.
    reset(); cup_on = &cup_pad; cup_leg = 2; context_on = &context; context.cup = 1;
    g.gameMode1 |= ADVENTURE_CUP;
    cup_pad.legs[2].lap = 35028003; cup_pad.legs[1].lap = 35028004; assert(cross(0) == 35028003);
    cup_leg = 1; assert(cross(0) == 35028004);
    cup_leg = 3; assert(cross(0) == -1); // a leg without a code
    reset(); context_on = &context; context.cup = 1; route_pad = &pad; assert(cross(0) == -1); // a stale cup context
    // A boss race sends its venue's code, by the encounter.
    for (int boss = 0; boss < 6; ++boss) {
        reset(); g.gameMode1 |= ADVENTURE_BOSS; g.bossID = boss; bosses.lap[boss] = 35028010 + boss;
        assert(cross(0) == 35028010 + boss);
    }
    reset(); g.gameMode1 |= ADVENTURE_BOSS; assert(cross(0) == -1); // no lap checks in the seed
    reset(); g.gameMode1 |= ADVENTURE_BOSS; g.bossID = 6; bosses.lap[0] = 35028010; assert(cross(0) == -1);
    // A boss cup leg is a cup leg of the garage's cup pad.
    reset(); g.gameMode1 |= ADVENTURE_BOSS | ADVENTURE_CUP; cup_on = &cup_pad; cup_leg = 1; context_on = &context;
    context.cup = 1; cup_pad.legs[1].lap = 35028006; bosses.lap[0] = 35028010; assert(cross(0) == 35028006);
    // Not this kart, not this race.
    reset(); standalone(); assert(cross(1) == -1);
    reset(); standalone(); driver.actionsFlagSet = ACTION_BOT; assert(cross(0) == -1);
    reset(); standalone(); driver.lapIndex = 0; assert(cross(0) == -1);
    reset(); standalone(); g.gameMode1 = ARCADE_MODE; assert(cross(0) == -1);
    reset(); standalone(); g.gameMode1 = ADVENTURE_MODE | CRYSTAL_CHALLENGE; assert(cross(0) == -1);
    reset(); standalone(); g.gameMode1 = ADVENTURE_MODE | ADVENTURE_ARENA; assert(cross(0) == -1);
    reset(); standalone(); g.gameMode1 = ADVENTURE_MODE | BATTLE_MODE; assert(cross(0) == -1);
    reset(); standalone(); g.boolDemoMode = 1; assert(cross(0) == -1);
    reset(); standalone(); active = 0; assert(cross(0) == -1);
    reset(); standalone(); offline = 1; assert(cross(0) == -1);
    reset(); standalone(); plan = 0; assert(cross(0) == -1);
    // Membership and checked state.
    { unsigned modes[] = {0, ARCADE_MODE, TIME_TRIAL, BATTLE_MODE,
        ARCADE_MODE|RELIC_RACE, TIME_TRIAL|RELIC_RACE, BATTLE_MODE|RELIC_RACE,
        ARCADE_MODE|ADVENTURE_MODE, TIME_TRIAL|ADVENTURE_MODE, BATTLE_MODE|ADVENTURE_MODE};
      for (unsigned i=0;i<sizeof modes/sizeof *modes;i++) {
        reset(); standalone(); g.gameMode1=modes[i]; assert(cross(0)==-1);
      }
      reset(); standalone(); g.gameMode1=RELIC_RACE; assert(cross(0)==35028003);
      for(int players=2;players<=4;players++) {
        reset(); standalone(); g.gameMode1=RELIC_RACE; g.numPlyrCurrGame=players; assert(cross(0)==-1);
      }
    }
    reset(); standalone(); exists = 0; assert(cross(0) == -1);
    reset(); standalone(); checked = 1; assert(cross(0) == -1);
    puts("Production lap dispatch: plan pads, cup legs, boss races, boss cup legs and every refusal passed");
    return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / 'laps.c', Path(tmp) / 'laps'
    src.write_text(mode_enum + fixture + source + tests)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-fsanitize=undefined', '-I', str(root / 'ap'), str(src),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
