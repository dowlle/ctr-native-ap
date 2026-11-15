"""A4-96 Tizi Madness Mask: the production wiring.

The rules live in ap/ap_tizi_madness_logic.h and the identity in
include/platform/native_tizi_madness.h, both covered by
tools/test-tizi-madness.c. This checks how the game calls them:

1. The frame tick runs every frame outside the custom-Arcade item hold
   (the ruling covers the custom Arcade page), and after the Turbo Grant, so a
   race's Turbo lands first.
2. The tick only acts on a load AP_TiziMadnessLoad names, and that
   answer covers the custom Arcade and Time Trial pages and the seed paths.
3. Both serving paths decide by the LEV and VRM pins, through the one
   identity function, and the offline answer ends with its runtime.
4. The Mask receipt and Itemsanity probe are the Tizi Helper's, so the two
   features cannot disagree about the Mask item.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
read = lambda p: (root / p).read_text()
frame = read('ap/ap_hooks_frame.c')
tick = read('ap/ap_tizi_madness.c')
offline = read('platform/native_custom_offline.c')
tracks = read('platform/native_custom_tracks.c')
tizi = read('ap/ap_tizi.c')
unity = read('game/game_unity.h')
fails = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        fails.append(what)


# 1. Placement of the tick.
check(frame.count('AP_TiziMadnessTick(gGT);') == 1, 'the tick is called once per frame')
call = frame.index('AP_TiziMadnessTick(gGT);')
turbo = frame.index('AP_TurboGrantTick(gGT);')
check(turbo < call, 'the tick runs after the Turbo Grant')
hold = frame.rindex('if (!AP_ArcadeHoldsRoom())', 0, call)
block = frame[hold:call]
check(block.count('{') == block.count('}'), 'the tick is outside the custom-Arcade item hold')
check('#include "../ap/ap_tizi_madness.c"' in unity and '#include "../ap/ap_tizi_madness.h"' in unity,
      'the module is in the game build')

# 2. The tick acts only on Tizi Madness, in the live-race window, on drivers[0].
check('if (!AP_TiziMadnessLoad(gGT))' in tick, 'the tick asks the load identity')
check('gGT->drivers[0]' in tick and 'ACTION_BOT' in tick, 'player 1 only, never a bot')
check('trafficLightsTimer < 1' in tick and 'END_OF_RACE' in tick and 'PAUSE_ALL' in tick,
      'after the countdown, not at the end of the race, not paused')
step = tick.index('AP_TurboGrantRaceStep(&g_tm_race,')
latch = tick.index('if (!g_tm_race.countdownSeen)')
write = tick.index('local->heldItemID = AP_TIZI_MADNESS_HELD_MASK;')
body_start = tick.index('void AP_TiziMadnessTick(struct GameTracker *gGT)')
check(body_start < step < latch < write and tick[body_start:step].count('return;') == 1,
      'the countdown latch runs every frame and gates the write (race-load gap)')
check(re.search(r'AP_TiziMadnessRefillOn\(1, AP_TiziItemsanitySeed\(\), AP_TiziMaskReceived\(\)\)', tick),
      'the gate reads the Tizi Helper receipt and probe')
check(len(re.findall(r'heldItemID\s*=[^=]', tick)) == 1 and 'local->heldItemID = AP_TIZI_MADNESS_HELD_MASK;' in tick,
      'the only write is the Mask into the slot')

# 3. Identity on both serving paths.
fn = tick[tick.index('static int AP_TiziMadnessLoad(struct GameTracker *gGT)'):]
fn = fn[:fn.index('\n}\n')]
check('CustomOffline_RuntimeTiziMadness()' in fn and 'MainRaceTrack_OfflineCustomLoad()' in fn,
      'the custom pages answer from the offline runtime')
check('CustomTrack_TiziMadness(&ctx)' in fn and 'ADVENTURE_BOSS' in fn and 'ADVENTURE_CUP' in fn,
      'the seed paths answer from the load context, cups and bosses included')
check('package_tizi_madness(&manifest)' in offline and 'activeTiziMadness = 0;' in offline,
      'the offline answer comes from the manifest pins and ends with the runtime')
body = tracks[tracks.index('int CustomTrack_TiziMadness('):]
body = body[:body.index('\n}\n')]
check(body.count('CustomTrack_IsTiziMadness(') == 3, 'garage venue, Oxide or Cortex venue, and the served descriptor')
check('CustomTrack_LoadServes(ctx)' in body, 'the descriptor counts only while it is served')
check('title' not in body.lower() and 'title' not in fn.lower(), 'no title is consulted')

# 4. One Mask receipt.
check('return g_tizi_mask_received;' in tizi and 'return AP_TiziItemsanityOn();' in tizi,
      'the accessors return the Tizi Helper state')

for f in fails:
    print('FAIL:', f)
print(f'{"FAIL" if fails else "PASS"}: {checks} checks, {len(fails)} failures')
raise SystemExit(1 if fails else 0)
