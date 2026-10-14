"""#458 follow-up: the boss garage star on the hub map after a Collect.

With a Bosses goal that still needs bosses, a boss counts as beaten on the hub
map only when this player won it (the #458 boss-won flags). A boss race checked
by another player's Collect keeps its blinking "accessible" star. Without a
Bosses goal, the checked boss race location decides, as before.

1. Compile the production AP_BossMapStarBeaten (from the ap_hooks unit) and the
   rule in ap/ap_boss_map_star.h against stub inputs and check the cases.
2. Check that game/232/AH_Map.c asks AP_BossMapStarBeaten for the AP star.
"""
from pathlib import Path
import subprocess
import tempfile
import ap_hooks_unit

root = Path(__file__).resolve().parents[1]


def extract(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end]


hooks = ap_hooks_unit.read()
wrapper = extract(hooks, 'int AP_BossMapStarBeaten(int bossIdx)\n{')

fixture = r'''
#include <stdio.h>
#include "ap_boss_map_star.h"
#define ADV_REWARD_FIRST_BOSS_KEY 94
static struct { int goal_bosses; } ctr_cfg;
static int g_checked[4];   /* boss race location checked (a Collect counts) */
static int g_known;        /* boss-won flags read from the server */
static unsigned g_bits;    /* boss-won flags */
int AP_LocationCheckedByBit(int bit)
{
    int b = bit - ADV_REWARD_FIRST_BOSS_KEY;
    return (b >= 0 && b < 4) ? g_checked[b] : 0;
}
int ap_net_boss_won_known(void) { return g_known; }
unsigned ap_net_boss_won_bits(void) { return g_bits; }
'''

tests = r'''
static int fails;
static void expect(const char *name, int got, int want)
{
    if (got != want) { printf("FAIL %s: got %d want %d\n", name, got, want); fails++; }
}
static void reset(int goal)
{
    int i;
    ctr_cfg.goal_bosses = goal;
    for (i = 0; i < 4; i++) g_checked[i] = 0;
    g_known = 1;
    g_bits = AP_BOSS_WON_PRESENT;
}
int main(void)
{
    /* Bosses goal 1, Ripper Roo checked by a Collect, not won: glow stays. */
    reset(1); g_checked[0] = 1;
    expect("bosses goal, collected, unbeaten -> glow", AP_BossMapStarBeaten(0), 0);

    /* Bosses goal 2, one other boss won, Roo collected: goal still needs a boss. */
    reset(2); g_checked[0] = 1; g_checked[1] = 1; g_bits |= AP_BOSS_WON_BIT(1);
    expect("bosses goal short, collected Roo -> glow", AP_BossMapStarBeaten(0), 0);
    expect("bosses goal short, won Papu -> no glow", AP_BossMapStarBeaten(1), 1);

    /* Bosses goal, Roo won (its location is checked by the win): no glow. */
    reset(1); g_checked[0] = 1; g_bits |= AP_BOSS_WON_BIT(0);
    expect("bosses goal, beaten -> no glow", AP_BossMapStarBeaten(0), 1);

    /* Bosses goal, won flag set but location not checked (sent later): no glow. */
    reset(3); g_bits |= AP_BOSS_WON_BIT(2);
    expect("bosses goal, beaten, check pending -> no glow", AP_BossMapStarBeaten(2), 1);

    /* Bosses goal, unbeaten and unchecked: glow. */
    reset(4);
    expect("bosses goal, open boss -> glow", AP_BossMapStarBeaten(3), 0);

    /* No Bosses goal: the checked location decides (current behaviour). */
    reset(0); g_checked[0] = 1;
    expect("no bosses goal, collected -> no glow", AP_BossMapStarBeaten(0), 1);
    reset(0);
    expect("no bosses goal, unchecked -> glow", AP_BossMapStarBeaten(0), 0);
    reset(0); g_bits |= AP_BOSS_WON_BIT(0);
    expect("no bosses goal, won flag only -> location decides", AP_BossMapStarBeaten(0), 0);

    /* Bosses goal already has its bosses: the location decides again. */
    reset(1); g_bits |= AP_BOSS_WON_BIT(1); g_checked[1] = 1; g_checked[0] = 1;
    expect("bosses goal met, collected Roo -> no glow", AP_BossMapStarBeaten(0), 1);
    expect("bosses goal met, unchecked Komodo -> glow", AP_BossMapStarBeaten(2), 0);

    /* Flags not read yet: the location decides, nothing glows on a guess. */
    reset(1); g_known = 0; g_bits = 0; g_checked[0] = 1;
    expect("flags unknown, collected -> no glow", AP_BossMapStarBeaten(0), 1);

    puts(fails ? "FAIL boss map star" : "ok boss map star");
    return fails != 0;
}
'''

# ── 2. wiring ───────────────────────────────────────────────────────────────
amap = (root / 'game/232/AH_Map.c').read_text()
assert 'sVar7 = (s16)AP_BossMapStarBeaten(base);' in amap
assert 'AP_LocationCheckedByBit(base + ADV_REWARD_FIRST_BOSS_KEY)' not in amap
assert 'AP_BossMapBeaten(' in wrapper
assert 'ctr_cfg.goal_bosses' in wrapper
assert '#include "ap_boss_map_star.h"' in hooks

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'star.c').write_text(fixture + wrapper + tests)
    subprocess.run(['cc', '-std=gnu99', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-I', str(root / 'ap'),
                    str(tmp / 'star.c'), '-o', str(tmp / 'star')], check=True)
    subprocess.run([str(tmp / 'star')], check=True)
