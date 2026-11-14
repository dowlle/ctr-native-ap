"""A3-26 lap progress on quads without a checkpoint: the wiring in game/Vehicle/VehLap.c.

The rule lives in ap/ap_lap_progress_logic.h and is covered by
tools/test-lap-progress.c. This checks how the engine calls it:

1. VehLap_UpdateProgress refuses a checkpoint, and each of the two forward
   neighbours it reads, unless the index is inside the restart point array,
   before the first restart point is read.
2. A refused index returns before any driver field is written, so the racer
   keeps its last progress and checkpoint, as a human does on such a quad.
3. The guard is AP only; the human 0xff skip and the bot index source are the
   retail ones.
4. A4-78: before the guard, AP_LapProgress_MeasureIndex picks the index to
   measure from the racer's index, its last checkpoint
   (driver->checkpoint.currentIndex) and that checkpoint's forward link, read
   only when the last checkpoint is inside the array. A nav frame naming a
   restart point past the array (Papu's Pyramid's 102) is measured from a last
   checkpoint that links to the finish, not skipped. It runs after the
   negative-index return and writes only locals.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
lap = (root / 'game/Vehicle/VehLap.c').read_text()
bots = (root / 'game/BOTS.c').read_text()
fails = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        fails.append(what)


check('#include "../../ap/ap_lap_progress_logic.h"' in lap, 'VehLap.c includes the rule')
start = lap.index('void VehLap_UpdateProgress(struct Driver *driver)')
body = lap[start:]

# 1. The guard covers the checkpoint and both forward neighbours.
guard = body.index('if (!AP_LapProgress_IndexUsable(checkpointIndex, level->cnt_restart_points) ||')
guard_text = body[guard:body.index('return;', guard)]
check(guard_text.count('AP_LapProgress_IndexUsable(') == 3, 'the guard checks three indices')
check(body.count('AP_LapProgress_IndexUsable(') == 4, 'the only other check is the A4-78 forward-link read')
check('AP_LapProgress_IndexUsable(nodes[checkpointIndex].nextIndex_forward, level->cnt_restart_points)' in body,
      'the progress node is checked')
check('AP_LapProgress_IndexUsable(nodes[nodes[checkpointIndex].nextIndex_forward].nextIndex_forward, level->cnt_restart_points)' in body,
      'the next node is checked')
nodes_decl = body.index('struct CheckpointNode *nodes = level->ptr_restart_points;')
first_read = body.index('struct CheckpointNode *checkpointNode = &nodes[checkpointIndex];')
check(nodes_decl < guard < first_read, 'the guard sits between the array and its first use')

# 2. A refused index returns before any driver write.
after = body[guard:]
ret = after.index('return;')
first_write = re.search(r'driver->\w+(\.\w+)?\s*(=|&=|\|=)', body)
check(first_write is not None and guard + ret < first_write.start(), 'the guard returns before the first driver write')

# 3. AP only; retail index sources unchanged.
pre = body[:guard]
check(pre.rfind('#ifdef CTR_AP') > pre.rfind('#endif'), 'the guard is inside #ifdef CTR_AP')
check('if ((quad != NULL) && (quad->checkpointIndex != 0xff))' in body, "the human 0xff skip is retail's")
check('checkpointIndex = driver->botData.ai_quadblock_checkpointIndex;' in body, 'a bot still takes its own index')
check('botDriver->botData.ai_quadblock_checkpointIndex = sps->hit.ptrQuadblock->checkpointIndex;' in bots,
      'BOTS.c still takes the quad index off the nav line')

# 4. A4-78: the index is chosen before the guard, from the last checkpoint.
last_line = 'int lastCheckpoint = driver->checkpoint.currentIndex;'
fwd_line = 'int lastForward = AP_LapProgress_IndexUsable(lastCheckpoint, level->cnt_restart_points) ? nodes[lastCheckpoint].nextIndex_forward : -1;'
pick_line = 'checkpointIndex = (s16)AP_LapProgress_MeasureIndex(checkpointIndex, lastCheckpoint, lastForward, level->cnt_restart_points);'
check(body.count('AP_LapProgress_MeasureIndex(') == 1, 'the index is picked once')
check(last_line in body, 'the last checkpoint is the racer\'s stored one')
check(fwd_line in body, 'its forward link is read only when it is inside the array')
check(pick_line in body, 'the pick takes the racer index, the last checkpoint, its forward link and the array size')
if last_line in body and fwd_line in body and pick_line in body:
    pick = body.index(pick_line)
    neg = body.index('(checkpointIndex < 0)')
    check(neg < body.index(last_line) < body.index(fwd_line) < pick < guard, 'the pick sits after the negative-index return and before the guard')
    check(nodes_decl < body.index(fwd_line), 'the forward link is read after the array is taken')
    check(pre.rfind('#ifdef CTR_AP') < body.index(last_line), 'the pick is inside #ifdef CTR_AP')
logic = (root / 'ap/ap_lap_progress_logic.h').read_text()
check('#define AP_LAP_PROGRESS_NO_CHECKPOINT 0xff' in logic, 'a quad without a checkpoint is 0xff')
check('if (index == AP_LAP_PROGRESS_NO_CHECKPOINT)' in logic, '0xff still skips (A3-26)')
check('&& (lastForward == 0))' in logic, 'the fallback needs a last checkpoint that links to the finish')

for f in fails:
    print('FAIL', f)
print(f'{checks - len(fails)} of {checks} checks passed')
raise SystemExit(1 if fails else 0)
