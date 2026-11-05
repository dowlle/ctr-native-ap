"""A2-28 boss lane switch on custom tracks: the production wiring in game/BOTS.c.

The rule lives in ap/ap_boss_lane_logic.h and is covered by
tools/test-boss-lane-ahead.c. This checks how the engine calls it:

1. The gate is settled on every load from the same recording-identity answer
   the navrec policy uses, so a retail load always clears it.
2. Only the boss lane switch reads the gate, and it adjusts the frame index
   only inside `if (s_bossLaneAhead)`. The overtaking lane change of ordinary
   racers is untouched.
3. Nothing in the boss branch touches Oxide's early start.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
bots = (root / 'game/BOTS.c').read_text()
fails = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        fails.append(what)


check('#include "../ap/ap_boss_lane_logic.h"' in bots, 'BOTS.c includes the rule')

# 1. The gate is set once, from the load's navrec answer, before the switch on it.
sets = re.findall(r'\bs_bossLaneAhead\s*=[^=]', bots)
check(len(sets) == 1, 'the gate is assigned in exactly one place')
check('s_bossLaneAhead = AP_BossLane_Applies(navrecOutcome);' in bots, 'the gate comes from AP_BossLane_Applies')
outcome = bots.index('int navrecOutcome = AP_NavRecIdentity_ForLoad(&facts);')
gate = bots.index('s_bossLaneAhead = AP_BossLane_Applies(navrecOutcome);')
switch = bots.index('switch (navrecOutcome)')
check(outcome < gate < switch, 'the gate is settled with the identity, before its switch')
check(bots.count('AP_NavRecIdentity_ForLoad(') == 1, 'one identity answer per load')

# 2. The search runs only in the boss branch, under the gate.
boss = bots.index('if ((botFlags & (BOT_FLAG_BOSS_PATH_REQUESTED | BOT_FLAG_BOSS_PATH_ACTIVE)) == BOT_FLAG_BOSS_PATH_REQUESTED)')
overtake = bots.index('if (BOTS_RACE_START_AI_COLLISION_DELAY_FRAMES < sdata->aiCollisionDelayFrameCount)', boss)
branch = bots[boss:overtake]
check(bots.count('AP_BossLane_AheadIndex(') == 1, 'the search has one caller')
check('AP_BossLane_AheadIndex(' in branch, 'the caller is the boss lane switch')
uses = [m.start() for m in re.finditer(r'\bs_bossLaneAhead\b', bots)]
check(len(uses) == 3, 'the gate is declared, set and read once each')
guard = branch.index('if (s_bossLaneAhead)')
check(guard < branch.index('AP_BossLane_AheadIndex('), 'the search sits under the gate')
check(branch.index('AP_BossLane_AheadIndex(') < branch.index('botDriver->botData.botNavFrame = &firstNavFrameOnPath[newFrameIndex];'),
      'the adjusted index is the one the boss takes')
check('AP_BossLane' not in bots[overtake:overtake + 6000], 'the overtaking lane change is unchanged')

# 3. Oxide's early start is not part of this change.
check(bots.count('trafficLightsTimer = CTR_MipsSubLo(trafficLightsTimer, 0x1e0);') == 1, "Oxide's early start is intact")
check('AP_BossLane' not in bots[bots.index('// Pretend there is less time (oxide is a cheater)') - 400:boss],
      "nothing new between Oxide's early start and the boss switch")

for f in fails:
    print('FAIL', f)
print(f'{checks - len(fails)} of {checks} checks passed')
raise SystemExit(1 if fails else 0)
