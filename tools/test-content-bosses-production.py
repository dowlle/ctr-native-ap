"""Boss garages on content plans (#133 PR 6): the production glue around the
parsed `bosses` record.

The parser (tools/test-content-plan.cpp covers it on apworld-emitted rooms)
fills the normal room's configuration from the plan: the retail Trophy doors,
the Oxide garage's four Keys, the goal arms, the Final Challenge relic gate and
venue, and the drawn venues as a boss_encounters record. This harness compiles
the production consumers out of the ap_hooks unit against that configuration:

1. AP_BossGarageOpen: a content plan's garages stay shut without the record
   and follow the normal room's Trophy doors with it; a refused venue record
   keeps them shut.
2. AP_OxideGarageOpen and its inputs: the companion Gem term counts the plan's
   distinct base Gem colours (ap_content_plan_gems_held), not the Gem sum, and
   Oxide disabled shuts the garage.
3. AP_EvaluateGoal: a content plan's goal is its Gem arm ANDed with the
   Oxide and Boss arms of the record; without the record it is the Gem arm
   alone, as before.
4. The wiring: the Oxide garage arms the bundled Cortex Vortex pair for a
   content plan's Final Challenge on Cortex Vortex only, and the race-loss
   stakes know boss races (tools/test-content-win-stakes-production.py runs
   AP_ContentWinFacts).

Engine and network state are controlled seams; this does not claim a Steam
race.
"""
from pathlib import Path
import subprocess
import tempfile
import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
unit = ap_hooks_unit.read()


def extract(signature):
    start = unit.index(signature)
    end = unit.index("\n}\n", start) + 3
    return unit[start:end]


prod = "".join(extract(s) for s in (
    "int AP_ReqMetCounts(const ctr_req *r, const int *counts)\n{",
    "int AP_BossReqMet(const ctr_req *r)\n{",
    "int AP_BossGarageOpen(int bossIdx)\n{",
    "int AP_BossGarageVenue(int bossIdx)\n{",
    "static AP_OxideGarageInputs AP_OxideInputs(void)\n{",
    "int AP_OxideGarageOpen(void)\n{",
    "void AP_EvaluateGoal(void)\n{",
))

fixture = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include "ap_boss_venue.h"
#include "ap_oxide_encounter.h"
#include "ap_goal_logic.h"
#include "ap_wumpa_shop.h"
#define ADV_REWARD_FIRST_TROPHY 6
#define CTR_CFG_BOSS_COUNT 5
#define ORANGE 0
enum { AP_IDX_TROPHY = 0, AP_IDX_SAPPHIRE = 1, AP_IDX_GOLD = 2, AP_IDX_PLATINUM = 3, AP_IDX_TOKEN_RED = 4, AP_IDX_GEM_RED = 9, AP_IDX_KEY = 14 };
typedef struct { int type, count, colour; } ctr_req;
struct {
    int schema_newer, goal_oxide, goal_bosses, goal_gems, oxide_1_optional, bossgarage_mode;
    ctr_req boss_req[5]; int boss_n_tracks[5]; int boss_tracks[5][4];
} ctr_cfg;
AP_BossEncounters ctr_boss_encounters;
AP_WumpaShopConfig ctr_wumpa_shop; /* #249: no Wumpa goal in these cases */
static long long ap_net_wumpa_lifetime(void) { return 0; }
typedef struct { int present, oxide_goal, bosses_required, oxide_1_optional, final_unlock, final_count,
                 track_shuffle, repeats, venues[4], final_track; } ctr_content_bosses;
static ctr_content_bosses g_bosses;
static int g_plan, g_active = 1, g_gems[5], g_first, g_relics, g_won, g_venueReady = 1, g_sent;
int ap_recv_count[15];
static int ap_goal_sent, ap_oxide_first_beaten, ap_oxide_final_beaten;
static int ap_goal_presentation;
static int ctr_cfg_active(void) { return g_active; }
static int ap_content_plan_active(void) { return g_plan; }
static const ctr_content_bosses *ap_content_plan_bosses(void) { static ctr_content_bosses none; return g_plan ? &g_bosses : &none; }
static int ap_content_plan_gems_held(const int received[5]) {
    int n = 0; for (int i = 0; i < 5; ++i) n += i < 3 && received[i] > 0; return n; } /* red, green, blue are the base colours */
