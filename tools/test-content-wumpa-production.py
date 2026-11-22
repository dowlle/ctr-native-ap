"""J11: the Wumpa Shop and the Wumpa goal on content-plan rooms.

Content plans fill the ordinary #249 configuration (ctr_wumpa_shop: the shop
slots from the plan's `wumpa_shop` record, the goal from its `wumpa_banked`
arm). Every runtime consumer reads that configuration, so this pins that no
consumer skips content-plan rooms and that content-plan races carry the race
modes the banking test expects:

1. the goal: the content-plan branch of AP_EvaluateGoal ANDs AP_WumpaGoalMet,
   and N. Oxide's garage waits for the Wumpa target (decision 19: the
   content-plan override of the companion arm is gone);
2. the goal line on the pause page: compiled from production, the Wumpa arm
   follows the Gem arm, a Wumpa-only goal shows the Wumpa counter alone, and
   a plan without the arm keeps its old line;
3. banking (decision 22): every content-plan race (track modes, crystal
   arenas, cups, boss races) keeps ADVENTURE_MODE and never sets Arcade,
   Time Trial or Battle, so AP_WumpaBankRaceMode banks its race fruit; an
   arena is not battle mode;
4. the shop, hub line, tracker line, wallet and hints read ctr_wumpa_shop and
   no content-plan gate (the decisions are in test-wumpa-bank.c,
   test-wumpa-shop-ledger.cpp and test-wumpa-shop-production.py).
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
failures = 0


def check(label, ok):
    global failures
    print(('ok    ' if ok else 'FAIL  ') + label)
    failures += 0 if ok else 1


def text(path):
    return (root / path).read_text()


def body(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


# 1. The goal and N. Oxide's garage.
goal = body(text('ap/ap_hooks_goal.c'), 'void AP_EvaluateGoal(void)')
plan = goal[goal.index('if (ap_content_plan_active())'):goal.index('else if (!ctr_cfg_active())')]
check('the content-plan goal ANDs the Wumpa arm on the lifetime total',
      'done = done && AP_WumpaGoalMet(ctr_wumpa_shop.goal, ap_net_wumpa_lifetime());' in plan)
gates = text('ap/ap_hooks_gates.c')
inputs = body(gates, 'static AP_OxideGarageInputs AP_OxideInputs(void)')
check('N. Oxide waits for the Wumpa target on every room (decision 19)',
      'in.wumpaShort = !AP_WumpaGoalMet(ctr_wumpa_shop.goal, ap_net_wumpa_lifetime());' in inputs and
      'in.wumpaShort = 0;' not in inputs)
check('AP_OxideGarageOpen reads those inputs', 'in = AP_OxideInputs();' in body(gates, 'int AP_OxideGarageOpen(void)'))

# 3. Banking race modes of content-plan races.
warp = text('game/232/AH_WarpPad.c')
start = warp.index('#ifdef CTR_CUSTOM_TRACKS\n\tif (ap_content_plan_active())\n\t{\n\t\tconst ctr_content_pad *pad = ap_content_plan_pad(physLevelID);')
block = warp[start:warp.index('goto WarpPad_RequestLoad;', start)]
check('a content-plan pad clears Arcade before the load',
      'gGT->gameMode1 &= ~(ADVENTURE_CUP | ADVENTURE_BOSS | RELIC_RACE | CRYSTAL_CHALLENGE | ARCADE_MODE);' in block and
      'sdata->Loading.OnBegin.RemBitsConfig0 |= ADVENTURE_CUP | ADVENTURE_BOSS | RELIC_RACE | CRYSTAL_CHALLENGE | ARCADE_MODE;' in block)
sets = re.findall(r'(?:gameMode1 \|=|AddBitsConfig0 \|=)\s*([A-Z_| ]+);', block)
setbits = {b.strip() for s in sets for b in s.split('|')}
check('a content-plan pad sets only Adventure race flags (no Battle, Arcade or Time Trial)',
      setbits <= {'CRYSTAL_CHALLENGE', 'RELIC_RACE', 'ADVENTURE_CUP'} and setbits and
      'ADVENTURE_MODE' not in ''.join(re.findall(r'&= ~\(([^)]*)\)', block)))
garage = text('game/232/AH_Garage.c')
check('boss races and boss cups set no Battle, Arcade or Time Trial flag',
      re.search(r'(gameMode1|AddBitsConfig0) \|=[^;]*(BATTLE_MODE|ARCADE_MODE|TIME_TRIAL)', garage) is None and
      re.search(r'AddBitsConfig0 \|=[^;]*(BATTLE_MODE|ARCADE_MODE|TIME_TRIAL)', block) is None)
crystal = text('game/221.c')
check('a content-plan arena load does not switch to Battle mode',
      re.search(r'(gameMode1|AddBitsConfig0) \|=[^;]*BATTLE_MODE', crystal) is None)
wumpa = text('ap/ap_wumpa.c')
check('banking has no content-plan gate', 'content_plan' not in wumpa)

# 4. Consumers read ctr_wumpa_shop, never a content-plan gate.
for path in ('ap/ap_wumpa_shop_ui.c', 'ap/ap_wumpa.c'):
    check(f'{path} has no content-plan gate', 'ap_content_plan' not in text(path))
net = text('ap/ap_net.cpp')
check('the wallet keys follow the shop configuration on every room',
      re.search(r'if \(AP_WumpaBankingActive\(&ctr_wumpa_shop\)\) \{\s*g_ap->SetNotify\(\{g_wumpa\.bankKey, g_wumpa\.shopKey\}\);', net) is not None)
check('the tracker shop line has no content-plan gate',
      'AP_WumpaShopHubTrackerLine(GEM_STONE_VALLEY+ap_tracker.hub,line,(int)sizeof line)' in text('ap/ap_tracker.c'))

# 2 and 3 compiled: the pause goal line from production, the race modes through
# the production decision.
advert = body(gates, 'int AP_GoalAdvertLines(AP_GoalLines lines)')
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ap/ap_goal_line.h"
#include "ap/ap_wumpa_shop.h"
#define ADVENTURE_MODE 0x80000u
#define RELIC_RACE 0x4000000u
#define CRYSTAL_CHALLENGE 0x8000000u
#define ADVENTURE_CUP 0x10000000u
#define ADVENTURE_BOSS 0x80000000u
#define ARCADE_MODE 0x400000u
#define TIME_TRIAL 0x20000u
#define BATTLE_MODE 0x20u
static struct { int goal_gems, goal_oxide, goal_bosses, schema_newer; } ctr_cfg;
static AP_WumpaShopConfig ctr_wumpa_shop;
static int active=1, planActive=1, gems[5]; static long long lifetime;
static int ap_oxide_first_beaten, ap_oxide_final_beaten, firstCleared, bossWins;
int AP_OxideFirstChallengeCleared(void) { return firstCleared; }
int AP_ComposedBossesWon(void) { return bossWins; }
int ctr_cfg_active(void) { return active; }
int ap_content_plan_active(void) { return planActive; }
int AP_GateCountGemColour(int i) { return gems[i]; }
int AP_GateCountGemSum(void) { return gems[0] + gems[1] + gems[2] + gems[3] + gems[4]; }
int ap_content_plan_gems_held(const int received[5]) { return !!received[0] + !!received[2]; }
long long ap_net_wumpa_lifetime(void) { return lifetime; }
''' + advert + r'''
int main(void) {
 AP_GoalLines lines; int cases=0;
 ctr_cfg.goal_oxide=AP_OXIDE_GOAL_ANY; firstCleared=1;
 ctr_cfg.goal_bosses=4; bossWins=2;
 ctr_cfg.goal_gems=3; ctr_wumpa_shop.goal=500; gems[0]=gems[1]=gems[2]=1; lifetime=120;
 assert(AP_GoalAdvertLines(lines)==4 && !strcmp(lines[0],"Oxide Challenge 1/1") &&
        !strcmp(lines[1],"Boss wins 2/4") && !strcmp(lines[2],"Gem colours 2/3") &&
        !strcmp(lines[3],"Wumpa 120/500")); cases++;
 lifetime=900; assert(AP_GoalAdvertLines(lines)==4 && !strcmp(lines[3],"Wumpa 500/500")); cases++;
 planActive=0; assert(AP_GoalAdvertLines(lines)==4 && !strcmp(lines[0],"Oxide Challenge 0/1") &&
        !strcmp(lines[2],"Gem colours 3/3")); cases++;
 planActive=1; ctr_cfg.schema_newer=1; assert(!AP_GoalAdvertLines(lines)); cases++;
 ctr_cfg.schema_newer=0; active=0; assert(!AP_GoalAdvertLines(lines)); cases++;
 active=1; ctr_cfg.goal_oxide=AP_OXIDE_GOAL_DISABLED; ctr_cfg.goal_bosses=0;
 /* A Wumpa-only goal (bosses off, gems_required_goal 0). */
 ctr_cfg.goal_gems=0; lifetime=40;
 assert(AP_GoalAdvertLines(lines)==1 && !strcmp(lines[0],"Wumpa 40/500")); cases++;
 /* Gem-only goal. */
 ctr_wumpa_shop.goal=0; ctr_cfg.goal_gems=3;
 assert(AP_GoalAdvertLines(lines)==1 && !strcmp(lines[0],"Gem colours 2/3")); cases++;
 /* The goal arm itself (AP_WumpaGoalMet). */
 assert(AP_WumpaGoalMet(0,0) && !AP_WumpaGoalMet(500,499) && AP_WumpaGoalMet(500,500) && !AP_WumpaGoalMet(-1,9999)); cases++;
 /* Race fruit banks in every content-plan race (Adventure), never Arcade, Time Trial or Battle. */
 {
  unsigned modes[]={ADVENTURE_MODE, ADVENTURE_MODE|RELIC_RACE, ADVENTURE_MODE|CRYSTAL_CHALLENGE,
                    ADVENTURE_MODE|ADVENTURE_CUP, ADVENTURE_MODE|ADVENTURE_BOSS, ADVENTURE_MODE|ADVENTURE_BOSS|ADVENTURE_CUP};
  for (unsigned i=0;i<sizeof modes/sizeof modes[0];i++) { assert(AP_WumpaBankRaceMode(modes[i])); cases++; }
  assert(!AP_WumpaBankRaceMode(ARCADE_MODE) && !AP_WumpaBankRaceMode(TIME_TRIAL|RELIC_RACE) &&
         !AP_WumpaBankRaceMode(BATTLE_MODE|ADVENTURE_MODE)); cases++;
 }
 printf("PASS: %d content-plan Wumpa goal line and banking mode cases\n",cases);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / 'wumpa.c'
    binary = Path(temporary) / 'wumpa'
    path.write_text(fixture)
    r = subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fsanitize=undefined',
                        '-fno-sanitize-recover=all', '-I', str(root), '-I', str(root / 'ap'), str(path), '-o', str(binary)])
    check('the goal line compiles from production', r.returncode == 0)
    if r.returncode == 0:
        r = subprocess.run([str(binary)])
        check('the goal line and banking mode cases pass', r.returncode == 0)

if failures:
    print(f'{failures} check(s) failed')
    sys.exit(1)
print('PASS: content-plan Wumpa wiring')