static int ap_content_plan_goal(const int received[5]) { return ap_content_plan_gems_held(received) >= ctr_cfg.goal_gems; }
static int AP_GateCountGemColour(int colour) { return g_gems[colour]; }
static int AP_GateCountGemSum(void) { int s = 0; for (int i = 0; i < 5; ++i) s += g_gems[i]; return s; }
static int AP_LocationCheckedByBit(int) { return 0; }
static int AP_OxideFirstChallengeCleared(void) { return g_first; }
static int AP_OxideFinalOpen(void) { return g_relics; }
static int AP_ComposedBossesWon(void) { return g_won; }
static int AP_OxideFinalVenueReady(void) { return g_venueReady; }
static int AP_GateCount(int i) { return ap_recv_count[i]; }
int AP_BossGarageVenue(int bossIdx);
static void AP_FeedEnqueue(const char *, int, int) {}
static void AP_LogLine(const char *) {}
static void AP_AppendLog(const char *) {}
static void ap_net_send_goal(void) { g_sent = 1; }
static void AP_GoalPresentationEvaluate(int *, int) {}
''' + prod + r'''
static void plan(int bosses) {
    std::memset(&ctr_cfg, 0, sizeof ctr_cfg); std::memset(&g_bosses, 0, sizeof g_bosses);
    std::memset(&ctr_boss_encounters, 0, sizeof ctr_boss_encounters); std::memset(ap_recv_count, 0, sizeof ap_recv_count);
    std::memset(g_gems, 0, sizeof g_gems);
    g_plan = 1; g_first = g_relics = g_won = g_sent = 0; ap_goal_sent = ap_oxide_first_beaten = ap_oxide_final_beaten = 0;
    ctr_cfg.goal_oxide = 3; /* what the parser writes without the record */
    if (!bosses) return;
    g_bosses.present = 1; ctr_cfg.bossgarage_mode = 2;
    for (int g = 0; g < 4; ++g) ctr_cfg.boss_req[g] = ctr_req{1, 4 * (g + 1), -1};
    ctr_cfg.boss_req[4] = ctr_req{2, 4, -1};
}
int main() {
    int cases = 0;
    /* 1. Garages. */
    plan(0); ap_recv_count[AP_IDX_TROPHY] = 16;
    for (int g = 0; g < 4; ++g) assert(!AP_BossGarageOpen(g));
    cases++;
    plan(1);
    for (int t = 0; t <= 16; ++t) {
        ap_recv_count[AP_IDX_TROPHY] = t;
        for (int g = 0; g < 4; ++g) assert(AP_BossGarageOpen(g) == (t >= 4 * (g + 1)));
    }
    cases++;
    ctr_boss_encounters.state = AP_BOSS_ENC_VALID;
    for (int g = 0; g < 4; ++g) ctr_boss_encounters.track[g] = (signed char)AP_BOSS_VENUE_GARAGE[(g + 1) % 4].homeLevel;
    assert(AP_BossGarageOpen(0) && AP_BossGarageVenue(0) == AP_BOSS_VENUE_GARAGE[1].homeLevel); cases++;
    ctr_boss_encounters.state = AP_BOSS_ENC_REFUSED;
    assert(!AP_BossGarageOpen(0)); cases++;
    /* 2. The Oxide garage: four Keys; any_percent with a Gem companion counts
       distinct base colours, so two reds are not two Gems. */
    plan(1); ctr_cfg.goal_oxide = 1; ctr_cfg.goal_gems = 2;
    ap_recv_count[AP_IDX_KEY] = 4; g_gems[0] = 2;
    assert(AP_GateCountGemSum() == 2 && !AP_OxideGarageOpen()); cases++;
    g_gems[1] = 1; assert(AP_OxideGarageOpen()); cases++;
    ap_recv_count[AP_IDX_KEY] = 3; assert(!AP_OxideGarageOpen()); cases++;
    ap_recv_count[AP_IDX_KEY] = 4; ctr_cfg.goal_bosses = 2; g_won = 1; assert(!AP_OxideGarageOpen()); cases++;
    g_won = 2; assert(AP_OxideGarageOpen()); cases++;
    ctr_cfg.goal_oxide = 3; assert(!AP_OxideGarageOpen()); cases++;                  /* disabled */
    /* 101_percent: the first challenge opens at four Keys; the Final needs relics. */
    plan(1); ctr_cfg.goal_oxide = 2; ctr_cfg.goal_gems = 1; ap_recv_count[AP_IDX_KEY] = 4;
    assert(AP_OxideGarageOpen()); cases++;
    g_first = 1; assert(!AP_OxideGarageOpen()); cases++;                              /* Final offered, no relics */
    g_relics = 1; assert(!AP_OxideGarageOpen()); cases++;                             /* nor the Gem companion */
    g_gems[2] = 1; assert(AP_OxideGarageOpen()); cases++;
    g_venueReady = 0; assert(!AP_OxideGarageOpen()); g_venueReady = 1; cases++;       /* venue bytes missing */
    /* 3. The goal. */
    plan(0); ctr_cfg.goal_gems = 2; g_gems[0] = g_gems[1] = 1; AP_EvaluateGoal(); assert(g_sent); cases++;
    plan(1); ctr_cfg.goal_oxide = 1; ctr_cfg.goal_bosses = 2; ctr_cfg.goal_gems = 1; g_gems[0] = 1; g_won = 2;
    AP_EvaluateGoal(); assert(!g_sent); cases++;                                      /* Oxide not beaten */
    ap_oxide_first_beaten = 1; g_won = 1; AP_EvaluateGoal(); assert(!g_sent); cases++;/* one boss short */
    g_won = 2; AP_EvaluateGoal(); assert(g_sent); cases++;
    /* A reconnect clears the session flag; the persisted personal win still counts (review finding 2). */
    plan(1); ctr_cfg.goal_oxide = 1; g_first = 1; AP_EvaluateGoal(); assert(g_sent); cases++;
    plan(1); ctr_cfg.goal_oxide = 2; ctr_cfg.goal_gems = 0; ap_oxide_first_beaten = 1;
    AP_EvaluateGoal(); assert(!g_sent); cases++;
    ap_oxide_final_beaten = 1; AP_EvaluateGoal(); assert(g_sent); cases++;           /* no Gem arm */
    plan(1); ctr_cfg.goal_oxide = 0; ctr_cfg.goal_bosses = 4; g_won = 4; AP_EvaluateGoal(); assert(g_sent); cases++;
    std::printf("PASS: %d boss garage, Oxide garage and goal cases through production code\n", cases);
    return 0;
}
'''

# 4. Wiring checks on the production text.
custom = (root / "ap" / "ap_hooks_custom_content.c").read_text(encoding="utf-8")
preflight = custom[custom.index("static void AP_OxideFinalContentPreflight(void)"):]
preflight = preflight[:preflight.index("\n}\n")]
plan_arm = preflight[preflight.index("if (ap_content_plan_active())"):preflight.index("ap_cortex_track_content_required = 0;")]
assert "b->final_track != CTR_CFG_OXIDE_FINAL_CORTEX_VORTEX" in plan_arm and "d.enabled = 1;" in plan_arm, \
    "a content plan arms the Oxide 2 venue for a Final Challenge on Cortex Vortex"
assert "padTrackEnabled" not in plan_arm, "a content plan never arms the legacy Cortex Vortex pad track"
assert "ap_oxide_final_content_required = 1;" in plan_arm, "a refused pair keeps the Final Challenge shut"
race = extract("int AP_ContentWinFacts(long *outCode, int *outNeedsItems, int *outBoost, int *outRacer)\n{")
assert "ap_content_plan_bosses()->present && (gGT->gameMode1 & ADVENTURE_BOSS)" in race, "boss races have content stakes"

with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "bosses.cpp"
    binary = Path(temporary) / "bosses"
    path.write_text(fixture)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-parameter", "-Wno-implicit-fallthrough",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(root / "ap"), "-I", str(root),
                    str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
